// The warp menu (DESIGN.md, Warp menu): holding Minus offers a warp to Tarin's house; a short press still opens the
// map.
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "game/hooks/level.hpp"
#include "game/time.hpp"
#include "loggers.hpp"

namespace AP::Bridge {
namespace {
    using Game::NowMs;

    // Holding Minus opens the warp menu; a short press still reaches the game (the map), sent when Minus is released.
    constexpr u64 kButtonMinus = 0x800;
    constexpr u64 kWarpMenuHoldMs = 1000;
    constexpr u64 kMinusReleasedMs = 60;   // no poll has seen Minus this long: it was released
    constexpr u64 kMinusTapMs = 120;       // how long the short press is passed on
    bool g_MinusHeld = false;
    bool g_MinusLong = false;              // held long enough for the menu
    bool g_WarpMenuWanted = false;         // the game thread opens the menu if it can
    bool g_WarpMenuShown = false;          // it did: releasing Minus sends nothing
    u64 g_MinusDownSince = 0;
    u64 g_MinusLastSeen = 0;
    u64 g_MinusTapUntil = 0;

    // Levels where a warp would break what runs there: the final dungeon, the Chamber Dungeons and their editor, the
    // fishing pond, the trendy game, the ending and Marin's event levels.
    bool WarpMenuBlockedLevel(const std::string& level) {
        static const char* kBlocked[] = {"Lv09WindFishsEgg", "PanelEdit", "FishingPond", kCraneGameLevel, "Ending",
                                         "EventMarin01", "EventMarin02"};
        if (level.starts_with("Panel")) return true;
        for (const char* b : kBlocked) {
            if (level == b) return true;
        }
        return false;
    }
}

    // Minus held long enough: opens the warp menu when the player is free, holds nothing and no item display
    // waits to start. The game does not tick this while paused (the map), so the menu needs Minus still held.
    void TickWarpMenu() {
        if (!g_WarpMenuWanted) return;
        g_WarpMenuWanted = false;
        if (g_MinusHeld && PlayerFree() && !PlayerCarrying() && !LinkDead() && ItemEventIdle() &&
            !WarpMenuBlockedLevel(LevelSystem::GetCurrentLevel())) {
            g_WarpMenuShown = static_cast<bool>(RequestCommonEvent(AP::WARP_MENU_ENTRY));
            Logging.Log("[AP] warp menu %s", g_WarpMenuShown ? "opened" : "could not be requested");
        }
    }

    void WarpMenuPad(u64& buttons) {
        u64 now = NowMs();
        // The game does not see Minus while it is held (TickWarpMenu). Each controller style is polled, so only a
        // poll that sees Minus counts; its release is noticed when no poll has seen it for a moment.
        if (g_MinusHeld && !(buttons & kButtonMinus) && now - g_MinusLastSeen > kMinusReleasedMs) {
            // Released: a short press reaches the game unless the menu opened.
            g_MinusHeld = false;
            if (!g_WarpMenuShown) g_MinusTapUntil = now + kMinusTapMs;
        }
        if (buttons & kButtonMinus) {
            if (!g_MinusHeld) {
                g_MinusHeld = true;
                g_MinusLong = false;
                g_WarpMenuShown = false;
                g_MinusDownSince = now;
            }
            g_MinusLastSeen = now;
            if (!g_MinusLong && now - g_MinusDownSince >= kWarpMenuHoldMs) {
                g_MinusLong = true;
                g_WarpMenuWanted = true;
            }
            buttons &= ~kButtonMinus;
        }
        if (now < g_MinusTapUntil) buttons |= kButtonMinus;
    }

    void WarpMenuDropMinus() {
        g_MinusHeld = false;
        g_MinusTapUntil = 0;
    }
}
