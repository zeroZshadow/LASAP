// Multiplayer ghosts, the network half (DESIGN.md, Ghosts): while the client asks for it (GHOSTS 1), Link's position
// goes to the client as POS lines, which it relays to the other players; their positions come back as GHOST lines,
// kept here per ghost slot for the puppets.
#include "ghost/ghost_net.hpp"
#include "ap/ap.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_text.hpp"
#include "game/actors/player.hpp"
#include "game/animation.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/level.hpp"
#include "game/math.hpp"
#include "game/model.hpp"
#include "game/time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 20 Hz while anything changes, a keep-alive at 2 Hz while Link stands still. What the player does goes out at
    // once: an animation starting, a turn of kTurnDeg or more, starting or stopping to move; an animation's frame
    // running on is no change (the receiver plays it on). Each line carries the time it was taken: the receiver
    // replays them on that timeline, so their delivery may be uneven.
    constexpr u64 kMovingMs = 50;
    constexpr u64 kStillMs = 500;
    constexpr float kTurnDeg = 10;
    // A jump this far between two sent positions is a warp (respawn, warp, door within a level): the ghost snaps.
    constexpr float kWarpDistance = 4.0f;
    constexpr int kFlagWarped = 1, kFlagStill = 4, kFlagStarted = 8;
    // Link's animations (game 1.0.0): his animation controller plays rows of PlayerAnimation.gsheet by key (the row
    // index). Its slot 0 is the whole body, slot 1 his upper body (idle_carry while he holds something over his head,
    // none otherwise). The frame is in the controller's layered animation: layer i is slot i, its bound animation the
    // frame control (f32 frame at +0).
    using Game::Actors::PlayerAnimRow;
    constexpr size_t kAnimFrameCtrl = 0x8;
    // Link's look: his model component's instance's shapeVisible (Player_ShowItem shows the items he holds this way),
    // as many as the model object's shapes. His tunic is the inventory's upgrade bits, red first. His face: the
    // material animations "eye", "mouth" and "ear" on his material animation component, each held at the frame the
    // game sets for his expression.

    struct Sample {
        bool inLevel = false;
        std::string level;
        float x = 0, y = 0, z = 0, yaw = 0;
        int anim = 0, animFrame = 0, upperAnim = 0;  // AP::GhostNet::Anim
        u32 starts = 0;
        bool still = false;           // not moved since the previous frame
        AP::GhostNet::Look look;
    };

    bool g_Wanted = false;  // the client relays positions (GHOSTS 1)
    bool g_Names = true;    // name tags over the ghosts (GHOSTNAMES 1)
    Sample g_Sent;
    bool g_SentAny = false;
    u64 g_SentAt = 0;
    // Link's position in the previous frame, for still
    u32 g_Frame = 0;
    bool g_PrevValid = false, g_Still = false;
    float g_PrevX = 0, g_PrevY = 0, g_PrevZ = 0;
    AP::GhostNet::Anim g_Anim;
    const PlayerAnimRow* g_AnimRow = nullptr;
    AP::GhostNet::Look g_Look;
#ifdef LASAP_TEST
    // DBG ghostnet: Link's animation starts and the GHOST lines with a start, by frame
    std::vector<std::string> g_StartLog;
    u32 FrameNow() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->frameCount : 0;
    }
#endif
    u32 g_PosLines = 0;
    std::string g_LastPos;
    AP::GhostNet::Ghost g_Ghosts[AP::GhostNet::SLOTS];
    u32 g_GhostLines = 0;

    using Game::At;
    using Game::NowMs;

    std::vector<std::string> Split(const std::string& line, size_t max) {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < line.size() && out.size() < max) {
            size_t start = line.find_first_not_of(' ', pos);
            if (start == std::string::npos) break;
            size_t end = line.find(' ', start);
            if (end == std::string::npos || out.size() + 1 == max) end = line.size();
            out.push_back(line.substr(start, end - start));
            pos = end;
        }
        return out;
    }

    // PlayerAnimation.gsheet's row of an anim id (key + 1), or nullptr. The sheet: **PlayerAnimationSheet_ptr (a
    // shared_ptr).
    const PlayerAnimRow* AnimRow(int anim) {
        const auto* sheet = At<const Game::Actors::PlayerAnimSheet*>(*Game::PlayerAnimationSheet_ptr, 0);
        const Game::Actors::PlayerAnimTable* data = sheet != nullptr ? sheet->data : nullptr;
        if (data == nullptr || anim <= 0 || static_cast<u32>(anim) > data->count) return nullptr;
        return &data->rows[anim - 1];
    }

    // The row playing on slot 0 of Link's animation controller and its frame, or nullptr.
    const PlayerAnimRow* LinkAnimRow(const Game::Actors::Player* player, float& frame) {
        const Game::Actors::PlayerAnimController* ctrl = player->animCtrl;
        if (ctrl == nullptr || ctrl->slots.first == nullptr || ctrl->slots.empty()) return nullptr;
        const PlayerAnimRow* row = ctrl->slots[0].row;
        const Game::LayeredAnimation* animation = ctrl->animation;
        const u8* fc = nullptr;
        if (animation != nullptr && animation->layers.count > 0) {
            const Game::AnimationLayer* layer =
                animation->layers.layers.first != nullptr ? animation->layers.layers[0] : nullptr;
            const Game::BoundAnimation* bound = layer != nullptr ? layer->animation : nullptr;
            fc = At<const u8*>(bound != nullptr ? bound->obj : nullptr, kAnimFrameCtrl);
        }
        frame = At<float>(fc, 0);
        if (!std::isfinite(frame) || frame < 0) frame = 0;
        return row;
    }

    // Once a frame: Link's animation, and whether one started (another row, or the same one from its start again
    // unless it loops).
    void ReadAnim(const Game::Actors::Player* player) {
        float frame = 0;
        const PlayerAnimRow* row = LinkAnimRow(player, frame);
        int anim = row != nullptr ? row->key + 1 : 0;
        float prevFrame = g_Anim.frame / 10.0f;
        if (anim != g_Anim.anim || row != g_AnimRow ||
            (anim != 0 && frame + 0.5f < prevFrame && row->policy != Game::Actors::kPlayerAnimLoops)) {
            if (anim != 0) g_Anim.starts++;
#ifdef LASAP_TEST
            const char* name = anim != 0 ? row->fileName.data : nullptr;
            if (name != nullptr && g_StartLog.size() < 60) g_StartLog.push_back(std::string(name) + "@" + std::to_string(FrameNow()));
#endif
        }
        g_AnimRow = row;
        g_Anim.anim = anim;
        g_Anim.frame = std::min(static_cast<int>(frame * 10), 0xffff);
        // the upper body's row: slot 1
        const Game::Actors::PlayerAnimController* ctrl = player->animCtrl;
        bool layered = ctrl != nullptr && ctrl->slots.first != nullptr && ctrl->slots.size() >= 2;
        const PlayerAnimRow* upper = layered ? ctrl->slots[1].row : nullptr;
        g_Anim.upper = upper != nullptr ? upper->key + 1 : 0;
    }

    // The frame of one of Link's material animations by name (0 when he has none of that name).
    int MatFrame(const Game::MaterialAnimComponent* mat, std::string_view name) {
        int count = std::min(mat != nullptr ? mat->slotCount : 0, 16);
        for (int i = 0; i < count && mat->slots.first != nullptr; i++) {
            const Game::MatAnimSlot* slot = mat->slots[i];
            const Game::MaterialAnim* anim = slot != nullptr ? slot->anim : nullptr;
            if (anim == nullptr || anim->res == nullptr || Game::ResNameOf(anim->res) != name) continue;
            float frame = slot->lastFrame;
            return std::isfinite(frame) ? std::clamp(static_cast<int>(frame + 0.5f), 0, 0xff) : 0;
        }
        return 0;
    }

    AP::GhostNet::Look ReadLook(const Game::Actors::Player* player) {
        AP::GhostNet::Look look;
        const Game::ModelInstance* instance = player->modelComp != nullptr ? player->modelComp->instance : nullptr;
        const u8* shapes = instance != nullptr ? instance->shapeVisible.first : nullptr;
        const Game::ModelObj* model = instance != nullptr ? instance->Model() : nullptr;
        int count = model != nullptr ? std::min<int>(model->shapeCount, AP::GhostNet::SHAPE_COUNT) : 0;
        if (shapes != nullptr && count > 0) {
            look.shapes = 0;
            for (int i = 0; i < count; i++)
                if (shapes[i] != 0) look.shapes |= 1u << i;
        }
        const Game::Data::Inventory::InventoryItems* items = Game::Data::Inventory::Items();
        look.tunic = items != nullptr && items->UpgradeItems.ClothesRed    ? AP::GhostNet::TUNIC_RED
                     : items != nullptr && items->UpgradeItems.ClothesBlue ? AP::GhostNet::TUNIC_BLUE
                                                                           : AP::GhostNet::TUNIC_GREEN;
        look.eye = MatFrame(player->materialAnim, "eye");
        look.mouth = MatFrame(player->materialAnim, "mouth");
        look.ear = MatFrame(player->materialAnim, "ear");
        return look;
    }

    Sample Current() {
        Sample s;
        Game::Framework* fmwk = Game::GetFramework();
        // no player while a level loads
        if (fmwk == nullptr || fmwk->player == nullptr || fmwk->player->playerCollision == nullptr) return s;
        const Game::Coords& c = fmwk->player->playerCollision->base.node.coordsNew;
        if (!std::isfinite(c.pos.x) || !std::isfinite(c.pos.y) || !std::isfinite(c.pos.z)) return s;
        // leaving a level by its exit (the well's stairs), Link stands at the origin of the old level until the next
        // one loads, about a second: in no level
        if (c.pos.x == 0 && c.pos.y == 0 && c.pos.z == 0) return s;
        s.inLevel = true;
        s.level = LevelSystem::GetCurrentLevel();
        s.x = c.pos.x;
        s.y = c.pos.y;
        s.z = c.pos.z;
        // Link turns about the vertical axis: yaw in degrees, 0 = +z (the bottom of the screen), 90 = +x
        s.yaw = Game::YawDegrees(c.rotate);
        if (s.yaw < 0) s.yaw += 360;
        // AP::Tick runs once per input poll, which can be more than once a frame
        if (fmwk->frameCount != g_Frame) {
            g_Still = g_PrevValid && std::fabs(s.x - g_PrevX) < 0.001f && std::fabs(s.y - g_PrevY) < 0.001f &&
                      std::fabs(s.z - g_PrevZ) < 0.001f;
            g_Frame = fmwk->frameCount;
            g_PrevValid = true;
            g_PrevX = s.x;
            g_PrevY = s.y;
            g_PrevZ = s.z;
            ReadAnim(fmwk->player);
            g_Look = ReadLook(fmwk->player);
        }
        s.still = g_Still;
        s.anim = g_Anim.anim;
        s.animFrame = g_Anim.frame;
        s.upperAnim = g_Anim.upper;
        s.starts = g_Anim.starts;
        s.look = g_Look;
        return s;
    }

    float Turn(const Sample& a, const Sample& b) {
        float turn = std::fabs(a.yaw - b.yaw);
        return std::min(turn, 360 - turn);
    }

    // What the player did, sent at once: another level, animation, facing or look, or starting or stopping.
    bool Acted(const Sample& a, const Sample& b) {
        if (a.inLevel != b.inLevel || a.level != b.level) return true;
        return a.inLevel && (a.starts != b.starts || a.upperAnim != b.upperAnim || a.still != b.still ||
                             a.look != b.look || Turn(a, b) >= kTurnDeg);
    }

    bool Changed(const Sample& a, const Sample& b) {
        if (a.inLevel != b.inLevel || a.level != b.level) return true;
        if (!a.inLevel) return false;
        if (a.starts != b.starts || a.upperAnim != b.upperAnim || a.still != b.still || a.look != b.look) return true;
        return std::fabs(a.x - b.x) > 0.01f || std::fabs(a.y - b.y) > 0.01f || std::fabs(a.z - b.z) > 0.01f ||
               Turn(a, b) > 1.0f;
    }

    void SendPos(const Sample& s, u64 now) {
        int flags = 0;
        if (s.inLevel && (!g_SentAny || !g_Sent.inLevel || g_Sent.level != s.level ||
                          std::hypot(s.x - g_Sent.x, s.y - g_Sent.y, s.z - g_Sent.z) > kWarpDistance)) {
            flags |= kFlagWarped;
        }
        if (s.still) flags |= kFlagStill;
        if (g_SentAny && s.starts != g_Sent.starts) flags |= kFlagStarted;
        char buf[192];
        if (s.inLevel) {
            // POS <level> <x> <y> <z> <yaw> <anim> <anim frame> <upper anim> <flags> <time ms> <shapes> <tunic> <eye>
            // <mouth> <ear>
            snprintf(buf, sizeof(buf), "POS %s %.3f %.3f %.3f %.1f %d %d %d %d %u %u %d %d %d %d", s.level.c_str(), s.x,
                     s.y, s.z, s.yaw, s.anim, s.animFrame, s.upperAnim, flags, static_cast<u32>(now), s.look.shapes,
                     s.look.tunic, s.look.eye, s.look.mouth, s.look.ear);
        } else {
            snprintf(buf, sizeof(buf), "POS none");
        }
        g_LastPos = buf;
        AP::Send(g_LastPos);
        g_Sent = s;
        g_SentAny = true;
        g_SentAt = now;
        g_PosLines++;
    }
}

namespace AP::GhostNet {
    void Tick() {
        Sample s = Current();  // reads Link's animation once a frame (LinkAnim), sent or not
        if (!g_Wanted || !AP::IsClientConnected()) return;
        u64 now = NowMs();
        bool acted = g_SentAny && Acted(s, g_Sent);
        if (now - g_SentAt < kMovingMs && !acted) return;
        if (!g_SentAny || Changed(s, g_Sent) || now - g_SentAt >= kStillMs) SendPos(s, now);
    }

    void Reset() {
        g_Wanted = false;
        g_Names = true;
        g_SentAny = false;
        for (auto& ghost : g_Ghosts) ghost.active = false;
    }

    u64 g_HandleTicks = 0;  // time spent on GHOST lines (system ticks, 19.2 MHz), for DBG ghostnet

    void Handle(const std::string& line);

    void HandleLine(const std::string& line) {
        u64 t0 = Game::Ticks();
        Handle(line);
        g_HandleTicks += Game::Ticks() - t0;
    }

    void Handle(const std::string& line) {
        auto a = Split(line, 18);
        if (a[0] == "GHOSTS" && a.size() >= 2) {
            // GHOSTS 1|0: whether the client relays positions; with 0 no ghost shows
            g_Wanted = a[1] == "1";
            g_SentAny = false;
            if (!g_Wanted) {
                for (auto& ghost : g_Ghosts) ghost.active = false;
            }
            return;
        }
        if (a[0] == "GHOSTNAMES" && a.size() >= 2) {
            // GHOSTNAMES 1|0: whether name tags show over the ghosts
            g_Names = a[1] == "1";
            return;
        }
        if (a[0] != "GHOST" || a.size() < 3) return;
        int n = atoi(a[1].c_str());
        if (n < 0 || n >= SLOTS) return;
        Ghost& ghost = g_Ghosts[n];
        g_GhostLines++;
        if (a[2] == "off") {
            // GHOST <n> off: that player left this area or the run
            ghost.active = false;
            return;
        }
        if (a.size() < 18) return;
        // GHOST <n> <level> <x> <y> <z> <yaw> <anim> <anim frame> <upper anim> <flags> <time> <shapes> <tunic> <eye>
        // <mouth> <ear> <slot name>; time is the sender's clock (ms, 16 bits): unwrapped against the slot's previous
        // line, a new timeline after an off
        u16 time = static_cast<u16>(strtoul(a[11].c_str(), nullptr, 10));
        if (ghost.active && ghost.frames > 0) {
            ghost.timeMs += static_cast<s16>(static_cast<u16>(time - static_cast<u16>(ghost.timeMs)));
        } else {
            ghost.timeMs = 0x10000 + time;
            ghost.timelineFrom = ghost.frames;
        }
        ghost.active = true;
        ghost.level = a[2];
        ghost.x = strtof(a[3].c_str(), nullptr);
        ghost.y = strtof(a[4].c_str(), nullptr);
        ghost.z = strtof(a[5].c_str(), nullptr);
        ghost.yaw = strtof(a[6].c_str(), nullptr);
        ghost.anim = atoi(a[7].c_str());
        ghost.animFrame = atoi(a[8].c_str());
        ghost.upperAnim = atoi(a[9].c_str());
        ghost.flags = atoi(a[10].c_str());
        ghost.look.shapes = static_cast<u32>(strtoul(a[12].c_str(), nullptr, 10));
        ghost.look.tunic = atoi(a[13].c_str());
        ghost.look.eye = atoi(a[14].c_str());
        ghost.look.mouth = atoi(a[15].c_str());
        ghost.look.ear = atoi(a[16].c_str());
        ghost.name = a[17];
        ghost.receivedMs = NowMs();
#ifdef LASAP_TEST
        if ((ghost.flags & 8) && g_StartLog.size() < 60) {
            const char* name = AnimName(ghost.anim);
            g_StartLog.push_back("rx:" + std::string(name != nullptr ? name : "?") + "@" + std::to_string(FrameNow()));
        }
#endif
        ghost.history[ghost.frames % HISTORY] = Frame{ghost.timeMs, ghost.receivedMs, ghost.x, ghost.y, ghost.z,
                                                      ghost.yaw, ghost.anim, ghost.animFrame, ghost.upperAnim,
                                                      ghost.flags, ghost.look};
        ghost.frames++;
    }

    const Ghost& Get(int n) {
        return g_Ghosts[n];
    }

    bool ShowNames() {
        return g_Names;
    }

    void ToggleNames() {
        if (!g_Wanted) return;
        g_Names = !g_Names;
        AP::Send(std::string("GHOSTNAMES ") + (g_Names ? "1" : "0"));
        AP::ShowMusicToast(g_Names ? "Ghost names on" : "Ghost names off");
    }

    Anim LinkAnim() {
        return g_Anim;
    }

    Look LinkLook() {
        return g_Look;
    }

    const char* AnimName(int anim) {
        const PlayerAnimRow* row = AnimRow(anim);
        return row != nullptr ? row->fileName.data : nullptr;
    }

    bool AnimLoops(int anim) {
        const PlayerAnimRow* row = AnimRow(anim);
        return row != nullptr && row->policy == Game::Actors::kPlayerAnimLoops;
    }

#ifdef LASAP_TEST
    std::string State() {
        std::string s = "GHOSTNET wanted=" + std::to_string(g_Wanted) + " names=" + std::to_string(g_Names) + " pos=" + std::to_string(g_PosLines) +
                        " ghostlines=" + std::to_string(g_GhostLines) + " handle_us=" + std::to_string(Game::TicksToUs(g_HandleTicks)) +
                        " last=" + g_LastPos;
        for (int n = 0; n < SLOTS; n++) {
            const Ghost& g = g_Ghosts[n];
            if (!g.active) continue;
            char buf[256];
            const char* anim = AnimName(g.anim);
            const char* upper = AnimName(g.upperAnim);
            snprintf(buf, sizeof(buf),
                     " | %d %s %s %.2f,%.2f,%.2f yaw=%.0f anim=%s upper=%s flags=%d shapes=%x tunic=%d face=%d,%d,%d"
                     " frames=%u", n, g.name.c_str(), g.level.c_str(), g.x, g.y, g.z, g.yaw, anim != nullptr ? anim : "-",
                     upper != nullptr ? upper : "-", g.flags, g.look.shapes, g.look.tunic, g.look.eye, g.look.mouth,
                     g.look.ear, g.frames);
            s += buf;
        }
        if (!g_StartLog.empty()) {
            s += " | starts";
            for (auto& e : g_StartLog) s += " " + e;
            g_StartLog.clear();
        }
        return s;
    }
#endif
}
