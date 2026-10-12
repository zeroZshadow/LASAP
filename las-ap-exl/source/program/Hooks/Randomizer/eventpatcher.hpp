#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include <string>

HOOK_DEFINE_TRAMPOLINE(EventFlow__ParseInt) {
    static int Callback(long arg1, long arg2) {
        return Orig(arg1, arg2);
    }
};

// Unused event for comparing 2 ints
// We repurpose this to simply just return the "value1" argument
// This gives us easy access to checking which dungeon an item belongs to when we can't access the index directly
HOOK_DEFINE_REPLACE(FlowControl__CompareInt) {
    static int Callback(long* arg1) {
        long unk = *arg1 + 0x28;
        std::string argument = "value1";
        int value1 = EventFlow__ParseInt::Callback((long)&unk, (long)&argument);
        Logging.Log(std::to_string(value1));
        return value1;
    }
};

namespace EventPatcher {
    void InstallHooks() {
        EventFlow__ParseInt::InstallAtOffset(offset_manager.Offset(0x109f790));
        FlowControl__CompareInt::InstallAtOffset(offset_manager.Offset(0x804960));
    }
}
