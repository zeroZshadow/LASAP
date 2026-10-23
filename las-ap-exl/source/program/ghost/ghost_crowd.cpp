// Crowds of ghosts (DESIGN.md, Ghosts): how many draw whole, in part or not at all, from the frame rate.
#include "game/actors/player.hpp"
#include "game/time.hpp"
#include "ghost/ghost_puppets_impl.hpp"
#include <algorithm>
#include <cmath>

namespace AP::Puppets {
    int g_FullLimit = 8;         // ghosts drawn whole
    int g_DrawnLimit = kGhosts;  // ghosts drawn at all
    u32 g_CrowdMask = kCrowdShapes;
    float g_KeepMargin = 2;      // DBG puppet keep
    u32 g_DetailChanges = 0;     // DBG puppet: ghosts' detail changed by Rank
    bool g_Adapt = true;         // DBG puppet lod <full> <drawn> fixes the limits, lod auto adapts them again
    float g_Fps = 60;

namespace {
    // Crowds. Each shape a ghost draws costs a draw call in each of two render passes, and on an emulator the draw
    // calls limit the frame rate (2026-10-15, Ryubing: 60 fps up to about 120 ghost shapes a pass, whatever their
    // size; a ghost in its usual look draws about 6). The nearest ghosts draw their whole look, the next ones only
    // kCrowdShapes (arms, tunic, face, hair), the farthest none. How many of each follows the frame rate: down by a
    // quarter after a second under kSlowFps, up by one after kRaiseMs at kSteadyFps or more, but back to a count that
    // was too slow only after kRetryMs. The kMinWhole nearest are always whole and the kMinDrawn nearest always drawn
    // (the game's own slow moments, as after a level load, which kSettleMs skips, are no reason to hide a few).
    // Ranked by distance, less g_KeepMargin per step of detail a ghost has: one near a limit keeps its detail until
    // another is that much nearer, so ghosts walking about at about the same distance do not switch back and forth.
    constexpr float kSlowFps = 55, kSteadyFps = 59;
    constexpr u64 kRaiseMs = 3000, kRetryMs = 30000, kSettleMs = 3000;
    constexpr int kMinWhole = 6, kMinDrawn = 16;
    u64 g_FpsStart = 0, g_LastChange = 0, g_TooSlowAt = 0, g_SettleUntil = 0;
    int g_TooSlowFull = kGhosts + 1, g_TooSlowDrawn = kGhosts + 1;  // the counts last found too slow
    u32 g_FpsFrame = 0;

    // Once a second: the frame rate, and the crowd limits after it.
    void Adapt(u32 frame, u64 now, int shown) {
        // a window spanning a level load (no ticks then) starts again
        if (g_FpsStart == 0 || now < g_FpsStart || frame < g_FpsFrame || now - g_FpsStart > 2000) {
            g_FpsStart = now;
            g_FpsFrame = frame;
            return;
        }
        if (now - g_FpsStart < 1000) return;
        g_Fps = static_cast<float>(frame - g_FpsFrame) * 1000 / static_cast<float>(now - g_FpsStart);
        g_FpsStart = now;
        g_FpsFrame = frame;
        if (!g_Adapt || shown == 0 || now < g_SettleUntil) return;
        int full = std::min(g_FullLimit, shown), drawn = std::min(g_DrawnLimit, shown);
        bool retry = now - g_TooSlowAt >= kRetryMs;
        if (g_Fps < kSlowFps) {
            if (full > kMinWhole) {
                g_TooSlowFull = full;
                g_FullLimit = std::max(kMinWhole, full - full / 4 - 1);
            } else if (drawn > kMinDrawn) {
                g_TooSlowDrawn = drawn;
                g_DrawnLimit = std::max(kMinDrawn, drawn - drawn / 4 - 1);
            }
            g_TooSlowAt = g_LastChange = now;
        } else if (g_Fps >= kSteadyFps && now - g_LastChange >= kRaiseMs) {
            if (drawn < shown && (drawn + 1 < g_TooSlowDrawn || retry)) g_DrawnLimit = drawn + 1;
            else if (drawn >= shown && full < shown && (full + 1 < g_TooSlowFull || retry)) g_FullLimit = full + 1;
            g_LastChange = now;
        }
    }
}

    void SettleCrowd() { g_SettleUntil = Game::NowMs() + kSettleMs; }

    // Each frame: the shown ghosts by distance from Link, nearest first, get their detail.
    void Rank(Game::Actors::Player* player, u32 frame, u64 now) {
        int order[kGhosts];
        int count = 0;
        for (int n = 0; n < kGhosts; n++) {
            if (g_Puppets[n].shown && g_Puppets[n].model != nullptr) order[count++] = n;
        }
        if (player->playerCollision != nullptr) {
            const Game::Vec3 at = player->playerCollision->base.node.coordsNew.pos;
            float key[kGhosts];
            for (int i = 0; i < count; i++) {
                const auto& p = g_Puppets[order[i]];
                key[order[i]] = std::hypot(p.track.pos.x - at.x, p.track.pos.z - at.z) - g_KeepMargin * p.detail;
            }
            std::sort(order, order + count, [&](int a, int b) { return key[a] < key[b]; });
        }
        for (int i = 0; i < count; i++) {
            int& detail = g_Puppets[order[i]].detail;
            int now = i < g_FullLimit ? 2 : i < g_DrawnLimit ? 1 : 0;
            if (now != detail) g_DetailChanges++;
            detail = now;
        }
        Adapt(frame, now, count);
    }
}
