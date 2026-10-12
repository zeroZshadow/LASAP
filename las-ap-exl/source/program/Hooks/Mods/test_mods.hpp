#pragma once
#include "../lib.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Hooks/Common/input.hpp"
#include "Game/actor.hpp"
#include "Game/Data/inventory.hpp"
#include <string>

uintptr_t hookshotActor = 0;
bool spawnHookshot = false;

HOOK_DEFINE_INLINE(BombHeight) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        if (spawnHookshot) {
            hookshotActor = (uintptr_t)ctx->X[21];
            if (hookshotActor != 0) {
                Game::Coords* hookshotCoords = (Game::Coords*)(hookshotActor + 0x260);
                hookshotCoords->pos.y += 3.0f;
                hookshotCoords->rotate.x = 0.0f;
                hookshotCoords->rotate.y = 0.0f;
                hookshotCoords->rotate.z = 0.0f;
            }
        }
    }
};

HOOK_DEFINE_INLINE(PlaceBomb) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        spawnHookshot = InputSystem::IsButtonPressed(InputSystem::NpadButton::R);
        if (spawnHookshot) {
            ctx->W[9] = 0x77; // ItemHookshot
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(UpdateBombCount) {
    static void Callback(int count) {
        if (count > 0) {
            Orig(count);
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(HasItemID) {
    static bool Callback(uint8_t id, int count, long index) {
        if (id == 6) {
            return false; // make game think the player does not have the hookshot so that the actor always spawns
        }
        else {
            return Orig(id, count, index);
        }
    }
};


namespace TestMods {
    void InstallHooks() {
        // BombHeight::InstallAtOffset(0xd8ffa8);
        // PlaceBomb::InstallAtOffset(0xd8fe58);
        // UpdateBombCount::InstallAtOffset(0xdf85b0);
        // HasItemID::InstallAtOffset(0x8d1a30);
    }
}