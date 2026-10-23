#pragma once
// NPC actors and their NPC component: the fields the module reads (TESTING.md, NPC rows).
#include "game/actor.hpp"
#include "game/animation.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // The facial animations an NPC's Facial.gsheet row names: count slots, one more for each extra set.
    struct FacialSet {
        u8 pad_0[0x18];
        s32 extraA;                // 0x18
        u8 pad_1c[0xc];
        s32 extraB;                // 0x28
        u8 pad_2c[0xc];
        s32 count;                 // 0x38
    };
    static_assert(offsetof(FacialSet, extraA) == 0x18 && offsetof(FacialSet, extraB) == 0x28);
    static_assert(offsetof(FacialSet, count) == 0x38);

    struct FacialControl {
        u8 pad_0[0x43];
        u8 flags;                  // 0x43: kFacialNoSlots
        u8 pad_44[0xf0 - 0x44];
        const FacialSet* set;      // 0xf0
    };
    static_assert(offsetof(FacialControl, flags) == 0x43 && offsetof(FacialControl, set) == 0xf0);
    constexpr u8 kFacialNoSlots = 1u << 6;

    // An NPC's component: its Npc.gsheet row, its animation player and its material animations.
    struct NpcComponent {
        Entity entity;
        Entity* owner;             // 0x58: the NPC actor
        u8 pad_60[0x88 - 0x60];
        const void* row;           // 0x88: the Npc.gsheet row (kNpcRowSize bytes)
        u8 pad_90[0xe8 - 0x90];
        void* animator;            // 0xe8: a SkeletalAnimation, or a LayeredAnimation (its vtable)
        u8 pad_f0[0x198 - 0xf0];
        FacialControl* facial;     // 0x198
        u8 pad_1a0[0x1b8 - 0x1a0];
        MaterialAnimComponent* materialAnim;  // 0x1b8
    };
    static_assert(offsetof(NpcComponent, owner) == 0x58 && offsetof(NpcComponent, row) == 0x88);
    static_assert(offsetof(NpcComponent, animator) == 0xe8 && offsetof(NpcComponent, facial) == 0x198);
    static_assert(offsetof(NpcComponent, materialAnim) == 0x1b8);
    constexpr size_t kNpcRowSize = 0x220;

    // An NPC actor (NpcGhost and the NPC classes like it).
    struct NpcActor {
        Actor actor;
        u8 pad_2a8[0x8];
        NpcComponent* npc;         // 0x2b0
    };
    static_assert(offsetof(NpcActor, npc) == 0x2b0);
}
