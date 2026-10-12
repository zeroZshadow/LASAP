#pragma once
#include "../lib.hpp"
#include "offsets.hpp"

HOOK_DEFINE_TRAMPOLINE(SetActorSwitch) {
    static void Callback(long arg1, long arg2, long arg3) {
        Orig(arg1, arg2, arg3);
    }
};

namespace ActorSwitchManager {
    void SetSwitch(long addr, long switchIndex, bool state) {
        SetActorSwitch::Callback(*(long*)addr, switchIndex, state & 1);
    }

    void InstallHooks() {
        SetActorSwitch::InstallAtOffset(offset_manager.Offset(0x231b0));
    }
}