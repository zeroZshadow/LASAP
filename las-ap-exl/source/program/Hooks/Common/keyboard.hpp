#pragma once
#include "lib.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include "nn/hid.h"
#include <string>

nn::hid::KeyboardState keyboardState{};
nn::util::BitFlagSet<256, nn::hid::KeyboardKey> prevKeys;
nn::util::BitFlagSet<256, nn::hid::KeyboardKey> newKeys;
inline bool isInit = false;

namespace KeyboardManager {
    void Init() {
        if (!isInit) {
            nn::hid::InitializeKeyboard();
            isInit = true;
        }
    }

    bool IsKeyPressed(nn::hid::KeyboardKey key) {
        return newKeys.Test((int)key);
    }
    bool IsKeyJustPressed(nn::hid::KeyboardKey key) {
        return prevKeys.Test((int)key) == false && newKeys.Test((int)key) == true;
    }
    bool IsKeyJustReleased(nn::hid::KeyboardKey key) {
        return prevKeys.Test((int)key) == true && newKeys.Test((int)key) == false;
    }

    bool IsModifierOn(nn::hid::KeyboardModifier mod) {
        return keyboardState.mModifiers.Test((int)mod);
    }

    bool AreKeysFreed() {
        return newKeys.IsAllOff();
    }

    void MainLoop() {
        nn::hid::GetKeyboardState(&keyboardState);
        prevKeys = newKeys;
        newKeys = keyboardState.mKeys;
    }
}
