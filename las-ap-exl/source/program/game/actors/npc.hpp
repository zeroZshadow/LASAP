#pragma once
// NPC actors, their NPC component and its facial control (scripts/re/ACTORS.md, NPCs).
#include "game/actor.hpp"
#include "game/animation.hpp"
#include "game/sheet.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // A row of Facial.gsheet: the material animations of an NPC's face. Its material animation component has one
    // slot per other animation, one more for each of eye and mouth that is set.
    struct FacialRow {
        SheetString key;
        SheetString eye;           // 0x10
        SheetString mouth;         // 0x20
        SheetArray<SheetString> others;  // 0x30
        SheetArray<u8> expressions;  // 0x40: elements of a type not mapped

        u32 SlotCount() const { return others.count + (eye.length != 0) + (mouth.length != 0); }
    };
    static_assert(offsetof(FacialRow, eye) == 0x10 && offsetof(FacialRow, mouth) == 0x20);
    static_assert(offsetof(FacialRow, others) == 0x30 && sizeof(FacialRow) == 0x50);

    // An NPC's facial control (FacialControl_Ctor, vtable 0x1b5e008; 0x150 bytes), a component.
    struct FacialControl {
        Entity entity;
        u32 priority;              // 0x58: 100 for NPCs
        u8 pad_5c[0x4];
        Function onChangeA;        // 0x60: the constructor's
        Function onChangeB;        // 0x90: NpcComponent_MakeFacial's
        u8 pad_c0[0x20];
        void* sheet;               // 0xe0: a shared_ptr keeping Facial.gsheet alive
        void* sheetCtrl;           // 0xe8
        const FacialRow* row;      // 0xf0
        UnorderedMap map_f8;       // 0xf8
        u32 field_120;             // 0x120
        u8 pad_124[0x4];
        Vector<u8> vector_128;     // 0x128: 0x10-byte elements
        u8 field_140;              // 0x140
        u8 pad_141[0xf];
    };
    static_assert(offsetof(FacialControl, priority) == 0x58 && offsetof(FacialControl, onChangeA) == 0x60);
    static_assert(offsetof(FacialControl, onChangeB) == 0x90 && offsetof(FacialControl, sheet) == 0xe0);
    static_assert(offsetof(FacialControl, row) == 0xf0 && offsetof(FacialControl, map_f8) == 0xf8);
    static_assert(offsetof(FacialControl, vector_128) == 0x128 && sizeof(FacialControl) == 0x150);

    // An NPC's component (NpcComponent_Ctor, vtable 0x1bdc570; 0x2e0 bytes): its Npc.gsheet row, its owner's LEB
    // parameters, the components it builds once its resources are loaded (each made by a virtual of its own, named by
    // that virtual's offset where the class is not known), and its resources.
    struct NpcComponent {
        Entity entity;
        Actor* owner;              // 0x58: a shared_ptr; null unless the owner is an actor
        void* ownerCtrl;           // 0x60
        SharedPtr parentNode;      // 0x68: the actor's root
        SharedPtr npcSheet;        // 0x78: keeps Npc.gsheet alive
        const void* row;           // 0x88: the Npc.gsheet row (kNpcRowSize bytes); null destroys the component
        s32 variant;               // 0x90: the owner's int LEB parameter the row names
        u8 pad_94[0x4];
        char lebString[0x20];      // 0x98: the owner's string LEB parameter the row names
        u64 lebStringLength;       // 0xb8
        u32 eventSubA;             // 0xc0: the event manager's signal handles
        u32 eventSubB;             // 0xc4
        SharedPtr task;            // 0xc8
        SharedPtr model;           // 0xd8: the skeletal model component (virtual +0x200)
        void* animator;            // 0xe8: a shared_ptr (virtual +0x208): a SkeletalAnimation, or a LayeredAnimation
        void* animatorCtrl;        // 0xf0
        SharedPtr collision;       // 0xf8: +0x210
        SharedPtr damageCollision; // 0x108: +0x218
        SharedPtr doAction;        // 0x118: +0x228
        SharedPtr eventChara;      // 0x128: +0x220
        SharedPtr comp230;         // 0x138
        SharedPtr comp238;         // 0x148
        SharedPtr tailorPlayer;    // 0x158: +0x240
        SharedPtr comp248;         // 0x168
        SharedPtr groundAttr;      // 0x178: +0x250
        SharedPtr comp258;         // 0x188: a 0x190-byte Node
        FacialControl* facial;     // 0x198: a shared_ptr (+0x260)
        void* facialCtrl;          // 0x1a0
        SharedPtr comp268;         // 0x1a8: a 0x130-byte component
        MaterialAnimComponent* materialAnim;  // 0x1b8: a shared_ptr (+0x270)
        void* materialAnimCtrl;    // 0x1c0
        SharedPtr railTrailer;     // 0x1c8: +0x278
        SharedPtr readySignal;     // 0x1d8: emitted once the components are built
        SharedPtr modelResource;   // 0x1e8: the bfres; NpcAnimation_Get binds animations from it
        SharedPtr flowchart;       // 0x1f8
        SharedPtr tailorResource;  // 0x208: the drsb
        UnorderedMap animationCache;  // 0x218: NpcAnimation_Get's bound animations by name
        UnorderedMap map_240;      // 0x240
        SharedPtr flow_268;        // 0x268: virtual +0x2c0's
        void* field_278;           // 0x278
        SharedPtr event_280;       // 0x280: an event of the flowchart
        Vector<u8> vector_290;     // 0x290: 0x20-byte elements
        bool triggersHandled;      // 0x2a8
        bool flag_2a9;             // 0x2a9
        u8 pad_2aa[0x2];
        u32 field_2ac;             // 0x2ac
        u8 object_2b0[0x20];       // 0x2b0: a polymorphic object (vtable 0x1b51a70)
        u32 field_2d0;             // 0x2d0
        u8 field_2d4;              // 0x2d4
        u8 pad_2d5[0xb];
    };
    static_assert(offsetof(NpcComponent, owner) == 0x58 && offsetof(NpcComponent, row) == 0x88);
    static_assert(offsetof(NpcComponent, variant) == 0x90 && offsetof(NpcComponent, lebString) == 0x98);
    static_assert(offsetof(NpcComponent, eventSubA) == 0xc0 && offsetof(NpcComponent, model) == 0xd8);
    static_assert(offsetof(NpcComponent, animator) == 0xe8 && offsetof(NpcComponent, eventChara) == 0x128);
    static_assert(offsetof(NpcComponent, comp258) == 0x188 && offsetof(NpcComponent, facial) == 0x198);
    static_assert(offsetof(NpcComponent, materialAnim) == 0x1b8 && offsetof(NpcComponent, readySignal) == 0x1d8);
    static_assert(offsetof(NpcComponent, modelResource) == 0x1e8 && offsetof(NpcComponent, animationCache) == 0x218);
    static_assert(offsetof(NpcComponent, flow_268) == 0x268 && offsetof(NpcComponent, vector_290) == 0x290);
    static_assert(offsetof(NpcComponent, triggersHandled) == 0x2a8 && sizeof(NpcComponent) == 0x2e0);
    constexpr size_t kNpcRowSize = 0x220;

    // NpcGhost (NpcGhost_Ctor, vtable 0x1be37c0; 0x2d0 bytes), the actor the ghosts are.
    struct NpcActor {
        Actor actor;
        const uintptr_t* vtable2;  // 0x2a8: a second base (vtable 0x1be3948)
        NpcComponent* npc;         // 0x2b0: a shared_ptr
        void* npcCtrl;             // 0x2b8
        u8 field_2c0;              // 0x2c0
        bool param0Max;            // 0x2c1: LEB parameter 0 is 0x7fffffff
        bool inEvent;              // 0x2c2: Update skips while set
        bool timerOff;             // 0x2c3
        float timer;               // 0x2c4: frames, then 40 again
        u8 pad_2c8[0x8];
    };
    static_assert(offsetof(NpcActor, vtable2) == 0x2a8 && offsetof(NpcActor, npc) == 0x2b0);
    static_assert(offsetof(NpcActor, inEvent) == 0x2c2 && offsetof(NpcActor, timer) == 0x2c4);
    static_assert(sizeof(NpcActor) == 0x2d0);
}
