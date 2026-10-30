# Framework, camera and audio (game 1.0.0)

The framework the game runs in (its lifecycle, the game state and its scenes, the scheduler and the frame's phases),
the camera view and the audio manager, from static analysis of the main module. Offsets are module offsets (what
`InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000).
Object offsets are bytes from the object's start. Field names are those of `game/framework.hpp`, `game/audio.hpp` and
`game/stl.hpp`, which list every field; this file gives the evidence and what their comments leave out. "Guess" marks
what the code suggests but was not followed to the end. The saved data (the save image, its header and the current
data) is in ITEMS.md, Saved data; what the actor phases run is in ACTORS.md, Entity manager and the frame.

## Framework

`Framework_ptr` (`0x1c97188`) is `{Framework* framework, void (*deleter)(Framework*)}`: a unique pointer with a
function deleter, which an exit destructor (`0x5320`) calls. The getter is inlined everywhere: without a framework it
allocates 0x6c0 bytes (`operator_new(0x6c0)` at every such site, `0x1bca0`, `0x846cd0`, `0x855d1c`), runs
`Framework_Ctor` (`0x7370`) and sets the deleter `Framework_Delete` (`0x1bcf0`: `Framework_Dtor` `0x9010`, then free).
The framework has no vtable.

**Lifecycle.** The game's entry loop (`0x1bc34`) runs `Framework_Init` (`0x9410`), `Framework_MainLoop` (`0x1b920`),
then `Framework_Shutdown` (`0x1ab30`), and starts at the init again while `restart` is set (its writer is not found).
`Framework_Init`, in order: the present setup; `systemEvents` (`0xa980`, `0xfa1e60`); `controller` (`0xa9f0`);
`Framework_InitGraphics` (`0xaa40`); `0xb6a0` (`physicsHeap`, `physics`); `Framework_InitResourceListener` (`0xb990`);
`ActorSystem_Make` (`0xba30`, `actorSystem`); `0xbde0`; `Audio_Init` (`0xbf00`); `PlayReports_Init` (`0xdf6870`,
`playReports`); the frame step's task in `frameQueue` (`0xbf90`: lambda vtable `0x1b502d0` → `Framework_FrameStep`
`0x17e30`); the 14 update and 5 render phases (below); `gameState` (`GameStateFactory_Create` `0xc450`).
`Framework_InitStorage` (`0xa4a0`) fills `romMount` (`"rom"`), `regionResources` (`region_common`, `common`,
`regionJP`, `JPja`), `heapMemory` (a block of (total − 0x6da00000) & ~0xfff bytes, freed through `heapMemoryFree`,
`0x5390` by default) and the main heap (0x53600000 bytes in it).

**Main loop.** Each pass runs `frameQueue` (`PhaseQueue_Run` `0x11cc0`) and counts `frameCount` unless `paused` or
`roomPaused` is set; the loop ends when the game state's task has `finished` set (`GameState_Finish`, below). A set
bit 0 in `systemEvents`' first byte makes it call `SetGameState(gameState, 0, 0)` (id 0: the game state finishes).

| Field | |
|---|---|
| `mainHeapVtable`, `mainHeap` | the main heap: an allocator interface (vtable `0x1c308c8`) and its implementation, owned (destructor `0x7af0`) |
| `nextFramePhaseId`, `nextUpdatePhaseId`, `nextRenderPhaseId` | the next phase id of each queue: 1, 14 and 5 at construction, the ids after the phases `Framework_Init` adds; an update phase added later takes `nextUpdatePhaseId` and increments it (`0xbdea0`) |
| `scheduler` | the task scheduler (Scheduler and phases) |
| `actorSystem` | the entity container the factories' spawns from code hang in (ACTORS.md, Creation): 0x148 bytes, no vtable: `+0x0` weak self, `+0x10`/`+0x38`/`+0x60` child maps, `+0x88` a `std::function` listener, `+0xc0` and `+0x118` mutexes, `+0x140` active (1). The shutdown empties it (`Entity_RemoveChildrenIf`) |
| `gameState` | the game state machine (Game state, below), the scheduler's root task |
| `player` | Link (PLAYER.md); about 500 places load the pair `+0x4b8`/`+0x4c0` |
| `resourceListener` | an interface (vtable `0x1c31138`) registered with the resource manager |
| `renderCallbackId` | the graphics callback the graphics init registers (graphics `+0x83c0` counts them); `Framework_TeardownGraphics` (`0x1b1d0`) unregisters it (`Graphics_UnregisterCallback` `0x10211f0`) |
| `presentCount`, `lastPresentVsync`, `skipRenderFrames`.. `presented` | `Phase_Present` (`0xc910`, render phase 4) counts presents, records the vsync counter (graphics `+0x81ac`) + 1 and sets `skipRenderFrames` from the vsyncs missed, clamped to `minSkip`..`maxSkip` (0 in levels: `0x854624`, `0x854c14`; 1 at construction); `Phase_EndOfUpdate` (`0x11960`) counts it down, flushing instead of rendering, and clears `presented` |
| `presentInitDone` | the one-time present setup (the camera's `active` cleared, the skip count to `minSkip`), also inlined at `0x8545e4`, `0x8379a4`, `0x8646ec` |
| `zoneFlagB5` | copied from the current zone's `+0xb5` (`0x7fe4e4`, `0x865fdc`; likely) |
| `shadowMinOffset`, `shadowMaxOffset` | (FLT_MIN × 3, 0) and (FLT_MAX × 3, 0) at construction, then the constants at `0x16af170` and `0x16aeec0` from the graphics init; render phase 0 adds them to the followed point for the camera's `shadowMin` and `shadowMax` (`0x10994`, `0x10998`) |
| `paused` | 1 at construction; the pause menu or a level load (ACTORS.md, Entity manager and the frame) |
| `roomPaused` | set by `Scene_UpdateZone` (`0x8364a4`, `0x8364f4`) |
| `controller` | make_shared of 0x570 bytes (constructor `0x87f850`; UI.md, Input) |
| `gfxMemory` | 0x2ed80000 bytes from the heap `0x1cab190`, for graphics |
| `renderer` | 0x58 bytes (vtable `0x1c466c0`, made by `0x18220`): `+0x20` a vector of views (`Renderer_AddView` `0x100f7a0`, virtual `+0x48`), `+0x40` the environment (`Renderer_SetEnv` `0x100fe60`); `Renderer_FindView` (`0x100fcd0`, `+0x60`, `(out weak, renderer, id)`). The skeletal model components' model list (MODELS.md) and the list `InstancedModel_Ctor` takes |
| `drawRoot` | the camera's draw node tree (its `+0x2d8`) |
| `camera` | the renderer's view with id 1 (Camera, below) |
| `view2` | the view with id 2: 0x5e0 bytes (vtable `0x1b50c68`, constructor `0x18d50`), orthographic (`ownMatrices` 1, an identity view) |
| `view3` | another view: render phase 0 (`0x109bc`, `0x11220`) gives it the camera's shadow bounds while its `active` is set; no writer found (likely) |
| `renderEnv` | 0x290 bytes (constructor `0xff94d0`; its init `0xff9880` loads `shader/ResetData_cube.bntx`); the renderer's `+0x40` |
| `physicsHeap`, `physics` | made by `0xb6a0`: 0x2000000 bytes handed to the allocator `0x1cd3668` (`0x1431470`); an object from `0x10c5730`, configured twice by `0x10c6790` (scenes 0 and 1, with the collision filter callbacks `0x829920` and `0x8299d0`, which compare group and mask words). PhysX strings (`0x16b73ca`..) suggest PhysX (likely) |
| `playReports` | 15 PlayReport objects (0x58 bytes, constructors `0xdf6d40 + i × 0x70`): 0 clear, 1 dead, 2 get_item, 3 play_minigame, 4 warp, 5 fishing_result, 6 mappin, 7 panel_dungeon, 8 panel_plus, 9 panel_theme, 10 amiibo_touched_id, 11 amiibo_get_panel, 12 amiibo_touched_count, 13 amiibo_write_dungeon, 14 amiibo_read_dungeon |
| `systemEvents` | 0x118 bytes (constructor `0xfa01a0`; init `0xfa1e60` sets four nn::oe notification modes); its `+0x18` takes a callback (`0x18af0`) that resizes the graphics on an operation mode change |
| `present` | `Present` (below) |

**Present** (`Framework::present`, 0x78 bytes; init `0x107cb90 (present, list, list, WeakPtr renderer)`, destructor
`0x107cb20`): `active` (1), `maxInterval` (the maximum present interval: 0xff at construction, 1 in levels, clamped
with graphics `+0x812c`), the display sizes `handheldSizes` and `dockedSizes` with `handheldIndex` (the first entry ≥
graphics `+0x6e48`) and `dockedIndex` (the first entry ≥ graphics `+0x6e4c`, used when graphics `+0x56a8` is 1; both
likely) and `renderer`. `vector_58` is freed by the destructor.

**Game state** (`GameState`, `Framework::gameState`; `GameStateFactory_Create` `0xc450`, `GameState_vtable`
`0x1bc4300`, 0xe0 bytes): the root task (Tasks, below; task name "Root", `parent` null). `GameState_Start` (`0x8672a0`,
task virtual `+0x48`) runs the present setup and `GlobalSave_Reset`, makes `fadePresets` (`0x87a9c0`), requests UI
0x22 (`Fade`), runs the startup chain (`globalFlags` from `0x86cb80` at `0x86c7a0`, `UiDatasheets_Load` `0x867f34`
reading it, the UI archives `UiManager_LoadGameArchives` `0xed5450`), sets `startupDone` once the UI datasheets are
loaded (`0x868528`) and starts the title. Each `GameState_Start*` (`GameState_StartTitle` `0x869820`,
`GameState_StartFileMenus` `0x86a200`, `GameState_StartLevel` `0x86a6d0`, `GameState_StartEnding` `0x86aba0`) sets
`sceneId` (`kSceneTitle` 1, `kSceneFileMenus` 2, `kSceneLevel` 4, `kSceneEnding` 5), makes the scene into `scene` and
connects a lambda (`0x1bc43f8`..) to the scene's `onFinished` that runs `GameState_NextScene` (`0x86a120`).

- `SetGameState` (`0x87b780 (state, u32 id, bool)`) stores `nextSceneId` and finishes the current scene by `sceneId`;
  ids 3 and 4 go through the level scene's `roomManager` and write its `jumpFlag` with the bool.
- `GameState_NextScene` resets the resource manager first (`0xfc6380`) when `resetResources` is set and clears it,
  then dispatches on `nextSceneId` (jump table `0x16b2d14`): 1 the title, 2 the file menus, 3 and 4 a level, 5 the
  ending, else `GameState_Finish` (`0x8674d0`, virtual `+0x50`), which resets the systems, drops `fadePresets`,
  `globalFlags` and `vector_c0` and finishes the task, which ends the main loop.
- `GameState_Update` (`0x8670f0`, virtual `+0x20`) drops the `vector_c0` entries whose first object's u32 `+0x78` is
  0.
- `titleSeen` is set when a title scene finishes (`0x87e318`); each title scene copies it into its `+0x84` and then
  starts on the title screen (Title UI state 5; `0x87e374`, `0x87de24`, `0x87df14`; likely).

**Resource groups.** A resource group (a make_shared of 0x68 bytes, control vtable `0x1b64040`, the object at `+0x18`:
vtable `0x1b640a0`, a second vtable `0x1b640f8` at `+0x48`) is a set of files loaded asynchronously: `+0x8` an
`unordered_map` by path (maximum load factor 1.0), `+0x30` the load (a shared pointer). `Datasheet_Load` (`0x83a1e0
(out, group, StringView* path)`) finds the path in the group's map (`0x153a50`) and returns the entry's `+0x30`. The
makers name their first file: `0x87a9c0` FadePreset.gsheet (`GameState::fadePresets`), `0x86cb80` GlobalFlags.gsheet
(`GameState::globalFlags`), `0x83e0a0` the level's sheets (`LevelScene::sheets`), `0xed5770` `ui/Game.arc` (UI.md, UI
manager).

**Scenes.** The game state's current scene (`GameState::scene`) is a task of one class per kind of screen, told apart
by its vtable (`SceneOf`, `Scene` in `game/framework.hpp`; `DBG uis` prints it). The makers take the game state as
`parent` (`WeakPtr_LockOrThrow` `0x869cf0`, from its `weakThis`):

| Class | Vtable | Task name | Size | Maker | Scene |
|---|---|---|---|---|---|
| TitleScene | `0x1bc49c0` | Title | 0x88 | `TitleScene_Make` `0x869e50` | the intro and the title screen |
| FileMenusScene | `0x1bc2740` | FileSelector | 0x88 | `FileMenusScene_Make` `0x86b970` | the file menus (file select, name entry) |
| LevelScene | `0x1bc2f90` | Main | 0x820 | `LevelScene_Make` `0x86b4e0` | a level, from the start of its load (the type `SceneHolder_Lock` takes) |
| EndingScene | `0x1bc2550` | Ending | 0x88 | `EndingScene_Make` `0x86b030` | the ending |

The small scenes' fields follow the task base: TitleScene `+0x81` the step (`TitleScene_Update` `0x87bde0`, 0 to 8),
`+0x82` set while the save read `0xe072b0(0)` runs (`0x87e570`; guess), `+0x83` set once the timer
`*(0x1cb26f0)+0xbc0` passes a threshold (`0x87ce04`; guess), `+0x84` the game state's `titleSeen`, which makes the
Title UI go to state 5; FileMenusScene `+0x81` the step; EndingScene `+0x81` the step (0 to 7), `+0x82` and `+0x83`.

The intro and the title share their scene: the intro plays while the title screen's UI (UI 0x1d, `Title`) is not
there yet or its machine waits in Deactive (UI.md, State machines). The module counts a save as loaded outside the
title and file-menu scenes (`NoSaveLoaded`), and names the level scene `loading` until Link exists. Measured from a
fresh boot to a level (2026-10-23, `screens`).

**Level scene** (`LevelScene`; `LevelScene_Ctor` `0x835fa0 (scene, SharedPtr* parent)`, destructor `0x844ea0`):

- `saveManager`: constructor `0xe081a0`, destructor `0x835d80`. `object_510`: constructor `0x7d69c0`, destructor
  `0x7d71a0`.
- `sheets`: the level's datasheets (`0x83e0a0` from `0x8389e0`; the loads `0x838b50`..`0x838e50`): PlayerAnimation,
  Facial, InterestIK, Npc, NpcPhysicsTraits, Reaction, CranePrize*, Light, FishingFish, FishingSpot and
  AutoSaveLocation (`.gsheet`) and `kodama/environment.kar`.
- `buffKind` (ITEMS.md) and `buffTime`, counted down by `0x837218` (likely).
- Four signals (`Signal`, ACTORS.md, Nodes): `zoneChanged` (constructor `0x8326e0`, emitted by `Scene_UpdateZone`),
  `roomChanged` (`0x833530`), `signal_750` (`0x834150`) and `signal_7a8` (`0x835000`). Subscribers reach a signal
  through its `owner` (`+0x10`), so the scene's `+0x708`, `+0x760` and `+0x7b8` other docs name are those of
  `roomChanged` (the actors), `signal_750` (`DungeonMap_SubscribeZone` outside dungeons, the BGM's `zoneSub`) and
  `signal_7a8` (`Actor_SubscribeZones`).
- `zone` and `room`: -1 at construction. `field_808` and `field_80c` (-1) take the current room (`0x836c38`), are
  read as a zone index into the room manager's `+0x208` `+0x210` and go back to -1 (`0x83661c`); likely a pending
  room pair. `field_810` (0xff) is compared in the update (`0x836724`, `0x836c28`).
- `field_674` and `field_694` are cleared by the scene's Finish (`0x838188`).

## Scheduler and phases

**Tasks.** The game state and the scenes derive from the task base (`Task_Ctor` `0x82ae90 (task, VTT, SharedPtr*
parent)`; the abstract `Task_vtable` `0x1bc48a0`, its name slot pure). The base is a primary virtual base (its offset
0 at vtable − 0x30, so the vptr is shared); its data ends at `+0x81`, and the derived classes start their fields in
its tail padding (`+0x81` or `+0x84`). The base's fields, as `GameState` names them:

- `weakThis` (`+0x8`): shared-from-this, filled by the make-shared helper `0xc6a0`.
- `parent` (`+0x18`): the task that made it; null for the game state (`Framework_Init` passes `{0, 0}`).
- `onFinished` (`+0x28`): a one-shot `Signal` (`nextHandle` 1, `oneShot` 1 at construction) that Finish emits; the
  tasks' signal vtable is `0x1b596b0`, derived from `Signal_vtable` (`0x1b59608`).
- `finished` (`+0x80`): set by Finish; the scheduler drops a finished task, and the main loop ends with the game
  state's.

| Task virtual | |
|---|---|
| `+0x0` / `+0x8` | destructor / deleting destructor |
| `+0x10` | `Task_OnFinishedIface` (`0x82b120`): `this + 0x30`, `onFinished`'s connector; callers read its `owner` and call the signal's `+0x20` |
| `+0x18` | `Task_IsFinished` (`0x82b130`): `finished` |
| `+0x20` | update, once per scheduler pass |
| `+0x28` / `+0x30` | `IsType(type id*)` / its type id |
| `+0x38` | `Task_Order` (`0x82ba00`): 0, `Scheduler_Add`'s order |
| `+0x40` | its name (a StrView out): "Root", "Title", "FileSelector", "Main", "Ending" |
| `+0x48` / `+0x50` | Start (the maker calls it before `Scheduler_Add`) / Finish |

A level runs a task of its own, "Play" (`PlayTask_Ctor` `0x852550`, vtable `0x1bc3850`, 0x410 bytes, made by
`0x83bf0c`). The makers (`GameStateFactory_Create` and the scene makers) run `operator_new`, the constructor, `0xc6a0`
(the shared pointer and `weakThis`), Start, then `Scheduler_Add(scheduler, &sp, Order(), &tag)` with a tag
`{SharedPtr, StrView}` (`"GameStateFactory::Create()"` for the game state), and increment `*TaskCount_ptr`
(`0x1cc6da8`, a u32 made on first use).

**Signals without an argument** (`Signal_vtable` `0x1b59608`; ACTORS.md, Nodes, has the layout): `+0x10` `Signal_Clear`
(`0xb0ca0`: closes the signal, `nextHandle` 0, and drops the slots), `+0x18` `Signal_IsOpen` (`0xb0cf0`), `+0x20`
`Signal_Connect` (`0xb0d10`, the tasks' `0xb1730`: a `std::function`, returns a u32 handle, 0 when closed), `+0x28`
`Signal_Disconnect` (`0xb0db0`), `+0x30` `Signal_Emit` (`0xb0f30`: calls every slot; a one-shot signal then closes and
drops them). The connector at `+0x8` has the vtable `0x1b59698`, whose slot 0 (`0xb0bd0`) forwards to the signal's
`+0x18`. Every operation holds `mutex` (lock `0x1431600`, unlock `0x1431610`).

**Scheduler** (`Scheduler`, `Framework::scheduler`; `Scheduler_Ctor` `0xfced30`, destructor `0xfcfeb0`; 0x150
bytes): `Task_Post` (`0x2d7430`) and `Delay_Make` (`0x7f3c90`) add tasks through `Scheduler_Add` (`0xfcff00
(scheduler, task sp*, u32 order, tag*)`), which inserts into `byOrder` and `tasks` and emits `taskAdded` with the
tag.

- `byOrder`: a multimap by s32 order, ascending (node 0x58 bytes: the order `+0x20`, the tag `+0x28`, `{SharedPtr,
  StrView name}`, the task's shared pointer `+0x48`).
- `pending`: the adds and removes made while `updateDepth` is above 0 (`0xfd07f0`, `Scheduler_Remove` `0xfd1690`), a
  deque of 0x40-byte entries, 0x40 a block (`+0x0` u32 op, 0 add, else remove; `+0x8` the tag; `+0x28` the task;
  `+0x38` s32 order), applied at the end of the outermost update with `flushing` set (a scope guard `0xfd2f00` clears
  it).
- `tasks`: an `unordered_set` of task pointers (`+0x48` buckets, `+0x50` bucket count, `+0x58` first node, `+0x60`
  size, `+0x68` f32 maximum load factor 1.0).
- `taskAdded` and `taskRemoved` (constructor `0xfcee00`: `nextHandle` 1, not one-shot): `taskRemoved` is emitted by
  `Scheduler_Remove`, `Scheduler_Clear` (`0xfd1cf0`) and the update when a task finishes.

`Scheduler_Update` (`0xfd1fb0`) is update phase 1's lambda (`0x174d0`): it raises `updateDepth` and goes through
`byOrder`, removing a finished task, else running its virtual `+0x20` and removing it if it is now finished.
`Framework_Shutdown` runs it once more, then `Scheduler_Clear`.

**ThreadPool** (`game/stl.hpp`; `ThreadPool_Ctor` `0x8700 (pool, u64 threadCount, std::function* threadFactory)`,
`ThreadPool_Dtor` `0x7b80`, `ThreadPool_Worker` `0x8bd0`; 0x78 bytes): `threads` (0x18 bytes each, element vtable
`0x1b50058`); `jobs`, a deque of `Function`s (0x30 bytes, 0x55 a block) whose front each thread pops under `mutex`,
waiting on `wake` (likely); a thread returns once `stop` is set and the deque is empty. The thread factories are plain
functions: the framework's `0x76c0`, `0x7840` and `0x7860` (each `0x81b0(out, 0x1001, 0x10000, index)`), the UI
manager's `0xecd020`.

**PhaseQueue** (`PhaseQueue_Ctor` `0x76e0 (queue, std::function* threadFactory)`, destructor `0x7ab0`, `PhaseQueue_Run`
`0x11cc0`; 0xf8 bytes): `byPriority` is a `std::map` by s32 priority to a group of phases (node `+0x28` the group,
`+0x38` its first phase, chained through `+0x0`; a phase's `Function` at `+0x20`, its pending result at `+0x50`;
likely), `phases` an `unordered_map`. `pending` holds the changes made while it runs: a deque of 0x50-byte entries,
0x33 a block (`+0x0` u32 op: 0 add, 1 remove, 2 clear, 3 set priority; `+0x4` u32 id; `+0x10` the `Function`; `+0x40`
s32 priority), applied after Run by `PhaseQueue_ApplyPending` (`0x13050`). `worker` is a one-thread `ThreadPool`: Run
hands a priority group to it (`0x11f70`) when one of its phases returned work, and waits (likely). The entity
manager's queues have the same shape (ACTORS.md).

**Phases.** `Framework_AddUpdatePhase` (`0xc0e0 (framework, id, priority, std::function*)`) adds to `updatePhases`,
`Framework_AddRenderPhase` (`0xc290`) to `renderPhases`; each queue runs in ascending priority. `Framework_FrameStep`
runs the update phases; the last, `Phase_EndOfUpdate`, runs the render phases.

| Queue | Id | Priority | Lambda | |
|---|---|---|---|---|
| update | 0 | `0x0` | `0x17800` | |
| update | 1 | `0x4e20` | `0x174d0` | `Scheduler_Update` |
| update | 2 | `0x61a8` | `0x15d80` | game control (guess) |
| update | 9 | `0x6d60` | `0x14510` | the UI manager |
| update | 3 | `0x7530` | `0x15b80` | `Phase_ActorUpdate`: the actors' update, the play time |
| update | 4 | `0x88b8` | `0x15730` | |
| update | 7 | `0x9088` | `0x15320` | |
| update | 8 | `0x9c40` | `0x14630` | node world update, effects |
| update | 11 | `0xb3b0` | `0x140f0` | sound, BGM |
| update | 5 | `0xb798` | `0x14350` | |
| update | 6 | `0xbb80` | `0x14210` | `Phase_ActorLateUpdate` |
| update | 10 | `0xbd74` | `0x143f0` | the HUD |
| update | 12 | `0xbf68` | `0x14050` | |
| update | 13 | `0xc350` | `0x11960` | `Phase_EndOfUpdate`: the room manager's unloads, then the render phases or a flush |
| render | 4 | `0x4240` | `0xc910` | `Phase_Present` |
| render | 3 | `0x5f90` | `0xd420` | the render-side queue (ACTORS.md) |
| render | 0 | `0x9c40` | `0x10540` | the camera's shadow bounds (Camera) |
| render | 1 | `0xc350` | `0xd6c0` | the render-side calculation queue (ACTORS.md) |
| render | 2 | `0xea60` | `0xd540` | |

## Camera

The camera (`Camera`, `Framework::camera`) is the renderer's view with id 1: `View_Ctor` (`0x190a0 (view, u32 id,
drawRoot sp*)`), destructor `View_Dtor` (`0x19690`), deleting `0x1026af0`, vtable `0x1c46db0`. `0x17fe0` makes it,
0x350 bytes from the graphics allocator (`*Graphics_ptr`'s virtual `+0x20`), and `0x19c70` wraps it in a shared
pointer (control block vtable `0x1b50db0`).

- `View_Update` (`0x1025c30`, virtual `+0x50`, `(view, bufferIndex, lights)`) copies each frame from the game camera
  `source` (a weak pointer): `view` from its `+0x10`, `inverseView` `+0x50`, `projection` `+0x90`, `fovY` `+0xf8`,
  `nearZ` `+0x100`, `farZ` `+0x104`, `frustum` `+0x110` (0xf0 bytes). A view with `ownMatrices` (`view2`) computes its
  orthographic projection from `nearZ`, `farZ` and `aspect`, the inverse from `view`, and the frustum through
  `Frustum_Make` (`0x112fb60 (l, r, b, t, n, f, out, matrix)`). It then builds `shadowMatrix` from `shadowMin`,
  `shadowMax` and the light set's directional light (`+0x20` its index, `+0x10` the entries of 0x9c bytes, the
  direction at `+0x50`; identity without one; likely), writes `shadowParams` with it (guess) and copies the matrix to
  `renderObject` `+0x200`..`+0x240`. The constructor's defaults: `nearZ` 0.2, `farZ` 1000, `aspect` 16/9,
  `frustumCulling` 1, `bufferIndex` -1, `active` 1.
- `frustum` (`Frustum`): `boxMin` and `boxMax` (w 0; guess), six planes (d = −dot(normal, a point of the plane);
  likely) and `planeCount` 6. Model culling (`0x10377f0`) tests models against it only while `frustumCulling` is set
  (`0x1037828`).
- `shadowMin` and `shadowMax`: render phase 0 (`0x10540`, `0x109b8`) sets them each frame to a world point (`+0x600`
  of an object, unless its `+0x700` bit 2) plus `Framework::shadowMinOffset` and `shadowMaxOffset`.
- `View_Setup` (`0x1025200`, `+0x20`, `(view, arg, width, height)`) makes `renderObject` (0x470 bytes, vtable
  `0x1c47c60`) and `viewport`, an nn::gfx `ViewportScissorState` (`ViewportScissorState_Ctor` `0x1146bc0`,
  `ViewportScissorState_Initialize` `0x1146bf0 (state, device, info)`, `ViewportScissorState_Finalize` `0x1146d80`;
  `viewportReady` marks it initialized; the two form a 0x50-byte holder whose destructor `0xfe2520` finalizes it),
  keeps `setupArg`, `width` and `height`, and sets `setUp`; `+0x28` (`0x1025800`) releases them.
- `View_Resize` (`0x1026ad0 (view, width, height)`, virtual `+0x88`) stores `pendingWidth` and `pendingHeight` and sets
  `resizePending` to 3, a bit per buffer; `View_Update` rebuilds the viewport for its `bufferIndex` and clears that
  bit. `bufferIndex` is `View_Update`'s second argument, which virtual `+0x58` (`0x1026ab0`) passes to the render
  object's virtual `+0x28`.
- `active`: the renderer adds a drawable to the view only while it is set (`0x1010004`). Set by
  `RoomManager_LevelJumpTeardown` (`0x8581f8`), the startup chain (`0x868508`), the pause menu's close (`0xeeac5c`,
  `0xee56ac`), `WarpMap_Close` and `WarpMap_OutEnter` (`0xf984d8`, `0xf9ac28`) and `GameOver_Open` (`0xea2318`);
  cleared by the present setup (`Framework_Init` and its inlined copies), the pause menu (`0xee8840`) and the warp map
  (`0xf9932c`), among others; `FileOperate1f_Open` sets it for its duration when it is clear (`0xf80f8c`).
- `drawables`: virtuals `+0x30` add (`0x193e0`) and `+0x38` remove (`0x19580`); `+0x48` (`0x19590`) calls each one's
  virtual `+0x20`; `+0x40` (`0x1025c10`) clears `drawRoot`'s draw lists.
- Further virtuals: `+0x60` (`0x1025920`) draws through `drawRoot`'s `+0x20`, `+0x68` `0x1025b30`, `+0x70` (`0x195e0`,
  `drawRoot`'s `+0x18`), `+0x78` `View_GetId` (`0x195f0`), `+0x80` `View_GetViewport` (`0x1026b20`).

**ViewportScissorState** (nn::gfx, 0x48 bytes): `extra` the second and later viewports and scissors (`viewportCount`
above 1, from the info's `+0x2`), `viewport` (x, y, width, height), `depthRange` (min, max), `scissor` (x, y, width,
height; 0, 0, INT_MAX, INT_MAX without a scissor), `initialized` (1 by Initialize, 0 by Finalize), `flags` (the
info's byte 0; bit 0 the scissor).

`WorldToLayout` (`0xf88c40`) projects through `view` and `projection` (TESTING.md, WorldToLayout).

## Audio

**BgmManager** (the static `Game::BgmManager`, `0x1ca1d70`, its guard `0x1cab240`) is the game's whole audio manager.
`BgmManager_Ctor` (`0x9d3b0`) writes up to `+0x9418` and the next static, the audio heap allocator `0x1cab190`,
starts at `+0x9420`: 0x9420 bytes. `Audio_Init` (`0xbf00`) registers its destructor (`0x9d220`, which shuts the
audio down through `BgmManager_Shutdown` `0x9d2c0` while `initialized` is set) and allocates `audioHeap` (0xe400000
bytes) and `audioHeap2` (0x3000000). `BgmManager_Init` (`0x9d840`) sets up the players, the loader and the outputs
and loads `GROUP_COMMON`. `BgmManager_Update` (`0xa0f90`, from the sound update phase) runs the listener, the
effects, the SE queue, the BGM lists and the environment SEs.

- `cameraEye`, `cameraTarget` and `cameraFov` (20 when made; no reader found): `AudioCamera_Update` (`0xb84f0`, a
  camera entity's task) copies them each frame. `listenerPos` is 0.7 × the eye + 0.3 × Link's world position (the
  eye alone without Link): the 3D listener for SE distance culling, positional BGM and ambient lines.
- `effects` (`AudioEffects`): the aux buses' effects, objects in a static at `0x1cac798`. A reverb on bus 0 (kinds
  1-2 one effect, 3-4 another, likely an I3DL2 reverb; `AudioEffects_ApplyReverb` `0x9cca0`) and a delay on bus 1
  (kinds 5-6: 250 ms, feedback 0.6 or 0.4; `AudioEffects_ApplyDelay` `0x9d070`). A state of 2 waits until the effect
  can be removed, then applies the next kind. The update picks the delay itself: 5 in a side-view room inside the
  area test `0x13c850`, else 6 while Link's Y is 24 or more, else none; `delayCooldown` (a `FrameTimer`
  `{step, elapsed, frames}`) holds 30 frames between changes. `zoneEffect` (the zone record's `+0xb4`) sets kinds 1-6
  on a zone change.
- `se` (`SePlayer`, 0x5870; constructor `0xac440`, destructor `0xac720`): 64 `SeInstance`s; `free` (manager
  `+0x5860`), whose last `Sound_Play` and `Sound_PlayAt` (`0xa5c70`) take; `queued` (`+0x5890`), the start queue the
  plays push to, which the next update starts (into `playing`) or frees (`SePlayer_IsQueued` `0xae110`); `playing`
  (`+0x5878`), released once their sound is gone (`SePlayer_FindPlaying` `0xae020`, `SePlayer_StopByName` `0xad8f0`).
  `SePlayer_Admit` (`0xace60`) applies the SE table's flags: the cooldowns `cooldown8`, `cooldown16`, `cooldown32`
  (45, 60 and 20 frames after an SE with flags bit 3, 4 or 5), ducking (bit 8: `duckVolume` 0.2, back up by 0.05 a
  frame after `duckHold`, 120 frames), restart limits, distance culling, counts and volume caps. `serial` (manager
  `+0x58bc`) counts the accepted plays.
- `bgm` (`BgmPlayer_Ctor` `0x99f90`, destructor `0x9a4e0`, `BgmPlayer_Init` `0x9a7e0`): `BgmManager_Play`
  (`0x9fd60`) takes the last of `free`, keeps it in `starting` while its stream is prepared and its `startDelay`
  runs, then in `playing`; fades and stops by name reach only `playing` (TESTING.md, `0x9fd60`). `pad_9d8` is zeroed
  and never read.
- `envSePlayer` (`EnvSePlayer`, 0x2900; `EnvSe_Ctor` `0x97840`, destructor `0x985b0`, `EnvSe_StopAll` `0x98620`):
  the environment's SEs, run by the update. 12 `AmbientLine`s (`SE_AMB_LINE_BG1` to `12`, rows at `0x1b58c38`;
  the volume by the listener's distance from the segment, `Sound_PlayAmbient` `0xa5ec0`); 3 forest insect spots
  (`EnvSeEmitter`s placed by an actor's update, `0xbf1e00`); 64 torch and lamp emitters (`EnvSe_AddLamp`
  `0x98e20 (name, pos, id, zone)`, `EnvSe_RemoveLamp` `0x99020 (id)`; each plays within 10 of Link in its `zone`);
  three `wind` SEs while Link's Y is 18 or more, unless `windOff` (an event action's); the cove (`coveProbe`,
  `coveHits`: one point a frame of a 5×5 grid around Link is ray-tested, and after 25 the count sets
  `SE_AMB_COVE_BG`); the cucco attack (`cuccoHits`: `SE_MAP_CUCCO_ATTACK` loops from 4); the frogs' croaks
  (`frogTimer`).
- `loader` (`SoundLoader`, 0x570; constructor `0xa7380`, destructor `0xa7760`, `SoundLoader_Init` `0xa78a0`): the
  sound data loader. Four `SoundSlot`s, each an nn::atk SoundHeap with its `memory`, `group`, `playLabel` (played
  once loaded) and `state` (0 free, 1 claimed, 2 loaded): slot 0 (0xa000000 bytes) holds `GROUP_COMMON`, slots 1-3
  (0xc00000 each) the zone's groups. `SoundLoader_Request` (`0xa8a80 (f32 volume, loader, StringView* group, u8 slot
  mode, StringView* playLabel, bool noPlay)`) queues a `LoadRequest` (0xa0) in `requests` unless the group is loaded
  or queued; a worker `task` and `machine` (`StateMachine<2>`) run the loads.
- `outputTv`, `outputSpeaker`, `outputJack`, `outputUsb`: `AudioTvOutput`, `AudioBuiltInSpeakerOutput`,
  `AudioStereoJackOutput`, `AudioUsbDeviceOutput`.
- `zoneTrack` and `zoneVolume` (1.0): `PlayZoneBgm` (`0xa6790`).
- `envSe` and `envGroup`: the environment SE and the sound group it needs, the zone record's (`+0x74`, `+0x94`) or an
  event's. `BgmManager_OnZoneChanged` (`0x9e460`, on the zone signal) sets them, plays the zone BGM with `startDelay`
  150 (0 with `zoneBgmNow`, which it then clears) and changes the effect. `BgmManager_PlayEnvSe` (`0x9f980`) plays
  the environment SE (kind 4) and its `_EX` (kind 5) once the group's slot is loaded. `BgmManager_SetEnvSound`
  (`0xa6180`, an event action's `label` and `groupLabel`) saves the current ones in `savedEnvSe` and `savedEnvGroup`,
  `BgmManager_RestoreEnvSound` (`0xa70a0`) plays them again; a zone change clears the saved ones.
- `holdTrack` (`BGM_FANFARE_BOSS_HEART_GET` from the constructor): `BgmManager_FadeZone` (`0xa6510`) fades nothing
  while it or `BGM_EVENT_RESCUE_BOWBOW` plays (`BgmManager_IsPlaying` `0x9ba40`: a starting or playing instance with
  that track id).
- `eventStartSub`, `eventEndSub` and `zoneSub`: handles on the event manager's `+0xe8` and `+0x140` signals and the
  scene's `+0x760` (`BgmManager_ConnectSignals` `0x9dba0`; `BgmManager_SceneEnd` `0xa0d80` disconnects them).
  `BgmManager_OnEventStart` (`0x9e1c0`) pauses the playing SEs and sets `eventQuiet` (not for the flow
  `ZeldaScroll`); `BgmManager_OnEventEnd` (`0x9df30`) undoes both.
- `quiet`: BGM and SEs play at 0.2 and the positional update stops; it is only cleared (no writer found).
  `eventQuiet`: SEs at 0.2. `zoneBgmNow`: set by a retry, a game over, warps and a file load.

**SeInstance** (`SeInstance_vtable` `0x1b59260`, 0x160 bytes; `SeInstance_Setup` `0xaab80` fills it from a play
request, `SeInstance_Release` `0xac2c0` clears it): `kind` 2 a 3D one-shot (`Sound_PlayAt`), 3 an ambient line or
wind, 4 the zone's environment SE, 5 its `_EX`. `flags` are the SE table's: bits 0-2 no restart within 3, 16 or 30
frames of a same-id SE, 3-5 the cooldowns, 6-7 volume caps 0.5 and 0.7, 8 ducking, 14-16 dropped beyond 5, 10 or 15
from the listener, 17 at most 3, 18 the volume divided by the count, 22 bus 1, 27 dropped within 3.5 of a same-id SE.
The sound's volume is `volume` × `volumeScale` (1, the duck volume or a cap). `actor` is a caller's nn::atk 3D actor,
`ownActor` one made for a 3D SE without; `onUpdate` and `onUpdate2` run each frame (kinds 2, 3 and 5 install
lambdas); `echoArea` is the delay's area test at the SE's position. `serial` (-1 free) is the play's: a play's out
argument is an `SeRef` `{instance, serial}`, valid while the instance's `serial` is still its own.

**BgmInstance** (vtable `0x1b58cf0`: `+0x0` `BgmInstance_Dtor` `0x9a400`, `+0x8` deleting `0x9a4b0`; 0x130 bytes,
the stride of `BgmPlayer::instances`). Its base is the sound handle (`SoundHandle`, `SoundHandle_vtable`
`0x1c30a90`, 0x60 bytes, SeInstance's base too): `{vtable, Sound* sound, std::string name}`, three fields no code
reads, and `fadeOutFrames` (30 at construction), which every `Sound_Stop` of the handle uses. Its destructor
(`0xfa20f0`) clears the sound's back-links (`Sound::handles`) that point at the handle's `+0x8`.

- `BgmManager_Play` fills an instance from the BgmTable row (`BgmTable_Find`, flags `0x10e24b0`): `onStart` with the
  row's flags bit 0 (lambda vtable `0x1b58de8`), called once when the stream starts; `onUpdate[0]` with bit 1
  (`0x1b58e30`, `BgmInstance_UpdateHeight` `0xa0480` through its thunk `0xa0470`: the volume by Link's height,
  reading Link's model node, Framework `+0x4b8` then `+0x80`, with no null check), `onUpdate[1]` cleared; both run
  every frame while playing. With `prepareNow` (its last argument) it
  prepares the stream at once (`BgmInstance_Prepare` `0x99800`) and drops the instance on a failure (likely).
- `kind`: 1 zone and event music; 2 a jingle (when Play's first bool is set): kinds 1 and 4 are `ducked` to 0 over 45
  frames while one plays and come back over 150; 3 a buff's music; 4 positional: `BgmInstance_SetPosition`
  (`0x998e0`, from `PlayZoneBgm`) sets `position`, `radius` 3, `fadeRange` 9 and `onUpdate[0]`
  `BgmInstance_UpdatePositional` (`0x99b70`: the volume by the listener's distance, kind 1 instances get 1 − it;
  volume 0 while `noPositional`).
- `flags` bit 2 marks a buff's music (`PlayZoneBgm` tests it); `volume` (`BgmManager_SetVolume` `0x9b3a0` writes
  it); `pitch` (1.0) has no reader.
- `startDelay`: the frames the starting list waits before `BgmInstance_Start` (`0x99130`) starts the stream, which
  passes `startOffset` as the stream's start offset when it is set.

**Sound** (`nn::atk::detail::BasicSound`, by its RTTI; `BasicSound_vtable` `0x1c53248`, `BasicSound_Ctor`
`0x10e93b0`; 0x210 bytes, abstract) is the base of the nn::atk sounds, whose instance pools' strides give their
sizes: StreamSound 0x11d40 (likely the BGM's), WaveSound 0x3f0, SequenceSound 0x5a0, AdvancedWaveSound 0x9b0. Its
virtuals `+0x18` `BasicSound_Initialize` (`0x10e9470`) and `+0x20` `BasicSound_Finalize` (`0x10e9620`).
`Sound_Stop` (`0x10e97f0`) calls virtual `+0x48`, re-sorts the sound in its `player`'s priority list
(`SoundPlayer_SortPriority` `0x10e5dd0`, by `priority` + `priorityBonus`), needs one of `active` set, finalizes at
once when `state` is 2 (likely paused), else sets `fadingOut` and fades out over `stopFade`, and clears `+0xee`,
`+0xf1`, `+0xf4` and `priorityBonus`. `Sound_FadeFramesLeft` (`0x10ea8a0`) is `stopFade`'s frames left, 0 when done;
`BgmManager_Play` uses it to tell a track still fading out. `Sound_SetVolume` (`0x10ea8c0 (sound, frames, volume)`)
sets the `volume` fade. `id` is the sound archive item (the BgmTable id), `playFrames` what the SE restart limits
compare, `lifecycle` 1 initialized, 2 finalized, 3 destroyed.

Open: the writers of `quiet`, `startOffset` and `noPositional`; the readers of `pitch`, `SoundHandle`'s unread fields,
`SeInstance`'s `params3d` and `function_d0`; most `Sound` fields' nn::atk names; the camera entity class
`AudioCamera_Update` runs for and the insect spots' actor (`0xbf1e00`); the effect classes (inferred from their
parameters); `SoundSlot::field_f0` and `LoadRequest`'s `+0x9c`/`+0x9e`; the loader's worker.

## Open questions

- Framework `field_48`, `field_4e8`, `sp_530` (only released), `field_5a0` (only cleared), the writers of `view3` and
  `restart`; Present `field_50`, `vector_58`, `field_70`.
- GameState `vector_c0`: what its entries' first object is, and their writer; the writer of `resetResources`.
- LevelScene `object_510`, `field_648`..`field_694`, the meaning of `field_808`..`field_810`; what `signal_750` and
  `signal_7a8` signal (their emitters).
- TitleScene `+0x82` and `+0x83`.
- Camera `field_308`, `sp_310`, `field_320`, `field_328`; the object whose `+0x600` the shadow bounds follow.
- PhaseQueue `id` (from an SDK import, `0x14312c0`); the exact shape of a priority group and a phase.
