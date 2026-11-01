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
    struct PhysicsWorld;

    // A character controller component (CharCtrlComp_Ctor, vtable 0x1c50038; 0x1d0 bytes): a node with a physics
    // body that Move moves by the pending move and the fall velocity.
    struct CharCtrlComp {
        Node node;
        const uintptr_t* vtable2;  // 0x148: an interface (vtable 0x1c50138): filter, enabled, shapes
        void* filter;              // 0x150: filter words, bit 0 of the first disabled
        void* body;                // 0x158: the physics body
        WeakPtr<PhysicsWorld> world;  // 0x160: the maker's third argument
        void* controllerFilter;    // 0x170: the world's; Move passes it wrapped (vtable 0x1c4fbd8)
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
    static_assert(offsetof(CharCtrlComp, vtable2) == 0x148 && offsetof(CharCtrlComp, world) == 0x160);
    static_assert(offsetof(CharCtrlComp, pendingMove) == 0x180 && offsetof(CharCtrlComp, fallVelocity) == 0x1b0);
    static_assert(offsetof(CharCtrlComp, maxFallSpeed) == 0x1c0 && offsetof(CharCtrlComp, enabled) == 0x1cc);
    static_assert(sizeof(CharCtrlComp) == 0x1d0);

    struct PhysicsBody;

    // PhysX PxControllerBehaviorFlag: what the controller does on a shape or controller it stands on.
    enum class CctBehavior : u8 { CanRideOnObject = 1u << 0, Slide = 1u << 1, UserDefinedRide = 1u << 2 };
    FLAGS_ENUM(CctBehavior)

    // A PxShape's wrapper (PhysicsShape_Ctor, PhysicsShape_vtable 0x1c50fb8; 0x50 bytes): the shape's userData, held
    // by its PhysicsBody's shapes. Vtable +0x0 is IsA.
    struct PhysicsShape {
        const uintptr_t* vtable;
        WeakPtr<> self;            // 0x8: shared-from-this; the shape's holders lock it
        void* pxShape;             // 0x18
        Vec4 extents;              // 0x20: from the geometry (a box's half extents); (0.5, 0.5, 0.5) when made
        bool walkable;             // 0x30: 1 when made; a character controller stands on it within its slope limit
        Flags<CctBehavior> behavior;  // 0x31: CanRideOnObject when made; what PlayerCollisionBehavior returns
        u8 pad_32[0x6];
        SharedPtr<> tag;           // 0x38: AttackColShapeTag, DamageColShapeTag or CollisionShapeTag; often null
        u8 pad_48[0x8];            // never written (2026-10-31)
    };
    static_assert(offsetof(PhysicsShape, pxShape) == 0x18 && offsetof(PhysicsShape, walkable) == 0x30);
    static_assert(sizeof(PhysicsShape) == 0x50);

    // PhysicsShape::tag of an AttackColComp's shape (AttackColShapeTag_vtable 0x1b5aec8; vtable +0x0 is IsA).
    struct AttackColShapeTag {
        const uintptr_t* vtable;
        void* attack;              // 0x8: the shape's own attack, used by its hits instead of the comp's; 0: none
    };
    static_assert(sizeof(AttackColShapeTag) == 0x10);

    // PhysicsShape::tag of a DamageColComp's shape (DamageColShapeTag_vtable 0x1b5b880; vtable +0x0 is IsA).
    struct DamageColShapeTag {
        const uintptr_t* vtable;
        void* defence;             // 0x8: the shape's own defence, used by hits on it instead of the comp's; 0: none
    };
    static_assert(sizeof(DamageColShapeTag) == 0x10);

    // PhysicsShape::tag from a collision file or an actor (CollisionShapeTag_vtable 0x1b75bd8; vtable +0x0 is IsA).
    struct CollisionShapeTag {
        const uintptr_t* vtable;
        bool noDashCrash;          // 0x8: the Dash state ignores contacts with the shape (no bonk)
        u8 unused_9;               // 0x9: never read; the collision file entry's +0x14
        u8 pad_a[0x6];
    };
    static_assert(offsetof(CollisionShapeTag, noDashCrash) == 0x8 && sizeof(CollisionShapeTag) == 0x10);

    // A shape the character controller hit (PhysX PxControllerShapeHit, CctHitReport_OnShapeHit): emitted on the
    // body's signal at +0x70, and with true on the hit PhysicsBody's at +0x618. Player::bodyContacts,
    // PlayerCollision::contacts.
    struct PlayerContact {
        SharedPtr<> component;     // the controller's
        Vec4 position;             // 0x10: world, w 0
        Vec4 normal;               // 0x20
        Vec4 direction;            // 0x30: of the move
        float length;              // 0x40: of the move
        u8 pad_44[0x4];
        SharedPtr<PhysicsShape> shape;  // 0x48: the PxShape's userData
        SharedPtr<PhysicsBody> body;  // 0x58: from the PxActor's userData (a 0x10-byte object, the body at +0x8)
        u32 triangleIndex;         // 0x68
        u8 pad_6c[0x4];
    };
    static_assert(offsetof(PlayerContact, length) == 0x40 && offsetof(PlayerContact, shape) == 0x48);
    static_assert(offsetof(PlayerContact, triangleIndex) == 0x68 && sizeof(PlayerContact) == 0x70);

    // Another character controller this one hit (PhysX PxControllersHit, CctHitReport_OnControllerHit): emitted on
    // both bodies' signals at +0xc8, with true on the other's. Player::bodyTouches, PlayerCollision::touches.
    struct PlayerTouch {
        SharedPtr<> component;     // the moving controller's
        Vec4 position;             // 0x10: world, w 0
        Vec4 normal;               // 0x20: negated when the signal's bool is set
        Vec4 direction;            // 0x30: of the move
        float length;              // 0x40: of the move
        u8 pad_44[0x4];
        SharedPtr<> other;         // 0x48: the hit controller's; its weak owner is the touched actor
        u8 pad_58[0x8];
    };
    static_assert(offsetof(PlayerTouch, normal) == 0x20 && offsetof(PlayerTouch, other) == 0x48);
    static_assert(sizeof(PlayerTouch) == 0x60);

    struct PlayerCollision;

    // PlayerCollision's controller behaviour (vtable 0x1c50360), which the PhysX adapter's PxControllerBehaviorCallback
    // calls: +0x10 Shape (this, SharedPtr<PhysicsShape>* shape, the PxActor's userData +0x8), +0x18 Controller (this,
    // the PxController's userData). Both return Flags<CctBehavior> through x8.
    struct PlayerCollisionBehavior {
        const uintptr_t* vtable;
        PlayerCollision* owner;    // 0x8: set by PlayerCollision_Init
    };
    static_assert(sizeof(PlayerCollisionBehavior) == 0x10);

    // Link's root component (PlayerCollision_Make, vtable 0x1c501f0; 0x290 bytes): his position.
    struct PlayerCollision {
        CharCtrlComp base;
        u8 pad_1d0[0x8];
        Vector<PlayerContact> contacts;  // 0x1d8
        Vector<PlayerTouch> touches;  // 0x1f0
        Vector<u64> wallNormals;   // 0x208: (float x, float z) each; Move slides along them and empties it
        PlayerCollisionBehavior behavior;  // 0x220: the controller desc's behaviour callback
        Vec4 velocity;             // 0x230: Move's, from force and frameForce, damped
        Vec4 force;                // 0x240: never written besides zeroing when made (always 0; 0 in play, 2026-10-31)
        Vec4 frameForce;           // 0x250: never written besides zeroing when made and by each Move (always 0)
        Vec4 groundNormal;         // 0x260: the steepest ground contact's normal; (0, -gravitySign, 0) at each Move
        float gravitySign;         // 0x270: -1 when gravity points down
        float slopeLimit;          // 0x274: cos of the steepest slope, taken from the desc (virtual +0xd8)
        float wallSkin;            // 0x278: 0.02: the push into a wall Move leaves
        float minGroundUp;         // 0x27c: the least normal y of this Move's ground contacts; 1 at each Move
        float maxWallUp;           // 0x280: the greatest normal y of its wall hits, ceilings left out; 0 at each Move
        bool projectOnGround;      // 0x284: Move projects the step onto groundNormal
        bool followSlope;          // 0x285: Move refits a downward step on the ground to slopeLimit
        bool slideOffWall;         // 0x286: Move adds a downward move along a steep wall
        bool clipWallPush;         // 0x287: Move takes the push into wallNormals out of the step
        bool touchedGround;        // 0x288: a contact within slopeLimit on a walkable shape; cleared after a Move
        bool touchedWall;          // 0x289: a contact steeper than slopeLimit; cleared after a Move
        bool lastContactGround;    // 0x28a: the last contact was ground; cleared at each Move
        u8 pad_28b[0x5];
    };
    static_assert(offsetof(PlayerCollision, contacts) == 0x1d8 && offsetof(PlayerCollision, behavior) == 0x220);
    static_assert(offsetof(PlayerCollision, velocity) == 0x230 && offsetof(PlayerCollision, frameForce) == 0x250);
    static_assert(offsetof(PlayerCollision, groundNormal) == 0x260 && sizeof(PlayerCollision) == 0x290);
    static_assert(offsetof(PlayerCollision, projectOnGround) == 0x284);
    static_assert(offsetof(PlayerCollision, touchedGround) == 0x288);

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
        SharedPtr<> sheetData;     // keeps the sheet data alive
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
        SharedPtr<> facial;        // 0x28: the FacialComp; a play sets the row's expression on it
        UnorderedMap anims;        // 0x38: by key: the row and its animation bound to the model
        Vector<SharedPtr<>> layerAnims;  // 0x60: per layer, the animation set on it
        Vector<PlayerAnimSlot> slots;  // 0x78: per layer
        bool facialEnabled;        // 0x90
        u8 pad_91[0x7];
    };
    static_assert(offsetof(PlayerAnimController, animation) == 0x8 && offsetof(PlayerAnimController, anims) == 0x38);
    static_assert(offsetof(PlayerAnimController, slots) == 0x78 && sizeof(PlayerAnimController) == 0x98);

    // Link's states (PlayerSM_ChangeState); PlayerState_*_vtable in las.sym.
    enum class PlayerState : u8 {
        Locomotion = 0, Jump = 1, SwordAttack = 2, Fall = 3, Talk = 4, Open = 5, Push = 6, Pull = 7, Carry = 8,
        Dash = 9, Guard = 10, Damage = 11, Ocarina = 12, MagicPower = 13, Ladder = 14, SwordChargeAttack = 15,
        MoveTarget = 16, SwordPoke = 17, Death = 18, Resurrection = 19, Throw = 20, FallAbyss = 21, DemoEvent = 22,
        BlownAway = 23, Boomerang = 24, Bow = 25, Hookshot = 26, MagicRod = 27, Bomb = 28, Warp = 29, Swim = 30,
        ForcePlayAnim = 31, Shovel = 32, Bottle = 33, Fishing = 34, DoPush = 35, None = kNoState,
    };

    // Player::flags.
    enum class PlayerFlag : u32 {
        InEvent = 1u << 0, Carrying = 1u << 2, ControlModeOverride = 1u << 4, SetUp = 1u << 7, Held = 1u << 8,
        ForcedAngle = 1u << 12,
    };
    FLAGS_ENUM(PlayerFlag)

    // Player::input.actions: the actions the buttons ask for (Player_RegisterActionChecks). Items are on Y or X.
    enum class PlayerAction : u8 {
        Sword = 1, RocsFeather = 2, A = 3, Dash = 4, Shield = 5, MagicPowder = 7, Ocarina = 8, Shovel = 9, Bomb = 10,
        Bow = 11, Hookshot = 12, Boomerang = 13, MagicRod = 14, SwordHeld = 15, SwordReleased = 16, Throw = 17,
        Bottle0 = 18, SwordNotHeld = 21,
    };

    // Player::signals by index: ocarina song start and its event's end (u8 song), shovel start (no argument), shovel
    // end (u8), dash crash (const Vec3*, WeakPtr*), fall into an abyss and respawn (const Coords*), a clinger released
    // ({Actor*, ctrl}).
    namespace PlayerSignal {
        constexpr size_t kOcarinaStart = 0, kOcarinaEnd = 1, kShovelStart = 2, kShovelEnd = 3, kDashCrash = 4;
        constexpr size_t kFallAbyss = 5, kRespawn = 6, kClingerReleased = 7;
    }

    // A check of Link's input: sets its action's bool when check(player, controller) holds.
    struct PlayerActionCheck {
        PlayerAction action;
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
        bool notHeldA;             // 0x5f
        bool heldFeather;          // 0x60: Y or X held with Roc's Feather on it
        bool pressedA2;            // 0x61
        bool heldA;                // 0x62
        bool pressedB2;            // 0x63
        bool pressedA3;            // 0x64
        bool heldA2;               // 0x65
        bool pressedLeftUpDown;    // 0x66: Left, Up or Down on the D-pad or the left stick; the fishing's
        bool pressedRight;         // 0x67: Right on the D-pad or the left stick; the fishing's
        bool pressedBottle[3];     // 0x68: Y or X pressed with bottle 0 to 2 on it
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

    // The transitions into Link's input-driven states (PlayerActionTransitions_Ctor; 0x1c0 bytes), shared by the
    // states that list them.
    struct PlayerActionTransitions {
        Function gate;             // every condition also needs it when set; states swap theirs in on enter
        StateTransition toJump;    // 0x30
        StateTransition toSwordAttack;  // 0x58
        StateTransition toGuard;   // 0x80
        StateTransition toDash;    // 0xa8
        StateTransition toSwordChargeAttack;  // 0xd0
        u8 pad_f8[0x8];
        SelectTransition toInteract;  // 0x100: A at the interaction target
        SelectTransition toItem;   // 0x160: the state of the item action
    };
    static_assert(offsetof(PlayerActionTransitions, toJump) == 0x30);
    static_assert(offsetof(PlayerActionTransitions, toSwordChargeAttack) == 0xd0);
    static_assert(offsetof(PlayerActionTransitions, toInteract) == 0x100 && sizeof(PlayerActionTransitions) == 0x1c0);

    // A ray cast query (RayQuery_vtable 0x1c50d18): the hit's position, normal and collider, cached on first use.
    struct RayQuery {
        const uintptr_t* vtable;
        u8 pad_8[0x8];
        Vec4 hitPosition;          // 0x10: flags bit 3
        Vec4 hitNormal;            // 0x20: flags bit 4
        SharedPtr<> collider;      // 0x30: flags bit 2
        u8 flags;                  // 0x40: bit 0 hit
        u8 pad_41[0x7];
        void* hit;                 // 0x48: callback's +0x8
        u8 callback[0x68];         // 0x50: RayCallback_vtable 0x1c50d38; the hit record at +0x8 (attribute index
                                   // +0x10, position +0x18, normal +0x24, distance +0x30)
    };
    static_assert(offsetof(RayQuery, collider) == 0x30 && offsetof(RayQuery, hit) == 0x48);
    static_assert(offsetof(RayQuery, callback) == 0x50 && sizeof(RayQuery) == 0xb8);

    // The knockback in progress (PlayerKnockback_Set): Player_Move adds velocity times the frame time while state
    // is 2, in two phases of moveFrames and endFrames frames.
    struct PlayerKnockback {
        Vec4 velocity;             // the hit's direction times the attack's speed
        s32 angle;                 // 0x10: a game angle, the hit's direction
        s32 facing;                // 0x14: a game angle, Link's, turned by 180 degrees when over 90 from angle
        u8 kind;                   // 0x18: 1, 2, 4, 5 by the hit kind
        u8 param;                  // 0x19: the caller's
        u8 state;                  // 0x1a: 2 active, 1 unscaled; 1 once the end phase is over
        bool ending;               // 0x1b: the move phase is over (PlayerKnockback_Update)
        float moveStep;            // 0x1c: 1 / moveFrames (0 for none); -1 when made
        s8 moveFrame;              // 0x20: frames of the move phase so far; 0 at a set
        s8 moveFrames;             // 0x21: the knockback animation's frames minus endFrames; -1 when made
        u8 pad_22[0x2];
        float endStep;             // 0x24: 1 / endFrames; -1 when made
        s8 endFrame;               // 0x28: frames of the end phase so far, velocity times 0.88 each; 0 at a set
        u8 endFrames;              // 0x29: 0, 5, 15 or 75; -1 when made
        u8 pad_2a[0x6];
    };
    static_assert(offsetof(PlayerKnockback, kind) == 0x18 && offsetof(PlayerKnockback, moveStep) == 0x1c);
    static_assert(offsetof(PlayerKnockback, endStep) == 0x24 && sizeof(PlayerKnockback) == 0x30);

    // The last hit Link took (Player_SetHit).
    struct PlayerHit {
        void* attack;              // the hit data's +0x20 (its +0xd the attack type)
        void* defence;             // 0x8: the hit data's +0x28, a reaction per attack type
        u8 kind;                   // 0x10: the caller's; 4 when made
        u8 pad_11[0x7];
        WeakPtr<> attacker;        // 0x18
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

    // How the moveTarget state turns Link (PlayerMoveTarget::flags)
    enum class PlayerMoveFlags : u8 {
        FaceMove = 1u << 0,        // each frame he faces his move (the playerCollision's lastMove)
        KeepGoalDirection = 1u << 1,  // turn to the enter's goal direction at once, not easing
    };
    FLAGS_ENUM(PlayerMoveFlags)

    // The request Player_MoveTo copies before the moveTarget state (event actions build it from mode, distance,
    // actor and animName).
    struct PlayerMoveTarget {
        Vec4 target;
        u8 relative;               // 0x10: 1 (when made): target is an offset from Link's position
        u8 mode;                   // 0x11: PlayerChara_MoveTo's 0..3 as 0, 1, 3, 2; 2 takes its own path (0xdcb4d0);
                                   // 4: the goal is Link's own position
        u8 pad_12[0x2];
        float speed;               // 0x14: 2.25 walking, 4.5 running; 1 when made
        float timeout;             // 0x18: seconds before the state gives up; 7 when made; PlayerChara_MoveTo's float
        float stopDistance;        // 0x1c: outside mode 4 the goal stops this short of target
        float time;                // 0x20: the state counts its copy down while not negative
        u8 pad_24[0x4];
        StrView animName;          // 0x28: played while moving when not empty
        Flags<PlayerMoveFlags> flags;  // 0x38: FaceMove when made
        u8 pad_39[0x7];
    };
    static_assert(offsetof(PlayerMoveTarget, relative) == 0x10 && offsetof(PlayerMoveTarget, animName) == 0x28);
    static_assert(offsetof(PlayerMoveTarget, flags) == 0x38 && sizeof(PlayerMoveTarget) == 0x40);

    // The request Player_BlowAway copies (0x13 bytes) before the blownAway state.
    struct PlayerBlowAway {
        Vec4 velocity;             // the launch; its y becomes the character controller's fall velocity
        s16 damage;                // 0x10: quarter hearts off Health, doubled with HeroMode
        bool mightPunch;           // 0x12: the state loads event/MightPunch.bfevfl
        u8 pad_13[0xd];
    };
    static_assert(offsetof(PlayerBlowAway, mightPunch) == 0x12 && sizeof(PlayerBlowAway) == 0x20);

    // PlayerLookAt::flags. HoldsTurn and TurnsToLimit act while the target is beyond angleLimit.
    enum class LookAtFlag : u32 {
        TracksTarget = 1u << 0, KeepsMatrices = 1u << 1,
        HoldsTurn = 1u << 2,       // keep turn after holdLimit frames instead of easing it to 0
        TurnsToLimit = 1u << 3,    // turn to the limit (never set)
    };
    FLAGS_ENUM(LookAtFlag)

    // The head's look-at (PlayerLookAt_Ctor; 0x100 bytes): angle limits are game angles.
    struct PlayerLookAt {
        SharedPtr<> head;          // the head locator
        Vec4 target;               // 0x10: mode 2's, from the look-at hook (0xd455e0)
        const uintptr_t* vtable_20;  // 0x20: a look-at controller (vtable 0x1b693f8, other actors too) spans 0x20..0xff
        float turnShare;           // 0x28: 0.7; below 1 the matrices split turn by it
        float pitchShare;          // 0x2c: 0.7; the matrices scale pitch by 1 - it
        s32 turn;                  // 0x30: the head's sideways turn toward target, by at most turnSpeed a frame
        s32 pitch;                 // 0x34: the head's vertical turn toward target, within angleMin..angleMax
        s32 pitchRequest;          // 0x38: without TracksTarget pitch eases toward it, then it is cleared
        s32 angleLimit;            // 0x3c: turn's limit either side: 60 degrees, 0 in mode 1, 85 in mode 2
        s32 angleMax;              // 0x40: pitch's limits: 30 and -30 degrees, 45 and -45 in mode 2
        s32 angleMin;              // 0x44
        s32 turnSpeed;             // 0x48: eased toward 4 degrees while the target is within angleLimit
        float holdRate;            // 0x4c: -1; 1 / holdFrames while the target is within angleLimit; never read
        u16 heldFrames;            // 0x50: frames turn has held with the target beyond angleLimit
        u16 holdLimit;             // 0x52: 0xffff; holdFrames, copied while the target is within angleLimit
        u16 holdFrames;            // 0x54: 120
        u8 pad_56[0xa];
        float matrices[2][16];     // 0x60: the head's two matrices, recomputed unless KeepsMatrices
        Flags<LookAtFlag> flags;   // 0xe0
        u8 pad_e4[0xc];
        u8 mode;                   // 0xf0: 0 looks ahead; 1 set with head (0xd1d230); 2 looks at target
        u8 pad_f1[0xf];
    };
    static_assert(offsetof(PlayerLookAt, vtable_20) == 0x20 && offsetof(PlayerLookAt, angleLimit) == 0x3c);
    static_assert(offsetof(PlayerLookAt, holdFrames) == 0x54 && offsetof(PlayerLookAt, mode) == 0xf0);
    static_assert(offsetof(PlayerLookAt, pitch) == 0x34 && offsetof(PlayerLookAt, matrices) == 0x60);
    static_assert(offsetof(PlayerLookAt, flags) == 0xe0 && offsetof(PlayerLookAt, target) == 0x10);
    static_assert(sizeof(PlayerLookAt) == 0x100);

    // Link's sword charge (Player::swordCharge; made by Player_Ctor): cleared once neither the sword's held nor its
    // released action is set.
    struct PlayerSwordCharge {
        bool charging;             // shows the sword, blocks lifting (Player_CanInteract)
        bool charged;              // with charging, releasing B starts swordChargeAttack
        u8 pad_2[0x2];
        float frames;              // 0x4: frames charging; charged at 45
    };
    static_assert(offsetof(PlayerSwordCharge, frames) == 0x4 && sizeof(PlayerSwordCharge) == 0x8);

    // The actors stuck to Link (Player_InitClingers, Player_ClearClingers): each comes off when its time reaches its
    // limit; with any, Link walks at a quarter speed.
    struct PlayerClingers {
        SharedPtr<> points[6];     // locators on the head bone
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

    // PlayerStateHandoff::kind into Jump (PlayerState_Jump_Start): the animation and launch speed. Recoil and
    // DJumpRecoil (PlayerHook_Recoil's) act as Jump and DJump; Hop and LeaveWater share branches of the update.
    enum class PlayerJumpKind : s32 {
        Jump = 0,                  // jump (s_jump in side view, raft_jump while kHeld), 4.5
        DJump = 1,                 // d_jump, 9
        Hop = 2,                   // jump (water_fall_st while kHeld), 2.5
        LeaveWater = 3,            // leave_water, 4.5
        Recoil = 4,
        DJumpRecoil = 5,
    };

    // What PlayerStateHandoff::payload holds.
    enum class PlayerHandoffPayload : s32 {
        None = 0,
        Ladder = 4,                // u32 0: the Ladder state's +0x314; Ladder to DemoEvent and back
    };

    // Values a state's exit, or the condition of the transition taken, leaves for the next state's enter, which
    // clears them; what each means depends on the next state (PLAYER.md, State handoff).
    struct PlayerStateHandoff {
        union {
            s32 kind;              // 1 for most states that read it
            PlayerJumpKind jumpKind;
        };
        s32 variant;               // 0x4
        s32 angle;                 // 0x8: Jump: Link's facing, a game angle; Locomotion: an apple's item id to eat
        s32 flag;                  // 0xc: Jump: velocityX and velocityZ are set
        float value;               // 0x10
        float velocityX;           // 0x14: Jump's launch
        float velocityZ;           // 0x18
        u32 unused_1c;             // 0x1c: zeroed with the rest, never read
        u8 payload[0x40];          // 0x20
        PlayerHandoffPayload payloadType;  // 0x60
    };
    static_assert(offsetof(PlayerStateHandoff, angle) == 0x8 && offsetof(PlayerStateHandoff, velocityX) == 0x14);
    static_assert(offsetof(PlayerStateHandoff, payload) == 0x20 && sizeof(PlayerStateHandoff) == 0x64);

    // Player::swimDepth (Player_SetSwimDepth): each value offsets the model and colliders
    enum class PlayerSwimDepth : u8 {
        None = 0,
        Float = 1,                 // -0.35: Swim floating, the Damage state
        Dive = 2,                  // -1.25: Swim diving
        Crash = 3,                 // +0.5: the Dash crash
    };

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
        SharedPtr<> damageCollision; // 0x2e8: DamageColComp
        SharedPtr<> eventChara;    // 0x2f8: EventChara, chara Link
        SharedPtr<> eventEntity;   // 0x308: the item-get's CarryModel entity; destroyed at an event's end
        SharedPtr<> camera;        // 0x318: CameraCom
        SharedPtr<> cameraShaker;  // 0x328
        SharedPtr<> boneEffect;    // 0x338
        SharedPtr<> damageColor;   // 0x348: the flash on a hit
        SharedPtr<> waterEvent;    // 0x358
        SharedPtr<> facial;        // 0x368: FacialComp
        SharedPtr<> eventSensor;   // 0x378
        SharedPtr<> itemSensor;    // 0x388
        SharedPtr<> ladderInfo;    // 0x398
        void* attackCols;          // 0x3a8: owned (0x2b0), the attack collision components (Player_AttackCols_Ctor)
        SharedPtr<> locItemAL;     // 0x3b0: bone locators: itemA_L
        SharedPtr<> locItemAR;     // 0x3c0: itemA_R, the bow's
        SharedPtr<> locHead;       // 0x3d0
        SharedPtr<> locHara;       // 0x3e0: n_hara
        SharedPtr<> locWaist;      // 0x3f0
        WeakPtr<> arrows[2];       // 0x400: PlayerArrow actors (Player_SpawnArrow)
        WeakPtr<> hookshot;        // 0x420: the HookShot actor
        Vector<WeakPtr<>> magicRodBalls;  // 0x430: PlayerMagicRodBall actors, a pool
        WeakPtr<> boomerang;       // 0x448: the boomerang actor
        WeakPtr<> swordBeam;       // 0x458: the PlayerSwordBeam actor
        WeakPtr<> tempEntity;      // 0x468: destroyed by the update while alive
        SharedPtr<> modelResource[2];  // 0x478: Link's bfres
        u8 parts[0x40];            // 0x498: PartsVisibility, the item shape groups (Player_ShowItem)
        u8 pad_4d8[0x8];
        StateMachine<64> machine;  // 0x4e0: PlayerSM_vtable; PlayerState
        void* states[64];          // 0x1340: owned, by PlayerState
        StateTransition* stateHooks[5];  // 0x1540: into states 22, 11, 21, 1, 0, checked in that order first
        PlayerAnimController* animCtrl;  // 0x1568: a shared_ptr
        void* animCtrlCtrl;        // 0x1570
        PlayerKnockback* knockback;  // 0x1578: owned
        PlayerActionTransitions* actionTransitions;  // 0x1580: owned
        u8 pad_1588[0x8];
        PlayerInput input;         // 0x1590
        Vector<PlayerContact> bodyContacts;  // 0x1600: this frame's (Player_OnBodyContact)
        Vector<PlayerTouch> bodyTouches;  // 0x1618: this frame's (Player_OnBodyTouch)
        WeakPtr<> selfWeak;        // 0x1630
        RayQuery groundQuery;      // 0x1640: cast down each frame (Player_UpdateGround)
        u8 pad_16f8[0x8];
        u32 groundAttribute;       // 0x1700: the ground's collision attribute: bits 0-5 material, 6-9 kind
        u8 pad_1704[0x4];
        WeakPtr<> groundActor;     // 0x1708: the owner of the collider under Link
        UnorderedMap lowerLevelHoles;  // 0x1718: AreaHoleLowerLevel entities Link is inside (0xd21ba0, 0xd22240)
        UnorderedMap slowAreas;    // 0x1740: nothing inserts: always empty
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
        WeakPtr<> fairy;           // 0x1990: an ItemFairy Link touched, for the Bottle state
        bool inWaterArea;          // 0x19a0
        u8 recoilFlags;            // 0x19a1: bit 0 hit, 1 during a dash, 2 from the second source
        u8 pad_19a2[0x2];
        float comboTime;           // 0x19a4: 1/6 s after a bomb or an arrow, counted down; a bomb arrow within it
        s32 comboArrow;            // 0x19a8: the arrow slot an arrow started comboTime with, -1 none
        bool comboBomb;            // 0x19ac: a placed bomb started comboTime
        u8 pad_19ad[0x3];
        Actor* carried;            // 0x19b0: a weak_ptr, set while Link holds something over his head
        void* carriedCtrl;         // 0x19b8
        WeakPtr<> holder;          // 0x19c0: the actor holding Link (PlayerFlag::Held)
        PlayerLookAt lookAt;       // 0x19d0
        s32 shieldAngle;           // 0x1ad0: clamped to 45 degrees (Player_AimShield)
        PlayerStateHandoff handoff;  // 0x1ad4
        u8 unused_1b38;            // 0x1b38: 1 when made, never read
        bool sideView;             // 0x1b39: the room is side-view (Player_OnRoomChanged)
        bool carryNoDrop;          // 0x1b3a
        bool modelFlag;            // 0x1b3b: Player_SetModelFlag
        float airTime;             // 0x1b3c: above 1.5 s, Link goes back to respawnCoords
        Coords respawnCoords;      // 0x1b40: the last safe ground
        s32 respawnKind;           // 0x1b70: 0 ground, 1 side-view, 3 reset
        u8 pad_1b74[0xc];
        u32 unused_1b80;           // 0x1b80: 0 when made, never accessed again
        s32 boneClavicleL;         // 0x1b84
        s32 boneHead;              // 0x1b88
        Flags<PlayerFlag> flags;   // 0x1b8c
        u16 itemGroupBits;         // 0x1b90: a bit per item shape group shown
        u16 handItems;             // 0x1b92: bit 0 sword shown, bit 1 shield shown
        u8 pendingState;           // 0x1b94: entered at the next chance, 0xff none
        u8 pad_1b95[0x3];
        SharedPtr<> companion;     // 0x1b98: the companion (Player_SpawnCompanion)
        PlayerSwordCharge* swordCharge;  // 0x1ba8: owned
        void* bottle;              // 0x1bb0: owned PlayerBottle
        void* bow;                 // 0x1bb8: owned PlayerItemModel
        u8 signals[8][0x58];       // 0x1bc0: PlayerSignal
        float damageFlashTime;     // 0x1e80
        float unused_1e84;         // 0x1e84: a timer counted down but never raised, so always 0
        SharedPtr<> tailor;        // 0x1e88: TailorPlayer
        SharedPtr<> tailorWaterSurface;  // 0x1e98
        PlayerSwimDepth swimDepth; // 0x1ea8
        u8 controlModeOverride;    // 0x1ea9: Player_ControlMode's while flags ControlModeOverride
        u8 pad_1eaa[0x6];
        Vec4 holdOffset;           // 0x1eb0: moves towards holdOffsetTarget
        Vec4 holdOffsetTarget;     // 0x1ec0
        SharedPtr<> animPowerUp;   // 0x1ed0: material animation powerUp
        SharedPtr<> animDefence;   // 0x1ee0: defence
        u8 eventKind;              // 0x1ef0: from the running event's actor
        u8 pad_1ef1[0x7];
        FixedString<0x20> ocarinaAreaName;  // 0x1ef8: Player_SetOcarinaArea
        WeakPtr<> ocarinaArea;     // 0x1f20
        u32 forcedAngle;           // 0x1f30: used while flags ForcedAngle
        u8 pad_1f34[0xc];
        float shieldArmMatrix[16]; // 0x1f40: Player_AimShield; identity when made
        s32 shapeBody;             // 0x1f80: body_low__MI_body
        u8 pad_1f84[0xc];

        PlayerState State() const { return static_cast<PlayerState>(machine.state); }
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
    static_assert(offsetof(Player, comboTime) == 0x19a4 && offsetof(Player, comboArrow) == 0x19a8);
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
