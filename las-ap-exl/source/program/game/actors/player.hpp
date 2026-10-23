#pragma once
#include "game/actor.hpp"
#include "game/animation.hpp"
#include "game/model.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // Link's root component (PlayerCollision).
    struct CharCtrlComp : Node {
        u8 pad_e8[0x180 - sizeof(Node)];
        Vec3 vecA;                 // 0x180
        Vec3 vel;
        Vec3 gravity;
        Vec3 vecC;
    };

    // A row of PlayerAnimation.gsheet: one of Link's animations, played by key (the row index); fileName names the
    // FSKA in his model.
    struct PlayerAnimRow {
        s32 key;
        u8 pad_4[0x4];
        const char* fileName;      // 0x8
        u8 pad_10[0xc];
        s32 policy;                // 0x1c: kPlayerAnimLoops
        u8 pad_20[0x28];
    };
    static_assert(offsetof(PlayerAnimRow, fileName) == 0x8 && offsetof(PlayerAnimRow, policy) == 0x1c);
    static_assert(sizeof(PlayerAnimRow) == 0x48);
    constexpr s32 kPlayerAnimLoops = 1;

    // PlayerAnimation.gsheet (**PlayerAnimationSheet_ptr): its data's rows.
    struct PlayerAnimTable {
        u8 pad_0[0x20];
        const PlayerAnimRow* rows; // 0x20
        u32 count;                 // 0x28
    };
    static_assert(offsetof(PlayerAnimTable, rows) == 0x20 && offsetof(PlayerAnimTable, count) == 0x28);
    struct PlayerAnimSheet {
        u8 pad_0[0x70];
        const PlayerAnimTable* data;  // 0x70
    };
    static_assert(offsetof(PlayerAnimSheet, data) == 0x70);

    struct PlayerAnimSlot {
        u8 pad_0[0x10];
        const PlayerAnimRow* row;  // 0x10: the row playing
    };
    static_assert(offsetof(PlayerAnimSlot, row) == 0x10 && sizeof(PlayerAnimSlot) == 0x18);

    // Link's animation controller: slot i plays on layer i of his layered animation (0 the whole body, 1 his upper
    // body from the bone spine_a up).
    struct PlayerAnimController {
        u8 pad_0[0x8];
        LayeredAnimation* animation;  // 0x8
        u8 pad_10[0x78 - 0x10];
        Vector<PlayerAnimSlot> slots; // 0x78
    };
    static_assert(offsetof(PlayerAnimController, animation) == 0x8 && offsetof(PlayerAnimController, slots) == 0x78);

    // Link's actor (Framework::player).
    struct Player {
        Actor actor;
        SkeletalModelComponent* modelComp;  // 0x2a8: Link's model component, a shared_ptr
        void* modelCompCtrl;       // 0x2b0
        u8 pad_2b8[0x10];
        MaterialAnimComponent* materialAnim;  // 0x2c8: the tunic's colour on slot 4, his face
        u8 pad_2d0[0x8];
        CharCtrlComp* playerCollision;  // 0x2d8: his position
        u8 pad_2e0[0x4ed - 0x2e0];
        u8 state;                  // 0x4ed: 0 free (no talk, event or cutscene)
        u8 pad_4ee[0x1568 - 0x4ee];
        PlayerAnimController* animCtrl;  // 0x1568
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
