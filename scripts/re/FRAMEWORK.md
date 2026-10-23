# Framework, camera and audio (game 1.0.0)

The framework the game runs in (its lifecycle, the scheduler and the frame's phases), the camera view and the audio manager, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/framework.hpp` and `game/audio.hpp`, which list every field; this file gives the evidence and what their comments leave out. "Guess" marks what the code suggests but was not followed to the end. The saved data (the save image, its header and the current data) is in ITEMS.md, Saved data; what the actor phases run is in ACTORS.md, Entity manager and the frame.

## Framework

`Framework_ptr` (`0x1c97188`) is `{Framework* framework, void (*deleter)(Framework*)}`: a unique pointer with a function deleter, which an exit destructor (`0x5320`) calls. The getter is inlined everywhere: without a framework it allocates 0x6c0 bytes (`operator_new(0x6c0)` at every such site, `0x1bca0`, `0x846cd0`, `0x855d1c`), runs `Framework_Ctor` (`0x7370`) and sets the deleter `Framework_Delete` (`0x1bcf0`: `Framework_Dtor` `0x9010`, then free). The framework has no vtable.

**Lifecycle.** The game's entry loop (`0x1bc34`) runs `Framework_Init` (`0x9410`), `Framework_MainLoop` (`0x1b920`), then `Framework_Shutdown` (`0x1ab30`), and starts at the init again while `restart` is set (its writer is not found). `Framework_Init`, in order: the present setup; `systemEvents` (`0xa980`, `0xfa1e60`); `controller` (`0xa9f0`); `Framework_InitGraphics` (`0xaa40`); `0xb6a0` (`field_5a8`, `sp_5b0`); `Framework_InitResourceListener` (`0xb990`); `ActorSystem_Make` (`0xba30`, `actorSystem`); `0xbde0`; `Audio_Init` (`0xbf00`); `PlayReports_Init` (`0xdf6870`, `playReports`); the frame step's task in `frameQueue` (`0xbf90`: lambda vtable `0x1b502d0` → `Framework_FrameStep` `0x17e30`); the 14 update and 5 render phases (below); `gameState` (`GameStateFactory_Create` `0xc450`). `Framework_InitStorage` (`0xa4a0`) fills `romMount` (`"rom"`), `regionResources` (`region_common`, `common`, `regionJP`, `JPja`), `heapMemory` (a block of (total − 0x6da00000) & ~0xfff bytes, freed through `heapMemoryFree`, `0x5390` by default) and `mainHeap` (0x53600000 bytes in it, vtable `0x1c308c8`).

**Main loop.** Each pass runs `frameQueue` (`PhaseQueue_Run` `0x11cc0`) and counts `frameCount` unless `paused` or `roomPaused` is set; the loop ends when the game state machine's byte `+0x80` is set. A set bit 0 in `systemEvents`' first byte makes it call `SetGameState(gameState, 0, 0)`.

| Field | |
|---|---|
| `field_50`, `field_54`, `field_58` | 1, 14 and 5 at construction (guess: the queues' sizes) |
| `actorSystem` | the entity container the factories' spawns from code hang in (ACTORS.md, Creation): 0x148 bytes, no vtable: `+0x0` weak self, `+0x10`/`+0x38`/`+0x60` child maps, `+0x88` a `std::function` listener, `+0xc0` and `+0x118` mutexes, `+0x140` active (1). The shutdown empties it (`Entity_RemoveChildrenIf`) |
| `gameState` | the game state machine: a 0xe0-byte StateMachine task (vtable `0x1bc4300`, base constructor `0x82ee90`) registered with the scheduler. Its `+0x88`/`+0x90` shared pointer is the current state, the scene (`SceneHolder_Lock` `0x11b70` checks its type with virtual `+0x28`); its byte `+0x80` ends the main loop |
| `player` | Link (PLAYER.md); about 500 places load the pair `+0x4b8`/`+0x4c0` |
| `resourceListener` | an interface (vtable `0x1c31138`) registered with the resource manager |
| `renderCallbackId` | the graphics callback the graphics init registers (graphics `+0x83c0` counts them); `Framework_TeardownGraphics` (`0x1b1d0`) unregisters it |
| `presentCount`, `lastPresentVsync`, `skipRenderFrames`.. `presented` | `Phase_Present` (`0xc910`, render phase 4) counts presents, records the vsync counter (graphics `+0x81ac`) + 1 and sets `skipRenderFrames` from the vsyncs missed, clamped to `minSkip`..`maxSkip` (0 in levels: `0x854624`, `0x854c14`; 1 at construction); `Phase_EndOfUpdate` (`0x11960`) counts it down, flushing instead of rendering, and clears `presented` |
| `presentInitDone` | the one-time present setup (the camera's `field_301` cleared, the skip count to `minSkip`), also inlined at `0x8545e4`, `0x8379a4`, `0x8646ec` |
| `zoneFlagB5` | copied from the current zone's `+0xb5` (`0x7fe4e4`, `0x865fdc`; likely) |
| `field_4f0`, `field_500` | (FLT_MIN × 3, 0) and (FLT_MAX × 3, 0) at construction, set by the graphics init |
| `paused` | 1 at construction; the pause menu or a level load (ACTORS.md, Entity manager and the frame) |
| `roomPaused` | set by `Scene_UpdateZone` (`0x8364a4`, `0x8364f4`) |
| `controller` | make_shared of 0x570 bytes (constructor `0x87f850`; UI.md, Input) |
| `gfxMemory` | 0x2ed80000 bytes from the heap `0x1cab190`, for graphics |
| `renderer` | 0x58 bytes (vtable `0x1c466c0`, made by `0x18220`): `+0x20` a vector of views (`Renderer_AddView` `0x100f7a0`, virtual `+0x48`), `+0x40` the environment (`Renderer_SetEnv` `0x100fe60`); `Renderer_FindView` (`0x100fcd0`, `+0x60`, `(out weak, renderer, id)`). The skeletal model components' model list (MODELS.md) and the list `InstancedModel_Ctor` takes |
| `drawRoot` | the camera's draw node tree (its `+0x2d8`) |
| `camera` | the renderer's view with id 1 (below) |
| `view2` | the view with id 2: 0x5e0 bytes (vtable `0x1b50c68`, constructor `0x18d50`), orthographic (`ownMatrices` 1, an identity view) |
| `renderEnv` | 0x290 bytes (constructor `0xff94d0`; its init `0xff9880` loads `shader/ResetData_cube.bntx`); the renderer's `+0x40` |
| `field_5a8`, `sp_5b0` | a 0x2000000-byte buffer handed to `0x1cd3668`; an object from `0x10c5730`, configured twice by `0x10c6790` (callbacks `0x829920`, `0x8299d0`) |
| `playReports` | 15 PlayReport objects (0x58 bytes, constructors `0xdf6d40 + i × 0x70`): 0 clear, 1 dead, 2 get_item, 3 play_minigame, 4 warp, 5 fishing_result, 6 mappin, 7 panel_dungeon, 8 panel_plus, 9 panel_theme, 10 amiibo_touched_id, 11 amiibo_get_panel, 12 amiibo_touched_count, 13 amiibo_write_dungeon, 14 amiibo_read_dungeon |
| `systemEvents` | 0x118 bytes (constructor `0xfa01a0`; init `0xfa1e60` sets four nn::oe notification modes); its `+0x18` takes a callback (`0x18af0`) that resizes the graphics on an operation mode change |
| `present` | initialized by `0x107cb90`: `+0x0` active (1), `+0x1` the maximum present interval (0xff at construction, 1 in levels, clamped with graphics `+0x812c`), `+0x10`/`+0x28`/`+0x58` vectors, `+0x40` a weak pointer (likely) |

**How the module reads the framework.** Two of its probes read the framework's memory in ways the game does not:

- The module's screen (`ScreenOf`, `kScreen*` in `game/framework.hpp`) is the bucket count of the scheduler's live-task hash set: `Scheduler::tasks` at Framework `+0x60` `+0x48`, its bucket count (a u64) at Framework `+0xb0`. libc++ grows a bucket count through the primes 2, 5, 11, 23 as the set grows, and the intro and title, the file menus, a level load and a level reach those four. It counts live tasks, not screens, so it tells the screens apart only as a heuristic.
- The module's intro probe (`kIntroProbe`, Framework `+0x6d0`) lies 0x10 bytes past the end of the 0x6c0-byte framework, in the heap block after it. The pointer seen there while the intro plays belongs to whatever the heap put in that block.

## Scheduler and phases

**Scheduler** (`Scheduler_Ctor` `0xfced30`, destructor `0xfcfeb0`; 0x150 bytes): `Task_Post` (`0x2d7430`) and `Delay_Make` (`0x7f3c90`) add tasks through `Scheduler_Add` (`0xfcff00 (scheduler, task sp*, u32, name*)`), which inserts into `tasks`, an `unordered_set` of task pointers (`+0x48` buckets, `+0x50` bucket count, `+0x58` first node, `+0x60` size, `+0x68` f32 maximum load factor 1.0). `byOrder` is a `std::map`; `object_98` and `object_f0` are made by `0xfcee00`.

**PhaseQueue** (`PhaseQueue_Ctor` `0x76e0`, destructor `0x7ab0`, `PhaseQueue_Run` `0x11cc0`; 0xf8 bytes): `byPriority` is a `std::map` by priority, `phases` an `unordered_map`, `deferred` holds the changes made while it runs (`0x8700`). The entity manager's queues have the same shape (ACTORS.md).

**Phases.** `Framework_AddUpdatePhase` (`0xc0e0 (framework, id, priority, std::function*)`) adds to `updatePhases`, `Framework_AddRenderPhase` (`0xc290`) to `renderPhases`; each queue runs in ascending priority. `Framework_FrameStep` runs the update phases; the last, `Phase_EndOfUpdate`, runs the render phases.

| Queue | Id | Priority | Lambda | |
|---|---|---|---|---|
| update | 0 | `0x0` | `0x17800` | |
| update | 1 | `0x4e20` | `0x174d0` | |
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
| render | 0 | `0x9c40` | `0x10540` | |
| render | 1 | `0xc350` | `0xd6c0` | the render-side calculation queue (ACTORS.md) |
| render | 2 | `0xea60` | `0xd540` | |

## Camera

The camera (`Camera`, `Framework::camera`) is the renderer's view with id 1: `View_Ctor` (`0x190a0 (view, u32 id, drawRoot sp*)`), destructor `View_Dtor` (`0x19690`), deleting `0x1026af0`, vtable `0x1c46db0`. `0x17fe0` makes it, 0x350 bytes from the graphics allocator (`*Graphics_ptr`'s virtual `+0x20`), and `0x19c70` wraps it in a shared pointer (control block vtable `0x1b50db0`).

- `View_Update` (`0x1025c30`, virtual `+0x50`) copies each frame from the game camera `source` (a weak pointer): `view` from its `+0x10`, `inverseView` `+0x50`, `projection` `+0x90`, `fovY` `+0xf8`, `nearZ` `+0x100`, `farZ` `+0x104`, `frustum` `+0x110` (0xf0 bytes). A view with `ownMatrices` (`view2`) computes its orthographic projection from `nearZ`, `farZ` and `aspect`, the inverse from `view`, and the frustum through `0x1133b60`. The constructor's defaults: `nearZ` 0.2, `farZ` 1000, `aspect` 16/9, `field_1d0` 1, `field_2d0` -1, `field_301` 1.
- `View_Setup` (`0x1025200`, `+0x20`, `(view, arg, width, height)`) makes `renderObject` (0x470 bytes, vtable `0x1c47c60`) and the render target `target` (constructor `0x114abc0`, made by `0x114abf0`, freed by `0x114ad80`), keeps `setupArg`, `width` and `height`, and sets `setUp`; `+0x28` (`0x1025800`) releases them.
- `drawables`: virtuals `+0x30` add (`0x193e0`) and `+0x38` remove (`0x19580`); `+0x48` (`0x19590`) calls each one's virtual `+0x20`; `+0x40` (`0x1025c10`) clears `drawRoot`'s draw lists.
- `field_301` is cleared at the first present (`Framework_Init`) and by `WarpMap_FieldInUpdate`.

`WorldToLayout` (`0xf88c40`) projects through `view` and `projection` (TESTING.md, WorldToLayout).

## Audio

**BgmManager** (the static `Game::BgmManager`, `0x1ca1d70`, its guard `0x1cab240`) is the game's whole audio manager. `BgmManager_Ctor` (`0x9d3b0`) writes up to `+0x9418` and the next static, the audio heap allocator `0x1cab190`, starts at `+0x9420`: 0x9420 bytes. `Audio_Init` (`0xbf00`) registers its destructor (`0x9d220`, which shuts the audio down through `0x9d2c0` while `initialized` is set) and allocates `audioHeap` (0xe400000 bytes) and `audioHeap2` (0x3000000).

- `se`: the sound effects' players (constructor `0xac440`, destructor `0xac720`): a pool at manager `+0x5860`, the active ones at `+0x5890`, a play serial at `+0x58bc`; `Sound_Play` and `Sound_PlayAt` (`0xa5c70`) take a player from the pool.
- `bgm` (`BgmPlayer_Ctor` `0x99f90`, destructor `0x9a4e0`): `BgmManager_Play` (`0x9fd60`) takes the last of `free`, keeps it in `starting` while its stream is prepared, then in `playing`; fades and stops by name reach only `playing` (TESTING.md, `0x9fd60`). `vector_9d8` is not traced.
- `field_62b0`: a sub-object (constructor `0x97840`, destructor `0x985b0`).
- `ambience` (likely): the zone's ambience (constructor `0xa7380`, destructor `0xa7760`): four slots (their name lengths at manager `+0x8c58`, `+0x8d50`, `+0x8e48`, `+0x8f40`, a state at each slot's `+0x51`) and a vector of 0xa0-byte entries at manager `+0x8f98` (name `+0x48`, length `+0x88`), played by `0xa8a80` from the zone update's lambda (`0x9e460`).
- `outputTv`, `outputSpeaker`, `outputJack`, `outputUsb`: `AudioTvOutput`, `AudioBuiltInSpeakerOutput`, `AudioStereoJackOutput`, `AudioUsbDeviceOutput`.
- `zoneTrack` and `zoneVolume` (1.0): `PlayZoneBgm` (`0xa6790`).
- `holdTrack` (`BGM_FANFARE_BOSS_HEART_GET` from the constructor): `BgmManager_FadeZone` (`0xa6510`) fades nothing while it or `BGM_EVENT_RESCUE_BOWBOW` plays (`BgmManager_IsPlaying` `0x9ba40`: a starting or playing instance with that track id).
- `field_9388` and `field_93d0` are cleared by the zone update's lambda.

**BgmInstance** (vtable `0x1b58cf0`: `+0x0` `BgmInstance_Dtor` `0x9a400`, `+0x8` deleting `0x9a4b0`; 0x130 bytes, the stride of `BgmPlayer::instances`). Its base class (vtable `0x1c30a90`) is the sound handle `{vtable, Sound* sound, std::string name}`; its destructor clears the sound's back-links (`Sound::handles`) that point at the handle's `+0x8`. `BgmManager_Play` fills an instance from the BgmTable row (`BgmTable_Find`, flags `0x10e24b0`): `onEvent[0]` when the row's flags bit 0 is set (lambda vtable `0x1b58de8`), `onEvent[1]` with bit 1 (`0x1b58e30`), `onEvent[2]` cleared; `kind` 1 zone and event music, 2 when its first bool is set, 3 a buff's music; `flags` bit 2 marks a buff's music (`PlayZoneBgm` tests it); `volume` (`BgmManager_SetVolume` `0x9b3a0` writes it); with `prepareNow` (its last argument) it prepares the stream at once (`0x99800`) and drops the instance on a failure (likely). `pitch` (1.0) and `position` (the default `*0x1c93de0`, the one `Sound_Play` passes) are guesses; `fadeOutFrames` is 30 at construction.

**Sound** (its size and class are not found): `Sound_Stop` (`0x10e97f0`) calls virtual `+0x48`, tells `owner` (`0x10e5dd0`), needs one of `active` set, sets `state` (2: stopping already) and resets `active`, `+0xee`, `+0xef`, `+0xf1`, `+0xf4`, `+0xf8`. `Sound_FadeFramesLeft` (`0x10ea8a0`) is `stopFrames − stopFramesGone`, 0 when done; `BgmManager_Play` uses it to tell a track still fading out. `Sound_SetVolume` (`0x10ea8c0 (sound, frames, volume)`) sets `target`, `volumeFrames` and `volumeFramesGone`.

## Open questions

- Framework `field_48`..`field_58`, `field_4e8`, `field_4f0`/`field_500`, `sp_530` and `sp_580` (only released), `field_5a0` (only cleared), what `0x10c5730` makes, the writer of `restart`.
- Camera `field_200`..`renderObject`, the meaning of `field_301`, `field_308`..`field_320`.
- BgmManager `field_0`..`field_40` and `field_62b0`; BgmInstance `field_28`..`field_54`, `pitch`, `position`; `Sound`'s class, vtable and size.
