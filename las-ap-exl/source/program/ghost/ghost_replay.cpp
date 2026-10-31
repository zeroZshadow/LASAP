// The puppets' replay (DESIGN.md, Ghosts): a puppet is fed frames (a ghost's lines, or Link's own path for DBG puppet),
// each stamped with the sender's clock, and shows them on that timeline a buffer behind: its position between the two
// frames around that time, its facing that of the last frame before it (Link turns in steps: a turn is not smoothed).
// The sender's clock maps to this one by the least delay seen, drifting up slowly, so a line that arrives late waits
// less instead of moving the ghost faster. The buffer covers how late the lines arrive past that delay: the lateness
// kLateShare of the last kLateLines stayed within, plus kLateMargin, from g_BufferMs up to g_BufferMaxMs (a link whose
// lines arrive unevenly, such as Wi-Fi or one that loses packets, which TCP holds the later lines back for). It moves
// there gradually, the replay running slower while it grows and faster while it shrinks, so it never jumps; a line later
// than the buffer is caught up with as below. Past the newest frame it goes on along the last segment for up to
// kExtrapolateMs, unless that frame stood still, then holds. A warped frame is jumped to at its time. A frame more than
// kResetMs behind the newest starts a new timeline. When new lines move the replay's position (the sender turned or
// stopped while the replay ran on past its newest line), the puppet does not jump: the difference is added to where it
// shows and fades out with the time constant kCorrectMs (unless it is over kCorrectMax units). A ghost that arrives (a
// warped frame: another level, a respawn, a warp) stays hidden from that frame's time until a frame after it moved,
// started or changed the animation, or kArriveHoldMs pass on its clock: a player entering a level stands still through
// its own screen's fade-in first (falling into the well: on the floor, then fall_from_top drops the model from above
// while the position sinks). It shows again with that frame, whose animation then plays unblended. An animation start
// in a frame the replay passed over (catching up) plays too, so the same animation started again is not missed.
#include "game/math.hpp"
#include "ghost/ghost_puppets_impl.hpp"
#include <algorithm>
#include <cmath>

namespace AP::Puppets {
    // Two lines' time (20 a second while moving), so the next line has mostly arrived when the replay needs it: a
    // shorter buffer runs on past the newest line and corrects when the next one comes in, which shows as jerks
    // (2026-10-15, 30 ghosts: per-frame speed spread 1.06 at 33 ms, 0.31 at 66, 0.24 at 100).
    // The least buffer, which a link that delivers evenly keeps. The most: a ghost further behind no longer looks like
    // the player beside Link, and kTrack lines are 1.6 s while moving.
    double g_BufferMs = 100, g_BufferMaxMs = 400;  // DBG puppet buffer
    // Animations: the sender's (its PlayerAnimation.gsheet key, named in this game's sheet, the same FSKA in the
    // puppet's model), from the frame it started at, played when the replay reaches that frame; for a sender that
    // sends none (anim id 0), by speed (units per second): each from its threshold on, with a margin either side so a
    // speed near one does not switch back and forth (kGaits).
    // Blend times in seconds (the layer adds the frame's time to its progress): Link's own (PlayerAnimation's
    // default, 0x16afc44) for the sender's animations, a little longer between gaits (kGaitBlend).
    float g_AnimBlend = 0.0667f;  // DBG puppet blend

namespace {
    constexpr double kArriveHoldMs = 1000;
    constexpr float kArriveMove = 0.05f;
    constexpr double kExtrapolateMs = 100, kOffsetRise = 0.02, kCorrectMs = 100;
    constexpr float kCorrectMax = 2;
    constexpr s64 kResetMs = 1000;
    // The buffer (2026-10-27, ghost_link: a runner over 60 ms of jitter held at its newest line in 10.1% of the frames
    // with 100 ms, 1.4% with these at 214 ms on average; over 2% of its frames 300 ms late 6.0% and 2.5% at 211 ms; an
    // even link stays at 100 ms).
    // It grows by kBufferGrow of the time passing (the replay at 0.85x) and shrinks by kBufferShrink (1.01x), so one
    // late burst does not set it swinging.
    constexpr double kLateShare = 0.95, kLateMargin = 20, kBufferGrow = 0.15, kBufferShrink = 0.01;
    constexpr float kGaitFrom[] = {0, 0.4f, 3.5f, 7.5f}, kGaitMargin = 0.3f;
    int GaitAt(float speed) {
        int g = 0;
        while (g + 1 < 4 && speed >= kGaitFrom[g + 1]) g++;
        return g;
    }

    Game::Vec3 PosOf(const Frame& f) { return Game::Vec3{f.x, f.y, f.z}; }

    // a + (b - a) * t
    Game::Vec3 Along(const Frame& a, const Frame& b, float t) {
        return Game::Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }

    // The replay at time t from the track's first count frames: the position, the speed, the frame at or before t and
    // how it was found ('i' between two frames, 'e' on past the last, 'h' holding the last, 'b' before them all).
    struct Replayed {
        Game::Vec3 pos;
        float speed = 0;
        u32 a = 0;
        char mode = 'b';
    };
    Replayed ReplayAt(const Track& tr, double t, u32 count) {
        Replayed r;
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        // the last frame at or before t (the oldest while t is before them all)
        r.a = count - 1;
        while (r.a > oldest && static_cast<double>(tr.frames[r.a % kTrack].timeMs) > t) r.a--;
        const Frame& fa = tr.frames[r.a % kTrack];
        r.pos = PosOf(fa);
        if (static_cast<double>(fa.timeMs) > t) return r;
        r.mode = 'h';
        if (r.a + 1 < count) {
            r.mode = 'i';
            const Frame& fb = tr.frames[(r.a + 1) % kTrack];
            float span = static_cast<float>(fb.timeMs - fa.timeMs);
            if (!fb.flags.Has(GhostFlag::Warped) && span > 0) {
                r.pos = Along(fa, fb, static_cast<float>(t - fa.timeMs) / span);
                r.speed = std::hypot(fb.x - fa.x, fb.z - fa.z) * 1000 / span;
            }
        } else if (!fa.flags.Any(GhostFlag::Still | GhostFlag::Warped) && r.a > oldest && t - fa.timeMs < kExtrapolateMs) {
            const Frame& fp = tr.frames[(r.a - 1) % kTrack];
            float span = static_cast<float>(fa.timeMs - fp.timeMs);
            if (span > 0) {
                r.mode = 'e';
                r.pos = Along(fp, fa, 1 + static_cast<float>(t - fa.timeMs) / span);
                r.speed = std::hypot(fa.x - fp.x, fa.z - fp.z) * 1000 / span;
            }
        }
        return r;
    }

    // Whether the replay at time t (frame r.a) holds the ghost hidden at its arrival (kArriveHoldMs).
    bool ArrivalHeld(Track& tr, const Replayed& r, double t) {
        if (tr.arrival == 0) return false;
        u32 w = tr.arrival - 1;
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        if (w < oldest) {
            tr.arrival = 0;
            return false;
        }
        if (r.a < w) return false;
        const Frame& fw = tr.frames[w % kTrack];
        bool released = t - static_cast<double>(fw.timeMs) >= kArriveHoldMs;
        for (u32 k = w + 1; k <= r.a && !released; k++) {
            const Frame& f = tr.frames[k % kTrack];
            released = f.flags.Has(GhostFlag::Started) || f.anim != fw.anim ||
                       std::hypot(f.x - fw.x, f.y - fw.y, f.z - fw.z) > kArriveMove;
        }
        if (released) tr.arrival = 0;
        return !released;
    }

    // The buffer the track's lines ask for: the lateness kLateShare of them stayed within, plus kLateMargin.
    double Target(const Track& tr) {
        u32 n = std::min<u32>(tr.count, kLateLines);
        float late[kLateLines];
        std::copy(tr.late, tr.late + n, late);
        u32 k = std::min(static_cast<u32>(n * kLateShare), n - 1);
        std::nth_element(late, late + k, late + n);
        return std::clamp(late[k] + kLateMargin, g_BufferMs, std::max(g_BufferMs, g_BufferMaxMs));
    }
}

    void ResetTrack(Track& tr) {
        int gait = tr.gait, anim = tr.anim;
        tr = Track{};
        tr.gait = gait;
        tr.anim = anim;
    }
    void Push(Track& tr, const Frame& f) {
        if (tr.count > 0) {
            const Frame& newest = tr.frames[(tr.count - 1) % kTrack];
            if (f.timeMs < newest.timeMs - kResetMs) ResetTrack(tr);
            else if (f.timeMs <= newest.timeMs) return;
        }
        double delay = static_cast<double>(f.receivedMs) - static_cast<double>(f.timeMs);
        if (tr.count == 0 || delay < tr.offset) tr.offset = delay;
        else tr.offset += (delay - tr.offset) * kOffsetRise;
        tr.late[tr.count % kLateLines] = static_cast<float>(delay - tr.offset);
        tr.frames[tr.count % kTrack] = f;
        tr.count++;
        tr.target = Target(tr);
        if (f.flags.Has(GhostFlag::Warped)) tr.arrival = tr.count;
    }
    // One frame of the replay; picks the animation from the speed.
    void ReplayFrame(Puppet& p, double now) {
        Track& tr = p.track;
        if (tr.count == 0) return;
        double dt = now - tr.lastNow;
        if (tr.shownFrames == 0) tr.buffer = tr.target;
        else if (dt > 0 && tr.buffer < tr.target) tr.buffer = std::min(tr.target, tr.buffer + dt * kBufferGrow);
        else if (dt > 0) tr.buffer = std::max(tr.target, tr.buffer - dt * kBufferShrink);
        double t = now - tr.offset - tr.buffer;
        Replayed r = ReplayAt(tr, t, tr.count);
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        // the animation starts in the frames reached since the last frame, the ones passed over included
        bool started = false;
        for (u32 k = std::max(tr.passed, oldest); k <= r.a; k++)
            started |= tr.frames[k % kTrack].flags.Has(GhostFlag::Started);
        tr.passed = std::max(tr.passed, r.a + 1);
        if (tr.replayed > oldest && tr.replayed < tr.count) {
            // new lines: where the replay had the puppet without them, less where it has it now
            bool warped = false;
            for (u32 k = tr.replayed; k < tr.count; k++) warped |= tr.frames[k % kTrack].flags.Has(GhostFlag::Warped);
            Game::Vec3 was = ReplayAt(tr, t, tr.replayed).pos;
            if (!warped) tr.error = Game::Vec3{tr.error.x + was.x - r.pos.x, tr.error.y + was.y - r.pos.y,
                                               tr.error.z + was.z - r.pos.z};
            else tr.error = Game::Vec3{};
        }
        tr.replayed = tr.count;
        bool held = tr.arriving;
        tr.arriving = ArrivalHeld(tr, r, t);
        // shown again: the animation from its start, not blended from the pose it held hidden
        float blend = held && !tr.arriving ? 0 : g_AnimBlend;
        float fade = tr.shownFrames > 0 && dt > 0 ? static_cast<float>(std::exp(-dt / kCorrectMs)) : 0;
        tr.error = Game::Vec3{tr.error.x * fade, tr.error.y * fade, tr.error.z * fade};
        if (std::fabs(tr.error.x) + std::fabs(tr.error.y) + std::fabs(tr.error.z) > kCorrectMax) {
#ifdef LASAP_TEST
            p.stats.snaps++;
#endif
            tr.error = Game::Vec3{};
        }
        Game::Vec3 pos{r.pos.x + tr.error.x, r.pos.y + tr.error.y, r.pos.z + tr.error.z};
        const Frame& fa = tr.frames[r.a % kTrack];
        float speed = r.speed;
        [[maybe_unused]] char mode = r.mode;
        if (tr.shownFrames > 0 && dt > 0)
            tr.moved[tr.shownFrames % 60] = static_cast<float>(std::hypot(pos.x - tr.pos.x, pos.z - tr.pos.z) * 1000 / dt);
        tr.shownFrames++;
        tr.lastNow = now;
#ifdef LASAP_TEST
        if (mode == 'e') g_Past++;
        if (mode == 'h' && !fa.flags.Has(GhostFlag::Still)) g_Held++;
        if (IndexOf(p) == g_TraceSlot) TraceReplay(now, t, fa.timeMs, r.a, tr.count, mode, pos.x, pos.z);
        auto& st = p.stats;
        st.frames++;
        if (mode == 'e') st.past++;
        if (mode == 'h' && !fa.flags.Has(GhostFlag::Still)) st.held++;
        // still catching up: a correction of a third of a unit or more shows
        if (std::fabs(tr.error.x) + std::fabs(tr.error.y) + std::fabs(tr.error.z) >= 0.3f) st.catchup++;
        if (tr.shownFrames > 1 && dt > 0) {
            double v = tr.moved[(tr.shownFrames - 1) % 60];
            st.speed += v;
            st.speedSq += v * v;
            st.speeds++;
        }
        st.buffer += tr.buffer;
        st.bufferMax = std::max(st.bufferMax, tr.buffer);
#endif
        tr.pos = pos;
        Game::Vec4 q = QuatOf(fa.yaw * Game::kPi / 180);
        Place(p, pos, &q);
        p.look = fa.look;
        // From the model's load on: the NPC plays its own idle as it loads.
        if (p.model == nullptr) return;
        SetUpper(p, fa.upperAnim);
        if (fa.anim > 0) {
            if (fa.anim != tr.anim || started) {
                if (const char* name = AP::GhostNet::AnimName(fa.anim)) PlayAnim(p, name, blend, fa.animFrame / 10.0f);
                tr.anim = fa.anim;
                tr.gait = -1;
            }
            return;
        }
        int gait = tr.gait < 0 ? GaitAt(speed) : std::clamp(tr.gait, GaitAt(speed - kGaitMargin),
                                                             GaitAt(speed + kGaitMargin));
        if (gait != tr.gait) {
            PlayAnim(p, kGaits[gait], blend == 0 ? 0 : kGaitBlend, 0);
            tr.gait = gait;
            tr.anim = -1;
        }
    }
}
