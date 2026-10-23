#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Config/config.hpp"
#include "Game/actor.hpp"
#include "Game/framework.hpp"
#include "Hooks/Common/eventflags.hpp"
#include <string>
#include <cstring>

inline std::string currentLevel = "";

HOOK_DEFINE_TRAMPOLINE(SetCurrentLevel) {
    static void Callback(long arg1, long arg2, long arg3, long arg4, long arg5) {
        Orig(arg1, arg2, arg3, arg4, arg5);
        std::string* levelNamePtr = reinterpret_cast<std::string*>(arg1);
        currentLevel = *levelNamePtr;
    }
};

HOOK_DEFINE_TRAMPOLINE(SetGameState) {
    static void Callback(Game::GameState* arg1, uint32_t arg2, bool arg3) {
        Orig(arg1, arg2, arg3);
    }
};

namespace LevelSystem {
    inline std::string GetCurrentLevel() {
        return currentLevel;
    }

    inline void LoadLevel(const char* level, Game::Vec3 pos) {
        Game::Framework* fmwk = Game::GetFramework();
        SetGameState::Callback(fmwk->mGameState, 4, false);

        uintptr_t* addr = reinterpret_cast<uintptr_t*>(g_symbols["InventoryPtr"]);
        char* charPtr = reinterpret_cast<char*>(*addr);
        std::string myString(charPtr);
        size_t len = std::strlen(level);
        std::memcpy(charPtr, level, len);
        charPtr[len] = '\0';

        char* setupPtr = reinterpret_cast<char*>(*addr + 0x40);
        const char* setup = "$NONE";
        size_t locLen = std::strlen(setup);
        std::memcpy(setupPtr, setup, locLen);
        setupPtr[locLen] = '\0';

        Game::Vec3* loadPos3 = reinterpret_cast<Game::Vec3*>(*addr + 0xdc);
        *loadPos3 = pos;

        // Since this is commonly used by the practice mod manager
        // we need to make sure certain flags are set
        // so the player can properly pause, collect rupees, etc
        EventFlags::SetFlag("OpTalked", true);
        EventFlags::SetFlag("ShieldGet", true);
    }

    inline void InstallHooks() {
        SetCurrentLevel::InstallAtOffset(offset_manager.Offset(0xdfa450));
        SetGameState::InstallAtOffset(offset_manager.Offset(0x87b780));
    }
}
