#include "config/config.hpp"
#include "game/addr.hpp"
#include "hooks.hpp"
#include <cmath>
#include <numbers>

// Movement (config.ini [movement]): walk, swim and dash speeds, and 360 Movement.
namespace {
    // Where the player's speeds are loaded: walk and shield walk, swim, and dash (neutral and held), each base and
    // with a Piece of Power
    constexpr uintptr_t kWalk = 0xdcdea4, kPowerWalk = 0xdcff24, kShieldWalk = 0xdb80a8, kPowerShieldWalk = 0xdb8eac;
    constexpr uintptr_t kSwim = 0xdde288, kPowerSwim = 0xddf128, kDashNeutral = 0xda74a8, kDashHold = 0xda7100;
    // The player's stick direction snapped to 8 directions
    constexpr uintptr_t kSnapDirection = 0xded9f0;
    constexpr float kDashSpeed = 9.0f;

    HOOK_DEFINE_INLINE(WalkSpeed) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[0] = global_config.movement.walk_speed;
        }
    };

    HOOK_DEFINE_INLINE(PowerWalkSpeed) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[0] = global_config.movement.walk_speed * global_config.movement.pop_speed;
        }
    };

    HOOK_DEFINE_INLINE(SwimSpeed) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[2] = global_config.movement.swim_speed;
        }
    };

    HOOK_DEFINE_INLINE(PowerSwimSpeed) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[2] = global_config.movement.swim_speed * global_config.movement.pop_speed;
        }
    };

    HOOK_DEFINE_INLINE(DashSpeedNeutral) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[1] = kDashSpeed * global_config.movement.dash_speed;
        }
    };

    HOOK_DEFINE_INLINE(DashSpeedHold) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            ctx->S[2] = kDashSpeed * global_config.movement.dash_speed;
        }
    };

    // 360 Movement: the stick's angle in whole degrees instead of 8 directions
    HOOK_DEFINE_REPLACE(PlayerLink_SnapDirection) {
        static int Callback(double, float x, float y) {
            float angleRad = std::atan2f(y, x);
            int angleDeg = (int)std::floor(angleRad * 180.0 / std::numbers::pi);
            angleDeg += 90;
            if (angleDeg >= 360) angleDeg -= 360;
            return angleDeg * (int)std::round(0xffffffff / 360);
        }
    };
}

namespace Controls {
    void Install() {
        WalkSpeed::InstallAtOffset(Game::Offset(kWalk));
        PowerWalkSpeed::InstallAtOffset(Game::Offset(kPowerWalk));
        WalkSpeed::InstallAtOffset(Game::Offset(kShieldWalk));
        PowerWalkSpeed::InstallAtOffset(Game::Offset(kPowerShieldWalk));
        SwimSpeed::InstallAtOffset(Game::Offset(kSwim));
        PowerSwimSpeed::InstallAtOffset(Game::Offset(kPowerSwim));
        DashSpeedNeutral::InstallAtOffset(Game::Offset(kDashNeutral));
        DashSpeedHold::InstallAtOffset(Game::Offset(kDashHold));
        if (global_config.movement.full_360) PlayerLink_SnapDirection::InstallAtOffset(Game::Offset(kSnapDirection));
    }
}
