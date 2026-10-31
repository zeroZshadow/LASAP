#pragma once
// The item base's actor and NPC component (scripts/re/ITEMS.md, Item actors), and what of their collision, body and
// water event the module reads.
#include "game/actor.hpp"
#include "game/actors/npc.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // A collision with a body (type id BodyCollision_TypeId): the parts the item base and the module use
    struct BodyCollision {
        Entity entity;
        u8 unknown58[0x118];
        struct PhysicsBody* body;  // 0x170: a shared pointer
        void* bodyCtrl;
        u8 unknown180[0x38];
        Signal bodyMade;           // 0x1b8: the body is made (connected through its owner, +0x1c8)
        Signal bodyLost;           // 0x210: the body is gone (owner +0x220)
    };
    static_assert(offsetof(BodyCollision, body) == 0x170 && offsetof(BodyCollision, bodyMade) == 0x1b8);
    static_assert(offsetof(BodyCollision, bodyLost) == 0x210);

    // A collision's body: its PhysX actor (PxRigidDynamic, PhysX 3.4), whose virtuals the item update and the drop
    // call: +0x130 getLinearVelocity (a PxVec3 through x8), +0x138 setLinearVelocity (const Vec3*, bool wake), +0x140
    // getAngularVelocity, +0x148 setAngularVelocity, +0x150 addForce (const Vec3*, mode, bool wake), +0x1c8
    // setLinearDamping, +0x1d8 setAngularDamping, +0x1e0 getAngularDamping (float)
    struct PhysicsBody {
        u8 unknown0[0x458];
        u8 flag458;                // 0x458: with flag459, the item update's water lift needs one of them
        u8 flag459;                // 0x459
        u8 unknown45a[0x216];
        void* pxActor;             // 0x670
    };
    static_assert(offsetof(PhysicsBody, flag459) == 0x459 && offsetof(PhysicsBody, pxActor) == 0x670);

    // The water event component (WaterEvent_Make, WaterEvent_vtable; 0x160 bytes): whether its owner is in water
    struct WaterEvent {
        Entity entity;
        u8 unknown58[0xf9];
        bool inWater;              // 0x151
        u8 pad_152[0x2];
        float surface;             // 0x154: the water surface's height while inWater
        u8 unknown158[0x8];
    };
    static_assert(offsetof(WaterEvent, inWater) == 0x151 && offsetof(WaterEvent, surface) == 0x154);
    static_assert(sizeof(WaterEvent) == 0x160);

    // The item base's NPC component (ItemNpcComponent_Ctor, ItemComponent_vtable; 0x360 bytes): an NPC component
    // with a body that a launch, the water and the row's physics traits move (ItemNpcComponent_Update, vtable +0x3c8)
    struct ItemComponent {
        NpcComponent npc;
        float angle;               // 0x2e0: random, -pi to pi, when made
        u8 zoneFlag;               // 0x2e4: cleared when the actor enters its zone (ItemActor_ZoneEnter)
        u8 pad_2e5;
        bool holdVertical;         // 0x2e6: the launch keeps the body's vertical velocity; set after each launch
        bool moving;               // 0x2e7: no rest timer while set
        bool launched;             // 0x2e8: the launch velocity was given; the traits are applied next
        u8 pad_2e9[0x3];
        float restTime;            // 0x2ec: seconds the body has been still; at 0.25 it is put to rest
        Vec4 launch;               // 0x2f0: a launch's velocity per frame (times the frame rate, 0x1c34538)
        Signal signal300;          // 0x300
        bool bodyReady;            // 0x358: the small key's (SmallKey_MakeCollision): its body is made
        bool fallPending;          // 0x359: a Fall action waits for the body (SmallKey_StartFall)
        u8 pad_35a[0x6];
    };
    static_assert(offsetof(ItemComponent, angle) == 0x2e0 && offsetof(ItemComponent, restTime) == 0x2ec);
    static_assert(offsetof(ItemComponent, launch) == 0x2f0 && offsetof(ItemComponent, signal300) == 0x300);
    static_assert(offsetof(ItemComponent, bodyReady) == 0x358 && sizeof(ItemComponent) == 0x360);

    // The item base's actor (ItemActor_Ctor, ItemActor_vtable; 0x320 bytes, allocated with its control block 0x340)
    struct ItemActor {
        Actor actor;
        FixedString<0x20> rowKey;  // 0x2a8: its Npc.gsheet row's (ItemActor_MakeComponent)
        SharedPtr<> sp2d0;         // 0x2d0
        u8 unknown2e0[0x8];
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
    static_assert(offsetof(ItemActor, rowKey) == 0x2a8 && offsetof(ItemActor, itemId) == 0x2e8);
    static_assert(offsetof(ItemActor, component) == 0x2f0 && offsetof(ItemActor, poses) == 0x300);
    static_assert(offsetof(ItemActor, touchDelay) == 0x308 && offsetof(ItemActor, madeInEvent) == 0x310);
    static_assert(sizeof(ItemActor) == 0x320);
}
