#pragma once
#include "lib.hpp"
#include "Game/sym.hpp"

namespace ActorSwitchManager {
    // addr is the actor's switch holder pointer (actor + 0x250).
    inline void SetSwitch(long addr, int switchIndex, bool state) {
        Game::ActorSwitch_Set(*reinterpret_cast<void**>(addr), switchIndex, state);
    }
}
