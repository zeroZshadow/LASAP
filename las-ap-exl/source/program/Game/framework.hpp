#pragma once
#include "lib.hpp"
#include "symbols.hpp"
#include "Game/actorsystem.hpp"
#include "Game/gamestate.hpp"
#include "Game/Actors/player.hpp"

namespace Game {
    struct Framework {
        unsigned char padding_498[0x498];
        ActorSystem* mActorSystem; // 0x498
        unsigned char padding_4a8[0x4a8 - 0x498 - 0x8];
        GameState* mGameState; // 0x4a8
        unsigned char padding_4b8[0x8];
        Actors::Player* mPlayer; // 0x4b8
        unsigned char padding_4d4[0x4d4 - 0x4b8 - 0x8];
        uint32_t mFrameCount; // 0x4d4
    };

    inline Framework* GetFramework() {
        Framework** fwPtr = reinterpret_cast<Framework**>(g_symbols["FrameworkPtr"]);
        return *fwPtr;
    }
}