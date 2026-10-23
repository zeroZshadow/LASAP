#pragma once
#include "lib.hpp"
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
    static bool Callback(void** flags, Game::StrView* name) {
        bool value;
        if (AP::MapFlag(name->View(), value)) return value;
        return Orig(flags, name);
    }
};

HOOK_DEFINE_TRAMPOLINE(EventFlags__SetFlag) {
    static void Callback(void** flags, Game::StrView* name, bool value) {
        Orig(flags, name, value);
        // The Wind Fish event sets ShadowClear once the final boss is defeated.
        if (value && name->View() == "ShadowClear") {
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

    inline bool CheckFlag(const std::string& flagName) {
        Game::StrView name{flagName};
        return EventFlags__CheckFlag::Callback(Game::EventFlags_ptr.Ptr(), &name);
    }

    inline void SetFlag(const std::string& flagName, bool flagState) {
        if (CheckFlag(flagName) != flagState) {
            Game::StrView name{flagName};
            EventFlags__SetFlag::Callback(Game::EventFlags_ptr.Ptr(), &name, flagState);
        }
    }

    inline void InstallHooks() {
        EventFlags__CheckGettingFlag::InstallAtFuncPtr(Game::Item_CheckGettingFlag.Ptr());
        EventFlags__SetGettingFlag::InstallAtFuncPtr(Game::Item_SetGettingFlag.Ptr());
        EventFlags__CheckFlag::InstallAtFuncPtr(Game::EventFlags_Check.Ptr());
        EventFlags__SetFlag::InstallAtFuncPtr(Game::EventFlags_Set.Ptr());
    }
}
