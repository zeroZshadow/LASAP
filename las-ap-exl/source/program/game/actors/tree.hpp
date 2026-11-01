#pragma once
// The trees (ObjTree and the other tree actors: ObjPalmTree, ObjTabahlTree, ObjSpeakTree, ObjDesertTree,
// ObjMysteriousTree, ObjCastleCrowlyTree, ObjTotakekeTree; ObjTree_vtable, ObjTree_Setup, 0x540 bytes). A tree drops
// an item when Link bumps it (ObjTree_DropItem, scripts/re/ITEMS.md, Seashell drops).
#include "game/actor.hpp"
#include "game/state_machine.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // Where a TreeBox's offset is on the box along an axis: Start is its -x, +y or +z face, End the opposite one
    enum class BoxAlign : u16 { Center = 0, Start = 1, End = 2 };

    // An override of one bit of a collision filter word
    enum class FilterBit : u8 { Keep = 0, Set = 1, Clear = 2 };

    // A collision filter word to make (Collision_SetFilter): a preset, then overrides of whether it collides with a
    // layer (the word's bit 5 + layer)
    struct CollisionFilterDesc {
        u16 preset;                // index of CollisionFilter_Presets, 0 to 18 (every tree box: 1, layer 1)
        FilterBit layer19;         // 0x2
        FilterBit layer26;         // 0x3
        FilterBit layer25;         // 0x4
        FilterBit layer23;         // 0x5
        FilterBit layer20;         // 0x6
        FilterBit layer21;         // 0x7
        FilterBit layer18;         // 0x8
        FilterBit layer6;          // 0x9
        FilterBit layer8;          // 0xa
        FilterBit layer1;          // 0xb
        FilterBit layer2;          // 0xc
        FilterBit layer16;         // 0xd
        u8 pad_e[0x2];
    };
    static_assert(offsetof(CollisionFilterDesc, layer16) == 0xd && sizeof(CollisionFilterDesc) == 0x10);

    // A box collision of the parameters, which Setup makes with half its size (every table: alignY End, the others
    // Center)
    struct TreeBox {
        Vec3 size;
        BoxAlign alignY;           // 0xc
        BoxAlign alignX;           // 0xe
        BoxAlign alignZ;           // 0x10
        u8 pad_12[0x2];
        Vec3 offset;               // 0x14
        CollisionFilterDesc filter;  // 0x20: the collision's, group 0
    };
    static_assert(offsetof(TreeBox, alignZ) == 0x10 && offsetof(TreeBox, offset) == 0x14);
    static_assert(sizeof(TreeBox) == 0x30);

    // Its parameters: a static table per actor id (ObjTree 0x1700c70; the others 0x14 into their table, whose first
    // word is Tree::tableWord)
    struct TreeParams {
        TreeBox collision;         // 0x0: "Collision", without apples
        TreeBox collision2;        // 0x30: "Collision2"
        float waitBlend;           // 0x60: the wait animation's blend time (ObjSpeakTree 8/60 s, else 0)
        float hitBlend;            // 0x64: the hit animation's (0 in every table)
        float launchSpeed;         // 0x68: its drop's
        float crownHeight;         // 0x6c: the crown above the tree's root
        u16 material;              // 0x70: Tree::collisionMaterial's bits 0-5 (9 in every table)
        u8 flag72;                 // 0x72: bit 0 sets collisionMaterial's bits 14-17 to 1 (1 in every table; never read)
        u8 flag73;                 // 0x73: bit 0 clears collisionMaterial's bit 19 (0 in every table): pushable
    };
    static_assert(offsetof(TreeParams, collision2) == 0x30 && offsetof(TreeParams, waitBlend) == 0x60);
    static_assert(offsetof(TreeParams, launchSpeed) == 0x68 && offsetof(TreeParams, material) == 0x70);
    static_assert(offsetof(TreeParams, crownHeight) == 0x6c && offsetof(TreeParams, flag73) == 0x73);

    struct Tree {
        Actor actor;
        u8 pad_2a8[0x8];
        StateMachine<3> machine;   // 0x2b0: Wait, Hit, Talk; Link's dash crash into it turns Wait to Hit, which drops
        SharedPtr<> model;         // 0x3b0: SkeletalModelCom
        SharedPtr<> animation;     // 0x3c0: SkeletalAnimationCom
        SharedPtr<> collision;     // 0x3d0: the params' box, without apples
        SharedPtr<> collision2;    // 0x3e0: the params' second box
        SharedPtr<> tailorPlayer;  // 0x3f0: ActorTailorPlayer
        SharedPtr<> damageCollision;  // 0x400: DamageColComp at Root
        SharedPtr<> doAction;      // 0x410: DoAction
        SharedPtr<> eventChara;    // 0x420: EventChara
        SharedPtr<> meshCollision; // 0x430: from Tree.cidb, with apples
        SharedPtr<> shellBeacon;   // 0x440: ShellBeacon, while its drop is a seashell not owned; disabled once dropped
        SharedPtr<> waitAnim;      // 0x450: the animation set's "wait"
        SharedPtr<> hitAnim;       // 0x460: "hit"
        u32 playerSlot;            // 0x470: its slot on Link's kDashCrash signal (ObjTree_OnPlayerSignal)
        u8 pad_474[0x4];
        const TreeParams* params;  // 0x478
        u32 tableWord;             // 0x480: the parameter table's first word (0 in every table)
        u8 pad_484[0x4];
        SharedPtr<> eventRequest;  // 0x488: EventManager_Request of its talk event
        SharedPtr<> eventSource;   // 0x498: ObjSpeakTree: event/SpeakTree.bfevfl's flowchart resource (0xcb32b0)
        const void* eventFlowchart;  // 0x4a8: its ResFlowchart; the talk event (0x80d580) needs it non-null
        SharedPtr<> event;         // 0x4b0: the talk event: its entry LEB parameter 2 (+0xcc, a string), else "talk"
        u8 apples;                 // 0x4c0: ObjTree's LEB parameter 1 (+0xc4, an int) when it is 1 or 2, else 0: an
                                   // apple tree (models Applehigh, AppleLow)
        u8 pad_4c1[0x7];
        SharedPtr<> appleLocators[3];  // 0x4c8: on the model's j_root (ObjTree_MakeApples, vtable +0x28)
        SharedPtr<> appleItems[3];  // 0x4f8: the apple items MakeApples drops at the locators
        bool applesDropped;        // 0x528: ObjTree_DropApples ran; no second time
        u8 pad_529[0x7];
        bool dropped;              // 0x530: ObjTree_DropItem dropped the item; no second drop
        u8 pad_531[0x3];
        u32 collisionMaterial;     // 0x534: the boxes' material word (NpcComponent::collisionMaterial's format)
        u8 pad_538[0x8];
    };
    static_assert(offsetof(Tree, machine) == 0x2b0 && offsetof(Tree, model) == 0x3b0);
    static_assert(offsetof(Tree, shellBeacon) == 0x440 && offsetof(Tree, playerSlot) == 0x470);
    static_assert(offsetof(Tree, params) == 0x478 && offsetof(Tree, eventRequest) == 0x488);
    static_assert(offsetof(Tree, event) == 0x4b0 && offsetof(Tree, apples) == 0x4c0);
    static_assert(offsetof(Tree, appleLocators) == 0x4c8 && offsetof(Tree, appleItems) == 0x4f8);
    static_assert(offsetof(Tree, eventFlowchart) == 0x4a8 && offsetof(Tree, applesDropped) == 0x528);
    static_assert(offsetof(Tree, dropped) == 0x530 && offsetof(Tree, collisionMaterial) == 0x534);
    static_assert(sizeof(Tree) == 0x540);
}
