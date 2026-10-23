#pragma once
// Skeletal and material animation components: the fields the module reads (TESTING.md, the NPC and player
// animation rows).
#include "game/actor.hpp"
#include "game/model.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    // A layer of a layered animation component: its bound animation -> +0x0 -> +0x8 the frame control (f32 frame
    // at +0).
    struct AnimationLayer {
        const uintptr_t* vtable;
        const u8* bound;           // 0x8
    };
    namespace AnimationLayerVirtual {
        constexpr size_t kSetLoop = 0x30, kSetStart = 0x48;
    }

    // SkeletalAnimationComponent (vtable 0x1c49d70): an NPC's animation player, one layer.
    struct SkeletalAnimation {
        Entity entity;
        u8 pad_58[0x88 - 0x58];
        ModelInstance* instance;   // 0x88
        ModelObj* model;           // 0x90
        u8 pad_98[0x168 - 0x98];
        SkeletalModelComponent* modelComp;  // 0x168: the model component it animates
    };
    static_assert(offsetof(SkeletalAnimation, instance) == 0x88 && offsetof(SkeletalAnimation, model) == 0x90);
    static_assert(offsetof(SkeletalAnimation, modelComp) == 0x168);
    namespace SkeletalAnimationVirtual {
        constexpr size_t kSetFrame = 0xc0;
    }

    // The layered one (LayeredAnimation_vtable): Link's and the ghosts'. It keeps its model instance and object with
    // its layers; its +0x88 and +0x90 are not them.
    struct LayeredAnimation {
        Entity entity;
        u8 pad_58[0x168 - 0x58];
        SkeletalModelComponent* modelComp;  // 0x168
        u8 pad_170[0x1b0 - 0x170];
        u8 layerSet[0x8];          // 0x1b0: AnimationLayers_SetBoneMask, AnimationLayers_SetEnabled
        ModelInstance* instance;   // 0x1b8
        ModelObj* model;           // 0x1c0
        s32 layerCount;            // 0x1c8
        u8 pad_1cc[0x4];
        AnimationLayer** layers;   // 0x1d0
    };
    static_assert(offsetof(LayeredAnimation, modelComp) == 0x168 && offsetof(LayeredAnimation, layerSet) == 0x1b0);
    static_assert(offsetof(LayeredAnimation, instance) == 0x1b8 && offsetof(LayeredAnimation, model) == 0x1c0);
    static_assert(offsetof(LayeredAnimation, layerCount) == 0x1c8 && offsetof(LayeredAnimation, layers) == 0x1d0);

    struct MaterialAnim {
        u8 pad_0[0x18];
        const u8* res;             // 0x18: FMAA
    };
    static_assert(offsetof(MaterialAnim, res) == 0x18);

    // A slot of a material animation component: the animation it plays and its frame.
    struct MatAnimSlot {
        const uintptr_t* vtable;
        MaterialAnim* anim;        // 0x8
        u8 pad_10[0x14];
        float frame;               // 0x24
    };
    static_assert(offsetof(MatAnimSlot, anim) == 0x8 && offsetof(MatAnimSlot, frame) == 0x24);
    namespace MatAnimSlotVirtual {
        constexpr size_t kSetFrame = 0x48, kSetRate = 0x60;
    }

    // A material animation component (vtable 0x1c499f0 on Link).
    struct MaterialAnimComponent {
        u8 pad_0[0xa0];
        MatAnimSlot** slots;       // 0xa0
        u8 pad_a8[0x10];
        s32 slotCount;             // 0xb8
    };
    static_assert(offsetof(MaterialAnimComponent, slots) == 0xa0 && offsetof(MaterialAnimComponent, slotCount) == 0xb8);
}
