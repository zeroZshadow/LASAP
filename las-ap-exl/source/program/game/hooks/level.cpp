#include "game/hooks/level.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"
#include <cstring>

namespace {
    std::string g_CurrentLevel;
    u32 g_LoadCount = 0;

    HOOK_DEFINE_TRAMPOLINE(SetCurrentLevel) {
        static void Callback(Game::StrView* level, Game::StrView* locator, Game::Vec3* pos) {
            Orig(level, locator, pos);
            g_CurrentLevel.assign(level->data, level->size);
            g_LoadCount++;
        }
    };

    // The level starts from the save's current level (SaveCurrent, which Inventory_ptr points at).
    constexpr u32 kReloadFromSave = 4;

    void SetSaveText(char* field, const char* text) {
        size_t length = std::strlen(text);
        std::memcpy(field, text, length);
        field[length] = '\0';
    }
}

namespace LevelSystem {
    std::string GetCurrentLevel() { return g_CurrentLevel; }
    u32 LoadCount() { return g_LoadCount; }

    void LoadLevel(const char* level, Game::Vec3 pos) {
        Game::SetGameState(Game::GetFramework()->gameState, kReloadFromSave, false);
        Game::Data::Inventory::InventoryItems* save = Game::Data::Inventory::Items();
        SetSaveText(save->LevelName, level);
        SetSaveText(save->Locator, "$NONE");
        save->RestartPos = pos;
        EventFlags::SetFlag("OpTalked", true);
        EventFlags::SetFlag("ShieldGet", true);
    }

    void Install() {
        SetCurrentLevel::InstallAtFuncPtr(Game::SetCurrentLevel.Ptr());
    }
}
