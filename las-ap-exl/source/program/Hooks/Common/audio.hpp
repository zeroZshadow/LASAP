#pragma once
#include "../lib.hpp"
#include <string>

inline long unkPtr = 0;
HOOK_DEFINE_TRAMPOLINE(AudioSystem__PlayBGM) {
    static long Callback(float volume, long arg2, long strAddr, uint8_t arg4, uint8_t arg5, uint8_t arg6) {
        if (unkPtr == 0) {
            unkPtr = arg2;
        }
        return Orig(volume, arg2, strAddr, arg4, arg5, arg6);
    }
};

HOOK_DEFINE_TRAMPOLINE(AudioSystem__PlaySoundEffect) {
    static long Callback(float arg1, float arg2, long strAddr, long arg4) {
        return Orig(arg1, arg2, strAddr, arg4);
    }
};

namespace AudioSystem {
    void PlayBGM(std::string bgm) {
        if (unkPtr == 0) {
            Logging.Log("Cannot play BGM due to missing pointer!");
        }
        else {
            AudioSystem__PlayBGM::Callback(1.0f, unkPtr, (long)&bgm, 1, 0, 0);
        }
    }

    void PlaySoundEffect(std::string soundEffect) {
        AudioSystem__PlaySoundEffect::Callback(1.0f, 1.0f, (long)&soundEffect, 0);
    }

    void InstallHooks() {
        AudioSystem__PlaySoundEffect::InstallAtOffset(0xa57e0);
        AudioSystem__PlayBGM::InstallAtOffset(0x9fd60);
    }
}
