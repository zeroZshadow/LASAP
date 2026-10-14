// Multiplayer ghosts, the network half (DESIGN.md, Ghosts): while the client asks for it (GHOSTS 1), Link's position
// goes to the client as POS lines, which it relays to the other players; their positions come back as GHOST lines,
// kept here per ghost slot for the puppets.
#include "AP/ap.hpp"
#include "Game/framework.hpp"
#include "Hooks/Common/level.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
    // 20 Hz while anything changes, a keep-alive at 2 Hz while Link stands still. What the player does goes out at
    // once: a new animation, a turn of kTurnDeg or more, starting or stopping; an animation's time running on is no
    // change (the receiver plays it on from anim_ms). Each line carries the time it was taken: the receiver replays
    // them on that timeline, so their delivery may be uneven.
    constexpr u64 kMovingMs = 50;
    constexpr u64 kStillMs = 500;
    constexpr float kTurnDeg = 10;
    // A jump this far between two sent positions is a warp (respawn, warp, door within a level): the ghost snaps.
    constexpr float kWarpDistance = 4.0f;
    constexpr int kFlagWarped = 1, kFlagStill = 4;

    struct Sample {
        bool inLevel = false;
        std::string level;
        int room = 255;
        float x = 0, y = 0, z = 0, yaw = 0;
        int anim = 0, animMs = 0;  // no animation ids yet
        bool still = false;        // not moved since the previous frame
    };

    bool g_Wanted = false;  // the client relays positions (GHOSTS 1)
    Sample g_Sent;
    bool g_SentAny = false;
    u64 g_SentAt = 0;
    // Link's position in the previous frame, for still
    u32 g_Frame = 0;
    bool g_PrevValid = false, g_Still = false;
    float g_PrevX = 0, g_PrevY = 0, g_PrevZ = 0;
    u32 g_PosLines = 0;
    std::string g_LastPos;
    AP::GhostNet::Ghost g_Ghosts[AP::GhostNet::SLOTS];
    u32 g_GhostLines = 0;

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

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

    // The overworld room around a position, (row - 1) * 16 + column (Field_<row><col>, col A = 0), as the relay
    // computes it; 255 outside Field.
    int RoomIndex(const std::string& level, float x, float z) {
        if (level != "Field") return 255;
        int col = std::min(15, std::max(0, static_cast<int>(std::floor(x / 15))));
        int row = std::min(15, std::max(0, static_cast<int>(std::floor(z / 12))));
        return row * 16 + col;
    }

    Sample Current() {
        Sample s;
        Game::Framework* fmwk = Game::GetFramework();
        // no player while a level loads
        if (fmwk == nullptr || fmwk->mPlayer == nullptr || fmwk->mPlayer->playerCollision == nullptr) return s;
        const Game::Coords& c = fmwk->mPlayer->playerCollision->coordsNew;
        if (!std::isfinite(c.pos.x) || !std::isfinite(c.pos.y) || !std::isfinite(c.pos.z)) return s;
        s.inLevel = true;
        s.level = LevelSystem::GetCurrentLevel();
        s.x = c.pos.x;
        s.y = c.pos.y;
        s.z = c.pos.z;
        // Link turns about the vertical axis: yaw in degrees, 0 = +z (the bottom of the screen), 90 = +x
        const Game::Vec4& q = c.rotate;
        s.yaw = std::atan2(2 * (q.x * q.z + q.t * q.y), 1 - 2 * (q.x * q.x + q.y * q.y)) * 180.0f / 3.14159265f;
        if (s.yaw < 0) s.yaw += 360;
        s.room = RoomIndex(s.level, s.x, s.z);
        // AP::Tick runs once per input poll, which can be more than once a frame
        if (fmwk->mFrameCount != g_Frame) {
            g_Still = g_PrevValid && std::fabs(s.x - g_PrevX) < 0.001f && std::fabs(s.y - g_PrevY) < 0.001f &&
                      std::fabs(s.z - g_PrevZ) < 0.001f;
            g_Frame = fmwk->mFrameCount;
            g_PrevValid = true;
            g_PrevX = s.x;
            g_PrevY = s.y;
            g_PrevZ = s.z;
        }
        s.still = g_Still;
        return s;
    }

    float Turn(const Sample& a, const Sample& b) {
        float turn = std::fabs(a.yaw - b.yaw);
        return std::min(turn, 360 - turn);
    }

    // What the player did, sent at once: another level, animation, facing, or starting or stopping.
    bool Acted(const Sample& a, const Sample& b) {
        if (a.inLevel != b.inLevel || a.level != b.level) return true;
        return a.inLevel && (a.anim != b.anim || a.still != b.still || Turn(a, b) >= kTurnDeg);
    }

    bool Changed(const Sample& a, const Sample& b) {
        if (a.inLevel != b.inLevel || a.level != b.level) return true;
        if (!a.inLevel) return false;
        if (a.anim != b.anim || a.still != b.still) return true;
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
        char buf[160];
        if (s.inLevel) {
            // POS <level> <room> <x> <y> <z> <yaw> <anim> <anim_ms> <flags> <time ms>; no animation ids yet
            snprintf(buf, sizeof(buf), "POS %s %d %.3f %.3f %.3f %.1f %d %d %d %u", s.level.c_str(), s.room, s.x, s.y,
                     s.z, s.yaw, s.anim, s.animMs, flags, static_cast<u32>(now));
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
        if (!g_Wanted || !AP::IsClientConnected()) return;
        u64 now = NowMs();
        Sample s = Current();
        bool acted = g_SentAny && Acted(s, g_Sent);
        if (now - g_SentAt < kMovingMs && !acted) return;
        if (!g_SentAny || Changed(s, g_Sent) || now - g_SentAt >= kStillMs) SendPos(s, now);
    }

    void Reset() {
        g_Wanted = false;
        g_SentAny = false;
        for (auto& ghost : g_Ghosts) ghost.active = false;
    }

    void HandleLine(const std::string& line) {
        auto a = Split(line, 12);
        if (a[0] == "GHOSTS" && a.size() >= 2) {
            // GHOSTS 1|0: whether the client relays positions; with 0 no ghost shows
            g_Wanted = a[1] == "1";
            g_SentAny = false;
            if (!g_Wanted) {
                for (auto& ghost : g_Ghosts) ghost.active = false;
            }
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
        if (a.size() < 12) return;
        // GHOST <n> <level> <x> <y> <z> <yaw> <anim> <anim_ms> <flags> <time> <slot name>; time is the sender's
        // clock (ms, 16 bits): unwrapped against the slot's previous line, a new timeline after an off
        u16 time = static_cast<u16>(strtoul(a[10].c_str(), nullptr, 10));
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
        ghost.animMs = atoi(a[8].c_str());
        ghost.flags = atoi(a[9].c_str());
        ghost.name = a[11];
        ghost.receivedMs = NowMs();
        ghost.history[ghost.frames % HISTORY] = Frame{ghost.timeMs, ghost.receivedMs, ghost.x, ghost.y, ghost.z,
                                                      ghost.yaw, ghost.anim, ghost.animMs, ghost.flags};
        ghost.frames++;
    }

    const Ghost& Get(int n) {
        return g_Ghosts[n];
    }

    std::string State() {
        std::string s = "GHOSTNET wanted=" + std::to_string(g_Wanted) + " pos=" + std::to_string(g_PosLines) +
                        " ghostlines=" + std::to_string(g_GhostLines) + " last=" + g_LastPos;
        for (int n = 0; n < SLOTS; n++) {
            const Ghost& g = g_Ghosts[n];
            if (!g.active) continue;
            char buf[200];
            snprintf(buf, sizeof(buf), " | %d %s %s %.2f,%.2f,%.2f yaw=%.0f flags=%d frames=%u", n, g.name.c_str(),
                     g.level.c_str(), g.x, g.y, g.z, g.yaw, g.flags, g.frames);
            s += buf;
        }
        return s;
    }
}
