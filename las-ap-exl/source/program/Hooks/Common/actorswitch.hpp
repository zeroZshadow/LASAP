#pragma once
#include "../lib.hpp"
#include "offsets.hpp"

namespace ActorSwitchManager {
    // The game's function that sets an actor switch: (switch holder, switch index, state).
    using SetActorSwitchFn = void (*)(long holder, long switchIndex, long state);
    constexpr uintptr_t kSetActorSwitch = 0x231b0;

    // addr is the actor's switch holder pointer (actor + 0x250).
    inline void SetSwitch(long addr, long switchIndex, bool state) {
        auto fn = reinterpret_cast<SetActorSwitchFn>(g_BaseAddress + offset_manager.Offset(kSetActorSwitch));
        fn(*(long*)addr, switchIndex, state & 1);
    }
}
