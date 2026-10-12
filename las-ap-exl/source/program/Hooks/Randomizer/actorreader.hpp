#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Config/config.hpp"
#include "Hooks/Randomizer/enemies.hpp"
#include <string>

HOOK_DEFINE_INLINE(InterceptActorLoad) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        EXL_ASSERT(global_config.initialized);
        if (global_config.randomizer.enemies || global_config.randomizer.enemy_sizes) {
            // send the leb actor data offset to our enemy rando code
            EnemyRandomizer::RandomizeEnemy(ctx->X[26]);
        }
    }
};

namespace ActorReader {
    void InstallHooks() {
        InterceptActorLoad::InstallAtOffset(offset_manager.Offset(0x8e177c));
    }
}