#include "Hooks/hooks.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Game/addr.hpp"

// The fishing pond's bottle: the game spawns it while the Bottle[1] bit (w8, 0x40) is clear; the bit given to it is
// the module's FishingBottleGet flag instead.
namespace {
    constexpr uintptr_t kCheckCanSpawnBottle = 0xe16da4;
    constexpr u32 kBottleBit = 0x40;

    HOOK_DEFINE_INLINE(TagFishingManager_CheckCanSpawnBottle) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            ctx->W[8] = EventFlags::CheckFlag("FishingBottleGet") ? kBottleBit : 0;
        }
    };
}

namespace Fishing {
    void Install() {
        TagFishingManager_CheckCanSpawnBottle::InstallAtOffset(Game::Offset(kCheckCanSpawnBottle));
    }
}
