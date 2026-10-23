#pragma once
// Skeletal and material animation components (scripts/re/MODELS.md, Animation components).
#include "game/actor.hpp"
#include "game/model.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    // An animation bound to a model (Animation_Bind; 0x20 bytes): its nn::g3d SkeletalAnimObj (+0x8 the frame
    // control, f32 frame at +0), the resource and the FSKA.
    struct BoundAnimation {
        void* obj;
        SharedPtr resource;        // 0x8
        const u8* res;             // 0x18: FSKA
    };
    static_assert(offsetof(BoundAnimation, res) == 0x18 && sizeof(BoundAnimation) == 0x20);

    // An animation layer (vtable 0x1c472f8; 0x100 bytes, the masked kind 0x150): the animation it plays on a model
    // instance, blended from the previous one. The masked kind (the layered animation's) adds +0xfc enabled and a
    // bone mask vector at +0x100.
    struct AnimationLayer {
        const uintptr_t* vtable;
        BoundAnimation* animation; // 0x8: a shared_ptr
        void* animationCtrl;       // 0x10
        SharedPtr previous;        // 0x18: the blend's source
        ModelInstance* instance;   // 0x28
        ModelObj* model;           // 0x30
        u8 blender[0x20];          // 0x38: nn::g3d SkeletalAnimBlender
        float blendFrames;         // 0x58
        float blendElapsed;        // 0x5c
        u8 heldAnimation[0x18];    // 0x60: holds the current animation
        u8 pad_78[0x8];
        Coords offset;             // 0x80: identity when an animation is set
        u8 pad_b0[0x34];
        u8 field_e4;               // 0xe4
        u8 pad_e5[0x3];
        float weight;              // 0xe8
        u8 field_ec;               // 0xec
        u8 pad_ed[0x3];
        u8 paused;                 // 0xf0
        u8 updated;                // 0xf1
        u8 started;                // 0xf2
        u8 blending;               // 0xf3
        float lastFrame;           // 0xf4: for loop counting
        s32 loopCount;             // 0xf8
        u8 pad_fc[0x4];
    };
    static_assert(offsetof(AnimationLayer, animation) == 0x8 && offsetof(AnimationLayer, instance) == 0x28);
    static_assert(offsetof(AnimationLayer, model) == 0x30 && offsetof(AnimationLayer, offset) == 0x80);
    static_assert(offsetof(AnimationLayer, weight) == 0xe8 && offsetof(AnimationLayer, paused) == 0xf0);
    static_assert(offsetof(AnimationLayer, loopCount) == 0xf8 && sizeof(AnimationLayer) == 0x100);
    namespace AnimationLayerVirtual {
        constexpr size_t kSetLoop = 0x30, kSetStart = 0x48, kGetFrame = 0x50, kSetRate = 0x60;
    }

    // SkeletalAnimationComponent (NpcAnimator_Make, vtable 0x1c49d70; 0x1b0 bytes): an NPC's animation player, one
    // layer; its instance and model are the layer's.
    struct SkeletalAnimation {
        Entity entity;
        u8 pad_58[0x8];
        AnimationLayer layer;      // 0x60
        u8 field_160;              // 0x160
        u8 pad_161[0x3];
        u32 order;                 // 0x164: 0x400
        SkeletalModelComponent* modelComp;  // 0x168: a weak_ptr, locked each update
        void* modelCompCtrl;       // 0x170
        u8 pad_178[0x8];
        Function onAnimationSet;   // 0x180: the Tailor listener
    };
    static_assert(offsetof(SkeletalAnimation, layer) == 0x60 && offsetof(SkeletalAnimation, order) == 0x164);
    static_assert(offsetof(SkeletalAnimation, modelComp) == 0x168);
    static_assert(offsetof(SkeletalAnimation, onAnimationSet) == 0x180 && sizeof(SkeletalAnimation) == 0x1b0);
    namespace SkeletalAnimationVirtual {
        constexpr size_t kSetFrame = 0xc0, kGetFrame = 0xc8, kSetRate = 0xd8;
    }

    // The layers of a layered animation (AnimationLayers_Init, vtable 0x1c47438; 0x60 bytes): 1 to 8 masked layers
    // on one model instance, blended into its skeleton (AnimationLayers_SetBoneMask, AnimationLayers_SetEnabled).
    struct AnimationLayers {
        const uintptr_t* vtable;
        ModelInstance* instance;   // 0x8
        ModelObj* model;           // 0x10
        s32 count;                 // 0x18
        u8 pad_1c[0x4];
        Vector<AnimationLayer*> layers;  // 0x20: owned, masked
        u8 blender[0x20];          // 0x38: nn::g3d SkeletalAnimBlender
        u8 paused;                 // 0x58
        u8 pad_59[0x7];
    };
    static_assert(offsetof(AnimationLayers, instance) == 0x8 && offsetof(AnimationLayers, count) == 0x18);
    static_assert(offsetof(AnimationLayers, layers) == 0x20 && sizeof(AnimationLayers) == 0x60);

    // The layered one (LayeredAnimation_Make, LayeredAnimation_vtable; 0x220 bytes): Link's and the ghosts'. Its own
    // base layer is unused.
    struct LayeredAnimation {
        SkeletalAnimation base;
        AnimationLayers layers;    // 0x1b0
        s32 requestedLayers;       // 0x210
        u32 order;                 // 0x214
        u8 pad_218[0x8];
    };
    static_assert(offsetof(LayeredAnimation, layers) == 0x1b0 && offsetof(LayeredAnimation, order) == 0x214);
    static_assert(sizeof(LayeredAnimation) == 0x220);

    // A bound material animation (MaterialAnim_Bind, vtable 0x1c35150; 0x28 bytes).
    struct MaterialAnim {
        const uintptr_t* vtable;
        SharedPtr resource;        // 0x8
        const u8* res;             // 0x18: FMAA
        void* obj;                 // 0x20: nn::g3d MaterialAnimObj; null when binding failed
    };
    static_assert(offsetof(MaterialAnim, res) == 0x18 && sizeof(MaterialAnim) == 0x28);

    // A slot of a material animation component (vtable 0x1c35300; 0x30 bytes): the animation it plays.
    struct MatAnimSlot {
        const uintptr_t* vtable;
        MaterialAnim* anim;        // 0x8: a shared_ptr
        void* animCtrl;            // 0x10
        ModelObj* model;           // 0x18
        u8 paused;                 // 0x20
        u8 applied;                // 0x21
        u8 started;                // 0x22
        u8 pad_23;
        float lastFrame;           // 0x24: the frame at the slot's last update (MatAnimSlotVirtual::kGetFrame is live)
        s32 loopCount;             // 0x28
        u8 pad_2c[0x4];
    };
    static_assert(offsetof(MatAnimSlot, anim) == 0x8 && offsetof(MatAnimSlot, model) == 0x18);
    static_assert(offsetof(MatAnimSlot, lastFrame) == 0x24 && sizeof(MatAnimSlot) == 0x30);
    namespace MatAnimSlotVirtual {
        constexpr size_t kSetFrame = 0x48, kGetFrame = 0x50, kSetRate = 0x60;
    }

    // A material animation component with slots (MultiMaterialAnimation, vtable 0x1c499f0; 0xc0 bytes): Link's, an
    // NPC's.
    struct MaterialAnimComponent {
        Entity entity;
        MatAnimSlot baseSlot;      // 0x58: the single-slot base class's, unused
        u8 paused;                 // 0x88
        u8 pad_89[0x3];
        u32 order;                 // 0x8c: 0x400
        WeakPtr modelComp;         // 0x90
        Vector<MatAnimSlot*> slots;  // 0xa0: owned
        s32 slotCount;             // 0xb8: 1 to 8
        u8 pad_bc[0x4];
    };
    static_assert(offsetof(MaterialAnimComponent, baseSlot) == 0x58 && offsetof(MaterialAnimComponent, order) == 0x8c);
    static_assert(offsetof(MaterialAnimComponent, slots) == 0xa0 && offsetof(MaterialAnimComponent, slotCount) == 0xb8);
    static_assert(sizeof(MaterialAnimComponent) == 0xc0);
}
