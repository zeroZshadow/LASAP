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
then `Framework_Shutdown` (`0x1ab30`), and starts at the init again while `restart` is set (nothing writes it, so
Init runs once).
`Framework_Init`, in order: the present setup; `systemEvents` (`0xa980`, `0xfa1e60`); `controller` (`0xa9f0`);
`Framework_InitGraphics` (`0xaa40`); `Physics_Init` (`0xb6a0`: `physicsHeap`, `physics`);
`Framework_InitResourceListener` (`0xb990`); `ActorSystem_Make` (`0xba30`, `actorSystem`); `0xbde0`; `Audio_Init`
(`0xbf00`); `PlayReports_Init` (`0xdf6870`, `playReports`); the frame step's task in `frameQueue` (`0xbf90`: lambda
vtable `0x1b502d0` → `Framework_FrameStep` `0x17e30`); the 14 update and 5 render phases (below); `gameState`
(`GameStateFactory_Create` `0xc450`).
`Framework_InitStorage` (`0xa4a0`) fills `romMount` (`"rom"`), `regionResources` (`region_common`, `common`,
`regionJP`, `JPja`), `heapMemory` (a block of (total − 0x6da00000) & ~0xfff bytes, freed through `heapMemoryFree`,
`0x5390` by default) and the main heap (0x53600000 bytes in it).

**Main loop.** Each pass (from `0x1b97c`) runs `frameQueue` (`PhaseQueue_Run` `0x11cc0`) and counts `frameCount`
unless `paused` or `roomPaused` is set, while the render phases still draw the frames; the loop ends when the game state's task has `finished` set (`GameState_Finish`, below). A set
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
| `lastFrameTick`, `elapsedOffset` | `Phase_Present` counts one missed vsync less when the time since `lastFrameTick` (ns) minus `elapsedOffset` is below 13333332 and graphics `+0x81c0` (ns: the frame's GPU time, the difference of two GPU timestamps, `0x101fb98`) below 14166666; nothing writes `elapsedOffset` |
| `presentCount`, `lastPresentVsync`, `skipRenderFrames`.. `presented` | `Phase_Present` (`0xc910`, render phase 4) counts presents, records the vsync counter (graphics `+0x81ac`) + 1 and sets `skipRenderFrames` from the vsyncs missed, clamped to `minSkip`..`maxSkip` (0 in levels: `0x854624`, `0x854c14`; 1 at construction); `Phase_EndOfUpdate` (`0x11960`) counts it down, flushing instead of rendering, and clears `presented` |
| `presentInitDone` | the one-time present setup (the camera's `active` cleared, the skip count to `minSkip`), also inlined at `0x8545e4`, `0x8379a4`, `0x8646ec` |
| `zoneFlagB5` | copied from a zone's `+0xb5`: the new zone's when the scene's zone changes (`0x865fdc`), the looked-at room's zone in `SetLookAtEnvironmentNonWait` (`0x7fe524`); while set, render phase 0 makes the shadow bounds the box around the camera's position and the bounding spheres of the renderer's models that lie in the camera's frustum, instead of the camera entity's eye plus the offsets (`0x1098c`) |
| `shadowMinOffset`, `shadowMaxOffset` | (FLT_MIN × 3, 0) and (FLT_MAX × 3, 0) at construction, then the constants at `0x16af170` and `0x16aeec0` from the graphics init; render phase 0 adds them to the camera's eye for the camera's `shadowMin` and `shadowMax` (`0x10994`, `0x10998`) |
| `paused` | 1 at construction; the pause menu or a level load (ACTORS.md, Entity manager and the frame) |
| `roomPaused` | set by `Scene_UpdateZone` (`0x8364a4`, `0x8364f4`) |
| `controller` | make_shared of 0x570 bytes (constructor `0x87f850`; UI.md, Input) |
| `gfxMemory` | 0x2ed80000 bytes from the heap `0x1cab190`, for graphics |
| `renderer` | 0x58 bytes (vtable `0x1c466c0`, made by `0x18220`): `+0x20` a vector of views (`Renderer_AddView` `0x100f7a0`, virtual `+0x48`), `+0x40` the environment (`Renderer_SetEnv` `0x100fe60`); `Renderer_FindView` (`0x100fcd0`, `+0x60`, `(out weak, renderer, id)`). The skeletal model components' model list (MODELS.md) and the list `InstancedModel_Ctor` takes |
| `drawRoot` | the camera's draw node tree (its `+0x2d8`) |
| `camera` | the renderer's view with id 1 (Camera, below) |
| `view2` | the view with id 2: 0x5e0 bytes (vtable `0x1b50c68`, constructor `0x18d50`), orthographic (`ownMatrices` 1, an identity view) |
| `view3` | another view: render phase 0 (`0x109bc`, `0x11220`) gives it the camera's shadow bounds while its `active` is set; nothing writes it |
| `unused_530`, `unused_5a0` | their only accesses are the clears in `Framework_TeardownGraphics` (`0x1b204`, `0x1b230`) and the destructor's release of `unused_530` (`0x9110`) |
| `renderEnv` | 0x290 bytes (constructor `0xff94d0`; its init `0xff9880` loads `shader/ResetData_cube.bntx`); the renderer's `+0x40` |
| `physicsHeap`, `physics` | made by `Physics_Init` (`0xb6a0`): 0x2000000 bytes handed to the physics heap `0x1ccf668` (`0x1431470`); the physics system from `0x10c5730`, whose PhysX 3.4 worlds 0 and 1 it makes through `Physics_GetWorld` (ITEMS.md, Physics step) |
| `playReports` | 15 PlayReport objects (0x58 bytes, constructors `0xdf6d40 + i × 0x70`): 0 clear, 1 dead, 2 get_item, 3 play_minigame, 4 warp, 5 fishing_result, 6 mappin, 7 panel_dungeon, 8 panel_plus, 9 panel_theme, 10 amiibo_touched_id, 11 amiibo_get_panel, 12 amiibo_touched_count, 13 amiibo_write_dungeon, 14 amiibo_read_dungeon |
| `systemEvents` | 0x118 bytes (constructor `0xfa01a0`; init `0xfa1e60` sets four nn::oe notification modes); its `+0x18` takes a callback (`0x18af0`) that resizes the graphics on an operation mode change |
| `present` | `Present` (below) |

**Present** (`Framework::present`, 0x70 bytes; `Present_Init` `0x107cb90 (present, list, list, WeakPtr renderer)`,
destructor `0x107cb20`): `active` (1), `maxInterval` (the maximum present interval: 0xff at construction, 1 in levels,
clamped with graphics `+0x812c`), `renderer`, and per mode a list of graphics levels with an index into it:
`handheldIndex` starts at the first entry ≥ graphics `+0x6e48` (14), `dockedIndex` at the one ≥ graphics `+0x6e4c`
(8); graphics `+0x56a8` picks the mode (1 docked). `Phase_Present` pushes each present's GPU load into `loadHistory`:
graphics `+0x81c0` in % of `frameTime` × the present interval. At 6 entries it clears the history, first moving the
mode's index one entry on (clamped) when all were above 90, or one back when all were below 70, and requesting the new
entry (`Present_RequestLevel` `0x107d110`: a graphics callback that applies it).

**Frame pacing.** `GraphicsSystem_Init` (`0x10194c0 (system, config)`, the graphics system `*(Graphics_ptr)[1]`,
`Graphics_ptr` `0x1cc8b30`) starts `VsyncWatcher_Init` (`0x10410d0 (watcher, system)`) at system `+0x8168`: a thread
that waits for the display's vsync event (`nn::vi::GetDisplayVsyncEvent`) and signals a light event at each one. While
the watcher runs (`+0x8169`), each frame's end (`0x101f820`, after the wait for the GPU) clears that event and waits
for it: a frame that misses a vsync waits for the next, so the game drops from 60 to 30 fps at once.

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
  a level scene through its `roomManager` (`LevelManager_LevelJump` with no level, unless the room manager has
  finished; ACTORS.md, Level jump), the bool into `jumpFlag`.
- `GameState_NextScene` resets the resource manager first (`0xfc6380`) when `resetResources` is set and clears it,
  then dispatches on `nextSceneId` (jump table `0x16b2d14`): 1 the title, 2 the file menus, 3 and 4 a level, 5 the
  ending, else `GameState_Finish` (`0x8674d0`, virtual `+0x50`), which resets the systems, drops `fadePresets`,
  `globalFlags` and `unused_c0` and finishes the task, which ends the main loop.
- `GameState_Update` (`0x8670f0`, virtual `+0x20`) drops the `unused_c0` entries whose first object's u32 `+0x78` is
  0.
- `titleSeen` is set when a title scene finishes (`0x87e374`); each title scene copies it into its `+0x84` as it
  starts (`0x87de24`, `0x87df14`), which nothing else reads.

**Resource groups.** A resource group (a make_shared of 0x68 bytes, control vtable `0x1b64040`, the object at `+0x18`:
vtable `0x1b640a0`, a second vtable `0x1b640f8` at `+0x48`) is a set of files loaded asynchronously: `+0x8` an
`unordered_map` by path (maximum load factor 1.0), `+0x30` the load (a shared pointer). `Datasheet_Load` (`0x83a1e0
(out, group, StringView* path)`) finds the path in the group's map (`0x153a50`) and returns the entry's `+0x30`. The
makers name their first file: `0x87a9c0` FadePreset.gsheet (`GameState::fadePresets`), `0x86cb80` GlobalFlags.gsheet
(`GameState::globalFlags`), `0x83e0a0` the level's sheets (`LevelScene::sheets`), `0xed5770` `ui/Game.arc` (UI.md, UI
manager). Single files go through the resource manager (`ResourceManager_ptr` `0x1cc6db8`): `Resource_Load`
(`0xfbbd80 (out handle, manager, StringView* path)`), `Resource_Wait` (`0x3dc20 (handle*)`) and the takers
(`Resource_TakeFlowchart` `0x3d9f0 (out, handle*)`; ACTORS.md, Creation).

**Scenes.** The game state's current scene (`GameState::scene`) is a task of one class per kind of screen, told apart
by its vtable (`SceneOf`, `Scene` in `game/framework.hpp`; `DBG uis` prints it). The makers take the game state as
`parent` (`WeakPtr_LockOrThrow` `0x869cf0`, from its `weakThis`):

| Class | Vtable | Task name | Size | Maker | Scene |
|---|---|---|---|---|---|
| TitleScene | `0x1bc49c0` | Title | 0x88 | `TitleScene_Make` `0x869e50` | the intro and the title screen |
| FileMenusScene | `0x1bc2740` | FileSelector | 0x88 | `FileMenusScene_Make` `0x86b970` | the file menus (file select, name entry) |
| LevelScene | `0x1bc2f90` | Main | 0x820 | `LevelScene_Make` `0x86b4e0` | a level, from the start of its load (the type `SceneHolder_Lock` takes) |
| EndingScene | `0x1bc2550` | Ending | 0x88 | `EndingScene_Make` `0x86b030` | the ending |

`SceneHolder_Lock` (`0x11b70 (shared_ptr* out, holder*)`) gives the holder's scene (`+0x88`/`+0x90`) when its type is
the level scene's; the game passes `gameState`.

The small scenes' fields follow the task base: TitleScene `+0x81` the step (`TitleScene_Update` `0x87bde0`, 0 to 8),
`+0x82` no save file (`0x87e570`, when the byte `0x1cc1896` is set: 0 when `SaveFile_Exists` `0xe072b0` finds slot
0, 1 or 2, else 1; `nn::fs::GetEntryType`, false only for PathNotFound); with `+0x84` clear it makes the update
ignore the buttons that otherwise end the intro (step 4; `0x87cac0`). `+0x83` is set once the timer
`*(0x1cb26f0)+0xbc0` passes a threshold and the scene moves the Title UI to LogoIn (1) (`0x87ce04`, `0x87cdf4`);
`+0x84` starts as the game state's `titleSeen` and is set when the scene moves the Title UI to state 5 (`0x87c7f4`,
`0x87c930`, `0x87cc6c`, `0x87ccb8`). FileMenusScene `+0x81` the step; EndingScene `+0x81` the step (0 to 7), `+0x82` and `+0x83`.

The intro and the title share their scene: the intro plays while the title screen's UI (UI 0x1d, `Title`) is not
there yet or its machine waits in Deactive (UI.md, State machines). The module counts a save as loaded outside the
title and file-menu scenes (`NoSaveLoaded`), and names the level scene `loading` until Link exists. Measured from a
fresh boot to a level (2026-10-23, `screens`).

**Level scene** (`LevelScene`; `LevelScene_Ctor` `0x835fa0 (scene, SharedPtr* parent)`, destructor `0x844ea0`):

- `saveManager`: constructor `0xe081a0`, destructor `0x835d80`. `environment`: constructor `0x7d69c0`, destructor
  `0x7d71a0`, the scene's Finish `0x7dad30`. The level load (`0x838e80`) hands it `archive` (`0x7d7510`), which also
  connects to `zoneChanged` and `roomBlend` and reads the environments. It takes `Framework::renderEnv`, whose `+0x10`
  holds 32 light slots of 0x9c bytes (`+0x8` intensity, `+0x90` handle, `+0x98` base intensity). Each frame
  `Scene_UpdateZone` steps the blend (`0x7da010`) while `blending`, else the fade (`0x7dbd70`) while `fadeFrame` is
  not `fadeEnd`, then the look-at blend. A string switch in its code (`0x7d9690`..) names screen effects (Mist,
  HotAir, SandCloud, VignetteTop, WindFishsEgg). The `Environment` event actor's actions (`Environment_FindAction`
  `0x8168ac`; each std::function's call is its vtable `+0x30`) work on it:
  - Every environment is found by name in `+0x58` (`LevelEnvironment_Find` `0x7d86c0 (map, const char* name, u64 length)`): by `LevelEnvironment_Change` and by the `roomBlend` slot (`0x7d8620`), which takes the names of room A's and room B's zone data (`+0xf0`, length `+0x110`; the LVB zone's environment) and changes to A's alone or blends the two.
  - `ChangeEnvironment`, `ChangeEnvironmentNonWait`: `0x7fe1a0 (name, duration)` → `0x7d9fc0`, which also resets the
    fade and clears `savedIntensities`.
  - `StashSaveIntensity` (`0x7fedf0 (attenuation, duration)` → `0x7db5f0`) saves every live light slot and starts the
    fade down from `fadeFrame` = `fadeFrames`; `StashPopIntensity` (`0x7fefb0 (duration)`) fades back up from 0 when
    lights are saved. Each fade step (`0x7dbd70`) sets a saved light's intensity to base × saved ×
    (`attenuation` + (1 − `attenuation`) × `fadeFrame` / `fadeFrames`).
  - `SetLookAtEnvironmentNonWait` (`0x7fe380`) takes the first object of a scene list (`0x1b730`, `+0x210`) whose box
    (its `+0x1a8` → `+0x20`..`+0x3c`) holds the camera's point `+0x600`. `0x7d80b0 (env, object, 0)` sets
    `lookAtPending` to the node under the object's `+0x1b8` that the predicate `0x7d8490` picks and, from state 0,
    starts the delay; then the action changes to the object's environment (`0x7d9fc0`). When `delayFrame` reaches
    `delayFrames`, `0x7dc510` moves `lookAtPending` into `lookAt` and starts the blend; it clears both and goes to state
    0 when either is gone or both are the same. While blending, `Scene_UpdateZone` waits for the `Loader` component
    among `lookAt`'s components (`+0x58`; ready: its `+0x58`), copies `lookAt`'s environment (`+0x318`, floats
    `+0x320`..`+0x388`) into the render env with its `+0x1d8` = 1 − `lookAtFrame` × `lookAtRate`, and at the end makes
    it the render env's own and calls `0x7dc510`.
- `sheets`: the level's datasheets (`0x83e0a0` from `0x8389e0`; the loads `0x838b50`..`0x838e50`): PlayerAnimation,
  Facial, InterestIK, Npc, NpcPhysicsTraits, Reaction, CranePrize*, Light, FishingFish, FishingSpot and
  AutoSaveLocation (`.gsheet`) and `kodama/environment.kar`.
- `buffKind` (ITEMS.md) and `buffTime`, which `Scene_UpdateZone` counts down by the frame time and, at 0, clears
  with `buffKind` (`0x837218`).
- `buyCount`: NpcCrazyTracy's code (`0x96cb44` adds 1 up to 99, `0x96cce0` zeroes it, `0x96c77c` reads it; its
  strings name the actions `AddBuyCount` and `ResetBuyCount`).
- `cameraFacing`: the camera system's `0xbc048` writes 1, 2, 3, 2 for its mode 1-4 (`0xbc190`), else 0. NPC code
  (`0x92bd10`) and Link's (`0xd6e994`) turn an offset by it: entry `64 × cameraFacing` of the 256-entry angle table at
  `*0x1c93ed0` (16 bytes each, outside the main module), whose first two floats scale the offset into z and x.
- `minigame`: NpcGameShopOwner sets 1 (`0x9bbb84`, `0x9bbd70`), TagFishingManager 2 (`0xe16e7c`), the rapids ride 3
  (`0xdeebd0`), each 0 again after; `NpcGhost_Update` counts its `timer` down only while it is 0 (`0x9beacc`).
- Four signals (`Signal`, ACTORS.md, Nodes and Dungeon rooms): `zoneChanged` (constructor `0x8326e0`),
  `roomChanged` (`0x833530`) and `roomBlend` (`0x834150`), which `Scene_UpdateZone` emits, and `activeZone`
  (`0x835000`), which `Scene_SetActiveZone` emits once the zone camera's scroll ends. Subscribers reach a signal
  through its `owner` (`+0x10`), so the scene's `+0x708`, `+0x760` and `+0x7b8` other docs name are those of
  `roomChanged` (the actors), `roomBlend` (`DungeonMap_SubscribeZone` outside dungeons, the BGM's `zoneSub`) and
  `activeZone` (`Actor_SubscribeZones`).
- `zone`, `room`, `roomA`, `roomB` (-1) and `blend` (0xff) at construction: Link's zone grid cell (`ZoneGrid_Lookup`)
  and room; `Scene_LeaveRooms` sets them back.
- `retryA`, `retryB`: `Event_ProcessLevelJump`'s `$RETRY` (`0x81bbb4`) copies `retryB` when its `set` is, else
  `retryA`, into the current data (position `+0xdc`, facing `+0x121`), then clears both `set`s and the buff through
  `minigame`, as the scene's Finish (`0x838180`) also does. `Player_UpdateAnimation` writes `retryB`
  (`0xd35928`..`0xd35a6c`): the centre of the cell under Link of the level data's 1.5-unit grid (`+0x228`, origin
  `+0x80`; `0x8e8520`), at his height, and his node's facing. `retryA` is Link's world position
  and facing at the end of each level load: `LevelLoad_Finish` sets room manager `+0x3fb`, and a callback that
  `RoomManager_FinishLevelLoad` installs (`0x859490`) then writes it (`Scene_SetRetryA` `0xd8c800`, which also clears
  `retryB`'s `set`) and clears `+0x3fb`.

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
bytes): `Task_Post` (`0x2d7430`) and `Delay_Make` (`0x7f3c90 (f32 seconds, out future)`; for 0 s or less no task but
the game's ready future, the static `0x1cc6e40` with a new control block, which Link's event actions also return when
they are done at once) add tasks through `Scheduler_Add` (`0xfcff00
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

**Delay task.** For more than 0 s, `Delay_Make` makes one block of 0x90 bytes (`DelayBlock` in `game/framework.hpp`):
the control block (vtable `0x1bbcf68`), the task at `+0x18` (vtable `0x1bbcfe0`) and the future at `+0x80` (vtable
`0x1bbd050`), which the out pair points to with the control block as its owner. The task holds a signal at its `+0x8`
(open while not finished: its `+0x38` is nonzero), the seconds left `+0x60` and `cancelled` `+0x64`.
`DelayTask_Update` (`0x7f4010`, task vtable `+0x20`) subtracts `FrameDelta` from the seconds left and, at 0 or less,
sets them to 0 and emits the signal, which finishes the future; `DelayTask_Cancel` (`0x7f4050`, `+0x28`) sets
`cancelled` and the seconds left to 0. Setting the seconds left to 0 ends the delay in the next update.
`Framework_Shutdown` runs it once more, then `Scheduler_Clear`.

**ThreadPool** (`game/stl.hpp`; `ThreadPool_Ctor` `0x8700 (pool, u64 threadCount, std::function* threadFactory)`,
`ThreadPool_Dtor` `0x7b80`, `ThreadPool_Worker` `0x8bd0`; 0x78 bytes): `threads` (0x18 bytes each, element vtable
`0x1b50058`); `jobs`, a deque of `Function`s (0x30 bytes, 0x55 a block) whose front each thread pops under `mutex`,
waiting on `wake` while it is empty; a thread returns once `stop` is set and the deque is empty. The thread factories are plain
functions: the framework's `0x76c0`, `0x7840` and `0x7860` (each `0x81b0(out, 0x1001, 0x10000, index)`), the UI
manager's `0xecd020`.

**PhaseQueue** (`PhaseQueue_Ctor` `0x76e0 (queue, std::function* threadFactory)`, destructor `0x7ab0`, `PhaseQueue_Run`
`0x11cc0`; 0xf8 bytes): `ownerThread` comes from `std::__libcpp_thread_get_current_id` (the import
`0x14312c0`). A `byPriority` node (0x50 bytes) holds the priority at `+0x20` and the group from `+0x28` (buckets,
`+0x38` the first phase, `+0x48` the maximum load factor 1.0); a phase node (0x60 bytes) holds the id `+0x10`, its
`Function` `+0x20` and the `std::future<void>` it returned `+0x50`. A `phases` node (0x18 bytes) holds the id `+0x10`.
`PhaseQueue_Add` (`0xe120 (queue, u32* id, std::function*, s32 priority)`, which `Framework_AddUpdatePhase` calls)
returns false for a known id, else records it and inserts the phase (`PhaseQueue_Insert` `0xe8e0`, making the group
when new), or queues the add in `pending` while `running` is set. `pending` holds the changes made while it runs: a
deque of 0x50-byte entries, 0x33 a block (`+0x0` u32 op: 0 add, 1 remove, 2 clear, 3 set priority; `+0x4` u32 id;
`+0x10` the `Function`; `+0x40` s32 priority), applied after Run by `PhaseQueue_ApplyPending` (`0x13050`; remove
`PhaseQueue_Take` `0x133c0`, set priority `0x13510`: take, then insert). Run calls each phase's `Function` in
ascending priority and keeps the future it returns; a group with a valid future goes to `worker` (`0x11f70`), while
the calling thread runs the jobs of its thread-local queue (one each `0xfd35d0`) until that job sets its done
flag. The entity manager's queues have the same
shape (ACTORS.md).

**Phases.** `Framework_AddUpdatePhase` (`0xc0e0 (framework, id, priority, std::function*)`) adds to `updatePhases`,
`Framework_AddRenderPhase` (`0xc290`) to `renderPhases`; each queue runs in ascending priority. `Framework_FrameStep`
runs the update phases; the last, `Phase_EndOfUpdate`, runs the render phases.

| Queue | Id | Priority | Lambda | |
|---|---|---|---|---|
| update | 0 | `0x0` | `0x17800` | |
| update | 1 | `0x4e20` | `0x174d0` | `Scheduler_Update` |
| update | 2 | `0x61a8` | `0x15d80` | the controller (`Framework::controller`; UI.md, Input) |
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
  `shadowMax` and the light set's directional light (`+0x20` its index, -1 none, which leaves the matrix as it is;
  `+0x10` the entries of 0x9c bytes, the direction at `+0x50`): a look-at from `shadowParams`, the bounds' centre
  moved back against the light by half their diagonal, to the centre; identity for a zero direction. It copies the
  matrix to `renderObject` `+0x200`..`+0x240`. The constructor's defaults: `nearZ` 0.2, `farZ` 1000, `aspect` 16/9,
  `frustumCulling` 1, `bufferIndex` -1, `active` 1.
- `frustum` (`Frustum`): `boxMin` and `boxMax`, the box around the frustum's eight corners (w 0), six planes (d = −dot(normal, a point of the plane); normals
  outward) and `planeCount` 6; `Frustum_Make` writes `planeCount` and `boxTest` (0) as one 8-byte store (`0x112fee4`).
  `Frustum_TestSphere` (`0x1130090 (frustum, {Vec4 center, f32 radius})`) is false when the sphere lies wholly outside a
  plane, `Frustum_ClassifySphere` (`0x1130100`) gives -1 outside, 0 crossing, 1 inside; `Frustum_TestBox` (`0x1130180
  (frustum, {Vec4 min, Vec4 max})`) tests the box against each plane, after an overlap test with `boxMin`..`boxMax`
  while `boxTest` is set (callers `0x174284`, the camera's frustum, and `0x8e7a58`). Model culling (`0x10377f0`) tests
  models with `Frustum_TestSphere` only while `frustumCulling` is set (`0x1037828`).
- `shadowMin` and `shadowMax`: render phase 0 (`0x10540`, `0x109b8`) sets them each frame to the active camera
  entity's eye plus `Framework::shadowMinOffset` and `shadowMaxOffset` (with `Framework::zoneFlagB5` clear). The
  entity is the shared pointer at `+0x0` of the CameraSystem (`CameraSystem_ptr`, 0xa8 bytes, constructor
  `0xbdaa0`); its eye `+0x600` (also `AudioCamera_Update`'s) is recomputed first while its `+0x700` bit 2 is set:
  the followed node's (`+0x510`, else the transform at `+0x530`) world position plus (0, 0, −`+0x560`) scaled and
  rotated by its scale and rotation (`0x10e48`).
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

`WorldToLayout` (`0xf88c40 (const Vec4* world)`) projects through the camera's `view` and `projection` (Framework
`+0x560`) to layout coordinates in s0/s1 (origin at the centre, x ±640, y ±360). It divides by clip w unchecked: a point
behind the camera lands mirrored on screen. `Hud_Update` places the action prompt next to Link with it (UI.md, HUD).

## Audio

**BgmManager** (the static `Game::BgmManager`, `0x1ca1d70`, its guard `0x1cab240`) is the game's whole audio manager.
`BgmManager_Ctor` (`0x9d3b0`) writes up to `+0x9418` and the next static, the audio heap allocator `0x1cab190`,
starts at `+0x9420`: 0x9420 bytes. `Audio_Init` (`0xbf00`) registers its destructor (`0x9d220`, which shuts the
audio down through `BgmManager_Shutdown` `0x9d2c0` while `initialized` is set) and allocates `audioHeap` (0xe400000
bytes) and `audioHeap2` (0x3000000). `BgmManager_Init` (`0x9d840`) sets up the players, the loader and the outputs
and loads `GROUP_COMMON`. `BgmManager_Update` (`0xa0f90`, from the sound update phase) runs the listener, the
effects, the SE queue, the BGM lists and the environment SEs.

- `cameraEye`, `cameraTarget` and `cameraFov` (20 when made; no reader found): `AudioCamera_Update` (`0xb84f0`)
  copies them each frame from MainCamera. MainCamera's `Setup` (`0xb60a0`) attaches an `AudioListenerComponent`
  (vtable `0x1b5a158`, type id `0x1cac9f8`, made by `0xb7970`) at its `+0x788`, whose `Register` (`0xb7e90`) posts
  that task. `listenerPos` is 0.7 × the eye + 0.3 × Link's world position (the
  eye alone without Link): the 3D listener for SE distance culling, positional BGM and ambient lines.
- `effects` (`AudioEffects`): the aux buses' effects, nn::atk effect objects (by their RTTI) in a static at
  `0x1cac798` (constructor `0xfa2650`): `EffectReverb` (`+0x0`) for kinds 1-2 and `EffectI3dl2Reverb` (`+0x98`) for
  kinds 3-4 on bus 0 (`AudioEffects_ApplyReverb` `0x9cca0`), `EffectDelay` (`+0x140`) for kinds 5-6 on bus 1 (250 ms,
  feedback 0.6 or 0.4; `AudioEffects_ApplyDelay` `0x9d070`). A state of 2 waits until the effect
  can be removed, then applies the next kind. The update picks the delay itself: 5 in a side-view room inside the
  area test `0x13c850`, else 6 while Link's Y is 24 or more, else none; `delayCooldown` (a `FrameTimer`
  `{step, elapsed, frames}`) holds 30 frames between changes. `zoneEffect` (the zone record's `+0xb4`) sets kinds 1-6
  on a zone change.
- `se` (`SePlayer`, 0x5870; constructor `0xac440`, destructor `0xac720`): 64 `SeInstance`s; `free` (manager
  `+0x5860`), whose last `Sound_Play` (`0xa57e0 (StringView* name, nullptr, volume, pitch)`) and `Sound_PlayAt`
  (`0xa5c70`) take; `queued` (`+0x5890`), the start queue the
  plays push to, which the next update starts (into `playing`) or frees (`SePlayer_IsQueued` `0xae110`); `playing`
  (`+0x5878`), released once their sound is gone (`SePlayer_FindPlaying` `0xae020`, `SePlayer_StopByName` `0xad8f0`).
  `SePlayer_Admit` (`0xace60`) applies the SE table's flags: the cooldowns `cooldown8`, `cooldown16`, `cooldown32`
  (45, 60 and 20 frames after an SE with flags bit 3, 4 or 5), ducking (bit 8: `duckVolume` 0.2, back up by 0.05 a
  frame after `duckHold`, 120 frames), restart limits, distance culling, counts and volume caps. `serial` (manager
  `+0x58bc`) counts the accepted plays. The SE names are plain strings in main, so `scan.py xstr` finds their
  callers: the low-health warning plays `SE_SY_HP_ALLEART` from `0xeabeb0` (the first five beeps) and `0xeabf44`
  (the quieter ones after).
- `bgm` (`BgmPlayer_Ctor` `0x99f90`, destructor `0x9a4e0`, `BgmPlayer_Init` `0x9a7e0`): `BgmManager_Play`
  (`0x9fd60 (f32 volume, BgmManager*, StringView* name, bool, bool, bool force)`) returns the new instance, or null
  for an unknown name or a track already playing or starting (a playing one that fades out does not count). It
  takes the last of `free`, keeps it in `starting` while its stream is prepared (a few frames) and its `startDelay`
  runs, then in `playing`; a new instance starts its stream from the beginning. Fades and stops by name reach only
  `playing`, so a track asked for just before another change starts after it and plays on. The zone music's calls
  return to `0x9f008` (the zone update) and `0xa688c`/`0xa6988` (`PlayZoneBgm`), the rapids' end's to `0xdf24d8`
  (below); the event action `PlayBGM`
  (`EventAction_PlayBGM` `0x805a30`) passes its `label` and `volume` with the bools false. `BgmTable_Find`
  (`0x10e23d0 (table, const char* name)`) gives the track id, -1 for an unknown name; the table (`0x1caba28`, guard
  `0x1cac378`) is made on first use. `pad_9d8` is zeroed and never read.
- The other BGM calls: `BgmManager_StopAll` (`0x9b550 (u32 frames)`: each playing instance's `fadeOutFrames`, then
  `Sound_Stop`; the event action `StopAllBGM` and the level load, 0 frames), `BgmManager_StopByName` (`0x9b7b0
  (StringView* name, u32 fade frames)`, the event action `StopBGM`), `BgmManager_SetVolume` (`0x9b3a0 (f32 volume,
  u32 frames)`: `Sound_SetVolume` on every playing kind 1 instance; the event action `SetBGMVolume`),
  `BgmManager_MuteZone` (`0x9b420 (u32 frames)`: fades the kind 1 instances out; the item-get's, ITEMS.md) and
  `BuffMusic_Play` (`0xaf560 (bool)`: `BGM_NUTS_INTRO`, kind 3). A door to another level (Tarin's house) runs
  `BgmManager_StopAll` and `BgmManager_FadeZone` from its event before the load, and the new level's start calls
  `PlayZoneBgm` (2026-10-17, sandbox).
- `envSePlayer` (`EnvSePlayer`, 0x2900; `EnvSe_Ctor` `0x97840`, destructor `0x985b0`, `EnvSe_StopAll` `0x98620`):
  the environment's SEs, run by the update. 12 `AmbientLine`s (`SE_AMB_LINE_BG1` to `12`, rows at `0x1b58c38`;
  the volume by the listener's distance from the segment, `Sound_PlayAmbient` `0xa5ec0`); 3 forest insect spots
  (`EnvSeEmitter`s placed by ObjLawn's Idle update `0xbf1e00`: within 2 to 10 of Link it takes a free one for
  `SE_AMB_FOREST_INSECT`, `2` or `3`, picked at random, at the lawn's position); 64 torch and lamp emitters (`EnvSe_AddLamp`
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
  or queued. `machine` (`StateMachine<2>`) runs them: state 0 (`SoundLoader_StartNext` `0xa7a80`) loads the first
  request at once, or with `async` posts a job (`0xa8a30`, `SoundLoader_LoadNow`) to `task`, a one-thread
  `ThreadPool` (thread factory `0xa7640`); state 1 (`0xa7e40`) waits until the slot is loaded.
- `outputTv`, `outputSpeaker`, `outputJack`, `outputUsb`: `AudioTvOutput`, `AudioBuiltInSpeakerOutput`,
  `AudioStereoJackOutput`, `AudioUsbDeviceOutput`.
- `zoneTrack` and `zoneVolume` (1.0): `PlayZoneBgm` (`0xa6790 (bool fadeOthers)`, the event action `PlayZoneBGM`)
  plays them at once and fades the other kind 1 instances out over 90 frames, but fades nothing while a Guardian
  Acorn's or Piece of Power's music plays. The zone update and `PlayZoneBgm` ask for the track `ZoneTrack_Choose`
  (`0xae390 (out StringView*, StringView* zone track)`) gives: some zone tracks map to variants by the game's flags
  (`BGM_TARUTARU` plays `BGM_TARUTARU2_AFTER_THE_RESCUE`), and while `BGM_FIELD_NORMAL_INTRO`, `BGM_GAME_OF_RAFT`,
  `BGM_RAFTING_TIMEATTACK`, `BGM_DUNGEON_BOSS_MIDDLE` (under a further check) or `BGM_FIELD_MARINE` plays, that
  track, which `BgmManager_Play` then leaves playing. The race's comes as `GM_RAFTING_TIMEATTACK` (a name one
  character short), an unknown name, with the same effect (2026-10-31, sandbox).
- The rapids game (the event system actor `RapidflowGame`, set up by `RaftShopMan.bfevfl`; mode 1 the ride, 2 the
  race): the ride's music starts in `RapidflowGame_PlayRideBgm` (`0xdf2840`: `BgmManager_FadeZone`, then
  `BGM_GAME_OF_RAFT` unless it plays), the race's in `Common.bfevfl` as Link boards the raft (`StopAllBGM`, then 2 s
  later `PlayBGM` `BGM_RAFTING_TIMEATTACK`). `RapidflowGame_Finish` (`0xdf2360`, down the last waterfall) stops every
  playing kind 1 instance itself (`fadeOutFrames` 90, `Sound_Stop`) and plays `BGM_FIELD_NORMAL`, its call returning
  to `0xdf24d8`; `RapidflowGame_StopBgm` (`0xdee710`) makes the same stop while the ride's or the race's track plays, and plays nothing. Measured
  2026-10-31 (sandbox, the race).
- `envSe` and `envGroup`: the environment SE and the sound group it needs, the zone record's (`+0x74`, `+0x94`) or an
  event's. `BgmManager_OnZoneChanged` (`0x9e460`, on the zone signal) sets them, plays the zone BGM with `startDelay`
  150 (0 with `zoneBgmNow`, which it then clears), fades the other kind 1 instances out over 90 frames and changes the
  effect. `BgmManager_PlayEnvSe` (`0x9f980`) plays
  the environment SE (kind 4) and its `_EX` (kind 5) once the group's slot is loaded. `BgmManager_SetEnvSound`
  (`0xa6180`, an event action's `label` and `groupLabel`) saves the current ones in `savedEnvSe` and `savedEnvGroup`,
  `BgmManager_RestoreEnvSound` (`0xa70a0`) plays them again; a zone change clears the saved ones.
- `holdTrack` (`BGM_FANFARE_BOSS_HEART_GET` from the constructor): `BgmManager_FadeZone` (`0xa6510`, an event
  action's) fades the kind 1 instances out over 90 frames, but nothing while it or `BGM_EVENT_RESCUE_BOWBOW` plays (`BgmManager_IsPlaying` `0x9ba40`: a starting or playing instance with
  that track id).
- `eventStartSub`, `eventEndSub` and `zoneSub`: handles on the event manager's `+0xe8` and `+0x140` signals and the
  scene's `+0x760` (`BgmManager_ConnectSignals` `0x9dba0`; `BgmManager_SceneEnd` `0xa0d80` disconnects them).
  `BgmManager_OnEventStart` (`0x9e1c0`) pauses the playing SEs and sets `eventQuiet` (not for the flow
  `ZeldaScroll`); `BgmManager_OnEventEnd` (`0x9df30`) undoes both.
- `quiet`: BGM and SEs play at 0.2 and the positional update stops. Nothing sets it: the constructor (`0x9d760`),
  `BgmManager_Shutdown` (`0x9d350`) and `BgmManager_SceneEnd` (`0xa0e68`) only clear it.
  `eventQuiet`: SEs at 0.2. `zoneBgmNow`: set by a retry, a game over, warps and a file load.

**SeInstance** (`SeInstance_vtable` `0x1b59260`, 0x160 bytes; `SeInstance_Setup` `0xaab80` fills it from a play
request, `SeInstance_Release` `0xac2c0` clears it): `kind` 2 a 3D one-shot (`Sound_PlayAt`), 3 an ambient line or
wind, 4 the zone's environment SE, 5 its `_EX`. `flags` are the SE table's: bits 0-2 no restart within 3, 16 or 30
frames of a same-id SE, 3-5 the cooldowns, 6-7 volume caps 0.5 and 0.7, 8 ducking, 14-16 dropped beyond 5, 10 or 15
from the listener, 17 at most 3, 18 the volume divided by the count, 22 bus 1, 27 dropped within 3.5 of a same-id SE.
The sound's volume is `volume` × `volumeScale` (1, the duck volume or a cap). `actor` is a caller's nn::atk 3D actor,
`ownActor` one made for a 3D SE without; `onUpdate` and `onUpdate2` run each frame (kinds 2, 3 and 5 install
lambdas); `echoArea` is the delay's area test at the SE's position. A kind 2 SE in the echo area without flags bit
13 gets a `wobble` (`BgmManager_Update`): from 0.95, `onUpdate2` (`0xab460`) steps it by 0.006 between 0.9 and 1.02
and its `apply` (`0xab5a0`) sets the sound's pitch. `serial` (-1 free) is the play's: a play's out
argument is an `SeRef` `{instance, serial}`, valid while the instance's `serial` is still its own.

**BgmInstance** (vtable `0x1b58cf0`: `+0x0` `BgmInstance_Dtor` `0x9a400`, `+0x8` deleting `0x9a4b0`; 0x130 bytes,
the stride of `BgmPlayer::instances`). Its base is the sound handle (`SoundHandle`, `SoundHandle_vtable`
`0x1c30a90`, 0x60 bytes, SeInstance's base too): `{vtable, Sound* sound, std::string name}`, unused bytes, and
`fadeOutFrames` (30 at construction), which every `Sound_Stop` of the handle uses. Its destructor
(`0xfa20f0`) clears the sound's back-links (`Sound::handles`) that point at the handle's `+0x8`.

- `BgmManager_Play` fills an instance from the BgmTable row (`BgmTable_Find`, flags `0x10e24b0`): `onStart` with the
  row's flags bit 0 (lambda vtable `0x1b58de8`), called once when the stream starts; `onUpdate[0]` with bit 1
  (`0x1b58e30`, `BgmInstance_UpdateHeight` `0xa0480` through its thunk `0xa0470`: the volume by Link's height,
  reading Link's model node, Framework `+0x4b8` then `+0x80`, with no null check; `BGM_TARUTARU2_AFTER_THE_RESCUE`
  is one such track), `onUpdate[1]` cleared; both run
  every frame while playing. With `prepareNow` (its last argument) it
  prepares the stream at once (`BgmInstance_Prepare` `0x99800`) and on a failure returns null without queuing the
  instance for its start.
- `kind`: 1 zone and event music; 2 a jingle (when Play's first bool is set): kinds 1 and 4 are `ducked` to 0 over 45
  frames while one plays and come back over 150; 3 a buff's music; 4 positional: `BgmInstance_SetPosition`
  (`0x998e0`, from `PlayZoneBgm`) sets `position`, `radius` 3, `fadeRange` 9 and `onUpdate[0]`
  `BgmInstance_UpdatePositional` (`0x99b70`: the volume by the listener's distance, kind 1 instances get 1 − it;
  volume 0 while `noPositional`). NpcMarin's singing (`0x9e6a60`) sets `noPositional` on the playing kind 4
  instances, and clears it while `BGM_MEVE`, `BGM_ANIMAL_VILLAGE` or a `BGM_EVENT_RESCUE_BOWBOW` track plays.
- `flags` bit 2 marks a buff's music (`PlayZoneBgm` tests it); `volume` (`BgmManager_SetVolume` `0x9b3a0` writes
  it); `pitch` (1.0): `BgmInstance_Start` passes it to the sound (`0x99238`).
- `startDelay`: the frames the starting list waits before `BgmInstance_Start` (`0x99130`) starts the stream, which
  passes `startOffset` (in samples) as the stream's start offset when it is set. The game writes it only as 0
  (`BgmManager_Play`, the constructor and the instance resets).
- The instruments' own songs, `BGM_INST_VIOLIN` (Full Moon Cello) to `BGM_INST_DRUM` (Thunder Drum), play only in
  the pedestal's whiteout (`MusicalInstrument.bfevfl`, `PlayAndWaitMusicalInstrument`); every instrument's
  `Items.gsheet` row names the shared fanfare `BGM_FANFARE_INST_GET`.
- The Ballad's streams (`Orchestra_Play`): `BGM_ENSEMBLE_DUET` and `BGM_ENSEMBLE_ALL` are stereo mixes of their own;
  `BGM_ENSEMBLE_INST` is eight mono tracks, one per instrument in dungeon order (Full Moon Cello first), panned in
  the sound archive. Every track is an instrument's part: with no instrument the Wind Fish's Egg's scene plays only
  the Ballad's ocarina intro (`BGM_OCA_PLAY_FISH_OF_WIND`), then silence.
- Stream channels: the sound archive's player info lets all streams together hold 10 channels. Every BGM is a
  2-channel stream but `BGM_ENSEMBLE_INST` (8), `BGM_FANFARE_BOSS_HEART_GET`, `BGM_TELEPHONE`, `BGM_PLACE_OF_FACE_KEY`
  (4 each) and `BGM_DEMO_BEFORE_ENDING_SE_MIX` (5), so beside the ensemble only one more stream fits. A stream that
  starts without room gets no sound and the BGM player drops it (a fanfare, the ocarina's song, or the ensemble
  itself). A stream fading out holds its channels until its fade ends.

**Sound** (`nn::atk::detail::BasicSound`, by its RTTI; `BasicSound_vtable` `0x1c53248`, `BasicSound_Ctor` `0x10e93b0`;
0x210 bytes, abstract) is the base of the nn::atk sounds, whose instance pools' strides give their sizes: StreamSound
0x11d40 (every BGM is a stream), WaveSound 0x3f0, SequenceSound 0x5a0, AdvancedWaveSound 0x9b0. Its field names and order are
those of BasicSound in a decompiled nn::atk of a later SDK (Super Mario 3D World + Bowser's Fury decomp, also 0x210
bytes); the constructor's defaults, `BasicSound_Initialize`, `BasicSound_Finalize`, `BasicSound_Update` (`0x10e9a40`),
`BasicSound_Pause` (`0x10e9920`), `Sound_Stop`, `SoundPlayer_SortPriority` and the setters (`0x10ea8b0` `initVolume`,
`0x10ea950` `surroundPan`) match it. Its virtuals: `+0x0` the type info (`0x10e8620`), `+0x8` the destructor
(`0x10eab60`), `+0x18` `BasicSound_Initialize` (`0x10e9470 (sound, output)`), `+0x20` `BasicSound_Finalize`
(`0x10e9620`), `+0x28`..`+0x40` pure (prepared, temporary special handle, its driver player), `+0x48` the priority hook
(empty), `+0x50` `BasicSound_UpdateMoveValue`, `+0x58` the param hook (empty). `BasicSound_Update` passes `ambientParam`
through `ambientInfo`'s param callback, copies `actorParam` from `soundActor`, starts a prepared sound when
`startRequested` is set, counts `playFrames` while not paused and finalizes once a fade-out ends. `Sound_Stop`
(`0x10e97f0`) clears `playerPriority`, re-sorts the sound in its `player`'s priority list (`SoundPlayer_SortPriority`
`0x10e5dd0`, by `ambientParam.priority` + `playerPriority` clamped to 0..127) and calls virtual `+0x48`, then sets
`fadingOut`, fades out over `stopFade` and clears `autoStop`, `pauseState`, `muteState`, `unpause` and `pauseMode`; it
finalizes at once instead for 0 frames, a `pauseState` of 2, or neither `startRequested` nor `started` set.
`Sound_FadeFramesLeft` (`0x10ea8a0`) is `stopFade`'s frames left, 0 when done; `BgmManager_Play` uses it to tell a track
still fading out. `Sound_SetVolume` (`0x10ea8c0 (sound, frames, volume)`) sets the `volume` fade. The pause menu sets
every BGM's target volume to 0.2 (a track that starts while it is open gets 0.2 at once) and back to 1 over 8 frames as
it closes. Every BGM volume change goes through `Sound_SetVolume`: `BgmInstance_Start` (the instance's `volume`
at once, 0 when a jingle plays), the positional update, `BgmManager_SetVolume`, the jingles' ducking and its end, and
the pause menu's (a hook on it held every BGM at 0, 2026-10-31).
