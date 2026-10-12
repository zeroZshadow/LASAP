#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include <cmath>
#include <numbers>
#include <Config/config.hpp>
#include <Game/framework.hpp>
#include <Game/Actors/player.hpp>
#include <string>

HOOK_DEFINE_INLINE(BaseWalkSpeedMultiplier) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[0] = global_config.movement.walk_speed;
    }
};

HOOK_DEFINE_INLINE(PowerWalkSpeedMultiplier) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[0] = global_config.movement.walk_speed * global_config.movement.pop_speed;
    }
};

HOOK_DEFINE_INLINE(BaseSwimSpeedMultiplier) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[2] = global_config.movement.swim_speed;
    }
};

HOOK_DEFINE_INLINE(PowerSwimSpeedMultiplier) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[2] = global_config.movement.swim_speed * global_config.movement.pop_speed;
    }
};

HOOK_DEFINE_INLINE(DashSpeedNeutralMultiplier) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[1] = 9.0f * global_config.movement.dash_speed;
    }
};

HOOK_DEFINE_INLINE(DashSpeedHoldMultiplier) {
    static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
        ctx->S[2] = 9.0f * global_config.movement.dash_speed;
    }
};

// We replace the function for mapping 8 directions with our own code that maps to 360 degrees of movement
HOOK_DEFINE_REPLACE(PlayerLink__SnapDirection) {
    static int Callback(double arg1, float x, float y) {
        float angleRad = std::atan2f(y, x);
        int angleDeg = (int)std::floor(angleRad * 180.0 / std::numbers::pi);

        angleDeg += 90;
        if (angleDeg >= 360) {
            angleDeg -= 360;
        }

        return angleDeg * (int)std::round(0xffffffff / 360);
    }
};

namespace Controls {
    void InstallHooks() {
        EXL_ASSERT(global_config.initialized);

        // walk
        BaseWalkSpeedMultiplier::InstallAtOffset(offset_manager.Offset(0xdcdea4));
        PowerWalkSpeedMultiplier::InstallAtOffset(offset_manager.Offset(0xdcff24));

        // shield walk
        BaseWalkSpeedMultiplier::InstallAtOffset(offset_manager.Offset(0xdb80a8));
        PowerWalkSpeedMultiplier::InstallAtOffset(offset_manager.Offset(0xdb8eac));

        // swim
        BaseSwimSpeedMultiplier::InstallAtOffset(offset_manager.Offset(0xdde288));
        PowerSwimSpeedMultiplier::InstallAtOffset(offset_manager.Offset(0xddf128));

        // dash
        DashSpeedNeutralMultiplier::InstallAtOffset(offset_manager.Offset(0xda74a8));
        DashSpeedHoldMultiplier::InstallAtOffset(offset_manager.Offset(0xda7100));

        if (global_config.movement.full_360) {
            // 360 movement
            PlayerLink__SnapDirection::InstallAtOffset(offset_manager.Offset(0xded9f0));
        }
    }
}
