#pragma once
// Link's actor, his character controller and his animation controller (scripts/re/PLAYER.md).
#include "game/actor.hpp"
#include "game/animation.hpp"
#include "game/model.hpp"
#include "game/sheet.hpp"
#include "game/state_machine.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // A character controller component (CharCtrlComp_Ctor, vtable 0x1c50038; 0x1d0 bytes): a node with a physics
    // body that Move moves by the pending move and the fall velocity.
    struct CharCtrlComp {
        Node node;
        const uintptr_t* vtable2;  // 0x148: an interface (vtable 0x1c50138): filter, enabled, shapes
        void* filter;              // 0x150: filter words, bit 0 of the first disabled
        void* body;                // 0x158: the physics body
        WeakPtr owner;             // 0x160
        void* field_170;           // 0x170: the owner's +0xc0, passed to the move
        u8 pad_178[0x8];
        Vec4 pendingMove;          // 0x180: added by callers, consumed by Move
        Vec4 lastMove;             // 0x190: the position after Move minus before
        Vec4 gravity;              // 0x1a0: (0, -9.80665, 0)
        Vec4 fallVelocity;         // 0x1b0: grows by gravity while useGravity, reset on the ground
        float maxFallSpeed;        // 0x1c0: 20
        bool useGravity;           // 0x1c4
        u8 pad_1c5[0x3];
        float maxMove;             // 0x1c8: 50, a step's length limit
        bool enabled;              // 0x1cc: the collision filter
        bool moving;               // 0x1cd: during Move
        u8 pad_1ce[0x2];
    };
    static_assert(offsetof(CharCtrlComp, vtable2) == 0x148 && offsetof(CharCtrlComp, owner) == 0x160);
    static_assert(offsetof(CharCtrlComp, pendingMove) == 0x180 && offsetof(CharCtrlComp, fallVelocity) == 0x1b0);
    static_assert(offsetof(CharCtrlComp, maxFallSpeed) == 0x1c0 && offsetof(CharCtrlComp, enabled) == 0x1cc);
    static_assert(sizeof(CharCtrlComp) == 0x1d0);

    // Link's root component (PlayerCollision_Make, vtable 0x1c501f0; 0x290 bytes): his position.
    struct PlayerCollision {
        CharCtrlComp base;
        u8 pad_1d0[0x8];
        Vector<u8> vector_1d8;     // 0x1d8: 0x70-byte elements
        Vector<u8> vector_1f0;     // 0x1f0: 0x60-byte elements
        Vector<u8> vector_208;     // 0x208
        u8 listener[0x38];         // 0x220: vtable 0x1c50360, registered with the maker's params
        u8 pad_258[0x8];
        Vec4 up;                   // 0x260: (0, 1, 0)
        float gravitySign;         // 0x270: -1 when gravity points down
        u32 field_274;             // 0x274
        float field_278[2];        // 0x278: 0.02, 1
        u32 field_280;             // 0x280
        u8 field_284[0x8];         // 0x284
        u8 pad_28c[0x4];
    };
    static_assert(offsetof(PlayerCollision, vector_1d8) == 0x1d8 && offsetof(PlayerCollision, listener) == 0x220);
    static_assert(offsetof(PlayerCollision, up) == 0x260 && sizeof(PlayerCollision) == 0x290);

    // A row of PlayerAnimation.gsheet: one of Link's animations, played by key (the row index); fileName names the
    // FSKA in his model.
    struct PlayerAnimRow {
        s32 key;
        u8 pad_4[0x4];
        SheetString fileName;      // 0x8
        float playRatio;           // 0x18
        s32 policy;                // 0x1c: kPlayerAnimLoops
        s32 nextKey;               // 0x20: played when the layer ends, -1 none
        bool rootMotionStepOffset; // 0x24
        bool requestCalculateBounding;  // 0x25
        u8 pad_26[0x2];
        s32 facialEye;             // 0x28
        s32 facialMouth;           // 0x2c
        s32 facialEar;             // 0x30
        u8 pad_34[0x4];
        SheetString facialName;    // 0x38
    };
    static_assert(offsetof(PlayerAnimRow, fileName) == 0x8 && offsetof(PlayerAnimRow, policy) == 0x1c);
    static_assert(offsetof(PlayerAnimRow, nextKey) == 0x20 && offsetof(PlayerAnimRow, facialEye) == 0x28);
    static_assert(offsetof(PlayerAnimRow, facialName) == 0x38 && sizeof(PlayerAnimRow) == 0x48);
    constexpr s32 kPlayerAnimLoops = 1;

    // PlayerAnimation.gsheet (**PlayerAnimationSheet_ptr) and its data.
    using PlayerAnimSheet = Sheet<PlayerAnimRow>;
    using PlayerAnimTable = SheetData<PlayerAnimRow>;

    struct PlayerAnimSlot {
        SharedPtr sheetData;       // keeps the sheet data alive
        const PlayerAnimRow* row;  // 0x10: the row playing
    };
    static_assert(offsetof(PlayerAnimSlot, row) == 0x10 && sizeof(PlayerAnimSlot) == 0x18);

    // Link's animation controller (PlayerAnimation_Ctor, vtable 0x1c17220; 0x98 bytes): slot i plays on layer i of
    // his layered animation (0 the whole body, 1 his upper body from the bone spine_a up).
    struct PlayerAnimController {
        const uintptr_t* vtable;
        LayeredAnimation* animation;  // 0x8: a shared_ptr
        void* animationCtrl;       // 0x10
        SkeletalModelComponent* model;  // 0x18: a shared_ptr
        void* modelCtrl;           // 0x20
        SharedPtr facial;          // 0x28: the FacialComp; a play sets the row's expression on it
        UnorderedMap anims;        // 0x38: by key: the row and its animation bound to the model
        Vector<SharedPtr> layerAnims;  // 0x60: per layer, the animation set on it
        Vector<PlayerAnimSlot> slots;  // 0x78: per layer
        bool facialEnabled;        // 0x90
        u8 pad_91[0x7];
    };
    static_assert(offsetof(PlayerAnimController, animation) == 0x8 && offsetof(PlayerAnimController, anims) == 0x38);
    static_assert(offsetof(PlayerAnimController, slots) == 0x78 && sizeof(PlayerAnimController) == 0x98);

    // Link's states (PlayerSM_ChangeState); PlayerState_*_vtable in las.sym.
    namespace PlayerState {
        constexpr u8 kLocomotion = 0, kJump = 1, kSwordAttack = 2, kFall = 3, kTalk = 4, kOpen = 5, kPush = 6,
                     kPull = 7, kCarry = 8, kDash = 9, kGuard = 10, kDamage = 11, kOcarina = 12, kMagicPower = 13,
                     kLadder = 14, kSwordChargeAttack = 15, kMoveTarget = 16, kSwordPoke = 17, kDeath = 18,
                     kResurrection = 19, kThrow = 20, kFallAbyss = 21, kDemoEvent = 22, kBlownAway = 23,
                     kBoomerang = 24, kBow = 25, kHookshot = 26, kMagicRod = 27, kBomb = 28, kWarp = 29, kSwim = 30,
                     kForcePlayAnim = 31, kShovel = 32, kBottle = 33, kFishing = 34, kDoPush = 35, kNone = 0xff;
    }

    // Player::flags.
    namespace PlayerFlags {
        constexpr u32 kInEvent = 1u << 0, kCarrying = 1u << 2, kSetUp = 1u << 7, kNoInteract = 1u << 8;
    }

    // Link's actor (Player_Ctor, Player_vtable 0x1c17b70; 0x1f90 bytes; Framework::player). The components are
    // shared_ptrs named for the component names Player_Setup makes them with. Health is the inventory's.
    struct Player {
        Actor actor;
        SkeletalModelComponent* modelComp;  // 0x2a8: a shared_ptr, model link
        void* modelCompCtrl;       // 0x2b0
        LayeredAnimation* animator;  // 0x2b8: a shared_ptr; 2 layers, layer 1 masked to spine_a
        void* animatorCtrl;        // 0x2c0
        MaterialAnimComponent* materialAnim;  // 0x2c8: a shared_ptr; clothes on slot 4, the mirror shield on 6
        void* materialAnimCtrl;    // 0x2d0
        PlayerCollision* playerCollision;  // 0x2d8: a shared_ptr; his position
        void* playerCollisionCtrl; // 0x2e0
        SharedPtr damageCollision; // 0x2e8: DamageColComp
        SharedPtr eventChara;      // 0x2f8: EventChara, chara Link
        SharedPtr eventEntity;     // 0x308: destroyed at an event's end
        SharedPtr camera;          // 0x318: CameraCom
        SharedPtr cameraShaker;    // 0x328
        SharedPtr boneEffect;      // 0x338
        SharedPtr damageColor;     // 0x348: the flash on a hit
        SharedPtr waterEvent;      // 0x358
        SharedPtr facial;          // 0x368: FacialComp
        SharedPtr eventSensor;     // 0x378
        SharedPtr itemSensor;      // 0x388
        SharedPtr ladderInfo;      // 0x398
        void* owned_3a8;           // 0x3a8: updated each frame
        SharedPtr locItemAL;       // 0x3b0: bone locators: itemA_L
        SharedPtr locItemAR;       // 0x3c0: itemA_R, the bow's
        SharedPtr locHead;         // 0x3d0
        SharedPtr locHara;         // 0x3e0: n_hara
        SharedPtr locWaist;        // 0x3f0
        WeakPtr weak_400;          // 0x400
        WeakPtr weak_410;          // 0x410
        WeakPtr hookshot;          // 0x420: the HookShot actor
        Vector<WeakPtr> weaks_430; // 0x430
        WeakPtr boomerang;         // 0x448: the boomerang actor
        WeakPtr weak_458;          // 0x458
        WeakPtr tempEntity;        // 0x468: destroyed by the update while alive
        SharedPtr modelResource[2];  // 0x478: Link's bfres
        u8 parts[0x40];            // 0x498: PartsVisibility, the item shape groups (Player_ShowItem)
        u8 pad_4d8[0x8];
        StateMachine<64> machine;  // 0x4e0: PlayerSM_vtable; PlayerState
        void* states[64];          // 0x1340: owned, by PlayerState
        void* stateHooks[5];       // 0x1540: states 22, 11, 21, 1, 0
        PlayerAnimController* animCtrl;  // 0x1568: a shared_ptr
        void* animCtrlCtrl;        // 0x1570
        void* owned_1578;          // 0x1578
        void* owned_1580;          // 0x1580
        u8 pad_1588[0x8];
        Vector<u8> vector_1590;    // 0x1590
        u8 field_15a8[0x58];       // 0x15a8
        Vector<u8> vector_1600;    // 0x1600
        Vector<u8> vector_1618;    // 0x1618
        WeakPtr selfWeak;          // 0x1630
        u8 groundQuery[0xb8];      // 0x1640: vtable 0x1c50d18; +0x40 bit 0 read by the update
        u8 pad_16f8[0x8];
        u32 moveFlags;             // 0x1700
        u8 pad_1704[0x4];
        WeakPtr weak_1708;         // 0x1708
        UnorderedMap map_1718;     // 0x1718
        UnorderedMap map_1740;     // 0x1740
        u8 pad_1768[0x8];
        Vec4 field_1770;           // 0x1770
        u32 field_1780;            // 0x1780
        u8 field_1784[0x4];        // 0x1784
        u8 respawnBlocked;         // 0x1788: blocks the respawn timer
        u8 pad_1789[0x17];
        Vec4 field_17a0;           // 0x17a0: zeroed by a respawn
        u8 hitInfo[0x10];          // 0x17b0
        u8 field_17c0;             // 0x17c0
        u8 pad_17c1[0x7];
        WeakPtr weak_17c8;         // 0x17c8
        u8 pad_17d8[0x28];
        u8 field_1800[0x50];       // 0x1800
        List list_1850;            // 0x1850
        u8 pad_1868[0x8];
        Vec4 field_1870;           // 0x1870
        u8 pad_1880[0x10];
        Vec4 field_1890;           // 0x1890
        u8 pad_18a0[0x20];
        float field_18c0;          // 0x18c0
        u8 pad_18c4[0xc];
        u8 object_18d0[0x60];      // 0x18d0: registered with the model component
        UnorderedMap timedActors;  // 0x1930
        u32 field_1958;            // 0x1958
        u8 pad_195c[0x4];
        u8 object_1960[0x20];      // 0x1960: vtable 0x1c17cf8
        u8 field_1980;             // 0x1980
        u8 pad_1981[0xf];
        WeakPtr weak_1990;         // 0x1990
        u8 field_19a0;             // 0x19a0
        u8 pad_19a1[0x3];
        float itemCooldown;        // 0x19a4: counted down; the item states read it
        s32 field_19a8;            // 0x19a8
        u8 field_19ac;             // 0x19ac
        u8 pad_19ad[0x3];
        Actor* carried;            // 0x19b0: a weak_ptr, set while Link holds something over his head
        void* carriedCtrl;         // 0x19b8
        WeakPtr weak_19c0;         // 0x19c0
        u8 object_19d0[0x100];     // 0x19d0: given the head locator
        u8 field_1ad0[0x24];       // 0x1ad0
        u8 pad_1af4[0x40];
        u32 field_1b34;            // 0x1b34
        u8 field_1b38[0x4];        // 0x1b38: 0x1b39 picks the demoEvent animation and the respawn branch
        float airTime;             // 0x1b3c: above 1.5 s, Link goes back to respawnCoords
        Coords respawnCoords;      // 0x1b40
        s32 field_1b70;            // 0x1b70
        u8 pad_1b74[0xc];
        u32 field_1b80;            // 0x1b80
        s32 boneClavicleL;         // 0x1b84
        s32 boneHead;              // 0x1b88
        u32 flags;                 // 0x1b8c: PlayerFlags
        u8 pad_1b90[0x2];
        u16 field_1b92;            // 0x1b92
        u8 field_1b94;             // 0x1b94
        u8 pad_1b95[0x3];
        SharedPtr sp_1b98;         // 0x1b98
        void* owned_1ba8;          // 0x1ba8: its first byte blocks interacting
        void* bottle;              // 0x1bb0: owned PlayerBottle
        void* bow;                 // 0x1bb8: owned PlayerItemModel
        u8 signals[8][0x58];       // 0x1bc0
        float damageFlashTime;     // 0x1e80
        float field_1e84;          // 0x1e84: counted down
        SharedPtr tailor;          // 0x1e88: TailorPlayer
        SharedPtr tailorWaterSurface;  // 0x1e98
        u8 field_1ea8;             // 0x1ea8
        u8 pad_1ea9[0x27];
        SharedPtr animPowerUp;     // 0x1ed0: material animation powerUp
        SharedPtr animDefence;     // 0x1ee0: defence
        u8 eventKind;              // 0x1ef0: from the running event's actor
        u8 pad_1ef1[0x7];
        u8 field_1ef8[0x28];       // 0x1ef8
        WeakPtr weak_1f20;         // 0x1f20
        u32 field_1f30;            // 0x1f30
        u8 pad_1f34[0xc];
        float matrix_1f40[16];     // 0x1f40: identity
        s32 shapeBody;             // 0x1f80: body_low__MI_body
        u8 pad_1f84[0xc];
    };
    static_assert(offsetof(Player, modelComp) == 0x2a8 && offsetof(Player, materialAnim) == 0x2c8);
    static_assert(offsetof(Player, playerCollision) == 0x2d8 && offsetof(Player, ladderInfo) == 0x398);
    static_assert(offsetof(Player, locItemAL) == 0x3b0 && offsetof(Player, hookshot) == 0x420);
    static_assert(offsetof(Player, modelResource) == 0x478 && offsetof(Player, parts) == 0x498);
    static_assert(offsetof(Player, machine) == 0x4e0 && offsetof(Player, states) == 0x1340);
    static_assert(offsetof(Player, animCtrl) == 0x1568 && offsetof(Player, selfWeak) == 0x1630);
    static_assert(offsetof(Player, moveFlags) == 0x1700 && offsetof(Player, field_1770) == 0x1770);
    static_assert(offsetof(Player, respawnBlocked) == 0x1788 && offsetof(Player, field_1800) == 0x1800);
    static_assert(offsetof(Player, timedActors) == 0x1930 && offsetof(Player, itemCooldown) == 0x19a4);
    static_assert(offsetof(Player, carried) == 0x19b0 && offsetof(Player, object_19d0) == 0x19d0);
    static_assert(offsetof(Player, field_1b34) == 0x1b34 && offsetof(Player, airTime) == 0x1b3c);
    static_assert(offsetof(Player, respawnCoords) == 0x1b40 && offsetof(Player, flags) == 0x1b8c);
    static_assert(offsetof(Player, bottle) == 0x1bb0 && offsetof(Player, signals) == 0x1bc0);
    static_assert(offsetof(Player, damageFlashTime) == 0x1e80 && offsetof(Player, tailor) == 0x1e88);
    static_assert(offsetof(Player, animPowerUp) == 0x1ed0 && offsetof(Player, eventKind) == 0x1ef0);
    static_assert(offsetof(Player, matrix_1f40) == 0x1f40 && sizeof(Player) == 0x1f90);
}
