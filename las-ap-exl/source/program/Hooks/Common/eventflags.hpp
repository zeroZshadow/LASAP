#pragma once
#include "../lib.hpp"
// #include "Config/config.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include "AP/ap.hpp"
#include <string>

HOOK_DEFINE_TRAMPOLINE(EventFlags__CheckGettingFlag) {
    static int Callback(int itemID) {
        return Orig(itemID);
    }
};

HOOK_DEFINE_TRAMPOLINE(EventFlags__SetGettingFlag) {
    static void Callback(int itemID) {
        Orig(itemID);
    }
};

HOOK_DEFINE_TRAMPOLINE(EventFlags__CheckFlag) {
    static bool Callback(long arg1, long strAddr) {
        return Orig(arg1, strAddr);
    }
};

HOOK_DEFINE_TRAMPOLINE(EventFlags__SetFlag) {
    static void Callback(long arg1, long strAddr, uint arg3) {
        Orig(arg1, strAddr, arg3);
        // The Wind Fish event sets ShadowClear once the final boss is defeated.
        if (arg3 && *reinterpret_cast<std::string*>(strAddr) == "ShadowClear") {
            AP::OnGoal();
        }
        // EXL_ASSERT(global_config.initialized);
        // bool flagState = !!arg3;
        // if (EventFlags__CheckFlag::Callback(arg1, strAddr) != flagState) {
        //     Orig(arg1, strAddr, arg3);
            // if (global_config.debug_mode.enabled) {
            //     std::string* strPtr = reinterpret_cast<std::string*>(strAddr);
            //     std::string stateStr = flagState ? "true" : "false";
            //     Logging.Log("Set flag '" + *strPtr + "' to " + stateStr);
            // }
        // }
    }
};

namespace EventFlags {
    inline bool CheckGettingFlag(int itemID) {
        return !!EventFlags__CheckGettingFlag::Callback(itemID);
    }

    inline void SetGettingFlag(int itemID) {
        EventFlags__SetGettingFlag::Callback(itemID);
    }

    inline bool CheckFlag(std::string flagName) {
        return EventFlags__CheckFlag::Callback(g_symbols["EventFlagsPtr"], (long)&flagName);
    }

    inline void SetFlag(std::string flagName, bool flagState) {
        if (CheckFlag(flagName) != flagState) {
            EventFlags__SetFlag::Callback(g_symbols["EventFlagsPtr"], (long)&flagName, (long)flagState);
        }
    }

    inline void InstallHooks() {
        EventFlags__CheckGettingFlag::InstallAtOffset(offset_manager.Offset(0x8d2940));
        EventFlags__SetGettingFlag::InstallAtOffset(offset_manager.Offset(0x8d2a70));
        EventFlags__CheckFlag::InstallAtOffset(offset_manager.Offset(0xdfa920));
        EventFlags__SetFlag::InstallAtOffset(offset_manager.Offset(0xdfa7b0));
    }
}
