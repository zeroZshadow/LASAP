#include "game/addr.hpp"
#include "hooks.hpp"

// Fixes for issues of the vanilla game
namespace {
    // The Shadow (DethI) moves towards its home position before its next phase, within a margin of only 0.006944389:
    // pushed slightly, it never goes on. The margin covers the whole room instead, which also speeds the fight up a
    // little.
    constexpr uintptr_t kShadowHomeMargin = 0x6604dc;

    HOOK_DEFINE_INLINE(Deathl_HomeMargin) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[1] = 50.0f;
        }
    };
}

namespace Fixes {
    void Install() {
        Deathl_HomeMargin::InstallAtOffset(Game::Offset(kShadowHomeMargin));
    }
}
