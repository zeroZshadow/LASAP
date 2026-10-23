#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Game/sym.hpp"
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
        // The flag name is a {data, length} view (a libstdc++ std::string starts the same way).
        auto* name = reinterpret_cast<const std::pair<const char*, size_t>*>(strAddr);
        bool value;
        if (AP::MapFlag(std::string_view(name->first, name->second), value)) return value;
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
        return EventFlags__CheckFlag::Callback(reinterpret_cast<long>(Game::EventFlags_ptr.Ptr()), (long)&flagName);
    }

    inline void SetFlag(std::string flagName, bool flagState) {
        if (CheckFlag(flagName) != flagState) {
            EventFlags__SetFlag::Callback(reinterpret_cast<long>(Game::EventFlags_ptr.Ptr()), (long)&flagName, (long)flagState);
        }
    }

    inline void InstallHooks() {
        EventFlags__CheckGettingFlag::InstallAtOffset(offset_manager.Offset(0x8d2940));
        EventFlags__SetGettingFlag::InstallAtOffset(offset_manager.Offset(0x8d2a70));
        EventFlags__CheckFlag::InstallAtOffset(offset_manager.Offset(0xdfa920));
        EventFlags__SetFlag::InstallAtOffset(offset_manager.Offset(0xdfa7b0));
    }
}
