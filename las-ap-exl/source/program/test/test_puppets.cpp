// DBG puppet (test build): a puppet of its own on Link's path, the puppets' state, and their logs.
#ifdef LASAP_TEST
#include "ap/ap_net.hpp"
#include "game/framework.hpp"
#include "game/math.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "ghost/ghost_nametag.hpp"
#include "ghost/ghost_puppets_impl.hpp"
#include "test/test_impl.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace AP::Puppets {
    int g_Past = 0, g_Held = 0, g_TraceSlot = -1;

namespace {
    using Game::At;
    using Game::SharedPtr;
    using Game::StrView;
    using Game::GetPlayer;

    u64 g_TickTicks[60] = {};           // the module's Tick per frame (system ticks), for DBG puppet
    u32 g_InfoDraws[8] = {};            // g_DrawsBy at the last DBG puppet
    u32 g_InfoFrame = 0;

    // DBG puppet framelog: per frame of the module's tick, the pause flag (Framework::paused), the ghost draws and the
    // instanced models' draws recorded so far, the instanced records written and the ghosts ready
    struct FrameRow { u32 frame; u8 paused; u32 draws, instDraws, records, ready; };
    constexpr u32 kFrameLog = 120;
    FrameRow g_FrameLog[kFrameLog];
    u32 g_FrameLogCount = 0;
    struct Sample { Game::Vec3 pos; Game::Vec4 rot; };
    // DBG puppet: Link's path g_Delay frames late, fed g_SteerHz times a second (0: written as it is)
    constexpr int kHistory = 1024;
    int g_Delay = -2;  // -1: stand at g_At; -2: not driven
    int g_SteerHz = 0;
    Game::Vec3 g_At{};
    Sample g_History[kHistory];
    u32 g_HistoryFrame = 0, g_TestFedFrame = 0, g_TestStarts = 0;
    bool g_HistoryValid = false;
    std::string g_AnimPending;  // from DBG puppet anim, played on the game thread

    // DBG puppet trace <slot>: the next kTraceRows replay frames of that ghost slot (DBG puppet tracedump)
    // and, per frame, how many ghosts ran past their newest line (extrapolating, or holding it)
    struct TraceRow { double now, t; s64 at; u32 a, count; char mode; float x, z; int past, held, lines; };
    constexpr u32 kTraceRows = 180;
    TraceRow g_Trace[kTraceRows];
    TraceRow g_TraceNext{};
    u32 g_TraceCount = 0;
    u32 g_LinesSeen = 0;  // GHOST lines of all slots, at the last frame

    std::string Hex(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

    float YawOf(const Game::Vec4& q) { return 2 * std::atan2(q.y, q.t); }

    // DBG puppet: on Link's path g_Delay frames late, exactly or replayed from frames taken g_SteerHz times a second;
    // or standing at g_At.
    void DriveTest(u32 frame, u64 now, double nowFine) {
        Puppet& p = g_Puppets[kTest];
        if (!p.actor.Holds()) return;
        if (!g_AnimPending.empty()) {
            PlayAnim(p, g_AnimPending.c_str(), kGaitBlend, 0);
            g_AnimPending.clear();
        }
        if (g_Delay >= 0 && g_HistoryValid && static_cast<u32>(g_Delay) < g_HistoryFrame) {
            const Sample& s = g_History[(g_HistoryFrame - 1 - g_Delay) % kHistory];
            if (g_SteerHz <= 0) {
                Place(p, s.pos, &s.rot);
            } else {
                u32 every = std::max(1, 60 / g_SteerHz);
                if (p.track.count == 0 || frame - g_TestFedFrame >= every) {
                    const Frame* last = p.track.count > 0 ? &p.track.frames[(p.track.count - 1) % kTrack] : nullptr;
                    bool still = last != nullptr && last->x == s.pos.x && last->y == s.pos.y && last->z == s.pos.z;
                    // Link's animation now (with follow, ahead of his path by the delay)
                    AP::GhostNet::Anim anim = AP::GhostNet::LinkAnim();
                    Flags<GhostFlag> flags{};
                    flags.Set(GhostFlag::Still, still);
                    flags.Set(GhostFlag::Started, anim.starts != g_TestStarts);
                    g_TestStarts = anim.starts;
                    Push(p.track, Frame{static_cast<s64>(now), now, s.pos.x, s.pos.y, s.pos.z,
                                        YawOf(s.rot) * 180 / Game::kPi, anim.anim, anim.frame,
                                        anim.upper, flags, AP::GhostNet::LinkLook()});
                    g_TestFedFrame = frame;
                }
                ReplayFrame(p, nowFine);
            }
        } else if (g_Delay == -1) {
            Place(p, g_At, nullptr);
        }
    }

    std::string PosText(const Game::Vec3& v) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f", v.x, v.y, v.z);
        return buf;
    }

    // DBG puppet: "PUPPET ... | <n> ..." per puppet with an actor (n = ghost slot, test for DBG puppet's).
    std::string Info() {
        std::string s = "PUPPET delay=" + std::to_string(g_Delay) + " steer=" + std::to_string(g_SteerHz) +
                        " buffer=" + std::to_string(static_cast<int>(g_BufferMs)) +
                        " blend=" + std::to_string(g_AnimBlend).substr(0, 6) +
                        " dithered=" + std::to_string(g_GhostDraws) + " remade=" + std::to_string(g_Remade) + " fps=" + std::to_string(static_cast<int>(g_Fps + 0.5f)) +
                        " lod=" + std::to_string(g_FullLimit) + "/" + std::to_string(g_DrawnLimit) + (g_Adapt ? "" : "fixed") +
                        " changes=" + std::to_string(g_DetailChanges);
        s += " inst=";
        for (int g = 0; g < kInstGroupCount; g++) s += (g ? "," : "") + std::to_string(g_Inst[g].drawn);
        u64 sum = 0, most = 0;
        for (u64 t : g_TickTicks) {
            sum += t;
            most = std::max(most, t);
        }
        s += " tick_us=" + std::to_string(sum * 10 / 192 / 60) + "/" + std::to_string(most * 10 / 192);
        // puppet draws per frame on each command buffer since the last DBG puppet
        u32 frames = std::max(1u, g_LastFrame - g_InfoFrame);
        s += " draws/frame=";
        for (int i = 0; i < 8; i++) {
            if (g_Contexts[i].first == nullptr) continue;
            s += std::to_string(i) + ":" + std::to_string((g_DrawsBy[i] - g_InfoDraws[i]) / frames) + ",";
            g_InfoDraws[i] = g_DrawsBy[i];
        }
        g_InfoFrame = g_LastFrame;
        for (auto& p : g_Puppets) {
            if (!Alive(p)) continue;
            int n = IndexOf(p);
            const char* name = p.actor.Peek()->entity.name;
            s += " | " + (n == kTest ? std::string("test") : std::to_string(n)) + " actor=" +
                 Hex(reinterpret_cast<uintptr_t>(p.actor.Peek())) + (p.shown ? "" : " hidden") + " name=" +
                 std::string(name, strnlen(name, 0x20));
            if (Game::Node* root = p.actor.Peek()->root) s += " pos=" + PosText(root->coordsNew.pos);
            s += " model=" + (p.model != nullptr ? Hex(reinterpret_cast<uintptr_t>(p.model)) : "-");
            s += " flags=" + Hex(p.actor.Peek()->entity.flags.Raw());
            s += " zone=" + std::to_string(p.actor.Peek()->zone);
            const char* anim = p.track.anim > 0 ? AP::GhostNet::AnimName(p.track.anim)
                                                : p.track.gait >= 0 ? kGaits[p.track.gait] : nullptr;
            s += " anim=" + std::string(anim != nullptr ? anim : "-");
            int slot;
            TunicSlot(p, slot);
            s += " shapes=" + Hex(p.look.shapes) + " tunic=" + std::to_string(p.tunic) + "@" + std::to_string(slot);
            s += " ready=" + std::to_string(p.ready) + " visfix=" + std::to_string(p.visFixes) + " face=" +
                 std::to_string(p.look.eye) + "," + std::to_string(p.look.mouth) + "," + std::to_string(p.look.ear) + "@" +
                 std::to_string(p.faceSlots[0]) + "," + std::to_string(p.faceSlots[1]) + "," +
                 std::to_string(p.faceSlots[2]) + (p.ownFace ? "own" : "") + " upper=" + std::to_string(p.upper) +
                 (p.track.arriving ? " arriving" : "") + " buffer=" + std::to_string(static_cast<int>(p.track.buffer));
            if (p.track.shownFrames > 60) {
                // speed over the last 60 frames: mean and standard deviation, units per second
                float sum = 0, sq = 0;
                for (float m : p.track.moved) {
                    sum += m;
                    sq += m * m;
                }
                char buf[48];
                snprintf(buf, sizeof(buf), " speed=%.2f~%.2f", sum / 60, std::sqrt(std::max(0.0f, sq / 60 - (sum / 60) * (sum / 60))));
                s += buf;
            }
        }
        return s;
    }

    // The plays by name on the puppets' animation components (DBG puppet animlog).
    HOOK_DEFINE_TRAMPOLINE(PlayByName) {
        static void Callback(void* comp, StrView* name, u8 mode, float blend, float frame) {
            if (mode == 0 && comp != nullptr) {
                for (auto& p : g_Puppets) {
                    if (comp == NpcOf(p) && p.animLog.size() < 40)
                        p.animLog.push_back(std::string(name->View()) + (blend == 0 ? "/0" : "") + "@" +
                                            std::to_string(g_LastFrame));
                }
            }
            Orig(comp, name, mode, blend, frame);
        }
    };
}

    void TraceReplay(double now, double t, s64 at, u32 a, u32 count, char mode, float x, float z) {
        g_TraceNext = TraceRow{now, t, at, a, count, mode, x, z, 0, 0, 0};
    }

    void TestInstallHooks() { PlayByName::InstallAtFuncPtr(Game::NpcAnimation_PlayByName.Ptr()); }

    void TestLevelLoad() { g_HistoryValid = false; }

    void TestFrameStart(Game::Actors::Player* player) {
        if (player->playerCollision != nullptr) {
            auto& c = player->playerCollision->base.node.coordsNew;
            g_History[g_HistoryFrame % kHistory] = Sample{c.pos, c.rotate};
            g_HistoryFrame++;
            g_HistoryValid = true;
        }
        g_Past = g_Held = 0;
        g_TraceNext.now = 0;
    }

    void TestGhostsDriven(u32 frame, u64 now, double nowFine) {
        if (g_TraceNext.now != 0 && g_TraceCount < kTraceRows) {
            g_TraceNext.past = g_Past;
            g_TraceNext.held = g_Held;
            u32 lines = 0;
            for (int n = 0; n < kGhosts; n++) lines += AP::GhostNet::Get(n).frames;
            g_TraceNext.lines = static_cast<int>(lines - g_LinesSeen);
            g_LinesSeen = lines;
            g_Trace[g_TraceCount++] = g_TraceNext;
        }
        DriveTest(frame, now, nowFine);
    }

    void TestFrameEnd(u32 frame, u8 paused) {
        u32 records = 0, ready = 0;
        for (auto& inst : g_Inst) records += inst.drawn;
        for (int n = 0; n < kGhosts; n++) ready += g_Puppets[n].ready;
        g_FrameLog[g_FrameLogCount++ % kFrameLog] = FrameRow{frame, paused, g_GhostDraws, g_InstDraws, records, ready};
    }

    void TestTickTime(u64 ticks) { g_TickTicks[g_LastFrame % 60] = ticks; }

    std::string StateField() {
        int shown = 0, actors = 0;
        for (int i = 0; i < kGhosts; i++) {
            shown += g_Spans[i].hi.load() != 0;
            actors += g_Puppets[i].actor.Holds();
        }
        // and ghost slot 0's look on its model: the shapes, tunic, face frames and upper-body animation last set
        const auto& p = g_Puppets[0];
        std::string gear = p.model != nullptr && p.shown
                               ? Hex(p.look.shapes) + "/" + std::to_string(p.tunic) + "/" + std::to_string(p.faceSet[0]) +
                                     "," + std::to_string(p.faceSet[1]) + "," + std::to_string(p.faceSet[2]) + "/" +
                                     std::to_string(p.upper)
                               : "-";
        return std::to_string(shown) + "/" + std::to_string(actors) + " gear=" + gear + " " + AP::NameTag::State();
    }

    bool Command(const std::vector<std::string>& a) {
        if (a[1] != "puppet") return false;
        const std::string sub = a.size() >= 3 ? a[2] : "";
        auto& p = g_Puppets[kTest];
        auto* player = GetPlayer();
        if (sub == "spawn" && a.size() >= 4) {
            // DBG puppet spawn <actor id hex> [dx dz]: beside Link
            if (player == nullptr || player->playerCollision == nullptr) {
                AP::Send("ERR puppet: no player");
                return true;
            }
            Game::Vec3 pos = player->playerCollision->base.node.coordsNew.pos;
            if (a.size() >= 6) {
                pos.x += atof(a[4].c_str());
                pos.z += atof(a[5].c_str());
            } else {
                pos.x += 1.5f;
            }
            std::string err;
            if (!Spawn(p, static_cast<u16>(strtoul(a[3].c_str(), nullptr, 16)), pos, g_LastFrame, err)) {
                AP::Send("ERR puppet: " + err);
                return true;
            }
            g_At = pos;
            g_Delay = -2;
        } else if (sub == "at" && a.size() >= 6) {
            g_At = Game::Vec3{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
            g_Delay = -1;
        } else if (sub == "follow" && a.size() >= 4) {
            // DBG puppet follow <frames>: Link's path (position and rotation) that many frames late
            g_Delay = std::min(atoi(a[3].c_str()), kHistory - 1);
        } else if (sub == "steer" && a.size() >= 4) {
            // DBG puppet steer <hz>: with follow, the path taken this many times a second and replayed; 0: exact
            g_SteerHz = atoi(a[3].c_str());
            ResetTrack(p.track);
        } else if (sub == "draw" && a.size() >= 4) {
            // DBG puppet draw on|off: the ghosts' models drawn or hidden; actors, animation and lines run on
            g_DrawGhosts = a[3] == "on";
        } else if (sub == "lod" && a.size() >= 4) {
            // DBG puppet lod auto | lod <whole> <drawn>: the crowd limits follow the frame rate, or are fixed
            g_Adapt = a[3] == "auto";
            if (!g_Adapt && a.size() >= 5) {
                g_FullLimit = std::clamp(atoi(a[3].c_str()), 0, kGhosts);  // below the minimums: for measuring
                g_DrawnLimit = std::clamp(atoi(a[4].c_str()), 0, kGhosts);
            }
        } else if (sub == "keep" && a.size() >= 4) {
            // DBG puppet keep <units>: the crowd ranking's margin for a ghost's present detail
            g_KeepMargin = static_cast<float>(atof(a[3].c_str()));
        } else if (sub == "crowdshapes" && a.size() >= 4) {
            // DBG puppet crowdshapes <hex mask>: the shapes of ghosts drawn as part of a crowd
            g_CrowdMask = strtoul(a[3].c_str(), nullptr, 16);
        } else if (sub == "shapes" && a.size() >= 4) {
            // DBG puppet shapes <hex mask>: only those of the look's shapes drawn
            g_ShapeMask = strtoul(a[3].c_str(), nullptr, 16);
        } else if (sub == "skip" && a.size() >= 4) {
            // DBG puppet skip <hex mask>: drops the puppets' draws on those command buffers (draws/frame's order)
            g_SkipMask = strtoul(a[3].c_str(), nullptr, 16);
        } else if (sub == "dither" && a.size() >= 4) {
            // DBG puppet dither on|off: the ghosts drawn with or without the dither state
            g_Dither = a[3] == "on";
        } else if (sub == "tagstyle" && a.size() >= 9) {
            // DBG puppet tagstyle <scale> <text alpha> <window alpha> <raise> <margin x> <margin y>: the tags' look
            AP::NameTag::Style(static_cast<float>(atof(a[3].c_str())), atoi(a[4].c_str()), atoi(a[5].c_str()),
                               static_cast<float>(atof(a[6].c_str())), static_cast<float>(atof(a[7].c_str())),
                               static_cast<float>(atof(a[8].c_str())));
        } else if (sub == "tags" && a.size() >= 4) {
            // DBG puppet tags on|off: the ghosts' name tags
            g_NameTags = a[3] == "on";
        } else if (sub == "tags") {
            // DBG puppet tags: the tags' text extents
            AP::Send(AP::NameTag::Info());
            return true;
        } else if (sub == "blend" && a.size() >= 4) {
            // DBG puppet blend <s>: the blend time of the sender's animations on the puppets
            g_AnimBlend = std::clamp(static_cast<float>(atof(a[3].c_str())), 0.0f, 2.0f);
        } else if (sub == "buffer" && a.size() >= 4) {
            // DBG puppet buffer <ms> [<most ms>]: the least and the most puppets replay behind the newest frame (on the
            // sender's clock); both the same: a fixed buffer
            g_BufferMs = std::clamp(atof(a[3].c_str()), 0.0, 1000.0);
            if (a.size() >= 5) g_BufferMaxMs = std::clamp(atof(a[4].c_str()), 0.0, 1000.0);
        } else if (sub == "stats" && a.size() >= 4 && a[3] == "reset") {
            // DBG puppet stats reset: the ghosts' replay stats start over
            for (auto& q : g_Puppets) q.stats = {};
        } else if (sub == "stats") {
            // DBG puppet stats: "STATS | <slot> frames=<n> held=<per mille> past=<per mille> catchup=<per mille>
            // snaps=<n> speed=<mean>~<deviation> buffer=<mean>/<most>" per ghost slot replayed since the reset: frames
            // held at the newest line while the sender moved, run on past it, still correcting by 0.3 units or more;
            // corrections over kCorrectMax (jumps); its speed per frame (units per second); the buffer (ms)
            std::string line = "STATS";
            for (int n = 0; n < kGhosts; n++) {
                const auto& st = g_Puppets[n].stats;
                if (st.frames == 0) continue;
                double mean = st.speeds > 0 ? st.speed / st.speeds : 0;
                double sd = st.speeds > 0 ? std::sqrt(std::max(0.0, st.speedSq / st.speeds - mean * mean)) : 0;
                char buf[160];
                snprintf(buf, sizeof(buf), " | %d frames=%u held=%u past=%u catchup=%u snaps=%u speed=%.2f~%.2f buffer=%.0f/%.0f",
                         n, st.frames, st.held * 1000 / st.frames, st.past * 1000 / st.frames,
                         st.catchup * 1000 / st.frames, st.snaps, mean, sd, st.buffer / st.frames, st.bufferMax);
                line += buf;
            }
            AP::Send(line);
            return true;
        } else if (sub == "anim" && a.size() >= 4) {
            // DBG puppet anim <name>: plays one of the model's animations (on the game thread)
            g_AnimPending = a[3];
        } else if (sub == "animlog") {
            // DBG puppet animlog: the animations played on each puppet since its spawn or the last animlog
            std::string line = "ANIMLOG";
            for (auto& q : g_Puppets) {
                if (q.animLog.empty()) continue;
                int n = IndexOf(q);
                line += " | " + (n == kTest ? std::string("test") : std::to_string(n));
                for (auto& l : q.animLog) line += " " + l;
                q.animLog.clear();
            }
            AP::Send(line);
            return true;
        } else if (sub == "vsyncwait" && a.size() >= 4) {
            // DBG puppet vsyncwait on|off: whether each frame ends by waiting for the next vsync (the graphics system,
            // *(0x1cc8b30)[1], +0x8169: its vsync watcher runs; the frame's end then clears and waits for its event).
            // Only with a watcher (+0x81b0 its thread): No 30 FPS Drops starts none, and nothing would signal the event.
            u8* graphics = *Game::Graphics_ptr;
            u8* system = graphics != nullptr ? *reinterpret_cast<u8**>(graphics + 8) : nullptr;
            if (system != nullptr && *reinterpret_cast<void**>(system + 0x81b0) != nullptr) system[0x8169] = a[3] == "on";
        } else if (sub == "trace" && a.size() >= 4) {
            // DBG puppet trace <slot>: records that ghost's next replay frames
            g_TraceSlot = atoi(a[3].c_str());
            g_TraceCount = 0;
        } else if (sub == "tracedump") {
            // DBG puppet tracedump: "TRACE <now ms> <replay time ms> <frame's time> <frame>/<frames> <mode> <x> <z> <past> <held> <lines>"
            // per recorded frame
            for (u32 i = 0; i < g_TraceCount; i++) {
                const TraceRow& r = g_Trace[i];
                char buf[96];
                snprintf(buf, sizeof(buf), "TRACE %.2f %.2f %lld %u/%u %c %.3f %.3f %d %d %d", r.now, r.t,
                         static_cast<long long>(r.at), r.a, r.count, r.mode, r.x, r.z, r.past, r.held, r.lines);
                AP::Send(buf);
            }
            AP::Send("TRACE end");
            return true;
        } else if (sub == "layered" && a.size() >= 4) {
            // DBG puppet layered on|off: whether new ghost models get the layered animation player
            g_LayeredGhosts = a[3] == "on";
        } else if (sub == "framelog") {
            // DBG puppet framelog: "FRAMELOG <frame> <paused> <ghost draws> <instanced draws> <records> <ready>" per
            // logged frame (the draws since the frame before), oldest first
            u32 from = g_FrameLogCount > kFrameLog ? g_FrameLogCount - kFrameLog : 0;
            for (u32 i = from; i < g_FrameLogCount; i++) {
                const FrameRow& r = g_FrameLog[i % kFrameLog];
                const FrameRow& q = g_FrameLog[(i > from ? i - 1 : i) % kFrameLog];
                char buf[96];
                snprintf(buf, sizeof(buf), "FRAMELOG %u %u %u %u %u %u", r.frame, r.paused, r.draws - q.draws,
                         r.instDraws - q.instDraws, r.records, r.ready);
                AP::Send(buf);
            }
            AP::Send("FRAMELOG end");
            return true;
        } else if (sub == "drop") {
            // DBG puppet drop: removes DBG puppet's actor
            Remove(p);
            g_Delay = -2;
        }
        AP::Send(Info());
        return true;
    }
}
#endif
