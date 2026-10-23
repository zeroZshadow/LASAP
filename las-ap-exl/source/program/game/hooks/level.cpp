#include "game/hooks/level.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"
#include <cstring>

namespace {
    std::string g_CurrentLevel;

    HOOK_DEFINE_TRAMPOLINE(SetCurrentLevel) {
        static void Callback(Game::StrView* level, Game::StrView* locator, Game::Vec3* pos) {
            Orig(level, locator, pos);
            g_CurrentLevel.assign(level->data, level->size);
        }
    };

    // The save's current level (SaveCurrent, which Inventory_ptr points at): +0x0 the level, +0x40 the locator ($NONE:
    // the position at +0xdc), +0xdc the restart position.
    constexpr size_t kSaveLevel = 0x0, kSaveLocator = 0x40, kSavePosition = 0xdc;
    constexpr u32 kReloadFromSave = 4;

    void SetSaveText(u8* save, size_t at, const char* text) {
        size_t length = std::strlen(text);
        std::memcpy(save + at, text, length);
        save[at + length] = '\0';
    }
}

namespace LevelSystem {
    std::string GetCurrentLevel() { return g_CurrentLevel; }

    void LoadLevel(const char* level, Game::Vec3 pos) {
        Game::SetGameState(Game::GetFramework()->gameState, kReloadFromSave, false);
        auto* save = reinterpret_cast<u8*>(Game::Data::Inventory::Items());
        SetSaveText(save, kSaveLevel, level);
        SetSaveText(save, kSaveLocator, "$NONE");
        *reinterpret_cast<Game::Vec3*>(save + kSavePosition) = pos;
        EventFlags::SetFlag("OpTalked", true);
        EventFlags::SetFlag("ShieldGet", true);
    }

    void Install() {
        SetCurrentLevel::InstallAtFuncPtr(Game::SetCurrentLevel.Ptr());
    }
}
