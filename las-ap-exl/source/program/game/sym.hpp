#pragma once
// The game's functions and globals the module calls or hooks, named as in scripts/re/las.sym and typed as the module
// calls them; offsets in game 1.0.0 (game/addr.hpp). Instruction addresses that a patch or an inline hook targets stay
// with that patch or hook.
#include "game/addr.hpp"
#include "game/stl.hpp"
#include "lib.hpp"

namespace Game {
    struct Framework;
    struct GameState;
    struct Node; struct InstancedModelComponent; struct Actor; struct ActorParams; struct LebActorRecord;
    struct Vec3;
    namespace Data { struct SaveHeader; }
    namespace Data::Inventory { struct InventoryItems; }
    namespace Actors { struct NpcComponent; }
    template<int N> struct StateMachine;
    namespace Ui { struct Pane; struct TextBox; struct TextHolder; struct LayoutWrapper; struct Animator;
                   struct Manager; struct Navi; struct NaviGuide; struct MenuPage; struct GmSystem;
                   struct MessageWindow; struct MessageText; }
    namespace Map { struct OverworldMap; struct DungeonMap; struct MapMarker; struct MapCursor; struct DungeonMapIcon;
                    struct MapActorDesc; struct UiDungeonAreaRow; struct DungeonItemsEntry; }
    namespace Audio { struct Sound; struct BgmInstance; struct BgmManager; }

    // ---- Framework ----
    // The framework (game/framework.hpp)
    inline constexpr Global<Framework*> Framework_ptr{0x1c97188};

    // ---- Actors and entities ----
    // (out actor, owner, params): the actor factory, as the player's hookshot calls it with the actor system as owner
    inline constexpr Func<void(SharedPtr*, SharedPtr*, ActorParams*)> Actor_Create{0x8f5ea0};
    // Member destructors of actor params: drsb, bfres, lebParams
    inline constexpr Func<void(void*)> ActorParams_Dtor0{0x1cf70};
    inline constexpr Func<void(void*)> ActorParams_Dtor1{0x1d190};
    inline constexpr Func<void(void*)> ActorParams_Dtor2{0x1d3b0};
    // Pointers to an identity quaternion and to a Vec4 of ones
    inline constexpr Global<void*> IdentityQuat_ptr{0x1c93de8};
    inline constexpr Global<void*> OneScale_ptr{0x1c93df0};
    // (actor, component, root): makes the component the actor's child
    inline constexpr Func<bool(Actor*, void*, Node*)> Actor_AttachComponent{0xfaa8a0};
    // (parent, name, length, immediate): removes a child by name
    inline constexpr Func<bool(void*, const char*, u64, bool)> Entity_RemoveChild{0xfb00c0};
    // (entity, child)
    inline constexpr Func<bool(void*, void*)> Entity_AttachChild{0xfa9e40};
    // (entity): its own destroy (vtable +0x58), not immediate
    inline constexpr Func<void(void*)> Entity_Destroy{0xfabf70};
    // The entity manager: its start task (manager, entity shared_ptr*)
    inline constexpr Global<void*> EntityManager_ptr{0x1cc6d98};
    inline constexpr Func<void(void*, SharedPtr*)> EntityManager_PostStart{0xfae2d0};
    // (out, object, control block): a shared_ptr from a weak one (an entity's self pointer at +0x8)
    inline constexpr Func<void(SharedPtr*, void*, void*)> SharedPtr_FromWeak{0x21470};
    // A component's name as its constructor takes it: {name, length, enabled, owner, owner's control block}
    struct ComponentName { const char* data; u64 size; u8 enabled; void* owner; void* ownerCtrl; };
    // (component, name*, model resource, cache key, shapes' mask, the model list at Framework +0x540)
    inline constexpr Func<void(InstancedModelComponent*, ComponentName*, void*, u32, u32, void*)>
        InstancedModel_Ctor{0x104d540};
    // The vtable of a control block of the component's own (FieldTip's)
    inline constexpr Global<const u8> InstancedModelCtrl_vtable{0x1b9cf50};

    // ---- NPCs ----
    // -> {shared_ptr Npc.gsheet, shared_ptr NpcPhysicsTraits.gsheet}
    inline constexpr Global<void**> NpcSheets_ptr{0x1cb8360};
    // "NpcGhost": the row key and the actor table's name
    inline constexpr Global<const char> str_NpcGhost{0x16ebdf4};
    // (sheet, key*, row size): the row, or null
    inline constexpr Func<void*(void*, StrView*, u64)> Datasheet_FindRow{0xfa6370};
    // (component, args, row key*, ?)
    inline constexpr Func<void*(void*, void*, StrView*, void*)> NpcComponent_Ctor{0x943e90};
    // (component, name*, mode, blend seconds, frame): virtual, overridden by NpcAnimation_PlayByName2
    inline constexpr Func<void(void*, StrView*, u8, float, float)> NpcAnimation_PlayByName{0x949260};
    inline constexpr Func<void(void*, StrView*, u8, float, float)> NpcAnimation_PlayByName2{0x9df590};
    // (out shared_ptr, NPC component, name*): the animation bound by name
    inline constexpr Func<void(SharedPtr*, void*, StrView*)> NpcAnimation_Get{0x9670d0};
    // (component, name*, the model component's shared_ptr*) -> the animation player; vtable +0x208
    inline constexpr Func<SharedPtrOut(Actors::NpcComponent*, StrView*, SharedPtr*)> NpcComponent_MakeAnimator{0x94b310};
    // (out shared_ptr, NPC component, name*, at once)
    inline constexpr Func<void(SharedPtr*, void*, StrView*, bool)> NpcComponent_PlayMaterialAnimation{0x9c1b30};
    // Link's kind of animation player: (out, entity, name*, model component shared_ptr*, int* layers)
    inline constexpr Func<void(SharedPtrOut*, void*, StrView*, SharedPtr*, int*)> LayeredAnimation_Make{0x67ba70};
    inline constexpr Global<const u8> LayeredAnimation_vtable{0x1c49ee8};
    // (player, layer, animation shared_ptr*, blend, start frame)
    inline constexpr Func<void(void*, int, SharedPtr*, float, float)> LayeredAnimation_SetSlot{0x1059dd0};
    // (layers, layer, bone name, bool): the layer masked to the bones from that one up
    inline constexpr Func<void(void*, int, const char*, bool)> AnimationLayers_SetBoneMask{0x1035390};
    // (layers, layer, on)
    inline constexpr Func<void(void*, int, bool)> AnimationLayers_SetEnabled{0x10355f0};
    inline constexpr Global<const char> str_spine_a{0x16b6ecb};

    // ---- Graphics ----
    // Starts the vsync watcher thread, whose event each frame's end waits for
    inline constexpr Func<void(u8*, u8*)> VsyncWatcher_Init{0x10410d0};
    // (command buffer wrapper, stage, slot, {GPU address, offset}*, size)
    inline constexpr Func<void(u8*, int, int, u64*, u64)> CmdBuf_BindUniformBuffer{0x113fd70};
    // (context, state): the context's cached raster-state setter
    inline constexpr Func<void(u8*, void*)> GfxContext_SetRasterState{0xff9430};
    // (context, DepthStencilState*): the context's cached depth-stencil setter (cache at +0x98)
    inline constexpr Func<void(u8*, void*)> GfxContext_SetDepthStencilState{0xff93f0};
    // (DepthStencilState*, device, DepthStencilStateInfo*)
    inline constexpr Func<void(void*, void*, const void*)> DepthStencilState_Initialize{0x1146600};
    // (DepthStencilStateInfo*): depth less, no test or write, stencil masks 0xff
    inline constexpr Func<void(void*)> DepthStencilStateInfo_SetDefault{0x113dfd0};
    // (group node, context): sets the context's pass id (+0x188) from each enabled child (+0x8, enabled +0x9) and
    // draws it (virtual +0x38); the draw tree's inner nodes
    inline constexpr Func<void(u8*, u8*)> DrawGroup_Draw{0xffc230};
    // (draw item, context): draws a model shape in the context's pass (ShapeDrawItem_vtable +0x10); a leaf pass node
    // binds its states and calls it for each of its sorted items
    inline constexpr Func<u64(u8*, u8*)> ShapeDrawItem_Execute{0x10399d0};
    // (wrapper, state)
    inline constexpr Func<void(void*, void*)> CmdBuf_BindRasterState{0x113ea70};
    inline constexpr Func<void(void*, u64, u64, u64, u64, u64, u64)> CmdBuf_DrawElementsBaseVertex{0x113e950};
    inline constexpr Func<void(void*, u64, u64, u64, u64, u64, u64)> CmdBuf_DrawElementsInstanced{0x113e9d0};
    // GOT slots of the game's NVN function pointers: each holds the address of a global with the pointer
    inline constexpr Global<void**> got_nvnMultisampleStateSetDefaults{0x1c951e8};
    inline constexpr Global<void**> got_nvnMultisampleStateSetAlphaToCoverageEnable{0x1c95200};
    inline constexpr Global<void**> got_nvnCommandBufferBindMultisampleState{0x1c947c0};
    // (node): marks the node and its children for a new world position and tells its listeners (the model); nothing
    // while it is marked (needsCoordUpdate), so setting that flag alone leaves the model where it was
    inline constexpr Func<void(Node*)> Node_MarkDirty{0xfb6aa0};
    // (node): the world coords from coordsNew and the parent's world coords, the parent first while it is marked;
    // lazy, when something reads a marked node
    inline constexpr Func<void(Node*)> Node_UpdateWorld{0xfb6310};

    // ---- Items ----
    // (out holder, owner entity, name*, root node, item id): loads the item's model and makes its component in a
    // task (ItemGet_ModelTask); the holder's +0x18 is then the component (a shared_ptr)
    inline constexpr Func<void(SharedPtr*, SharedPtr*, StrView*, SharedPtr*, u8)> ItemModel_Make{0x8d3670};
    // (task): the item-get's model; the task's +0x8 is the item-get's holder
    inline constexpr Func<void(u8*)> ItemGet_ModelTask{0x8d41c0};
    // (out, resource holder, model name)
    inline constexpr Func<void(void*, void*, const char*)> ModelResource_Get{0x1046190};
    // An item's pose under the item-get's item node: position, rotation (degrees about x, y, z) and scale
    struct ItemPose { float pos[3]; float rotate[3]; float scale[3]; };
    // (item id, side view): the pose the item-get holds the item up in, by item id (the heart piece's scale 1.7, the
    // ocarina's 1.17); ids without one (63-99, 101-107, 109, 118, 119, 127-255) get a default: scale 1, tipped 20
    // degrees
    inline constexpr Func<const ItemPose*(u8, bool)> ItemPose_ItemGet{0x8d2f50};
    // (out future, player, item id, keep carry, message entry*): the item-get, the body of the event actions
    // Link.GenericItemGetSequence(ByKey)
    inline constexpr Func<void(SharedPtr*, void*, int, bool, StrView*)> Player_ItemGetSequence{0xd795d0};
    // (player): Link's actor slots +0xb0 and +0xb8, which the event manager's start and end signals call
    // (re/EVENTS.md)
    inline constexpr Func<void(void*)> Player_OnEventStart{0xd32a60};
    inline constexpr Func<void(void*)> Player_OnEventEnd{0xd32d30};
    // (wait closure): the item-get's fanfare has stopped
    inline constexpr Func<bool(void*)> ItemGet_FanfareDone{0xd7af30};
    // (item id): the item's Items.gsheet row asks for an autosave
    inline constexpr Func<bool(u32)> ItemGet_RowAutoSave{0x8d5ad0};
    // The chamber stones owned: {bits, bytes, bit count}
    struct BitTable { const u8* bits; s32 bytes; s32 count; };
    inline constexpr Global<BitTable> PanelPieceBits{0x1cc1208};

    // ---- Events and resources ----
    // (args, name): an event action's int argument by name; the event action CompareInt (action)
    inline constexpr Func<int(void*, StrView*)> EventFlow_ParseInt{0x109f790};
    inline constexpr Func<int(long*)> FlowControl_CompareInt{0x804960};
    inline constexpr Global<void*> ResourceManager_ptr{0x1cc6db8};
    inline constexpr Global<void*> EventManager_ptr{0x1cb24d8};
    // (out handle, manager, path*)
    inline constexpr Func<void(SharedPtr*, void*, StrView*)> Resource_Load{0xfbbd80};
    // (resource)
    inline constexpr Func<void(void*)> Resource_Wait{0x3dc20};
    // (out flowchart, handle*)
    inline constexpr Func<void(SharedPtr*, SharedPtr*)> Resource_TakeFlowchart{0x3d9f0};
    // (out event, flowchart*, entry*)
    inline constexpr Func<void(SharedPtr*, SharedPtr*, StrView*)> Event_Make{0x80c290};
    // (event)
    inline constexpr Func<void(void*)> Event_Prepare{0x80db00};
    // (out, event manager, event*)
    inline constexpr Func<void(SharedPtr*, void*, SharedPtr*)> EventManager_Request{0x81f2e0};
    // (seconds, out future): for 0 seconds or less the game's ready future
    inline constexpr Func<void(float, SharedPtr*)> Delay_Make{0x7f3c90};
    // (out, holder): the scene, from *(Framework +0x4a8)
    inline constexpr Func<void(SharedPtr*, void*)> SceneHolder_Lock{0x11b70};
    // (save manager, kind, bool): false when a request waits already
    inline constexpr Func<bool(void*, u8, bool)> SaveManager_RequestAutoSave{0xe0b360};
    // The save header (game/data/inventory.hpp)
    inline constexpr Global<const Data::SaveHeader*> SaveHeader_ptr{0x1cc1360};

    // ---- Memory ----
    inline constexpr Func<void*(size_t)> operator_new{0x1431170};
    inline constexpr Func<void(void*)> operator_delete{0x1431110};
    // std::__shared_weak_count::__release_weak (control block)
    inline constexpr Func<void(void*)> shared_weak_count_release_weak{0x14311f0};

    // ---- UI ----
    // The UI manager (a static object, game/ui.hpp)
    inline constexpr Global<Ui::Manager> UiManager{0x1c971c0};
    // The graphics object: +8 points at the gfx device.
    inline constexpr Global<u8*> Graphics_ptr{0x1cc8b30};
    // (wrapper, {name, length}*, priority, archive set (0 Game.arc), bool): builds the layout through the UI manager.
    inline constexpr Func<void(Ui::LayoutWrapper*, StrView*, int, int, bool)> LayoutWrapper_Build{0xe59100};
    // (wrapper, page, the page's root pane): hangs the layout under that pane.
    inline constexpr Func<void(Ui::LayoutWrapper*, Ui::LayoutWrapper*, Ui::Pane*)> LayoutWrapper_AttachTo{0xe59d40};
    // A text box queued for its glyphs: {owner, shared count, text box}.
    struct GlyphRequest { void* owner; void* count; Ui::TextBox* textBox; };
    // (manager, request*, archive set)
    inline constexpr Func<bool(Ui::Manager*, GlyphRequest*, int)> UiManager_QueueGlyphs{0xeda8d0};
    // ({name, length}*, null, volume, pitch)
    inline constexpr Func<void(const StrView*, void*, float, float)> Sound_Play{0xa57e0};
    // (navi, page index): the pause menu header's button guide for that page
    inline constexpr Func<void(Ui::Navi*, u32)> Navi_SetGuide{0xeed6f0};
    // (guide, slot, kind)
    inline constexpr Func<void(Ui::NaviGuide*, int, u32)> Guide_Set{0xf571b0};
    // Navi, the pause menu header: its state machine's ChangeState (machine, state); the updates and enters of its
    // states (navi); its page titles (navi, index).
    inline constexpr Func<void(StateMachine<6>*, int)> Navi_ChangeState{0xee7dc0};
    inline constexpr Func<void(Ui::Navi*)> Navi_WaitKey{0xee8ca0};
    inline constexpr Func<void(Ui::Navi*)> Navi_CloseOldUiUpdate{0xeea4f0};
    inline constexpr Func<void(Ui::Navi*)> Navi_CloseEnter{0xeeac10};
    inline constexpr Func<void(Ui::Navi*, u32)> Navi_SetTitles{0xeedec0};
    // The pause menu's System page (Gm_System): setup and update (page)
    inline constexpr Func<void(Ui::GmSystem*)> GmSystem_Setup{0xf82f40};
    inline constexpr Func<void(Ui::GmSystem*)> GmSystem_Update{0xf83650};
    // Gm_System's destructor, which PdPage's calls too (page)
    inline constexpr Func<void(Ui::GmSystem*)> GmSystem_Dtor{0xec4330};
    // The message window's Reading state update (window): shows the page letter by letter
    inline constexpr Func<void(Ui::MessageWindow*)> MessageWindow_Reading{0xef1960};
    // (text, letters): shows that many letters at once
    inline constexpr Func<void(Ui::MessageText*, s32)> MessageText_ShowLetters{0xf53b60};
    // (text object, {"file:label", length}*): sets the text object's pane (+0x10) to that message
    inline constexpr Func<void(Ui::TextHolder*, const StrView*)> MessageText_SetLabel{0xf8c8b0};

    // (out, {"File:Label", length}*) of the message manager, and how a message is referred to: {file, control block,
    // index}, index -1 none
    struct MessageRef { void* msbt; void* ctrl; s32 index; };
    inline constexpr Global<u8*> MessageManager_ptr{0x1cb2db8};
    inline constexpr Func<void(MessageRef*, u8*, const StrView*)> MessageManager_Find{0x903250};
    // MSBT files: the label lookup (out ref, msbt, label, length) and the text getter (out, msbt, index), whose out is
    // {owner, control block, text, length in bytes without the terminator}.
    struct MsbtText { void* owner; void* ctrl; const char* text; s64 length; };
    inline constexpr Func<void(MessageRef*, void*, const char*, size_t)> Msbt_FindLabel{0x10aaf40};
    inline constexpr Func<void(MsbtText*, void*, s32)> Msbt_GetText{0x10aa5a0};
    // The autosave popup (Ld_ThrobberSave, "Now Saving"): Show (auto) None/Out -> InAuto (0.5 s) or In; its state
    // machine's ChangeState (machine, state).
    inline constexpr Func<void(bool)> ThrobberSave_Show{0xf895c0};
    inline constexpr Func<void(StateMachine<6>*, s32)> ThrobberSave_ChangeState{0xf85d20};
    // (animator): its animation's frame count
    inline constexpr Func<u16(Ui::Animator*)> Animator_FrameCount{0x114c840};
    // (child wrapper, parent wrapper, pane, sibling): hangs the child's layout under the pane, before the sibling
    inline constexpr Func<void(Ui::LayoutWrapper*, Ui::LayoutWrapper*, Ui::Pane*, Ui::Pane*)>
        LayoutWrapper_AttachBefore{0xe5a0a0};

    // ---- Music ----
    // The BGM manager (game/audio.hpp) and the BGM table: statics of the game, made on first use (guard bit 0 set
    // then)
    inline constexpr Global<Audio::BgmManager> BgmManager{0x1ca1d70};
    inline constexpr Global<volatile u8> BgmManager_guard{0x1cab240};
    inline constexpr Global<u8> BgmTable{0x1caba28};
    inline constexpr Global<volatile u8> BgmTable_guard{0x1cac378};
    // (volume, manager, name*, bool, bool, bool force): the new instance, or null
    inline constexpr Func<Audio::BgmInstance*(float, Audio::BgmManager*, StrView*, bool, bool, bool)>
        BgmManager_Play{0x9fd60};
    // (name*, fade frames)
    inline constexpr Func<void(StrView*, u32)> BgmManager_StopByName{0x9b7b0};
    // (fade frames)
    inline constexpr Func<void(u32)> BgmManager_StopAll{0x9b550};
    // Fades the kind 1 instances out over 90 frames
    inline constexpr Func<void()> BgmManager_FadeZone{0xa6510};
    // (frames): every kind 1 instance's volume to 0
    inline constexpr Func<void(u32)> BgmManager_MuteZone{0x9b420};
    // (fade others): plays the zone's track; the new instance, or null
    inline constexpr Func<Audio::BgmInstance*(bool)> PlayZoneBgm{0xa6790};
    // (table, name): the track's id, -1 for an unknown name
    inline constexpr Func<int(void*, const char*)> BgmTable_Find{0x10e23d0};
    // (sound, fade frames)
    inline constexpr Func<void(Audio::Sound*, u32)> Sound_Stop{0x10e97f0};
    // (sound, frames, volume)
    inline constexpr Func<void(Audio::Sound*, int, float)> Sound_SetVolume{0x10ea8c0};
    // (volume, instance, track, fade frames): one track of the instance's stream; nothing before the stream starts
    inline constexpr Func<void(float, Audio::BgmInstance*, u32, u32)> BgmInstance_SetTrackVolume{0x99e70};
    // libc++ std::function's swap (a, b)
    inline constexpr Func<void(Function*, Function*)> Function_Swap{0x999c0};
    // The Wind Fish's Egg's onStart for BGM_ENSEMBLE_INST (Orchestra_Play): a callable {vtable, instance, u64
    // instruments} in a Function's buffer that sets track i to volume 1 with instrument bit i, else 0
    inline constexpr Global<const u8> OrchestraTracks_vtable{0x1b592c8};
    // (item id): the item's Items.gsheet jingle
    inline constexpr Func<void(u32)> ItemGet_PlayFanfare{0x8d5700};
    // (intro): the Guardian Acorn's and Piece of Power's music
    inline constexpr Func<void(bool)> BuffMusic_Play{0xaf560};

    // ---- Maps ----
    // The map pages and their parts (game/map.hpp). The overworld map page's destructor (page)
    inline constexpr Func<void(Map::OverworldMap*)> OverworldMap_Dtor{0xe8e680};
    // The dungeon map page (UI 0xe): its setup and destructor (page); per room ShowCellFloor (page, cell, Area
    // shared_ptr*, UiDungeonArea row*) shows the room on the shown floor; KeyWait runs MoveCursor (page, move),
    // PinButton () -> A pressed, then UpdateHover (page), the pins' hover; MemoriesButton (page) -> X pressed.
    inline constexpr Func<void(Map::DungeonMap*)> DungeonMap_Setup{0xe748a0};
    inline constexpr Func<void(Map::DungeonMap*)> DungeonMap_Dtor{0xe73970};
    inline constexpr Func<void(Map::DungeonMap*, u8, SharedPtr*, const Map::UiDungeonAreaRow*)>
        DungeonMap_ShowCellFloor{0xe82250};
    inline constexpr Func<void(Map::DungeonMap*, bool)> DungeonMap_MoveCursor{0xe86750};
    inline constexpr Func<bool()> DungeonMap_PinButton{0xe86c10};
    inline constexpr Func<void(Map::DungeonMap*)> DungeonMap_UpdateHover{0xe87a20};
    inline constexpr Func<bool(Map::DungeonMap*)> DungeonMap_MemoriesButton{0xe87840};
    // (x, y, z, page, out vec2, Area shared_ptr*): an icon's position under N_MapPin_00
    inline constexpr Func<void(float, float, float, Map::DungeonMap*, float*, SharedPtr*)>
        DungeonMap_PositionIcon{0xe84b60};
    // (icon, actor): an icon's visibility and open state
    inline constexpr Func<void(Map::DungeonMapIcon*, const Map::MapActorDesc*)> DungeonMapIcon_Update{0xf38040};
    // Map markers (Gm_MapMarker): Make (out shared_ptr), SetupPart (marker), ChangeState (the marker's machine, state)
    inline constexpr Func<void(SharedPtr*)> MapMarker_Make{0xe9c870};
    inline constexpr Func<void(Map::MapMarker*)> MapMarker_SetupPart{0xf3ad80};
    inline constexpr Func<void(StateMachine<4>*, u8)> MapMarker_ChangeState{0xf3a8c0};
    // (cursor, message ref*): the map cursor's name
    inline constexpr Func<void(Map::MapCursor*, MessageRef*)> MapCursor_ShowName{0xea0f50};

    // ---- Flags ----
    // The event flags; EventFlags_Check (flags, name) and _Set (flags, name, value) take the address of this pointer
    inline constexpr Global<void*> EventFlags_ptr{0x1cc1120};
    inline constexpr Func<bool(void**, StrView*)> EventFlags_Check{0xdfa920};
    inline constexpr Func<void(void**, StrView*, bool)> EventFlags_Set{0xdfa7b0};
    // (item id): the event flag the item's Items.gsheet row names, and setting it
    inline constexpr Func<int(int)> Item_CheckGettingFlag{0x8d2940};
    inline constexpr Func<void(int)> Item_SetGettingFlag{0x8d2a70};
    // (LEB record, slot, on): sets the actor switch's flag
    inline constexpr Func<void(const LebActorRecord*, int, bool)> ActorSwitch_Set{0x231b0};

    // ---- Player ----
    // -> shared_ptr PlayerAnimation.gsheet
    inline constexpr Global<const u8*> PlayerAnimationSheet_ptr{0x1cbaf70};

    // ---- Inventory and levels ----
    // (item id, count, index): adds an item; per item id in scripts/re/ITEMS.md
    inline constexpr Func<void(int, int, int)> Inventory_AddItemID{0x8d08e0};
    // (level, locator, position): the level the game loads; the save takes them
    inline constexpr Func<void(StrView*, StrView*, Vec3*)> SetCurrentLevel{0xdfa450};
    // (the framework's game state, state, bool): 4 reloads from the save, 1 quit, 5 ending
    inline constexpr Func<void(GameState*, u32, bool)> SetGameState{0x87b780};
    // The inventory (game/data/inventory.hpp)
    inline constexpr Global<Data::Inventory::InventoryItems*> Inventory_ptr{0x1cc1368};
    // The current dungeon (8 the Chamber Dungeon, 0xff outside dungeons), which dungeon items count for
    inline constexpr Global<u8> LevelIndex{0x1cc13c8};
    // Per dungeon a pointer to its entry (game/map.hpp)
    inline constexpr Global<Map::DungeonItemsEntry**> DungeonItems_ptr{0x1cc11b0};
}
