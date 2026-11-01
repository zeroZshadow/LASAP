#pragma once
// The item base's actor and NPC component (scripts/re/ITEMS.md, Item actors), and their collision, body and water
// event.
#include "game/actor.hpp"
#include "game/actors/npc.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    struct PhysicsBody;

    // A physics world's settings (Physics_CreateWorlds builds worlds 0 and 1; PhysicsWorld_Ctor copies 0x81 bytes):
    // fields of the PhysX PxSceneDesc, its CPU dispatcher and the scene's callbacks. A null callback takes a default.
    struct PhysicsWorldDesc {
        Vec4 gravity;              // (0, -9.8, 0); the PxSceneDesc gets (0, -9.81, 0) regardless
        void* filterShader;        // 0x10: PxSimulationFilterShader, Physics_FilterPair in world 0
        void* filterCallback;      // 0x18: PxSimulationFilterCallback
        void* ccdContactModify;    // 0x20: PxCCDContactModifyCallback
        void* contactModify;       // 0x28: PxContactModifyCallback
        void* controllerQueryFilter;  // 0x30: PhysicsScene::controllerQueryFilter
        void* blockFilter;         // 0x38: PhysicsScene::blockFilter
        void* touchFilter;         // 0x40: PhysicsScene::touchFilter
        void* controllerFilter;    // 0x48: PhysicsWorld::controllerFilter
        u32 threadCount;           // 0x50: the CPU dispatcher's threads
        u32 treeRebuildRate;       // 0x54: PxSceneDesc dynamicTreeRebuildRateHint
        u32 threadCores;           // 0x58: bits 0-2, the cores the threads run on in order; 0: cores 2, 1, 0
        u32 limits[8];             // 0x5c: PxSceneLimits, maxNbActors first
        u32 frictionType;          // 0x7c: PxFrictionType
        bool desc80;               // 0x80: PhysicsWorld::desc80
        u8 pad_81[0xf];
    };
    static_assert(offsetof(PhysicsWorldDesc, controllerFilter) == 0x48 && offsetof(PhysicsWorldDesc, limits) == 0x5c);
    static_assert(offsetof(PhysicsWorldDesc, desc80) == 0x80 && sizeof(PhysicsWorldDesc) == 0x90);

    // A world's PhysX objects (PhysicsWorld::scene; 0x30 bytes from the physics heap). The filters are
    // PxQueryFilterCallbacks; their preFilter applies the layer rule of Physics_FilterPair to the query's filter data.
    struct PhysicsScene {
        void* pxScene;             // PxScene; its userData (+0x8) is the world
        void* cpuDispatcher;       // 0x8: PxDefaultCpuDispatcher
        void* controllerManager;   // 0x10: PxControllerManager
        void* controllerQueryFilter;  // 0x18: the character controllers' moves' (eTOUCH)
        void* blockFilter;         // 0x20: the scene queries' that report the blocking hit (eBLOCK)
        void* touchFilter;         // 0x28: the scene queries' that report all hits (eTOUCH)
    };
    static_assert(offsetof(PhysicsScene, controllerQueryFilter) == 0x18 && sizeof(PhysicsScene) == 0x30);

    // A physics world (PhysicsWorld_Ctor, vtable 0x1c51288; 0x2e0 bytes from the physics heap, PhysicsWorld_Make): a
    // PxScene. The physics update phase (0x15730) steps each world; Physics_DeliverEvents then delivers its queued
    // contacts and triggers and emits its stepped.
    struct PhysicsWorld {
        const uintptr_t* vtable;
        WeakPtr<> self;            // 0x8: shared-from-this
        u32 id;                    // 0x18: the maker's argument
        u8 pad_1c[0x4];
        PhysicsWorldDesc desc;     // 0x20: a copy of the maker's
        PhysicsScene* scene;       // 0xb0
        void* events;              // 0xb8: the scene's PxSimulationEventCallback (PhysicsEvents_vtable)
        void* controllerFilter;    // 0xc0: PxControllerFilterCallback for controller pairs; CharCtrlComp's
        const uintptr_t* stepTask; // 0xc8: the simulate's completion task (vtable 0x1c51308, run 0x10cceb0)
        u8 stepTaskData[0x28];     // 0xd0: its task manager +0xd8, PhysicsWorld* +0xf0
        u8 unused_f8[0xd0];        // 0 when made; unwritten in play (2026-10-31)
        SharedPtr<> unused_1c8;    // 0x1c8: null when made, released by the destructor; null in play (2026-10-31)
        bool resultsReady;         // 0x1d8: cleared before simulate, set by the step task; DeliverEvents waits for it
        u8 pad_1d9[0x7];
        u8 mutex[0x20];            // 0x1e0: held by the step and the step task
        u8 condition[0x8];         // 0x200: signalled by the step task
        bool desc80;               // 0x208: the desc's +0x80; the step task then calls PxScene virtual +0x2c8 (1, 1)
        u8 pad_209[0x7];
        Vector<void*> pendingAdds; // 0x210: the bodies waiting to be added (BodyCollision::pendingIndex)
        Signal beforeStep;         // 0x228: emitted before each simulate (ObjBomb connects, 0xa877b4)
        Signal stepped;            // 0x280: after the step's contacts and triggers
        u8 pad_2d8[0x8];
    };
    static_assert(offsetof(PhysicsWorld, desc) == 0x20 && offsetof(PhysicsWorld, stepTask) == 0xc8);
    static_assert(offsetof(PhysicsWorld, scene) == 0xb0 && offsetof(PhysicsWorld, pendingAdds) == 0x210);
    static_assert(offsetof(PhysicsWorld, resultsReady) == 0x1d8 && offsetof(PhysicsWorld, desc80) == 0x208);
    static_assert(offsetof(PhysicsWorld, beforeStep) == 0x228 && offsetof(PhysicsWorld, stepped) == 0x280);
    static_assert(sizeof(PhysicsWorld) == 0x2e0);

    // A pair's events, PhysX PxPairFlag's
    enum class PairEvent : u16 { Found = 0x4, Persists = 0x8, Lost = 0x10 };
    FLAGS_ENUM(PairEvent)

    // A contact of two bodies (PhysicsEvents_OnContact), passed to both bodies' contact signals
    struct PhysicsContact {
        SharedPtr<PhysicsBody> bodies[2];
        SharedPtr<> shapes[2];     // 0x20: the pair's shapes' wrappers, from their userData (+0x10)
        Flags<PairEvent> events;   // 0x40
        u8 pad_42[0x6];
        Vector<void*> points;      // 0x48: pooled records, one per contact point of the pair
    };
    static_assert(offsetof(PhysicsContact, events) == 0x40 && sizeof(PhysicsContact) == 0x60);

    // A trigger pair (PhysicsEvents_OnTrigger), passed to both bodies' trigger signals
    struct PhysicsTrigger {
        SharedPtr<PhysicsBody> trigger;
        SharedPtr<PhysicsBody> other;  // 0x10
        SharedPtr<> triggerShape;  // 0x20: the shapes' wrappers
        SharedPtr<> otherShape;    // 0x30
        PairEvent status;          // 0x40: Found or Lost
        u8 pad_42[0x6];
    };
    static_assert(offsetof(PhysicsTrigger, status) == 0x40 && sizeof(PhysicsTrigger) == 0x48);

    // A collision with a body (type id BodyCollision_TypeId; BodyCollision_Ctor, BodyCollision_vtable 0x1c50420; a
    // Node, 0x270 bytes; the NPC collision NpcComponent_MakeCollision makes, 0x320 bytes, derives from it). Virtuals:
    // +0x28 BodyCollision_SyncPose (the node's world pose to the body), +0xa8 BodyCollision_SetEnabled (+0xd8
    // enable, +0xe0 disable).
    struct BodyCollision {
        Node node;
        const uintptr_t* interfaceVtable;  // 0x148: a second base (0x1c50518)
        WeakPtr<PhysicsWorld> world;  // 0x150: the world it adds the body to
        SharedPtr<> shapeSource;   // 0x160: the resource of its shapes (BodyCollision_MakeBodyFromResource)
        PhysicsBody* body;         // 0x170: a shared pointer
        void* bodyCtrl;
        SharedPtr<> aggregate;     // 0x180: when set, the body goes into its PxAggregate (its +0x58), not the scene
        SharedString name;         // 0x190: also the PxActor's (BodyCollision_SetName, vtable +0x80; +0x88 gets it)
        u32 nodeSlot;              // 0x1b0: the handle of its slot on the node's own transformChanged
        u32 worldSlot;             // 0x1b4: the handle of its slot on the world's stepped (BodyCollision_ConnectWorld)
        Signal bodyMade;           // 0x1b8: emitted with the body once it is added (owner +0x1c8)
        Signal bodyLost;           // 0x210: the body is gone (owner +0x220)
        s32 pendingIndex;          // 0x268: -1, else an entry of the world's pending list (+0x210) AddToScene flushes
        bool enabled;              // 0x26c: BodyCollision_SetEnabled; 1 when made
        u8 pad_26d[0x3];
    };
    static_assert(offsetof(BodyCollision, interfaceVtable) == 0x148 && offsetof(BodyCollision, world) == 0x150);
    static_assert(offsetof(BodyCollision, body) == 0x170 && offsetof(BodyCollision, aggregate) == 0x180);
    static_assert(offsetof(BodyCollision, name) == 0x190 && offsetof(BodyCollision, nodeSlot) == 0x1b0);
    static_assert(offsetof(BodyCollision, bodyMade) == 0x1b8);
    static_assert(offsetof(BodyCollision, bodyLost) == 0x210 && offsetof(BodyCollision, pendingIndex) == 0x268);
    static_assert(sizeof(BodyCollision) == 0x270);

    // A collision's body (PhysicsBody_Ctor, base vtable 0x1c50e48; the dynamic body PhysicsBody_MakeDynamic makes,
    // vtable 0x1c50f88, 0x680 bytes, from the 0x10-aligned physics heap): a PhysX 3.4 actor and its shapes. Its
    // vtable +0x0 is IsA. The item update and the drop call the PxRigidDynamic's virtuals: +0x130 getLinearVelocity (a
    // PxVec3 through x8), +0x138 setLinearVelocity (const Vec3*, bool wake), +0x140 getAngularVelocity, +0x148
    // setAngularVelocity, +0x150 addForce (const Vec3*, mode, bool wake), +0x170 setRigidBodyFlag, +0x180
    // getRigidBodyFlags, +0x1b8 setKinematicTarget, +0x1c8 setLinearDamping, +0x1d8 setAngularDamping, +0x1e0
    // getAngularDamping (float), +0x248 wakeUp; PxActor +0x38 getScene, +0x58 setActorFlag, +0xb0 setGlobalPose.
    struct PhysicsBody {
        const uintptr_t* vtable;
        WeakPtr<> self;            // 0x8: enable_shared_from_this's (set by 0x10c6fc0)
        void** actorRef;           // 0x18: a 0x10-byte block {PxActor*, u8}
        u8 shapeCount;             // 0x20: getShapes' count, up to 64
        u8 pad_21[0x7];
        SharedPtr<> shapes[64];    // 0x28: each shape's wrapper, from the shape's userData (+0x10)
        u8 unused_428[0x10];       // 0x428: no access in the physics code; 0 on Link's and item bodies (2026-10-31)
        WeakPtr<BodyCollision> collision;  // 0x438: its owner (the body makers set it)
        SharedPtr<> userData;      // 0x448: the PxActor's userData (+0x10), a 0x10-byte object pointing back (+0x8)
        bool inScene;              // 0x458: added to the PxScene directly (BodyCollision_AddToScene)
        bool inAggregate;          // 0x459: added to a PxAggregate (PhysicsBody_AddToAggregate); in a scene while the
                                   // PxActor's getScene is not null
        u8 pad_45a[0x6];
        Signal contactFound;       // 0x460: (other body sp*, PhysicsContact*, bool second), PhysicsBody_EmitContact
        Signal contactPersists;    // 0x4b8: as contactFound
        Signal contactLost;        // 0x510: as contactFound
        Signal triggerEntered;     // 0x568: (PhysicsTrigger*), on the trigger and the other body
        Signal triggerLeft;        // 0x5c0: as triggerEntered
        Signal controllerHit;      // 0x618: (PlayerContact*, true): a character controller hit one of its shapes
        void* pxActor;             // 0x670: the dynamic body's PxRigidDynamic (concrete type 6, else null)
        bool kinematic;            // 0x678: its eKINEMATIC flag (BodyCollision_SetKinematic)
        u8 pad_679[0x7];
    };
    static_assert(offsetof(PhysicsBody, shapes) == 0x28 && offsetof(PhysicsBody, userData) == 0x448);
    static_assert(offsetof(PhysicsBody, collision) == 0x438 && offsetof(PhysicsBody, contactFound) == 0x460);
    static_assert(offsetof(PhysicsBody, inAggregate) == 0x459 && offsetof(PhysicsBody, triggerEntered) == 0x568);
    static_assert(offsetof(PhysicsBody, controllerHit) == 0x618 && offsetof(PhysicsBody, pxActor) == 0x670);
    static_assert(offsetof(PhysicsBody, kinematic) == 0x678 && sizeof(PhysicsBody) == 0x680);

    // The water event component (WaterEvent_Make, WaterEvent_vtable; 0x160 bytes): whether its owner is in water,
    // probed at the owner's root each frame (WaterEvent_Update).
    struct WaterEvent {
        Entity entity;
        Signal entered;            // 0x58: emitted (surface, attribute*) when the owner enters water
        Signal left;               // 0xb0: emitted with true when it leaves (also WaterEvent_Leave, vtable +0x48)
        u8 pad_108[0x8];
        Function attributeProvider;  // 0x110: u32(), the attribute; when empty the ground's (WaterEvent_Update)
        Vec4 offset;               // 0x140: the probe's offset (NpcComponent_MakeWaterEvent: the row's offsetY)
        bool hasOffset;            // 0x150: the probe adds offset
        bool inWater;              // 0x151
        u8 pad_152[0x2];
        float surface;             // 0x154: the water surface's height while inWater; -FLT_MAX when made
        u32 attribute;             // 0x158: passed with entered; a collision attribute (Player::groundAttribute's)
        u8 pad_15c[0x4];
    };
    static_assert(offsetof(WaterEvent, entered) == 0x58 && offsetof(WaterEvent, left) == 0xb0);
    static_assert(offsetof(WaterEvent, attributeProvider) == 0x110 && offsetof(WaterEvent, offset) == 0x140);
    static_assert(offsetof(WaterEvent, inWater) == 0x151 && offsetof(WaterEvent, surface) == 0x154);
    static_assert(offsetof(WaterEvent, attribute) == 0x158 && sizeof(WaterEvent) == 0x160);

    // The item base's NPC component (ItemNpcComponent_Ctor, ItemComponent_vtable; 0x360 bytes): an NPC component
    // with a body that a launch, the water and the row's physics traits move (ItemNpcComponent_Update, vtable +0x3c8)
    struct ItemComponent {
        NpcComponent npc;
        float angle;               // 0x2e0: random, -pi to pi, when made
        u8 zoneFlag;               // 0x2e4: cleared when the actor enters its zone (ItemActor_ZoneEnter)
        bool swallowed;            // 0x2e5: ItemComponent_Swallow ran; no impulses or rest timer since
        bool holdVertical;         // 0x2e6: the launch keeps the body's vertical velocity; set after each launch
        bool moving;               // 0x2e7: no rest timer while set
        bool launched;             // 0x2e8: the launch velocity was given; the traits are applied next
        u8 pad_2e9[0x3];
        float restTime;            // 0x2ec: seconds the body has been still; at 0.25 it turns kinematic
        Vec4 launch;               // 0x2f0: a launch's velocity per frame (times the frame rate, 0x1c34538)
        Signal itemAdded;          // 0x300: emitted with the actor's shared pointer by ItemComponent_AddItem; TagRapidFlowGame
                                   // connects (0xe2a2b0)
        bool bodyReady;            // 0x358: the small key's (SmallKey_MakeCollision): its body is made
        bool fallPending;          // 0x359: a Fall action waits for the body (SmallKey_StartFall)
        u8 pad_35a[0x6];
    };
    static_assert(offsetof(ItemComponent, angle) == 0x2e0 && offsetof(ItemComponent, swallowed) == 0x2e5);
    static_assert(offsetof(ItemComponent, restTime) == 0x2ec);
    static_assert(offsetof(ItemComponent, launch) == 0x2f0 && offsetof(ItemComponent, itemAdded) == 0x300);
    static_assert(offsetof(ItemComponent, bodyReady) == 0x358 && sizeof(ItemComponent) == 0x360);

    // A row of Items.gsheet (field names the sheet's)
    struct ItemsRow {
        SheetString symbol;
        s32 itemID;                // 0x10
        s32 actorID;               // 0x14
        SheetString npcKey;        // 0x18: Npc.gsheet key
        struct { SheetString pop, ground, getting; } tailor;  // 0x28
        SheetString jingle;        // 0x58
        bool keepAsset;            // 0x68
        bool eventPause;           // 0x69
        SheetString gettingFlag;   // 0x70: GlobalFlags.gsheet key
        struct { bool enabled; u32 category; SheetString parameter; } autoSave;  // 0x80: category and parameter a condition
    };
    static_assert(offsetof(ItemsRow, keepAsset) == 0x68 && offsetof(ItemsRow, autoSave) == 0x80);
    static_assert(sizeof(ItemsRow) == 0x98);

    // The item base's actor (ItemActor_Ctor, ItemActor_vtable; 0x320 bytes, allocated with its control block 0x340)
    struct ItemActor {
        Actor actor;
        FixedString<0x20> rowKey;  // 0x2a8: its Npc.gsheet row's (ItemActor_MakeComponent)
        SharedPtr<> itemsData;     // 0x2d0: ItemActor_Setup's ItemsSheet_FindRow: the Items.gsheet data, kept alive
        const ItemsRow* itemRow;   // 0x2e0: its item's
        u8 itemId;                 // 0x2e8
        u8 pad_2e9[0x3];
        u32 count;                 // 0x2ec
        ItemComponent* component;  // 0x2f0: a shared pointer (actor vtable +0x128 makes it)
        void* componentCtrl;
        const void* poses;         // 0x300: the pose pair the model takes (ItemPose[2])
        float touchDelay;          // 0x308: seconds before Link can take it; 0.5 when made
        u32 zoneValue;             // 0x30c: cleared when the actor enters its zone
        bool madeInEvent;          // 0x310: an event ran when it was made
        u8 pad_311[0xf];
    };
    static_assert(offsetof(ItemActor, rowKey) == 0x2a8 && offsetof(ItemActor, itemsData) == 0x2d0);
    static_assert(offsetof(ItemActor, itemRow) == 0x2e0 && offsetof(ItemActor, itemId) == 0x2e8);
    static_assert(offsetof(ItemActor, component) == 0x2f0 && offsetof(ItemActor, poses) == 0x300);
    static_assert(offsetof(ItemActor, touchDelay) == 0x308 && offsetof(ItemActor, madeInEvent) == 0x310);
    static_assert(sizeof(ItemActor) == 0x320);
}
