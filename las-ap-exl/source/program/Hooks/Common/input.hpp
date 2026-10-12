#pragma once
#include "lib.hpp"
#include "nn/hid.h"

namespace InputSystem {
    void MainLoop(nn::hid::NpadFullKeyState pad);
    void ModInputLoop();
    void InstallHooks();
}
