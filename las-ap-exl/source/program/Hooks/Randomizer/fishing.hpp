#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Config/config.hpp"

HOOK_DEFINE_INLINE(TagFishingManager__CheckCanSpawnBottle) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        // The game normally compares against the Bottle[1] bit
        // Only runs the spawn func if the bit is not set
        // We feed it a new value with the bit tied to our custom flag
        if (EventFlags::CheckFlag("FishingBottleGet")) {
            ctx->W[8] = 0b01000000;
        }
        else {
            ctx->W[8] = 0b00000000;
        }
    }
};

namespace FishingTweaks {
    void InstallHooks() {
        TagFishingManager__CheckCanSpawnBottle::InstallAtOffset(offset_manager.Offset(0xe16da4));
    }
}
