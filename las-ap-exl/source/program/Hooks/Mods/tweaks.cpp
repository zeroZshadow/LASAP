#include "Hooks/hooks.hpp"
#include "Config/config.hpp"
#include "Game/mem.hpp"
#include "Game/sym.hpp"
#include "patches.hpp"

// Settings LASR lists but never implemented (config.ini [tweaks])
namespace {
    using Game::Field;
    using Patches::Patch;
    namespace inst = exl::armv8::inst;

    // The message window (talk, item-get and telop text) shows a page letter by letter in its state "Reading".
    // B during it skips to the page's end; InstantPage does the same on the state's first update
    // (MessageWindow_Reading).
    constexpr size_t kWindowText = 0xe18;            // text*
    constexpr size_t kWindowPageLetters = 0x1b6a;    // u16, letters in the current page
    constexpr size_t kWindowSkipping = 0x1d1a;       // u8, set by B: show pages whole, skip the text's pauses
    constexpr size_t kTextShown = 0x288;             // s32, letters shown

    // The low-health warning: the two calls that play SE_SY_HP_ALLEART (the first five beeps, then quieter ones)
    constexpr Patch kNoLowHealthBeep[] = {{0xeabeb0, inst::Nop()}, {0xeabf44, inst::Nop()}};

    // An enemy's death picks the buff that replaces its drop: a Guardian Acorn once KillCountNoDamage reaches 14
    // (b.hs to it), else a Piece of Power once KillCount reaches maximum hearts + 32 (b.lt to the normal drop below
    // it, which the patch makes a b).
    constexpr Patch kNoGuardianAcorn[] = {{0x89c038, inst::Nop()}};
    constexpr Patch kNoPieceOfPower[] = {Patches::BranchTo(0x89c054, 0x89ba3c)};

    // A hooked fish's line tension (fish +0x9e4) rises while Link reels against it and falls otherwise; its fight
    // states snap the line once it reaches the fish's limit. Easy Fishing stores 0 in place of the new tension
    // (str w8, [x20, #0x9e4] -> str wzr, [x20, #0x9e4]).
    constexpr Patch kEasyFishing[] = {{0xb5c534, exl::armv8::InstBitSet(0xb909e69f)}};

    // Frame pacing (TESTING.md): the graphics system's init starts a vsync watcher (VsyncWatcher_Init: watcher,
    // system), and while it runs each frame ends by waiting for the display's next vsync, so a frame that misses one
    // waits for the next and the game drops from 60 to 30 fps at once. No 30 FPS Drops starts no watcher: frames end
    // when the GPU is done. "emulator": only where the kernel is no Atmosphere (mesosphere answers
    // svcGetInfo(InfoType_MesosphereMeta); Ryujinx and the yuzu-based emulators do not).
    bool OnEmulator() {
        u64 version = 0;
        return R_FAILED(svcGetInfo(&version, InfoType_MesosphereMeta, INVALID_HANDLE, 0));
    }

    HOOK_DEFINE_TRAMPOLINE(InstantPage) {
        static void Callback(u8* window) {
            u8* text = Field<u8*>(window, kWindowText);
            s32 letters = Field<u16>(window, kWindowPageLetters);
            if (text != nullptr && Field<s32>(text, kTextShown) < letters) {
                window[kWindowSkipping] = 1;
                Game::MessageText_ShowLetters(text, letters);
            }
            Orig(window);
        }
    };

    HOOK_DEFINE_REPLACE(NoVsyncWatcher) {
        static void Callback(u8*, u8*) {}
    };

    const char* FpsDropsName(FpsDrops drops) {
        return drops == FpsDrops::On ? "true" : drops == FpsDrops::Off ? "false" : "emulator";
    }
}

namespace Tweaks {
    void Install() {
        const auto& tweaks = global_config.tweaks;
        if (tweaks.instant_text) InstantPage::InstallAtFuncPtr(Game::MessageWindow_Reading.Ptr());
        if (tweaks.no_low_health_beep) Patches::Apply(kNoLowHealthBeep);
        if (tweaks.no_guardian_acorn) Patches::Apply(kNoGuardianAcorn);
        if (tweaks.no_piece_of_power) Patches::Apply(kNoPieceOfPower);
        if (tweaks.easy_fishing) Patches::Apply(kEasyFishing);
        bool emulator = OnEmulator();
        if (tweaks.no_30fps_drops == FpsDrops::On || (tweaks.no_30fps_drops == FpsDrops::Emulator && emulator)) {
            NoVsyncWatcher::InstallAtFuncPtr(Game::VsyncWatcher_Init.Ptr());
        }
        Logging.Log("[AP] %s, no_30fps_drops=%s", emulator ? "emulator" : "Atmosphere",
                    FpsDropsName(tweaks.no_30fps_drops));
    }
}
