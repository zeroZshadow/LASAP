#include "game/sym.hpp"
#include "hooks.hpp"

// The event action CompareInt, which no flow of the game uses, returns its value1 argument: LASR's event flows read
// which dungeon an item belongs to through it, where they cannot reach the index directly.
namespace {
    constexpr long kActionArgs = 0x28;

    HOOK_DEFINE_REPLACE(FlowControl_CompareInt) {
        static int Callback(long* action) {
            long args = *action + kActionArgs;
            Game::StrView name{"value1"};
            int value = Game::EventFlow_ParseInt(&args, &name);
            return value;
        }
    };
}

namespace EventPatcher {
    void Install() {
        FlowControl_CompareInt::InstallAtFuncPtr(Game::FlowControl_CompareInt.Ptr());
    }
}
