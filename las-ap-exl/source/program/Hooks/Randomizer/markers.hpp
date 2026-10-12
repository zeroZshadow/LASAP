#pragma once
#include "../lib.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Config/config.hpp"
#include <string>

/*
    TODO - I might still want to edit these to support non-randomizer
    to show heart pieces and seashells that the player HASNT obtained
    could be a nice option for casuals?
*/

inline const std::string heart_flags[] = {
    "MabeWellHeartGet",
    "SmallFishFirstGet",
    "DiamondIslandHeartGet"
};

HOOK_DEFINE_INLINE(CheckDisplayHeartPiece) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        EXL_ASSERT(global_config.initialized);
        if (global_config.randomizer.enabled) {
            // Check global flag at heart_flags[index] instead of HeartPiece[index]
            // Reverse condition so that the markers show what the player is missing
            int index = ctx->X[9];
            if (!EventFlags::CheckFlag(heart_flags[index]))
            {
                ctx->X[8] = 1ULL << index;
            }
            else {
                ctx->X[8] = 0;
            }
        }
    }
};

namespace MarkerTweaks {
    void InstallHooks() {
        CheckDisplayHeartPiece::InstallAtOffset(0xe91c28);
    }
}
