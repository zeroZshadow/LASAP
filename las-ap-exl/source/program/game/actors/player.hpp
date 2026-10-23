#pragma once
#include "game/actor.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // Link's root component (PlayerCollision).
    struct CharCtrlComp : RootComp {
        u8 pad_e8[0x180 - sizeof(RootComp)];
        Vec3 vecA;                 // 0x180
        Vec3 vel;
        Vec3 gravity;
        Vec3 vecC;
    };

    // Link's actor (Framework::player); its first 0x2a8 bytes are the actor's.
    struct Player {
        u8 pad_0[0x2a8];
        RootComp* modelComp;       // 0x2a8: Link's model component, a shared_ptr
        void* modelCompCtrl;       // 0x2b0
        u8 pad_2b8[0x10];
        u8* materialAnim;          // 0x2c8: the material animation component (the tunic's colour on slot 4)
        u8 pad_2d0[0x8];
        CharCtrlComp* playerCollision;  // 0x2d8: his position
        u8 pad_2e0[0x4ed - 0x2e0];
        u8 state;                  // 0x4ed: 0 free (no talk, event or cutscene)
        u8 pad_4ee[0x1568 - 0x4ee];
        u8* animCtrl;              // 0x1568: the animation controller
        u8 pad_1570[0x19b0 - 0x1570];
        void* carried;             // 0x19b0: set while Link holds something over his head
        u8 pad_19b8[0x1b40 - 0x19b8];
        Coords respawnCoords;      // 0x1b40
    };
    static_assert(offsetof(Player, modelComp) == 0x2a8 && offsetof(Player, materialAnim) == 0x2c8);
    static_assert(offsetof(Player, playerCollision) == 0x2d8 && offsetof(Player, state) == 0x4ed);
    static_assert(offsetof(Player, animCtrl) == 0x1568 && offsetof(Player, carried) == 0x19b0);
    static_assert(offsetof(Player, respawnCoords) == 0x1b40);
}
