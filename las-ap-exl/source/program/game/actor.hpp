#pragma once
// Entities, scene-graph nodes and actors (scripts/re/ACTORS.md).
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    struct LebActorRecord;

    // Vec3 still pads to 0x10 bytes just like Vec4, we use both for clarity
    struct Vec3 {
        float x;
        float y;
        float z;
    };

    struct Vec4 {
        float x;
        float y;
        float z;
        float t;
    };

    struct Coords {
        Vec3 pos;
        float paddingPos;
        Vec4 rotate;
        Vec3 scale;
        float paddingScale;
    };

    // Entity::flags. Active (updated and drawn) while (flags & kActiveMask) == kActive.
    namespace EntityFlags {
        constexpr u32 kEnabled = 1u << 0, kParentActive = 1u << 1, kInZone = 1u << 2, kInitialized = 1u << 3,
                      kStarted = 1u << 4, kDestroyRequested = 1u << 30, kDestroyed = 1u << 31;
        constexpr u32 kActiveMask = 0x40000017, kActive = 0x17, kGone = kDestroyRequested | kDestroyed;
    }
    // Entity virtuals, byte offsets for CallVirtual (ACTORS.md, Classes).
    namespace EntityVirtual {
        constexpr size_t kSetup = 0x20, kOnActiveChanged = 0x50, kRegister = 0x78;
    }

    // The engine's root class (Entity_vtable 0x1c313e8), the base of actors and components. A component is an
    // Entity whose parent is its actor.
    struct Entity {
        const uintptr_t* vtable;
        WeakPtr self;              // 0x8: shared-from-this
        char name[0x20];           // 0x18: NUL-terminated
        u64 nameLength;            // 0x38
        u32 flags;                 // 0x40: EntityFlags
        u8 pad_44[0x4];
        WeakPtr parent;            // 0x48: an actor's container; a component's actor

        bool Active() const { return (flags & EntityFlags::kActiveMask) == EntityFlags::kActive; }
        bool Gone() const { return (flags & EntityFlags::kGone) != 0; }
    };
    static_assert(offsetof(Entity, self) == 0x8 && offsetof(Entity, name) == 0x18);
    static_assert(offsetof(Entity, nameLength) == 0x38 && offsetof(Entity, flags) == 0x40);
    static_assert(offsetof(Entity, parent) == 0x48 && sizeof(Entity) == 0x58);

    // A signal: its slots by handle, called by Emit under the mutex. One layout for every argument type: a bool
    // (SignalBool_vtable 0x1b59008, SignalBool_Connect, ...), none (Signal_vtable 0x1b59608, Signal_Connect, ...).
    struct Signal {
        const uintptr_t* vtable;
        const uintptr_t* connectorVtable;  // 0x8: a second base (0x1b59098) whose slots forward to owner
        Signal* owner;             // 0x10: the signal itself
        OrderedMap slots;          // 0x18: std::map<u32 handle, std::function<void(bool)>>
        u32 nextHandle;            // 0x30: Connect's next handle, from 1; 0 closed
        u32 oneShot;               // 0x34: 1: Emit drops every slot and closes the signal after calling them
        u8 mutex[0x20];            // 0x38: nn::os mutex
    };
    static_assert(offsetof(Signal, slots) == 0x18 && offsetof(Signal, nextHandle) == 0x30);
    static_assert(offsetof(Signal, mutex) == 0x38 && sizeof(Signal) == 0x58);

    // A component that is a scene-graph node (Node_vtable 0x1c31a68, Node_Ctor; 0x148 bytes, allocated 0x150): an
    // actor's root, a model component, the item-get's model. Node_UpdateWorld makes coords from coordsNew and the
    // parent's coords; Node_MarkDirty sets needsCoordUpdate and emits transformChanged.
    struct Node {
        Entity entity;
        Node* parent;              // 0x58: a weak_ptr
        void* parentCtrl;          // 0x60
        Vector<SharedPtr> children;  // 0x68
        Coords coordsNew;          // 0x80: local
        Coords coords;             // 0xb0: world
        bool needsCoordUpdate;     // 0xe0
        u8 pad_e1[0x7];
        Signal transformChanged;   // 0xe8: the skeletal model subscribes
        void* attachment;          // 0x140: an owned bone attachment; without a parent the world is local times its
    };
    static_assert(offsetof(Node, parent) == 0x58 && offsetof(Node, children) == 0x68);
    static_assert(offsetof(Node, coordsNew) == 0x80 && offsetof(Node, coords) == 0xb0);
    static_assert(offsetof(Node, needsCoordUpdate) == 0xe0 && offsetof(Node, transformChanged) == 0xe8);
    static_assert(offsetof(Node, attachment) == 0x140 && sizeof(Node) == 0x148);

    struct LebParam {
        u32 value;
        u32 type;
    };

    // An actor (Actor_vtable 0x1b50f28, Actor_Ctor; ACTORS.md, Actor fields): the base every actor class shares.
    struct Actor {
        Entity entity;
        UnorderedMap components;   // 0x58: by name (FNV-1a)
        Node* root;                // 0x80: a shared_ptr; the actor's coordinates
        void* rootCtrl;            // 0x88
        s32 componentIterDepth;    // 0x90
        u8 pad_94[0x4];
        u64 lebKey;                // 0x98: the LEB record's key
        u16 actorId;               // 0xa0
        u16 zoneState;             // 0xa2: bit 0 suspended
        u16 classBits;             // 0xa4: bit 0 zone-scoped
        bool placed;               // 0xa6: Actor_PlaceAtSpawn
        u8 pad_a7;
        u32 serial;                // 0xa8: never 0
        u32 priority;              // 0xac: the actor table's
        s32 zone;                  // 0xb0: -1 none
        s32 roomZone;              // 0xb4: the owning room's zone, -1
        s32 removalZone;           // 0xb8: the zone its removal is recorded under
        LebParam lebParams[8];     // 0xbc
        Vector<void*> links;       // 0x100: one per LEB link, owned (vtable 0x1b51088)
        Vector<void*> railLinks;   // 0x118: one per LEB rail link, owned (vtable 0x1b51068)
        SharedPtr bfres[8];        // 0x130
        SharedPtr drsb[8];         // 0x1b0
        SharedPtr flowchart;       // 0x230
        SharedPtr lebRoom;         // 0x240
        const LebActorRecord* record;  // 0x250: its switches
        u8 pad_258[0x8];
        Vec4 spawnPos;             // 0x260
        Vec4 spawnRot;             // 0x270: a quaternion
        Vec4 spawnScale;           // 0x280
        // Signal handles: the room manager's, the event manager's two, the scene's room change and zone signals.
        u32 roomManagerSub;        // 0x290
        u32 eventSubA;             // 0x294
        u32 eventSubB;             // 0x298
        u32 roomChangedSub;        // 0x29c
        u32 zoneSignalSub;         // 0x2a0
        u8 pad_2a4[0x4];
    };
    static_assert(offsetof(Actor, components) == 0x58 && offsetof(Actor, root) == 0x80);
    static_assert(offsetof(Actor, componentIterDepth) == 0x90 && offsetof(Actor, lebKey) == 0x98);
    static_assert(offsetof(Actor, actorId) == 0xa0 && offsetof(Actor, placed) == 0xa6);
    static_assert(offsetof(Actor, serial) == 0xa8 && offsetof(Actor, zone) == 0xb0);
    static_assert(offsetof(Actor, removalZone) == 0xb8 && offsetof(Actor, lebParams) == 0xbc);
    static_assert(offsetof(Actor, links) == 0x100 && offsetof(Actor, railLinks) == 0x118);
    static_assert(offsetof(Actor, bfres) == 0x130 && offsetof(Actor, drsb) == 0x1b0);
    static_assert(offsetof(Actor, flowchart) == 0x230 && offsetof(Actor, lebRoom) == 0x240);
    static_assert(offsetof(Actor, record) == 0x250 && offsetof(Actor, spawnPos) == 0x260);
    static_assert(offsetof(Actor, spawnScale) == 0x280 && offsetof(Actor, roomManagerSub) == 0x290);
    static_assert(offsetof(Actor, zoneSignalSub) == 0x2a0 && sizeof(Actor) == 0x2a8);

    // Actor_Create's params (0x1c0 bytes; ACTORS.md, Params): zeroed but for the id, zone and transform, they make
    // an actor as PopEnemy does. The caller releases the shared and weak pointers and the members with destructors.
    struct alignas(16) ActorParams {
        u64 lebKey;
        u16 actorId;               // 0x8: the actor table's index, as in LEB files
        u8 pad_a[0x2];
        s32 zone;                  // 0xc: -1 none
        LebParam lebParams[8];     // 0x10: ActorParams_Dtor2
        SharedPtr bfres[8];        // 0x50: ActorParams_Dtor1
        SharedPtr drsb[8];         // 0xd0: ActorParams_Dtor0
        SharedPtr flowchart;       // 0x150
        SharedPtr lebRoom;         // 0x160
        const LebActorRecord* lebRecord;  // 0x170
        u8 pad_178[0x8];
        Vec4 pos;                  // 0x180
        Vec4 rot;                  // 0x190: a quaternion
        Vec4 scale;                // 0x1a0
        WeakPtr unread;            // 0x1b0: copied, never read
    };
    static_assert(offsetof(ActorParams, actorId) == 0x8 && offsetof(ActorParams, zone) == 0xc);
    static_assert(offsetof(ActorParams, lebParams) == 0x10 && offsetof(ActorParams, bfres) == 0x50);
    static_assert(offsetof(ActorParams, drsb) == 0xd0 && offsetof(ActorParams, flowchart) == 0x150);
    static_assert(offsetof(ActorParams, lebRoom) == 0x160 && offsetof(ActorParams, pos) == 0x180);
    static_assert(offsetof(ActorParams, rot) == 0x190 && offsetof(ActorParams, scale) == 0x1a0);
    static_assert(offsetof(ActorParams, unread) == 0x1b0 && sizeof(ActorParams) == 0x1c0);

    // What a factory hands an actor class's constructor (ACTORS.md, Creation): the actor's name, its owner (a shared
    // reference) and a copy of the params. ActorArgs_Dtor releases the owner and the params' members.
    struct alignas(16) ActorArgs {
        const char* name;
        u64 length;                // 0x8
        u8 enabled;                // 0x10: 1
        u8 pad_11[0x7];
        void* owner;               // 0x18
        void* ownerCtrl;           // 0x20
        u8 pad_28[0x8];
        ActorParams params;        // 0x30
    };
    static_assert(offsetof(ActorArgs, owner) == 0x18 && offsetof(ActorArgs, params) == 0x30);
    static_assert(sizeof(ActorArgs) == 0x1f0);

    // An entry of the actor table (ActorTable, one per actor id; ACTORS.md, Actor table).
    struct ActorTableEntry {
        // (out actor, owner, params)
        SharedPtrOut (*factory)(SharedPtr* owner, ActorParams* params);
        const char* resources[8];  // 0x8: .bfres, .drsb or .bfevfl names
        u8 loadGroup;              // 0x48
        u8 priority;               // 0x49: the actor's +0xac
        u8 resourceCount;          // 0x4a
        u8 systemOwned;            // 0x4b: made with the actor system as owner
        u8 pad_4c[0x4];
        const char* name;          // 0x50
    };
    static_assert(offsetof(ActorTableEntry, loadGroup) == 0x48 && offsetof(ActorTableEntry, name) == 0x50);
    static_assert(sizeof(ActorTableEntry) == 0x58);

    // A LEB actor record's link to another actor of the room, and its link to a rail point.
    struct LebLink {
        LebParam params[2];
        u32 actorIndex;            // 0x10: LebRoom virtual +0x28
    };
    static_assert(sizeof(LebLink) == 0x14);
    struct LebRailLink {
        LebParam params[2];
        u32 rail;                  // 0x10
        u32 point;                 // 0x14
    };
    static_assert(sizeof(LebRailLink) == 0x18);

    // An actor's record in a level's LEB file (ACTORS.md, LEB actor record), as the level loads it: 0x90 bytes, then
    // linkCount links, railLinkCount rail links and linkedIdCount actor indices.
    struct LebActorRecord {
        u64 key;
        u32 nameOffset;            // 0x8
        u16 actorId;               // 0xc
        u8 pad_e[0x2];
        u32 zone;                  // 0x10: the room
        Vec3 pos;                  // 0x14
        Vec3 rotDegrees;           // 0x20
        Vec3 scale;                // 0x2c
        LebParam params[8];        // 0x38
        u8 switchTypes[4];         // 0x78
        u16 switchIndices[4];      // 0x7c
        u8 removalGate;            // 0x84: e, the removal memory's gate without links
        u8 linkDecision;           // 0x85: k, read on each linked record
        u8 b;                      // 0x86: not read by the room or the actor
        u8 linkCount;              // 0x87: x
        u8 linkedIdCount;          // 0x88: y
        u8 railLinkCount;          // 0x89: z
        u8 pad_8a[0x6];

        const LebLink* Links() const { return reinterpret_cast<const LebLink*>(this + 1); }
        const LebRailLink* RailLinks() const { return reinterpret_cast<const LebRailLink*>(Links() + linkCount); }
        const u32* LinkedIds() const { return reinterpret_cast<const u32*>(RailLinks() + railLinkCount); }
    };
    static_assert(offsetof(LebActorRecord, actorId) == 0xc && offsetof(LebActorRecord, zone) == 0x10);
    static_assert(offsetof(LebActorRecord, pos) == 0x14 && offsetof(LebActorRecord, rotDegrees) == 0x20);
    static_assert(offsetof(LebActorRecord, scale) == 0x2c && offsetof(LebActorRecord, params) == 0x38);
    static_assert(offsetof(LebActorRecord, switchTypes) == 0x78 && offsetof(LebActorRecord, switchIndices) == 0x7c);
    static_assert(offsetof(LebActorRecord, removalGate) == 0x84 && offsetof(LebActorRecord, linkCount) == 0x87);
    static_assert(offsetof(LebActorRecord, railLinkCount) == 0x89 && sizeof(LebActorRecord) == 0x90);
}
