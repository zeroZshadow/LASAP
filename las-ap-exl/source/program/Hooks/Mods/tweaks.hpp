// Settings LASR lists but never implemented (config.ini [tweaks])
#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include <Config/config.hpp>

namespace Tweaks {
    // The message window (talk, item-get and telop text) shows a page letter by letter in its state "Reading".
    // B during it skips to the page's end; InstantPage does the same on the state's first update.
    constexpr uintptr_t kMessageReading = 0xef1960;  // void(window*): the Reading state's update
    constexpr uintptr_t kTextShowUpTo = 0xf53b60;    // void(text*, int letters): shows that many letters at once
    constexpr size_t kWindowText = 0xe18;            // text*
    constexpr size_t kWindowPageLetters = 0x1b6a;    // u16, letters in the current page
    constexpr size_t kWindowSkipping = 0x1d1a;       // u8, set by B: show pages whole, skip the text's pauses
    constexpr size_t kTextShown = 0x288;             // s32, letters shown
    using TextShowUpToFn = void (*)(u8*, s32);

    // The low-health warning: the two calls that play SE_SY_HP_ALLEART (the first five beeps, then quieter ones)
    constexpr uintptr_t kLowHealthBeeps[] = {0xeabeb0, 0xeabf44};
}

HOOK_DEFINE_TRAMPOLINE(InstantPage) {
    static void Callback(u8* window) {
        u8* text = *reinterpret_cast<u8**>(window + Tweaks::kWindowText);
        s32 letters = *reinterpret_cast<u16*>(window + Tweaks::kWindowPageLetters);
        if (text != nullptr && *reinterpret_cast<s32*>(text + Tweaks::kTextShown) < letters) {
            window[Tweaks::kWindowSkipping] = 1;
            reinterpret_cast<Tweaks::TextShowUpToFn>(g_BaseAddress + offset_manager.Offset(Tweaks::kTextShowUpTo))(
                text, letters);
        }
        Orig(window);
    }
};

namespace Tweaks {
    void Install() {
        EXL_ASSERT(global_config.initialized);
        if (global_config.tweaks.instant_text) {
            InstantPage::InstallAtOffset(offset_manager.Offset(kMessageReading));
        }
        if (global_config.tweaks.no_low_health_beep) {
            exl::patch::CodePatcher p(0);
            for (uintptr_t call : kLowHealthBeeps) {
                p.Seek(offset_manager.Offset(call));
                p.WriteInst(exl::armv8::inst::Nop());
            }
        }
    }
}
