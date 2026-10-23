#pragma once
// Entities, scene-graph nodes and actors: the fields the module reads (scripts/re/ACTORS.md).
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
    // Entity virtuals, byte offsets for CallVirtual.
    namespace EntityVirtual {
        constexpr size_t kSetup = 0x20, kOnActiveChanged = 0x50, kRegister = 0x78;
    }

    // The engine's root class (Entity_vtable 0x1c313e8), the base of actors and components; the common prefix.
    struct Entity {
        const uintptr_t* vtable;
        WeakPtr self;              // 0x8: shared-from-this
        char name[0x20];           // 0x18: NUL-terminated
        u64 nameLength;            // 0x38
        u32 flags;                 // 0x40: EntityFlags
        u8 pad_44[0x4];
        WeakPtr parent;            // 0x48

        bool Active() const { return (flags & EntityFlags::kActiveMask) == EntityFlags::kActive; }
        bool Gone() const { return (flags & EntityFlags::kGone) != 0; }
    };
    static_assert(offsetof(Entity, self) == 0x8 && offsetof(Entity, name) == 0x18);
    static_assert(offsetof(Entity, nameLength) == 0x38 && offsetof(Entity, flags) == 0x40);
    static_assert(offsetof(Entity, parent) == 0x48 && sizeof(Entity) == 0x58);

    // A component that is a scene-graph node: an actor's root, a model component, the item-get's model.
    // Node_UpdateWorld makes coords from coordsNew and the parent's coords; Node_MarkDirty sets needsCoordUpdate.
    struct Node {
        Entity entity;
        Node* parent;              // 0x58
        u8 pad_60[0x20];
        Coords coordsNew;          // 0x80: local
        Coords coords;             // 0xb0: world
        bool needsCoordUpdate;     // 0xe0
    };
    static_assert(offsetof(Node, parent) == 0x58 && offsetof(Node, coordsNew) == 0x80);
    static_assert(offsetof(Node, coords) == 0xb0 && offsetof(Node, needsCoordUpdate) == 0xe0);

    struct LebParam {
        u32 value;
        u32 type;
    };

    // An actor (Actor_vtable 0x1b50f28): the prefix every actor class the module reads shares (Player's own fields
    // start at 0x2a8).
    struct Actor {
        Entity entity;
        u8 components[0x28];       // 0x58: by name
        Node* root;                // 0x80: a shared_ptr; the actor's coordinates
        void* rootCtrl;            // 0x88
        u8 pad_90[0x8];
        u64 lebKey;                // 0x98: the LEB record's key
        u16 actorId;               // 0xa0
        u16 zoneState;             // 0xa2: bit 0 suspended
        u16 classBits;             // 0xa4: bit 0 zone-scoped
        u8 pad_a6[0xa];
        s32 zone;                  // 0xb0: -1 none
        u8 pad_b4[0x250 - 0xb4];
        const LebActorRecord* record;  // 0x250: its switches
        u8 pad_258[0x2a8 - 0x258];
    };
    static_assert(offsetof(Actor, root) == 0x80 && offsetof(Actor, lebKey) == 0x98);
    static_assert(offsetof(Actor, actorId) == 0xa0 && offsetof(Actor, zone) == 0xb0);
    static_assert(offsetof(Actor, record) == 0x250 && sizeof(Actor) == 0x2a8);

    // Actor_Create's params (0x1c0 bytes; ACTORS.md, Params): zeroed but for the id, zone and transform, they make
    // an actor as PopEnemy does. The caller releases the shared and weak pointers and the members with destructors.
    struct alignas(16) ActorParams {
        u64 lebKey;
        u16 actorId;               // 0x8: the actor table's index, as in LEB files
        u8 pad_a[0x2];
        s32 zone;                  // 0xc: -1 none
        LebParam lebParams[8];     // 0x10: ActorParams_Dtor2
        u8 bfres[0x80];            // 0x50: ActorParams_Dtor1
        u8 drsb[0x80];             // 0xd0: ActorParams_Dtor0
        SharedPtr flowchart;       // 0x150
        SharedPtr lebRoom;         // 0x160
        const LebActorRecord* lebRecord;  // 0x170
        u8 pad_178[0x8];
        Vec4 pos;                  // 0x180
        Vec4 rot;                  // 0x190: a quaternion
        Vec4 scale;                // 0x1a0
        WeakPtr unread;            // 0x1b0
    };
    static_assert(offsetof(ActorParams, actorId) == 0x8 && offsetof(ActorParams, zone) == 0xc);
    static_assert(offsetof(ActorParams, lebParams) == 0x10 && offsetof(ActorParams, bfres) == 0x50);
    static_assert(offsetof(ActorParams, drsb) == 0xd0 && offsetof(ActorParams, flowchart) == 0x150);
    static_assert(offsetof(ActorParams, lebRoom) == 0x160 && offsetof(ActorParams, pos) == 0x180);
    static_assert(offsetof(ActorParams, rot) == 0x190 && offsetof(ActorParams, scale) == 0x1a0);
    static_assert(offsetof(ActorParams, unread) == 0x1b0 && sizeof(ActorParams) == 0x1c0);

    // An actor's record in a level's LEB file (ACTORS.md, LEB actor record), as the level loads it.
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
    };
    static_assert(offsetof(LebActorRecord, actorId) == 0xc && offsetof(LebActorRecord, zone) == 0x10);
    static_assert(offsetof(LebActorRecord, pos) == 0x14 && offsetof(LebActorRecord, rotDegrees) == 0x20);
    static_assert(offsetof(LebActorRecord, scale) == 0x2c && offsetof(LebActorRecord, params) == 0x38);
    static_assert(offsetof(LebActorRecord, switchTypes) == 0x78 && offsetof(LebActorRecord, switchIndices) == 0x7c);
}
