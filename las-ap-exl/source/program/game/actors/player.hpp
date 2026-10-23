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
        void* ignoreObject;        // 0x170: the owner's +0xc0, the move query's ignore filter
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

    // A contact the physics body reports (its +0x80 signal): Player::bodyContacts, PlayerCollision::contacts.
    struct PlayerContact {
        SharedPtr first;
        u8 data_10[0x38];          // 0x10
        SharedPtr second;          // 0x48
        SharedPtr third;           // 0x58
        u32 field_68;              // 0x68
        u8 pad_6c[0x4];
    };
    static_assert(offsetof(PlayerContact, second) == 0x48 && sizeof(PlayerContact) == 0x70);

    // A touch the physics body reports (its +0xd8 signal): Player::bodyTouches, PlayerCollision::touches.
    struct PlayerTouch {
        SharedPtr first;
        u8 data_10[0x10];          // 0x10
        Vec4 normal;               // 0x20: negated when the signal's bool is set
        u8 data_30[0x10];          // 0x30
        u32 field_40;              // 0x40
        u8 pad_44[0x4];
        SharedPtr other;           // 0x48: its weak owner is the touched actor
        u8 pad_58[0x8];
    };
    static_assert(offsetof(PlayerTouch, normal) == 0x20 && offsetof(PlayerTouch, other) == 0x48);
    static_assert(sizeof(PlayerTouch) == 0x60);

    // Link's root component (PlayerCollision_Make, vtable 0x1c501f0; 0x290 bytes): his position.
    struct PlayerCollision {
        CharCtrlComp base;
        u8 pad_1d0[0x8];
        Vector<PlayerContact> contacts;  // 0x1d8
        Vector<PlayerTouch> touches;  // 0x1f0
        Vector<u8> vector_208;     // 0x208: emptied at each Move
        u8 listener[0x40];         // 0x220: vtable 0x1c50360, registered with the maker's params
        Vec4 up;                   // 0x260: (0, 1, 0)
        float gravitySign;         // 0x270: -1 when gravity points down
        float field_274;           // 0x274: from the maker's params
        float field_278;           // 0x278: 0.02
        float field_27c;           // 0x27c: 1, again at each Move
        u32 field_280;             // 0x280
        u8 field_284[0x7];         // 0x284: bools Move reads; 0x284 and 0x287 set by Player_Setup
        u8 pad_28b[0x5];
    };
    static_assert(offsetof(PlayerCollision, contacts) == 0x1d8 && offsetof(PlayerCollision, listener) == 0x220);
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
        constexpr u32 kInEvent = 1u << 0, kCarrying = 1u << 2, kControlModeOverride = 1u << 4, kSetUp = 1u << 7,
                      kHeld = 1u << 8, kForcedAngle = 1u << 12;
    }

    // Player::input.actions: the actions the buttons ask for (Player_RegisterActionChecks). Items are on Y or X.
    namespace PlayerAction {
        constexpr u8 kSword = 1, kRocsFeather = 2, kA = 3, kDash = 4, kShield = 5, kMagicPowder = 7, kOcarina = 8,
                     kShovel = 9, kBomb = 10, kBow = 11, kHookshot = 12, kBoomerang = 13, kMagicRod = 14,
                     kSwordHeld = 15, kSwordReleased = 16, kThrow = 17, kBottle0 = 18, kSwordNotHeld = 21;
    }

    // Player::signals by index: shovel start (no argument), shovel end (u8), fall into an abyss and respawn
    // (const Coords*), a clinger released ({Actor*, ctrl}).
    namespace PlayerSignal {
        constexpr size_t kShovelStart = 2, kShovelEnd = 3, kFallAbyss = 5, kRespawn = 6, kClingerReleased = 7;
    }

    // A check of Link's input: sets its action's bool when check(player, controller) holds.
    struct PlayerActionCheck {
        u8 action;
        u8 pad_1[0xf];
        Function check;            // 0x10
    };
    static_assert(offsetof(PlayerActionCheck, check) == 0x10 && sizeof(PlayerActionCheck) == 0x40);

    // Link's buttons and stick as his states see them, rebuilt each frame by Player_UpdateAnimation.
    struct PlayerInput {
        Vector<PlayerActionCheck> checks;
        u8 pad_18[0x8];
        bool actions[22];          // 0x20: PlayerAction; cleared each frame unless holdActions
        u8 pad_36[0xa];
        Vec4 stickRotation;        // 0x40: the stick's direction relative to the camera, a yaw quaternion
        float stickX;              // 0x50
        float stickY;              // 0x54
        float stickLength;         // 0x58: up to 1
        bool anyPressed;           // 0x5c
        bool pressedA;             // 0x5d
        bool pressedB;             // 0x5e
        bool releasedA;            // 0x5f
        bool heldFeather;          // 0x60: the button of Roc's Feather
        bool pressedA2;            // 0x61
        bool heldA;                // 0x62
        bool pressedB2;            // 0x63
        bool pressedA3;            // 0x64
        bool heldA2;               // 0x65
        bool pressedMaskB0D00;     // 0x66: the fishing's
        bool pressedMask40200;     // 0x67: the fishing's
        bool pressedBottle[3];     // 0x68: the button of bottle 0 to 2
        bool holdActions;          // 0x6b
        u8 pad_6c[0x4];
    };
    static_assert(offsetof(PlayerInput, actions) == 0x20 && offsetof(PlayerInput, stickRotation) == 0x40);
    static_assert(offsetof(PlayerInput, stickX) == 0x50 && offsetof(PlayerInput, anyPressed) == 0x5c);
    static_assert(offsetof(PlayerInput, pressedBottle) == 0x68 && offsetof(PlayerInput, holdActions) == 0x6b);
    static_assert(sizeof(PlayerInput) == 0x70);

    // A transition into a state (StateTransition_vtable 0x1c1b3d0): taken when every condition holds.
    struct StateTransition {
        const uintptr_t* vtable;
        u8 state;                  // 0x8: PlayerState
        u8 pad_9[0x7];
        Vector<Function> conditions;  // 0x10
    };
    static_assert(offsetof(StateTransition, conditions) == 0x10 && sizeof(StateTransition) == 0x28);

    // A transition whose state a function picks (SelectTransition_vtable 0x1c1b3f8).
    struct SelectTransition {
        StateTransition base;
        u8 pad_28[0x8];
        Function select;           // 0x30: gives state when the conditions hold
    };
    static_assert(offsetof(SelectTransition, select) == 0x30 && sizeof(SelectTransition) == 0x60);

    // The transitions into Link's input-driven states (PlayerActionTransitions_Ctor; 0x1c0 bytes).
    struct PlayerActionTransitions {
        Function function_0;       // empty when made
        StateTransition toJump;    // 0x30
        StateTransition toSwordAttack;  // 0x58
        StateTransition toGuard;   // 0x80
        StateTransition toDash;    // 0xa8
        StateTransition toSwordChargeAttack;  // 0xd0
        u8 pad_f8[0x8];
        SelectTransition select_100;  // 0x100
        SelectTransition select_160;  // 0x160
    };
    static_assert(offsetof(PlayerActionTransitions, toJump) == 0x30);
    static_assert(offsetof(PlayerActionTransitions, toSwordChargeAttack) == 0xd0);
    static_assert(offsetof(PlayerActionTransitions, select_100) == 0x100 && sizeof(PlayerActionTransitions) == 0x1c0);

    // A ray cast query (RayQuery_vtable 0x1c50d18): the hit's position, normal and collider, cached on first use.
    struct RayQuery {
        const uintptr_t* vtable;
        u8 pad_8[0x8];
        Vec4 hitPosition;          // 0x10: flags bit 3
        Vec4 hitNormal;            // 0x20: flags bit 4
        SharedPtr collider;        // 0x30: flags bit 2
        u8 flags;                  // 0x40: bit 0 hit
        u8 pad_41[0x7];
        void* hit;                 // 0x48: callback's +0x8
        u8 callback[0x68];         // 0x50: RayCallback_vtable 0x1c50d38; the hit record at +0x8 (attribute index
                                   // +0x10, position +0x18, normal +0x24, distance +0x30)
    };
    static_assert(offsetof(RayQuery, collider) == 0x30 && offsetof(RayQuery, hit) == 0x48);
    static_assert(offsetof(RayQuery, callback) == 0x50 && sizeof(RayQuery) == 0xb8);

    // The knockback in progress (PlayerKnockback_Set): Player_Move adds velocity times the frame time while state
    // is 2.
    struct PlayerKnockback {
        Vec4 velocity;
        s32 frames;                // 0x10
        s32 frames2;               // 0x14
        u8 kind;                   // 0x18: 1, 2, 4, 5 by the hit kind
        u8 param;                  // 0x19
        u8 state;                  // 0x1a: 2 active, 1 unscaled
        u8 pad_1b;
        float time;                // 0x1c: -1 when made
        u8 field_20;               // 0x20
        s8 field_21;               // 0x21: -1 when made
        u8 pad_22[0x2];
        float field_24;            // 0x24: -1 when made
        u8 field_28;               // 0x28
        u8 field_29;               // 0x29: -1 when made, 0 or 5
        u8 pad_2a[0x6];
    };
    static_assert(offsetof(PlayerKnockback, kind) == 0x18 && offsetof(PlayerKnockback, time) == 0x1c);
    static_assert(offsetof(PlayerKnockback, field_24) == 0x24 && sizeof(PlayerKnockback) == 0x30);

    // The last hit Link took (Player_SetHit).
    struct PlayerHit {
        void* attack;              // the hit data's +0x20 (its +0xd the attack type)
        void* defence;             // 0x8: the hit data's +0x28, a reaction per attack type
        u8 kind;                   // 0x10: the caller's; 4 when made
        u8 pad_11[0x7];
        WeakPtr attacker;          // 0x18
        u8 pad_28[0x8];
        Vec4 direction;            // 0x30: from the attacker's position, zero without one
        Vec4 direction2;           // 0x40
        bool pending;              // 0x50: cleared once taken and by a respawn
        u8 pad_51[0x3];
        float scaleA;              // 0x54: the knockback of kind 2
        float scaleB;              // 0x58: the knockback of kinds 1, 4, 5
        bool reaction0;            // 0x5c: from the defence for the attack type
        bool reaction1;            // 0x5d
        u8 pad_5e[0x2];
    };
    static_assert(offsetof(PlayerHit, attacker) == 0x18 && offsetof(PlayerHit, direction) == 0x30);
    static_assert(offsetof(PlayerHit, pending) == 0x50 && offsetof(PlayerHit, scaleB) == 0x58);
    static_assert(sizeof(PlayerHit) == 0x60);

    // The request Player_MoveTo copies before the moveTarget state (event actions build it from mode, distance,
    // actor and animName).
    struct PlayerMoveTarget {
        u64 field_0;
        u64 field_8;               // 0x8
        u16 field_10;              // 0x10: 1 when made
        u8 pad_12[0x2];
        float field_14;            // 0x14: 1 when made
        float field_18;            // 0x18: 7 when made
        float field_1c;            // 0x1c
        float field_20;            // 0x20
        u8 pad_24[0x4];
        u64 field_28;              // 0x28
        u64 field_30;              // 0x30
        u8 flags;                  // 0x38: 1 when made
        u8 pad_39[0x7];
    };
    static_assert(offsetof(PlayerMoveTarget, field_10) == 0x10 && offsetof(PlayerMoveTarget, field_28) == 0x28);
    static_assert(offsetof(PlayerMoveTarget, flags) == 0x38 && sizeof(PlayerMoveTarget) == 0x40);

    // The request Player_BlowAway copies (0x13 bytes) before the blownAway state.
    struct PlayerBlowAway {
        u64 field_0;               // with field_8 likely a direction, zero when made
        u64 field_8;               // 0x8
        u16 field_10;              // 0x10
        u8 field_12;               // 0x12
        u8 pad_13[0xd];
    };
    static_assert(offsetof(PlayerBlowAway, field_12) == 0x12 && sizeof(PlayerBlowAway) == 0x20);

    // The actors stuck to Link (Player_InitClingers, Player_ClearClingers): each comes off when its time reaches its
    // limit; with any, Link walks at a quarter speed.
    struct PlayerClingers {
        SharedPtr points[6];       // locators on the head bone
        UnorderedMap actors;       // 0x60: node +0x18 WeakPtr actor, +0x28 float limit, +0x2c float time
        u32 next;                  // 0x88
        u8 pad_8c[0x4];
    };
    static_assert(offsetof(PlayerClingers, actors) == 0x60 && sizeof(PlayerClingers) == 0x90);

    // Where a carried actor is put (PlayerPutTarget_vtable 0x1c17cf8): Throw aims at it.
    struct PlayerPutTarget {
        const uintptr_t* vtable;
        u8 pad_8[0x8];
        Vec4 position;             // 0x10: an actor's spawnPos
        bool valid;                // 0x20
        u8 pad_21[0xf];
    };
    static_assert(offsetof(PlayerPutTarget, position) == 0x10 && sizeof(PlayerPutTarget) == 0x30);

    // Values a state's exit leaves for the next state's enter, which clears them.
    struct PlayerStateHandoff {
        s32 kind;                  // 0, 1, 2
        s32 variant;               // 0x4
        s32 count;                 // 0x8
        s32 flag;                  // 0xc
        float value;               // 0x10
        u32 field_14;              // 0x14
        u32 field_18;              // 0x18
        u32 field_1c;              // 0x1c: cleared with the rest
        u8 payload[0x40];          // 0x20: read when payloadType is 4 (DemoEvent's, Ladder's)
        s32 payloadType;           // 0x60
    };
    static_assert(offsetof(PlayerStateHandoff, payload) == 0x20 && sizeof(PlayerStateHandoff) == 0x64);

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
        void* attackCols;          // 0x3a8: owned (0x2b0), the attack collision components (Player_AttackCols_Ctor)
        SharedPtr locItemAL;       // 0x3b0: bone locators: itemA_L
        SharedPtr locItemAR;       // 0x3c0: itemA_R, the bow's
        SharedPtr locHead;         // 0x3d0
        SharedPtr locHara;         // 0x3e0: n_hara
        SharedPtr locWaist;        // 0x3f0
        WeakPtr arrows[2];         // 0x400: PlayerArrow actors (Player_SpawnArrow)
        WeakPtr hookshot;          // 0x420: the HookShot actor
        Vector<WeakPtr> magicRodBalls;  // 0x430: PlayerMagicRodBall actors, a pool
        WeakPtr boomerang;         // 0x448: the boomerang actor
        WeakPtr swordBeam;         // 0x458: the PlayerSwordBeam actor
        WeakPtr tempEntity;        // 0x468: destroyed by the update while alive
        SharedPtr modelResource[2];  // 0x478: Link's bfres
        u8 parts[0x40];            // 0x498: PartsVisibility, the item shape groups (Player_ShowItem)
        u8 pad_4d8[0x8];
        StateMachine<64> machine;  // 0x4e0: PlayerSM_vtable; PlayerState
        void* states[64];          // 0x1340: owned, by PlayerState
        StateTransition* stateHooks[5];  // 0x1540: into states 22, 11, 21, 1, 0
        PlayerAnimController* animCtrl;  // 0x1568: a shared_ptr
        void* animCtrlCtrl;        // 0x1570
        PlayerKnockback* knockback;  // 0x1578: owned
        PlayerActionTransitions* actionTransitions;  // 0x1580: owned
        u8 pad_1588[0x8];
        PlayerInput input;         // 0x1590
        Vector<PlayerContact> bodyContacts;  // 0x1600: this frame's (Player_OnBodyContact)
        Vector<PlayerTouch> bodyTouches;  // 0x1618: this frame's (Player_OnBodyTouch)
        WeakPtr selfWeak;          // 0x1630
        RayQuery groundQuery;      // 0x1640: cast down each frame (Player_UpdateGround)
        u8 pad_16f8[0x8];
        u32 groundAttribute;       // 0x1700: the ground's collision attribute: bits 0-5 material, 6-9 kind
        u8 pad_1704[0x4];
        WeakPtr groundActor;       // 0x1708: the owner of the collider under Link
        UnorderedMap lowerLevelHoles;  // 0x1718: entities a fall into an abyss takes to a lower level
        UnorderedMap slowAreas;    // 0x1740: Link moves at 0.75 while not empty
        u8 pad_1768[0x8];
        Vec4 groundNormal;         // 0x1770: up when not grounded
        float airTimer;            // 0x1780: seconds since grounded
        bool onGround;             // 0x1784
        bool groundWalkable;       // 0x1785: inside the field grid, not steep
        bool onGroundSurface;      // 0x1786: standing on the ray's ground, not on an object
        bool neverGrounded;        // 0x1787: until the first ground update
        u8 prevMoveResult;         // 0x1788: airTimer grows only while 0
        u8 pad_1789[0x7];
        u8 moveResult;             // 0x1790: playerCollision's Move's
        u8 pad_1791[0xf];
        Vec4 moveDelta;            // 0x17a0: the last Move's displacement; zeroed by a respawn
        PlayerHit hit;             // 0x17b0
        PlayerMoveTarget moveTarget;  // 0x1810
        List touchedActors;        // 0x1850
        u8 pad_1868[0x8];
        PlayerBlowAway blowAway;   // 0x1870
        Vec4 pullVelocity;         // 0x1890: towards pullCenter, on ground of kind 6
        Vec4 pullStart;            // 0x18a0
        Vec4 pullCenter;           // 0x18b0: the centre of the grid cell under Link
        float pullBlend;           // 0x18c0: 1 to 0, scales Link's own move while pulled
        float pullTime;            // 0x18c4: seconds
        bool pullRestart;          // 0x18c8
        bool pullActive;           // 0x18c9
        u8 pad_18ca[0x6];
        PlayerClingers clingers;   // 0x18d0
        PlayerPutTarget putTarget; // 0x1960
        WeakPtr fairy;             // 0x1990: an ItemFairy Link touched, for the Bottle state
        bool inWaterArea;          // 0x19a0
        u8 recoilFlags;            // 0x19a1: bit 0 hit, 1 during a dash, 2 from the second source
        u8 pad_19a2[0x2];
        float itemCooldown;        // 0x19a4: counted down; the item states read it
        s32 itemRequest;           // 0x19a8: -1 none; the Bow's and Bomb's
        bool itemRequestTaken;     // 0x19ac
        u8 pad_19ad[0x3];
        Actor* carried;            // 0x19b0: a weak_ptr, set while Link holds something over his head
        void* carriedCtrl;         // 0x19b8
        WeakPtr holder;            // 0x19c0: the actor holding Link (PlayerFlags::kHeld)
        u8 lookAt[0x100];          // 0x19d0: the head's look-at (PlayerLookAt_Ctor), given the head locator
        s32 shieldAngle;           // 0x1ad0: clamped to 45 degrees (Player_AimShield)
        PlayerStateHandoff handoff;  // 0x1ad4
        u8 field_1b38;             // 0x1b38: 1 when made
        bool sideView;             // 0x1b39: the room is side-view (Player_OnRoomChanged)
        bool carryNoDrop;          // 0x1b3a
        bool modelFlag;            // 0x1b3b: Player_SetModelFlag
        float airTime;             // 0x1b3c: above 1.5 s, Link goes back to respawnCoords
        Coords respawnCoords;      // 0x1b40: the last safe ground
        s32 respawnKind;           // 0x1b70: 0 ground, 1 side-view, 3 reset
        u8 pad_1b74[0xc];
        u32 field_1b80;            // 0x1b80
        s32 boneClavicleL;         // 0x1b84
        s32 boneHead;              // 0x1b88
        u32 flags;                 // 0x1b8c: PlayerFlags
        u16 itemGroupBits;         // 0x1b90: a bit per item shape group shown
        u16 handItems;             // 0x1b92: bit 0 sword shown, bit 1 shield shown
        u8 pendingState;           // 0x1b94: entered at the next chance, 0xff none
        u8 pad_1b95[0x3];
        SharedPtr companion;       // 0x1b98: the companion (Player_SpawnCompanion)
        void* owned_1ba8;          // 0x1ba8: its first byte blocks interacting
        void* bottle;              // 0x1bb0: owned PlayerBottle
        void* bow;                 // 0x1bb8: owned PlayerItemModel
        u8 signals[8][0x58];       // 0x1bc0: PlayerSignal
        float damageFlashTime;     // 0x1e80
        float field_1e84;          // 0x1e84: counted down
        SharedPtr tailor;          // 0x1e88: TailorPlayer
        SharedPtr tailorWaterSurface;  // 0x1e98
        u8 field_1ea8;             // 0x1ea8
        u8 controlModeOverride;    // 0x1ea9: Player_ControlMode's while flags kControlModeOverride
        u8 pad_1eaa[0x6];
        Vec4 holdOffset;           // 0x1eb0: moves towards holdOffsetTarget
        Vec4 holdOffsetTarget;     // 0x1ec0
        SharedPtr animPowerUp;     // 0x1ed0: material animation powerUp
        SharedPtr animDefence;     // 0x1ee0: defence
        u8 eventKind;              // 0x1ef0: from the running event's actor
        u8 pad_1ef1[0x7];
        FixedString<0x20> ocarinaAreaName;  // 0x1ef8: Player_SetOcarinaArea
        WeakPtr ocarinaArea;       // 0x1f20
        u32 forcedAngle;           // 0x1f30: used while flags kForcedAngle
        u8 pad_1f34[0xc];
        float shieldArmMatrix[16]; // 0x1f40: Player_AimShield; identity when made
        s32 shapeBody;             // 0x1f80: body_low__MI_body
        u8 pad_1f84[0xc];
    };
    static_assert(offsetof(Player, modelComp) == 0x2a8 && offsetof(Player, materialAnim) == 0x2c8);
    static_assert(offsetof(Player, playerCollision) == 0x2d8 && offsetof(Player, ladderInfo) == 0x398);
    static_assert(offsetof(Player, attackCols) == 0x3a8 && offsetof(Player, arrows) == 0x400);
    static_assert(offsetof(Player, locItemAL) == 0x3b0 && offsetof(Player, hookshot) == 0x420);
    static_assert(offsetof(Player, magicRodBalls) == 0x430 && offsetof(Player, swordBeam) == 0x458);
    static_assert(offsetof(Player, modelResource) == 0x478 && offsetof(Player, parts) == 0x498);
    static_assert(offsetof(Player, machine) == 0x4e0 && offsetof(Player, states) == 0x1340);
    static_assert(offsetof(Player, stateHooks) == 0x1540 && offsetof(Player, knockback) == 0x1578);
    static_assert(offsetof(Player, animCtrl) == 0x1568 && offsetof(Player, input) == 0x1590);
    static_assert(offsetof(Player, bodyContacts) == 0x1600 && offsetof(Player, selfWeak) == 0x1630);
    static_assert(offsetof(Player, groundQuery) == 0x1640 && offsetof(Player, groundAttribute) == 0x1700);
    static_assert(offsetof(Player, lowerLevelHoles) == 0x1718 && offsetof(Player, groundNormal) == 0x1770);
    static_assert(offsetof(Player, airTimer) == 0x1780 && offsetof(Player, prevMoveResult) == 0x1788);
    static_assert(offsetof(Player, moveResult) == 0x1790 && offsetof(Player, hit) == 0x17b0);
    static_assert(offsetof(Player, moveTarget) == 0x1810 && offsetof(Player, touchedActors) == 0x1850);
    static_assert(offsetof(Player, blowAway) == 0x1870 && offsetof(Player, pullVelocity) == 0x1890);
    static_assert(offsetof(Player, pullBlend) == 0x18c0 && offsetof(Player, clingers) == 0x18d0);
    static_assert(offsetof(Player, putTarget) == 0x1960 && offsetof(Player, fairy) == 0x1990);
    static_assert(offsetof(Player, itemCooldown) == 0x19a4 && offsetof(Player, itemRequest) == 0x19a8);
    static_assert(offsetof(Player, carried) == 0x19b0 && offsetof(Player, holder) == 0x19c0);
    static_assert(offsetof(Player, lookAt) == 0x19d0 && offsetof(Player, shieldAngle) == 0x1ad0);
    static_assert(offsetof(Player, handoff) == 0x1ad4 && offsetof(Player, sideView) == 0x1b39);
    static_assert(offsetof(Player, airTime) == 0x1b3c && offsetof(Player, respawnKind) == 0x1b70);
    static_assert(offsetof(Player, respawnCoords) == 0x1b40 && offsetof(Player, flags) == 0x1b8c);
    static_assert(offsetof(Player, itemGroupBits) == 0x1b90 && offsetof(Player, companion) == 0x1b98);
    static_assert(offsetof(Player, bottle) == 0x1bb0 && offsetof(Player, signals) == 0x1bc0);
    static_assert(offsetof(Player, damageFlashTime) == 0x1e80 && offsetof(Player, tailor) == 0x1e88);
    static_assert(offsetof(Player, controlModeOverride) == 0x1ea9 && offsetof(Player, holdOffset) == 0x1eb0);
    static_assert(offsetof(Player, animPowerUp) == 0x1ed0 && offsetof(Player, eventKind) == 0x1ef0);
    static_assert(offsetof(Player, ocarinaAreaName) == 0x1ef8 && offsetof(Player, ocarinaArea) == 0x1f20);
    static_assert(offsetof(Player, forcedAngle) == 0x1f30 && offsetof(Player, shieldArmMatrix) == 0x1f40);
    static_assert(offsetof(Player, shapeBody) == 0x1f80 && sizeof(Player) == 0x1f90);
}
