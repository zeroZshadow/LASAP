#pragma once
// NPC actors, their NPC component and its facial control (scripts/re/ACTORS.md, NPCs).
#include "game/actor.hpp"
#include "game/animation.hpp"
#include "game/sheet.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    struct BodyCollision; struct WaterEvent;
    // Facial.gsheet FacialSettings: the open and close random ranges of a blink or a lip sync.
    struct FacialSettings {
        SheetString key;
        s32 openMin;               // 0x10
        s32 openRange;             // 0x14
        s32 closeMin;              // 0x18
        s32 closeRange;            // 0x1c
    };
    static_assert(offsetof(FacialSettings, openMin) == 0x10 && sizeof(FacialSettings) == 0x20);

    // An expression of a Facial.gsheet row: a frame and a length per material animation slot (eye, mouth, others).
    struct FacialExpression {
        SheetString label;
        s32 eyeFrame;              // 0x10
        s32 mouthFrame;            // 0x14
        SheetArray<s32> otherFrames;  // 0x18
        s32 eyeLength;             // 0x28
        s32 mouthLength;           // 0x2c
        SheetArray<s32> otherLengths;  // 0x30
        s32 eyeBlinkFrame;         // 0x40
        u8 pad_44[0x4];
        const FacialSettings* eyeBlinkSettings;  // 0x48: null for none
        s32 lipSyncCloseFrame;     // 0x50
        s32 lipSyncOpenFrame;      // 0x54
        const FacialSettings* lipSyncSettings;   // 0x58: null for none
    };
    static_assert(offsetof(FacialExpression, otherFrames) == 0x18 && offsetof(FacialExpression, eyeLength) == 0x28);
    static_assert(offsetof(FacialExpression, otherLengths) == 0x30);
    static_assert(offsetof(FacialExpression, eyeBlinkFrame) == 0x40);
    static_assert(offsetof(FacialExpression, eyeBlinkSettings) == 0x48);
    static_assert(offsetof(FacialExpression, lipSyncCloseFrame) == 0x50);
    static_assert(offsetof(FacialExpression, lipSyncSettings) == 0x58 && sizeof(FacialExpression) == 0x60);

    // A row of Facial.gsheet: the material animations of an NPC's face. Its material animation component has one
    // slot per other animation, one more for each of eye and mouth that is set.
    struct FacialRow {
        SheetString key;
        SheetString eye;           // 0x10
        SheetString mouth;         // 0x20
        SheetArray<SheetString> others;  // 0x30
        SheetArray<FacialExpression> expressions;  // 0x40

        u32 SlotCount() const { return others.count + (eye.length != 0) + (mouth.length != 0); }
    };
    static_assert(offsetof(FacialRow, eye) == 0x10 && offsetof(FacialRow, mouth) == 0x20);
    static_assert(offsetof(FacialRow, others) == 0x30 && sizeof(FacialRow) == 0x50);

    // An NPC's facial control (FacialControl_Ctor, vtable 0x1b5e008; 0x150 bytes), a component. FacialControl_Setup
    // builds the expression index, the frames and the blink and lip sync components; FacialControl_Update advances
    // the current expression's frames.
    struct FacialControl {
        Entity entity;
        u32 priority;              // 0x58: 100 for NPCs
        u8 pad_5c[0x4];
        Function onChangeA;        // 0x60: the constructor's
        Function onChangeB;        // 0x90: NpcComponent_MakeFacial's
        SharedPtr<> eyeBlink;      // 0xc0: EyeBlinkControl (0xc0 bytes), when an expression has blink settings
        SharedPtr<> lipSync;       // 0xd0: LipSync (0x100 bytes), when an expression has lip sync settings
        void* sheet;               // 0xe0: a shared_ptr keeping Facial.gsheet alive
        void* sheetCtrl;           // 0xe8
        const FacialRow* row;      // 0xf0
        UnorderedMap expressionsByLabel;  // 0xf8: the expressions' indices by label (node {next, hash, label, s32})
        s32 expression;            // 0x120: the current one (FacialControl_SetExpression)
        u8 pad_124[0x4];
        Vector<s32> frames;        // 0x128: one per material animation slot
        bool frozen;               // 0x140: Update does nothing while set
        u8 pad_141[0xf];
    };
    static_assert(offsetof(FacialControl, priority) == 0x58 && offsetof(FacialControl, onChangeA) == 0x60);
    static_assert(offsetof(FacialControl, onChangeB) == 0x90 && offsetof(FacialControl, eyeBlink) == 0xc0);
    static_assert(offsetof(FacialControl, lipSync) == 0xd0 && offsetof(FacialControl, sheet) == 0xe0);
    static_assert(offsetof(FacialControl, row) == 0xf0 && offsetof(FacialControl, expressionsByLabel) == 0xf8);
    static_assert(offsetof(FacialControl, expression) == 0x120 && offsetof(FacialControl, frames) == 0x128);
    static_assert(offsetof(FacialControl, frozen) == 0x140 && sizeof(FacialControl) == 0x150);

    // Npc.gsheet graphics.animSettings element: how an animation is played.
    struct NpcAnimSetting {
        SheetString name;
        s32 blendTime;             // 0x10: frames
        s32 lipsync;               // 0x14: LipSyncMode
        s32 cull;                  // 0x18: CullMode
        u8 pad_1c[0x4];
    };
    static_assert(offsetof(NpcAnimSetting, blendTime) == 0x10 && sizeof(NpcAnimSetting) == 0x20);

    // An animSettings entry and the shared_ptr keeping Npc.gsheet alive (NpcComponent_AnimSetting's result).
    struct AnimSettingRef {
        SharedPtr<> sheet;
        const NpcAnimSetting* setting;  // 0x10
    };
    static_assert(sizeof(AnimSettingRef) == 0x18);

    // An event-trigger area the NPC listens to (NpcComponent_ConnectTrigger: the row's eventTriggers of condition 10
    // to 13 name LEB links 0 to 3).
    struct NpcTriggerLink {
        u64 linkIndex;
        SharedPtr<> area;          // 0x8: AreaEventBox, AreaEventSphere, AreaEventBoxWithoutJump or AreaLevelOpen
        u32 handle;                // 0x18: on the area's signal; 0 for AreaLevelOpen (a function of its own)
        u8 pad_1c[0x4];
    };
    static_assert(offsetof(NpcTriggerLink, area) == 0x8 && sizeof(NpcTriggerLink) == 0x20);

    // The last ground-attribute raycast (GroundQueryCache_vtable 0x1b51a70; GroundQuery_Cached): its attribute is
    // reused while the position stays within an epsilon.
    struct alignas(16) GroundQueryCache {
        const uintptr_t* vtable;
        u8 pad_8[0x8];
        Vec4 position;             // 0x10
        u32 attribute;             // 0x20
        bool valid;                // 0x24
        u8 pad_25[0xb];
    };
    static_assert(offsetof(GroundQueryCache, position) == 0x10 && offsetof(GroundQueryCache, valid) == 0x24);
    static_assert(sizeof(GroundQueryCache) == 0x30);

    // An NPC's component (NpcComponent_Ctor, vtable 0x1bdc570; 0x2e0 bytes): its Npc.gsheet row, its owner's LEB
    // parameters, the components it builds once its resources are loaded (each made by a virtual of its own, named by
    // that virtual's offset where the class is not known), and its resources.
    struct NpcComponent {
        Entity entity;
        Actor* owner;              // 0x58: a shared_ptr; null unless the owner is an actor
        void* ownerCtrl;           // 0x60
        SharedPtr<> parentNode;    // 0x68: the actor's root
        SharedPtr<> npcSheet;      // 0x78: keeps Npc.gsheet alive
        const void* row;           // 0x88: the Npc.gsheet row (kNpcRowSize bytes); null destroys the component
        s32 layoutId;              // 0x90: the owner's int LEB parameter the row's actorArgs.layoutID names
        u8 pad_94[0x4];
        FixedString<0x20> identifier;  // 0x98: the string LEB parameter the row's actorArgs.identifier names
        u32 eventSubA;             // 0xc0: the event manager's signal handles
        u32 eventSubB;             // 0xc4
        SharedPtr<> task;          // 0xc8
        SharedPtr<> model;         // 0xd8: the skeletal model component (virtual +0x200)
        void* animator;            // 0xe8: a shared_ptr (virtual +0x208): a SkeletalAnimation, or a LayeredAnimation
        void* animatorCtrl;        // 0xf0
        SharedPtr<BodyCollision> collision;  // 0xf8: +0x210
        SharedPtr<> damageCollision; // 0x108: +0x218
        SharedPtr<> doAction;      // 0x118: +0x228
        SharedPtr<> eventChara;    // 0x128: +0x220
        SharedPtr<> ocarinaReaction; // 0x138: +0x230, a 0x1b0-byte Node; with the row's ocarina.enable
        SharedPtr<> lookAtTarget;  // 0x148: +0x238, a 0x150-byte Node; when the row's lookAtOffsetY is not 0
        SharedPtr<> tailorPlayer;  // 0x158: +0x240
        SharedPtr<WaterEvent> waterEvent;  // 0x168: +0x248, with the row's graphics.waterChannel.enable
        SharedPtr<> groundAttr;    // 0x178: +0x250
        SharedPtr<> tailorWaterSurface;  // 0x188: +0x258, a 0x190-byte Node; with waterChannel.enable
        FacialControl* facial;     // 0x198: a shared_ptr (+0x260)
        void* facialCtrl;          // 0x1a0
        SharedPtr<> aimControl;    // 0x1a8: +0x268, 0x130 bytes; when the row's graphics.interestIK is set
        MaterialAnimComponent* materialAnim;  // 0x1b8: a shared_ptr (+0x270)
        void* materialAnimCtrl;    // 0x1c0
        SharedPtr<> shellBeacon;   // 0x1c8: +0x278, ShellBeaconComponent; when the row's shellSensor has entries
        SharedPtr<> readySignal;   // 0x1d8: emitted once the components are built
        SharedPtr<> modelResource; // 0x1e8: the bfres; NpcAnimation_Get binds animations from it
        SharedPtr<> flowchart;     // 0x1f8
        SharedPtr<> tailorResource;  // 0x208: the drsb
        UnorderedMap animationCache;  // 0x218: NpcAnimation_Get's bound animations by name
        UnorderedMap animSettingCache;  // 0x240: SharedString name to AnimSettingRef (NpcComponent_AnimSetting)
        SharedPtr<> animSettingSheet;  // 0x268: keeps Npc.gsheet alive for animSetting
        const NpcAnimSetting* animSetting;  // 0x278: the playing animation's (NpcComponent_PlayAnimation)
        SharedPtr<> event_280;     // 0x280: an event of the flowchart
        Vector<NpcTriggerLink> triggerLinks;  // 0x290: the event-trigger areas it is connected to
        bool triggersPending;      // 0x2a8: built before Start; Start connects the triggers
        bool popRejected;          // 0x2a9: the pop conditions failed, so the ready signal came without a build
        u8 pad_2aa[0x2];
        u32 collisionMaterial;     // 0x2ac: the collision shape's material word (NpcComponent_CollisionMaterial)
        GroundQueryCache groundCache;  // 0x2b0: NpcComponent_GroundAttribute's
    };
    static_assert(offsetof(NpcComponent, owner) == 0x58 && offsetof(NpcComponent, row) == 0x88);
    static_assert(offsetof(NpcComponent, layoutId) == 0x90 && offsetof(NpcComponent, identifier) == 0x98);
    static_assert(offsetof(NpcComponent, eventSubA) == 0xc0 && offsetof(NpcComponent, model) == 0xd8);
    static_assert(offsetof(NpcComponent, animator) == 0xe8 && offsetof(NpcComponent, eventChara) == 0x128);
    static_assert(offsetof(NpcComponent, ocarinaReaction) == 0x138 && offsetof(NpcComponent, waterEvent) == 0x168);
    static_assert(offsetof(NpcComponent, tailorWaterSurface) == 0x188 && offsetof(NpcComponent, facial) == 0x198);
    static_assert(offsetof(NpcComponent, aimControl) == 0x1a8 && offsetof(NpcComponent, shellBeacon) == 0x1c8);
    static_assert(offsetof(NpcComponent, materialAnim) == 0x1b8 && offsetof(NpcComponent, readySignal) == 0x1d8);
    static_assert(offsetof(NpcComponent, modelResource) == 0x1e8 && offsetof(NpcComponent, animationCache) == 0x218);
    static_assert(offsetof(NpcComponent, animSettingCache) == 0x240 && offsetof(NpcComponent, animSetting) == 0x278);
    static_assert(offsetof(NpcComponent, triggerLinks) == 0x290 && offsetof(NpcComponent, triggersPending) == 0x2a8);
    static_assert(offsetof(NpcComponent, collisionMaterial) == 0x2ac && offsetof(NpcComponent, groundCache) == 0x2b0);
    static_assert(sizeof(NpcComponent) == 0x2e0);
    constexpr size_t kNpcRowSize = 0x220;

    // NpcGhost (NpcGhost_Ctor, vtable 0x1be37c0; 0x2d0 bytes), the actor the ghosts are. Its second base is the
    // companion interface Link takes along (NpcBowWow, NpcMarin, NpcGhost and NpcFlyingCucco share it: kind 1 to 4).
    struct NpcActor {
        Actor actor;
        const uintptr_t* companionVtable;  // 0x2a8: NpcGhost_vtable2 0x1be3948
        NpcComponent* npc;         // 0x2b0: a shared_ptr
        void* npcCtrl;             // 0x2b8
        bool following;            // 0x2c0: Link has this companion with him
        bool param0Max;            // 0x2c1: LEB parameter 0 is 0x7fffffff
        bool inEvent;              // 0x2c2: Update skips while set
        bool scenarioRequested;    // 0x2c3: the timer started a Lv5Ghost scenario event; the timer stops meanwhile
        float timer;               // 0x2c4: frames, then 40 again
        u8 pad_2c8[0x8];
    };
    static_assert(offsetof(NpcActor, companionVtable) == 0x2a8 && offsetof(NpcActor, npc) == 0x2b0);
    static_assert(offsetof(NpcActor, following) == 0x2c0 && offsetof(NpcActor, scenarioRequested) == 0x2c3);
    static_assert(offsetof(NpcActor, inEvent) == 0x2c2 && offsetof(NpcActor, timer) == 0x2c4);
    static_assert(sizeof(NpcActor) == 0x2d0);
}
