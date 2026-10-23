// Settings LASR lists but never implemented (config.ini [tweaks])
#pragma once
#include "lib.hpp"
#include "offsets.hpp"
#include "Game/sym.hpp"
#include <Config/config.hpp>

namespace Tweaks {
    // The message window (talk, item-get and telop text) shows a page letter by letter in its state "Reading".
    // B during it skips to the page's end; InstantPage does the same on the state's first update
    // (MessageWindow_Reading).
    constexpr size_t kWindowText = 0xe18;            // text*
    constexpr size_t kWindowPageLetters = 0x1b6a;    // u16, letters in the current page
    constexpr size_t kWindowSkipping = 0x1d1a;       // u8, set by B: show pages whole, skip the text's pauses
    constexpr size_t kTextShown = 0x288;             // s32, letters shown

    // The low-health warning: the two calls that play SE_SY_HP_ALLEART (the first five beeps, then quieter ones)
    constexpr uintptr_t kLowHealthBeeps[] = {0xeabeb0, 0xeabf44};

    // An enemy's death picks the buff that replaces its drop: a Guardian Acorn once KillCountNoDamage reaches 14
    // (b.hs to it), else a Piece of Power once KillCount reaches maximum hearts + 32 (b.lt to the normal drop below it)
    constexpr uintptr_t kGuardianAcornBranch = 0x89c038;
    constexpr uintptr_t kPieceOfPowerBranch = 0x89c054;
    constexpr uintptr_t kNormalDrop = 0x89ba3c;

    // A hooked fish's line tension (fish +0x9e4) rises while Link reels against it and falls otherwise; its fight
    // states snap the line once it reaches the fish's limit. Easy Fishing stores 0 in place of the new tension.
    constexpr uintptr_t kLineTensionStore = 0xb5c534;  // str w8, [x20, #0x9e4]
    constexpr u32 kStoreZeroTension = 0xb909e69f;      // str wzr, [x20, #0x9e4]

    // Frame pacing (TESTING.md): the graphics system's init starts a vsync watcher (VsyncWatcher_Init: watcher,
    // system), and while it runs each frame ends by waiting for the display's next vsync, so a frame that misses one
    // waits for the next and the game drops from 60 to 30 fps at once. No 30 FPS Drops starts no watcher: frames end
    // when the GPU is done. "emulator": only where the kernel is no Atmosphere (mesosphere answers
    // svcGetInfo(InfoType_MesosphereMeta); Ryujinx and the yuzu-based emulators do not).

    bool OnEmulator() {
        u64 version = 0;
        return R_FAILED(svcGetInfo(&version, InfoType_MesosphereMeta, INVALID_HANDLE, 0));
    }
}

HOOK_DEFINE_TRAMPOLINE(InstantPage) {
    static void Callback(u8* window) {
        u8* text = *reinterpret_cast<u8**>(window + Tweaks::kWindowText);
        s32 letters = *reinterpret_cast<u16*>(window + Tweaks::kWindowPageLetters);
        if (text != nullptr && *reinterpret_cast<s32*>(text + Tweaks::kTextShown) < letters) {
            window[Tweaks::kWindowSkipping] = 1;
            Game::MessageText_ShowLetters(text, letters);
        }
        Orig(window);
    }
};

HOOK_DEFINE_TRAMPOLINE(NoVsyncWatcher) {
    static void Callback(u8*, u8*) {}
};

namespace Tweaks {
    void Install() {
        EXL_ASSERT(global_config.initialized);
        if (global_config.tweaks.instant_text) {
            InstantPage::InstallAtFuncPtr(Game::MessageWindow_Reading.Ptr());
        }
        if (global_config.tweaks.no_low_health_beep) {
            exl::patch::CodePatcher p(0);
            for (uintptr_t call : kLowHealthBeeps) {
                p.Seek(offset_manager.Offset(call));
                p.WriteInst(exl::armv8::inst::Nop());
            }
        }
        exl::patch::CodePatcher p(0);
        if (global_config.tweaks.no_guardian_acorn) {
            p.Seek(offset_manager.Offset(kGuardianAcornBranch));
            p.WriteInst(exl::armv8::inst::Nop());
        }
        if (global_config.tweaks.no_piece_of_power) {
            p.Seek(offset_manager.Offset(kPieceOfPowerBranch));
            p.WriteInst(exl::armv8::inst::Branch(-static_cast<int>(kPieceOfPowerBranch - kNormalDrop)));
        }
        if (global_config.tweaks.easy_fishing) {
            p.Seek(offset_manager.Offset(kLineTensionStore));
            p.WriteInst(exl::armv8::InstBitSet(kStoreZeroTension));
        }
        const std::string& drops = global_config.tweaks.no_30fps_drops;
        bool emulator = OnEmulator();
        if (drops == "true" || (drops == "emulator" && emulator)) {
            NoVsyncWatcher::InstallAtFuncPtr(Game::VsyncWatcher_Init.Ptr());
        }
        Logging.Log(std::string("[AP] ") + (emulator ? "emulator" : "Atmosphere") + ", no_30fps_drops=" + drops);
    }
}
