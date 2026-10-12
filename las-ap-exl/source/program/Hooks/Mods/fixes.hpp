// These are general fixes for issues with the vanilla game
// You probably want to keep these for your own romhacks :)

#pragma once
#include "../lib.hpp"
#include "offsets.hpp"

// Normally, the shadow tries to move towards its home position before starting the next phase
// The margin of error is only 0.006944389, so if it gets pushed slightly, it wont continue
// We change the margin of error to cover the whole room
// This also results in speeding up the fight ever so slightly :)
HOOK_DEFINE_INLINE(Deathl__SoftlockFix) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[1] = 50.0f; // vanilla ~= 0.006f
    }
};

namespace GeneralFixes {
    void InstallHooks() {
        Deathl__SoftlockFix::InstallAtOffset(offset_manager.Offset(0x6604dc));
    }
}
