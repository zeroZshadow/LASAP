#include "game/hooks/eventflags.hpp"
#include "ap/ap.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"
#include "map/map_tracker.hpp"

namespace {
    HOOK_DEFINE_TRAMPOLINE(EventFlags_Check) {
        static bool Callback(void** flags, Game::StrView* name) {
            bool value;
            if (AP::MapTracker::Flag(name->View(), value)) return value;
            if (AP::GoalFlag(name->View(), value)) return value;
            return Orig(flags, name);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(EventFlags_Set) {
        static void Callback(void** flags, Game::StrView* name, bool value) {
            Orig(flags, name, value);
            // The Wind Fish event sets ShadowClear once the final boss is defeated.
            if (value && name->View() == "ShadowClear") AP::OnGoal();
        }
    };
}

namespace EventFlags {
    bool CheckGettingFlag(int itemId) { return Game::Item_CheckGettingFlag(itemId) != 0; }

    void SetGettingFlag(int itemId) { Game::Item_SetGettingFlag(itemId); }

    bool CheckFlag(std::string_view name) {
        Game::StrView view{name};
        return EventFlags_Check::Callback(Game::EventFlags_ptr.Ptr(), &view);
    }

    void SetFlag(const std::string& name, bool value) {
        if (CheckFlag(name) == value) return;
        Game::StrView view{name};
        EventFlags_Set::Callback(Game::EventFlags_ptr.Ptr(), &view, value);
    }

    void Install() {
        EventFlags_Check::InstallAtFuncPtr(Game::EventFlags_Check.Ptr());
        EventFlags_Set::InstallAtFuncPtr(Game::EventFlags_Set.Ptr());
    }
}
