# Events (game 1.0.0)

How events are queued and run, how a flowchart's actors, actions and queries reach the game's code, and what an event's start does to the actors, from static analysis of the main module and a probe in the test build. Offsets are module offsets; `las.sym` names them at their runtime addresses (module offset + 0x8504000). The module's `game/event.hpp` types the structures below.

The game runs Nintendo's evfl library, the runtime of the `.bfevfl` format the patcher's evfl package reads (strings `FlowchartContext:Run()`, `kSuccess, kInvalidOperation, kResFlowchartNotFound, kEntryPointNotFound`). The open-ead project's evfl reimplementation (for Breath of the Wild) has the same `FlowchartContext`, `FlowchartContextNode` and `ActorBinding` layouts; its names are used below.

## Event manager

`EventManager_ptr` (`0x1cb24d8`): 0x1e0 bytes, made on first use (`EventManager_Get` `0x81c070`, `EventManager_Ctor` `0x81c100`, deleted by `0x81c430`); `Framework_Init` makes it.

| Offset | |
|---|---|
| `+0x8` | vtable `0x1bc1910` |
| `+0x10` | a resource listener (vtable `0x1bc1940`), registered with the resource manager by the constructor |
| `+0x18` | a resource holder: `Event_AddChara` loads `event/common.evhb` from it (Actor map, Class hierarchy) |
| `+0x28` | `queueStarted`: a `Signal`, emitted when a request enters an empty queue, before its event starts |
| `+0x80` | `queueEnded`: emitted once the last request has finished |
| `+0xd8` | `eventStarted`: emitted before each event starts |
| `+0x130` | `eventEnded`: emitted when an event's request finishes, before the next starts |
| `+0x188`, `+0x198` | two event camera actors (shared pointers; `EventCamera_Ctor` `0x7dc6a0 (camera, args, u32 id)`, vtable `0x1bbbce8`: a transform, then 10, 0.349, 0.1, 4096 and 16:9, likely a field of view, near and far planes and an aspect), disabled (flags bit 0) when the queue empties; during a talk neither is enabled; `Link.GenericItemGetSequenceByKey` enables the first as it starts, in the beach chest's flow and in the module's item-get (a test build compared the cameras' enabled bits around each action `Flow_RunAction` ran; measured 2026-10-30). `EventManager_MakeCameras` (`0x823340 (manager)`, called once by `Framework_Init`) makes both in the actor system (`Framework` `+0x498`), named `EventCameraLocator_1P` and `EventCameraLocator_2P` |
| `+0x1a8` | the requests: a libc++ deque of `shared_ptr<EventRequest>` (map `+0x1a8`..`+0x1c0`, blocks of 256; `+0x1c8` the first element's index, `+0x1d0` the count, 0 while no event runs) |
| `+0x1d8` | `cancelAll`: the next request end cancels every queued request |

Code reaches a signal through its `owner` field (signal `+0x10`, the signal itself) and calls the vtable from there: `Actor_RegisterEvents` loads `+0xe8` and `+0x140`, the owners of `eventStarted` and `eventEnded`; callbacks that pause something for the whole queue load `+0x38` and `+0x90` (`0x76910` and `0x76840`, which the actor setup `0x73920` connects: they disable its `+0x2b8` component and enable it again).

**Requests.** `EventManager_Request` (`0x81f2e0 (out request sp, manager, event sp*)`, body `EventManager_RequestImpl` `0x81f310`) returns null unless the level's room manager is in state 1 (running). It makes an `EventRequest` (make_shared, 0xa8 bytes; the object 0x90 bytes, vtable `0x1bc2240`, a second at `+0x88` `0x1bc2298`):

| Offset | |
|---|---|
| `+0x8` | `finished`: a one-shot `Signal` (the tasks' vtable `0x1b596b0`) |
| `+0x60` | the event (shared pointer) |
| `+0x80` | `cancelled` |

Its virtuals: `+0x10` the connector of `finished`, `+0x18` finished (`finished` closed), `+0x20` `EventRequest_Cancel` (`0x81f2b0`: once, sets `cancelled` and finishes the event's task unless the event's `+0x181` is set or the task is done), `+0x28` `cancelled`. The request goes to the back of the queue; when the queue was empty it emits `queueStarted` and starts it (`EventManager_StartRequest` `0x81fd20`). Then it connects `EventManager_OnRequestFinished` (`0x820bc0`, lambda `0x1bc1fa8` over the manager and the request) to `finished`.

`EventManager_StartRequest` (`0x81fd20 (out, request)`): a cancelled request emits its `finished` at once. Otherwise it emits `eventStarted`, unbinds the flow (`Flow_UnbindAll` `0x1090690`), binds the event's actor map to it (`Flow_Bind` `0x1089270`, Binding), starts the flowchart context at the entry (evfl's `FlowchartContext::Start`, inlined in `Flow_Start` `0x1090a40`) and posts its run as a scheduler task (`FunctionTask_Make` `0xb03a0` on the framework's scheduler, name `FlowchartContext:Run()`), kept in the event's `+0x40`.

`EventManager_OnRequestFinished` emits `eventEnded` and pops the request. Unless it was cancelled, it runs the event's pending level jump (`Event_ProcessLevelJump`). With `cancelAll` set it cancels every queued request. Then it starts the next request, or, with the queue empty, sets the interaction cooldown (`0x1cacc88`) to 0.5 s, disables the event cameras (`+0x188`, `+0x198`) and emits `queueEnded`. While the cooldown runs, `Interaction_Target` gives no target, so the talk and examine prompts stay off; `Interaction_Update` (`0xe8be0`) counts it down by the frame time.

## Event

`Event_Make` (`0x80c290 (out, flowchart resource sp*, StringView* entry)`) builds the event (400 bytes, make_shared by `0x80caa0`). It throws (Japanese messages) when the resource is invalid, holds no flowchart, or the flow context cannot be made.

| Offset | |
|---|---|
| `+0x10` | the flow (Flow) |
| `+0x20` / `+0x28` | the entry's name as a shared string |
| `+0x30` | the entry's name (data, length), as `BgmManager_OnEventStart` compares it with `ZeldaScroll` |
| `+0x40` | the `FlowchartContext:Run()` task (a pointer into the task object, whose task base is virtual) |
| `+0x50` | the actor map (Binding): libc++ `unordered_map` by name (buckets, bucket count, first node, size, max load factor 1.0 at `+0x70`) |
| `+0x78` | the charas in the event: an `unordered_map` keyed by EventChara pointer (`Event_HasChara` `0x81aac0`) |
| `+0xa8` / `+0xe8` | the pending level jump's level: up to 0x3f characters and a NUL, its length |
| `+0xf0` / `+0x130` | its locator, the same way; a length above 0 means a jump is pending |
| `+0x140` | its offset (0x10 bytes, offsetX and offsetZ) |
| `+0x180` | a kind, which `Player_OnEventStart` turns into Link's `eventKind` |
| `+0x181` | set: cancelling the request leaves the task running |

The level jump is the flowchart's `GameControl` actor's: `GameControl.RequestLevelJump` (`0x7ff160`) and `WarpUI.JumpToSelectedLocation` write it through `Event_SetLevelJump` (`0x81b950 (event, StringView* level, StringView* locator, offset*)`), and the request's end runs it (`Event_ProcessLevelJump` `0x81ba80`): locator `$RETRY` (game state 4, health refilled to 3 to 10 hearts by the maximum), `$QUIT` (title), `$ENDING`; else `LevelManager_LevelJump`, or `LevelManager_WarpInLevel` when the level is empty.

## Flow and the evfl runtime

`EventFlow_Make` (`0x10924f0 (out, flowchart resource sp*, StringView* entry)`) makes the flow, 0x1c8 bytes:

| Offset | |
|---|---|
| `+0x0` | a weak pointer to itself; the context's `+0x78` points here |
| `+0x20` | the evfl `FlowchartContext` (0x98 bytes): its `+0x28` the `FlowchartObj`s (pointer, count; 0x30 bytes each: the flowchart, then the `ActBinder` at `+0x8` with the bindings at `+0x18` and their count at `+0x20`), `+0x38` the nodes (0x20 bytes each), `+0x70` the metadata pack, `+0x78` the user pointer, `+0x88` the current object's index, `+0x8c` the entry's, `+0x90` processing |
| `+0x128` | the flowchart (`ResFlowchart`) |
| `+0x138`, `+0x160` | two `unordered_map`s of `std::function`s (node 0x50 bytes, the function at `+0x20`), emptied by `Flow_UnbindAll`: `+0x138` the action steps by action key, `+0x160` likely the queries' |
| `+0x188` | the pending actions' waiters (an `unordered_set` of shared pointers; Actions and queries) |
| `+0x1b8` | cancelled |
| `+0x1c0` | the event (`Event_Make` sets it) |

A context node (`FlowchartContextNode`, 0x20 bytes): `+0x0` the object, `+0x8` the variable pack, `+0x10` a counter, `+0x14` the event index, `+0x16` the next node, `+0x18` its own index, `+0x1a` a state (0 invalid, 1 free, 2 not invoked, 3 invoked, 4 done, 5 waiting), `+0x1b` owns the variable pack. An actor binding (`ActorBinding`, 0x48 bytes): `+0x0` the actions (an array of 0x18-byte `{handler, user data, ResAction*}`, its count at `+0x8`), `+0x18` the queries the same way, `+0x30` user data, `+0x38` the `ResActor`, `+0x40` initialized, `+0x41` used.

`Flow_FromContext` (`0x108c070 (out flow sp, FlowchartContext*)`) locks the weak pointer the context's `+0x78` points to. The flow's cancel callback (`0x10910d0`, lambda `0x1c4c788`) clears the context once (`+0x1b8`) and finishes the pending waiters.

The flowchart is the `.bfevfl` file loaded and relocated in place (`ResFlowchart`: `EVFL`, counts at `+0x10`, name `+0x20`, actors `+0x28` of 0x38 bytes, events `+0x30` of 0x28 bytes, the entry names' dictionary `+0x38`, entries `+0x40` of 0x20 bytes with the main event at `+0x1c`), with each offset a pointer and each string a u16 length and its characters. The probe found the same flowchart (`SinkingSword`, entry `diamond-island`) at the flow's `+0x128` for a LASR heart piece's event.

## Binding

A flowchart actor is a name (and a secondary name) that the event resolves through its actor map (`+0x50`). Each entry (0x90 bytes, after the key) holds:

| Offset | |
|---|---|
| `+0x0` | user data: the EventChara registered under the name, null for a system actor |
| `+0x8` | a map from the secondary name to user data |
| `+0x30` | `std::function` action lookup: the action's evfl handler (returned through x8) for its `ResAction` |
| `+0x60` | `std::function` query lookup, the same for queries |

`Flow_Bind` (`0x1089270 (actor map, flow sp*)`) hands three lambdas over the map to the bind loop (`0x1090160`): the actor's user data (`0x10898b0`: the entry by name, then by secondary name when there is one), each action's handler (`0x10896a0`: the entry's action lookup with the action's name) and each query's (`0x1089480`). A name the map lacks gets an empty handler.

**System actors.** `Event_Prepare` (`0x80db00 (event*)`) registers 17 entries without user data (`EventActorMap_Add` `0x10887f0 (map, name, secondary name, user data)`) and sets the lookups of those it serves: `EventFlags`, `Inventory`, `Dialog`, `WarpUI`, `GameOverUI`, `PanelResultUI`, `Hud`, `Timer`, `Camera`, `Environment`, `Audio`, `Vibration`, `GameControl` (`GameControl_FindAction` `0x8140e0`), `FlowControl`, `Fade`, `RapidflowGame`, `PanelDungeon`. Then it adds Link's chara (player `+0x2f8`, `0x80ebb0`) and, when one of the flowchart's actors is named `companion` (`0x80ed90`), the companion's (`0x80f160`).

**Charas.** `Event_AddChara` (`0x80f7d0 (event, EventChara sp*)`) skips a chara that is being destroyed or has no name (`+0x378` 0). It registers the chara (`EventActorMap_AddChara` `0x80ff60`) under its name (`+0x358`) and, with a sub name (`+0x380`), under that in the entry's secondary map; then again under each ancestor of its name in `event/common.evhb`. It adds the chara to the event's chara map (`+0x78`) too. Each of these entries has both lookups set (measured), likely forwarding to the chara's own (EventChara).

**Class hierarchy** (`event/common.evhb`, 156 names): a 0x10-byte header (`evhb`, u32 version 1, u32 count), then one 0x10-byte entry per name (`u32` offset of the name in the file, `u32` 0, `u32` length, `s32` parent index, -1 none). The 17 system actors have no parent; `Actor` is the root of `Character` (Link, Marin, Tarin, the other NPCs, `Shadow`, `WindFish`), `Object` (chests, doors, owl statues, `WindFishsEgg`, `SirenInstrument`; `Item` below it: `SmallKey`, `GoldenLeaf`, `Honeycomb`), `Enemy` (the bosses and minibosses, `Octorok`), `EventTriggerTag`, `Area` and `WarpHoleEventBox`. So a flowchart actor `Character` binds to whichever character's chara joined the event last under it.

Measured 2026-10-30 (test build, `DBG event`): the module's item-get (`Common`, entry `APItemGet`) has 20 map entries, the 17 system actors and `Link`, `Character`, `Actor` with Link's chara, and one chara; Link's binding has its one action bound, the system actors' have no user data. The Mabe shopkeeper's talk (`ToolShopkeeper`, entry `talk`) adds `ToolShopkeeper` with its chara (two charas); of the flowchart's two `ToolShopkeeper` actors only the one whose names match a map entry gets user data. In the field about 540 actors are connected to `eventStarted` and `eventEnded`, 25 and 30 callbacks to `queueStarted` and `queueEnded`.

## EventChara

The component that makes an actor a flowchart actor (`EventChara_Ctor` `0xf1730 (chara, ComponentArgs*, StringView* name, StringView* subName)`, vtable `0x1b5c208`, 0x520 bytes): a Component with

| Offset | |
|---|---|
| `+0x60`..`+0x2a0` | 13 `std::function` hooks (0x30 bytes each), below; NPCs fill several (`NpcComponent_MakeEventChara`) |
| `+0x2d0`, `+0x2f0` | two animation names (up to 0xf characters, the length at `+0x10`, a float at `+0x18`); NPCs set `@IDLE` and `@TALK`, -1 |
| `+0x358` / `+0x378` | the name (up to 0x1f characters), its length: a flowchart actor's name binds to it |
| `+0x380` / `+0x3a0` | the sub name, its length: a flowchart actor's secondary name |
| `+0x3a8` | a `Signal` |
| `+0x400` | the action lookup: `std::function<bool(Function* step, const StrView* name)>` |
| `+0x430` | the query lookup |
| `+0x460` | hook: the look target's position (a `Vec4` and a valid bool) |
| `+0x490` | the look mode the look-at hook gets (1 when made) |
| `+0x4a0` | hook: the chara's camera animation by name (up to 0x3f characters), for `PlayActorOriginCameraAnimation` and `PlayOtherActorOriginCameraAnimation` (steps `0x111b80`, `0x113910`: `name`, `chaseRatio`, `offsetX`..`offsetZ`, `ignoreRotation`, `lookAtDistanceCtrl`, `nearClipDistanceCtrl`, `farClipDistanceCtrl`, `aspectRatioCtrl`; through `0x111eb0`, nothing without the hook). Most NPCs and objects set `0x961a90` (the NPC component's, from its animation set `+0x1e8`); enemies, one-way doors, `ObjSpeakTree` and `NpcFox` set `0xf5390` |
| `+0x4d0` | hook: a named point's world position (name up to 0x3f characters); only Link sets it (`0xd44c40`). `0xfcbb0 (actor sp*, StrView* name, ...)` calls it for an `Aim`/`LookAt` target that names a point on an actor, and without it looks the name up in the actor's model |
| `+0x500`, `+0x504` | the turn's rate (a float), a float 1 when made |
| `+0x508`, `+0x509`, `+0x50c` | turning on, turning held, looking on (bytes) |

The hooks Link and the Mabe shopkeeper set (`DBG event` listing, measured 2026-10-30), the actions that call them and what their functors do. The action column comes from a test build that logged each action `Flow_RunAction` ran and each hook functor called after it, through the shopkeeper's talk, the beach chest, the item-get and Link-only entries started with `DBG flow` (`Area` `ShieldSteal`, `Common` `OcarinaKamae_Base`, `PlayerStart` `WalkIn`, `DungeonObject` `Warp`; measured 2026-10-30), and from the steps' parameter names:

| Hook | Actions | Link (`Player`) | NPC (`NpcComponent`) |
|---|---|---|---|
| 0 `+0x60` | `PlayAnimation`, `PlayAnimationEx` (`EventAction_PlayAnimation` `0x1190c0`: `name`, `blendTime`), an NPC's `GenericTalkSequence` | play an animation by name (`wait`, `$death` and `$resurrection` map to his own); returns a waiter | `0x9631e0 (out, StrView* name, blend)`: `@IDLE` and `@TALK` to the component's virtual `+0xd8` and `+0xe0`, any other name to `+0xc0` (`0x948550`: `NpcComponent_PlayAnimation`, then a waiter for the animation's end); a done waiter (vtable `0x1b56f60`) for an actor being destroyed |
| 1 `+0x90` | `PlayAnimation` as a timeline clip (the arg's trigger type 1 or 2): the clip's time gives the frame (`0x119720`, mode 2) | play an animation with a mode (2 sets its rate to 1) | `0x962e90`: virtual `+0xe8` `NpcAnimation_PlayByName (name, mode, blend, frame)`, no waiter |
| 5 `+0x150` | `SetFacialExpression` | `0xd45540 (StrView* name)`: a facial by name (two face parts' colours and patterns) | - |
| 6 `+0x180` | `MoveToCompassPoint` and the other `MoveTo` actions | `0xd45fb0 (Vec* target, u8 mode, float)`: `Player_MoveTo`, mode 0 at 2.25, 1..3 at 4.5 | `0x9629c0`: virtual `+0xf8` (`0x949710`): the row's walk or run animation (row `+0x50`/`+0x60` by mode), its speed (`+0x108`) and turn rate (`+0x110`) into a move task (`0x96af20`) on the framework's scheduler; returns it as the waiter |
| 7 `+0x1b0` | none directly: `EventChara_LookAndTurn` each frame after an `Aim` action | `0xd45870 (Vec* target, float* rate)`: turn toward a position | - |
| 8 `+0x1e0` | `TrailRail`, `TrailLinkedActorRail`, `TrailTargetActorRail` (steps `0x10e6a0`: `actor`, `railIndex`, `speed`, `timeOut`; `0x10edc0`: `linkIndex`, `railIndex`, `speed`, `timeOut`) through `EventChara_TrailRail` (`0x100840 (out waiter, chara, actor sp*, ...)`: nothing while the target actor is being destroyed or the hook is unset) | - | `0x9624d0`: virtual `+0x100` (`0x9499c0`): a move task along the rail (`0x96ae40`) |
| 12 `+0x2a0` | none directly: `EventChara_LookAndTurn` each frame while looking is on (a chara in the event; `LookAt` actions give the target); Link's functor also ran outside events | `0xd455e0 (u8 mode, Vec* target)`: look at (Link `+0x1ac0` mode; mode 2 stores the target at `+0x19e0`) | - |
| `+0x460` | read by `EventChara_LookAndTurn` | during the item-get `0x100080`: a stored position | `0xfc400`: the world position of the node it holds, invalid while that actor is being destroyed |

Hooks 2-4 and 9-11 override a default; each has a wrapper that calls the hook when set and does the default otherwise:

| Hook | Wrapper | Callers | Default |
|---|---|---|---|
| 2 `+0xc0` | `EventChara_PlayTailorOther` (`0xf8430 (out waiter, chara, StrView* value, ...)`) | `PlayTailorOtherChannel` (`0x10bdb0`: `channel`, `restart`, `index`), `PlayTailorOtherChannelEx` (`EventAction_ChannelTime` `0x10c120`: `channel`, `time`, `restart`, `index`), `0x10c4b0` | the owner's Tailor component: `Tailor_Play` of the value on channel `Other`, and a task that waits for it |
| 3 `+0xf0` | `EventChara_StopTailorOther` (`0xf8e30 (chara, StrView* value)`) | `StopTailorOtherChannel` (`0xf8d60`, `0x10bac0`, `0x10c4b0`) | `Tailor_StopValue` on the owner's channel `Other` |
| 4 `+0x120` | `EventChara_SetTailorProperty` (`0xf9530 (chara, u32 index, value*)`) | `SetTailorProperty` (`0x10b7a0`: `propertyIndex`, `value`, `index`) | the owner's Tailor component (`+0x68`, `0xe3e7e0`) |
| 9 `+0x210` | `EventChara_SetPose` (`0x100460 (chara, Vec* pos, Quat* rot)`) | the `Warp` actions' steps (`0x10ccb0`: `index`, `direction`, `offsetY`; `0x10d7c0`: `distance`, `direction`, `offsetY`) | the owner's root node: `Node_SetWorldPos`, `Node_SetWorldRotation` |
| 10 `+0x240` | `EventChara_SetGravity` (`0x1009d0 (chara, bool)`) | a step with the parameter `enable` (`0x10a9e0`; likely `SetGravityEnable`) | the owner's components |
| 11 `+0x270` | `EventChara_ResetVelocity` (`0x100db0 (chara)`) | a step with no parameters (`0x10a740`; likely `ResetVelocity`) | the owner's components |

A census of the loaded actors' charas (a test build listing every set hook per actor class, at 24 places across the field, houses and dungeons; measured 2026-10-30) found hooks 2 and 3 only as a pair, set by actors whose Tailor effects live on a child model they hold (`+0x158`, its Tailor at `+0x58`): `ObjTreasureBox`, the `ObjFieldTip*`/`ObjFloorTip*` tiles, `ObjDungeonWarpHole`, `NpcWalrus`, `EnemyDeguTail`, `EnemyPotDemonKing`. They set them through one setter (`EventChara_SetTailorOtherHooks` `0x283c90`, whose other callers sit next to the factories of `EnemyDeguFlame`, `EnemyHooker`, `EnemyPiccoloMaster`, `EnemyTurtleRock` and `ObjFlameFountainFire`). No class seen sets hooks 4, 9, 10 or 11, so their defaults run.

The generic actions every chara knows (`TalkKeep`, `PlayAnimation`, `LookAt...`, `Aim...`, `MoveTo...`, `Trail...`, `Warp...`, `PlayTailorOtherChannel...`, `SetFacialExpression`, `SetGravityEnable`, `ResetVelocity`, `Generic...Sequence`, ...) are named in `EventChara_FindAction` (`0x1049a0`); the action lookup `EventActorMap_AddChara` gives a chara's map entry (functor vtable `0x1bbe118`, `0x810e70`) calls it with the chara. Their steps are wrapped in a functor (`0x107190`, vtable `0x1b5cb20`) that emits the chara's `+0x3a8` signal before it calls the step.

`EventChara_Start` (vtable `+0x20`, `0xf2230`) connects the chara to the manager's `eventStarted` and `eventEnded` (handles at `+0x510`, `+0x514`). At an event's start looking (`+0x50c`) is set when the chara is one of the event's (`Event_HasChara`) and `+0x50a` cleared (`0xf3d40`). At its end (`0xf3960`) the look-at hook gets mode 1 and a zero vector, the look target (`+0x460`) is dropped, the look mode becomes 1, turning held, the rate 0 and looking off.

`EventChara_Register` (vtable `+0x78`, `0xf2ad0`) sets flags bit 8 and queues a per-frame task (`EventChara_LookAndTurn` `0xf30f0`): while the actor is active (flags & `0x40000117` == `0x117`) it asks `+0x460` for the target; with looking on (`+0x50c`) it calls the look-at hook (12) with mode 2 for a valid target, else `+0x490`; with turning on (`+0x508`), not held (`+0x509`) and a valid target it calls the turn hook (7) with the rate `+0x500`.

Each actor class fills `+0x400` and `+0x430` with lambdas over the actor: Player_Setup sets Link's (`0x1c18228`, `0x1c18270`; the action lookup `0xd6a3e0` compares the name with `Heal`, `GenericItemGetSequence`, `GenericItemGetSequenceByKey`, `AddDeadCounter`, ...). For a name it knows, the action lookup puts the action's step into `*step` and returns true. The step is a closure that holds its actor, so an action's code reaches its actor without the binding's user data. The item base wraps an NPC's action lookup with its own actions (`ItemNpcComponent_MakeEventChara`); the module's drop actor wraps that again with `Fall` (`ap_drop.cpp`).

## Actions and queries

The bind function (`0x108c160`) gives every bound action of a binding the same evfl handler, `Flow_RunAction` (`0x108e300 (ActionArg*, ActionDoneHandler*)`), and keeps the action's step in the flow's `+0x138` map under the action's key (the arg's `+0x20`). The handler finds the step and calls it once as `(out sp, step, Arg**)`. It returns a waiter, a task with a virtual base (`+0x18` finished, `+0x10` its finished signal's interface): with none (a null pair, `GameControl.RequestLevelJump`, the module's `Fall`), or one already finished, the done handler runs at once: the node's state becomes 4 (done) and the context processes its nodes again (evfl's `ActionDoneHandler::Invoke`, inlined). Otherwise the waiter goes into the flow's `+0x188` set of pending waiters, the done handler moves into a 0x48-byte holder (vtable `0x1c4c6c0`), and a functor (vtable `0x1c4c6f8`, `Flow_ActionDone` `0x108fa20`: flow, done handler, waiter) is connected to the waiter's finished signal; when it fires, the node is done, the context runs on and the waiter leaves the set. The item-get's waiter is its sequence. An action with no step in the map is done at once as well. The event's `FlowchartContext:Run()` task (`Flow_RunTask` `0x1091470`) advances nothing: it reports running while the context plays (object and entry set, nodes allocated), then finishes the pending waiters that have not finished (`+0x10`) and ends once the set is empty. Its cancel (`Flow_CancelCallback`) clears the context and finishes the pending waiters. `*arg` points at evfl's `ActionArg`. Its `+0x28` is the `FlowchartContext`, so `Flow_FromContext(arg+0x28)` gives the flow and the flow's `+0x1c0` the event. Parameters are read by name: `EventFlow_ParseInt` (`0x109f790`), the float getter `0x109fad0` and the string getter `0x109fb80`; the getters throw (Japanese messages) for a missing `Int`, `Float`, `Bool`, `String` or `ActorIdentifier` parameter (`0x109fa80`, `0x109fb24`, `0x109f734`, `0x109fcb8`, `0x109f42c`).

## What an event's start does to the actors

- `Actor_RegisterEvents` (`0x22a30`), run once an actor is ready, subscribes it to `eventStarted` and `eventEnded` (handles at actor `+0x294`, `+0x298`). Their callbacks (`0x22dd0`, `0x22d20`) call the actor's virtual `+0xb0` and `+0xb8` unless the actor is being destroyed (entity `+0x43` bit 6). An actor registered while an event runs gets `+0xb0` at once.
- The base actor's `+0xb0` and `+0xb8` are `Actor_SuspendUpdates` and `Actor_ResumeUpdates`: the update task runs the class's update only while flags bits 8 to 10 are set, so every actor that keeps the base slots stands still for the whole event. The actor update phase (`Phase_ActorUpdate`) itself runs during events; only the play time stops. Event actions reach their actors through the event system, not through the actors' updates, so a suspended actor still acts in its event. NpcGhost overrides the slots; which other classes do is open.
- Of the actor vtables that keep the base `+0xc0` (`Actor_SuspendUpdates`), 270 keep the base `+0xb0` and 191 override it. 78 overrides do nothing, so those actors keep updating through every event: among them `ObjTreasureBox`, `ObjWarpManbo`, `ObjBridge`, `ObjKanaletCastleGate`, `ObjFieldOwlStatue`, `ObjButterfly`, `ObjLawn`, `ObjClothBag`, `NpcOwl`, `NpcFox`, `NpcDanpei`, `NpcGrandmaUlrira`, `NpcMadamMeowMeow`, `NpcMermaidMartha`, `NpcRichardsFrog`, `AreaAmbienceCapsule`, `TagLight`, `TagTailorPlayer` and the event cameras. The others decide, as Link's does: `NpcMarin`'s (`0x9e3660`) keeps her updating when she is one of the event's charas (starting her `wait` animation unless her virtual `+0x160` says otherwise) and suspends her otherwise only while her `+0x6d3` is set. (Static scan of the vtables, 2026-10-30.)
- `Player_OnEventStart` (`+0xb0`) asks whether Link's EventChara (player `+0x2f8`) is one of the event's actors (`Event_HasChara`). If it is: `eventKind` from the event's kind, `kInEvent` (flags bit 0), `damageFlashTime` 0 and the DamageColComp off. If not: `Actor_SuspendUpdates`, so Link stands still like the others. It clears flags bit 24 either way. `Player_OnEventEnd` undoes whichever happened, and destroys `eventEntity`.
- `kInEvent` takes Link into the demoEvent state (22): the first of his state hooks (PLAYER.md, State machine), checked each frame before the action transitions, has one condition (`0xd44940`): `kInEvent` set and the state neither 22 nor 16 (`moveTarget`); the current state's virtual `+0x60` must let him go. The demoEvent state's enter (`0xdac500`) plays an idle animation (unless the handoff says otherwise) and zeroes the knockback's velocity; its update (`0xdabee0`) moves him with `Player_Move` and a zero vector, so only gravity moves him; its exit is `0xdac8e0`. The state he was in (a dash, a swim) has ended, and after the event he starts again from `locomotion`.
- Link's control mode (`Player_ControlMode`) does not depend on events: the scene's mode, the override or `kHeld`. During an event the input is still read; the demoEvent state ignores it.
- `BgmManager_OnEventStart` pauses the sound effects that play and sets `eventQuiet` (new ones at 0.2) for every event but `ZeldaScroll` (FRAMEWORK.md).

## How an event gets its scene actors

An event made with `Event_Make` and prepared has the system actors, Link and the companion, and no others: actions of a flowchart actor that is a room's actor (the Wind Fish's egg) reach nothing. Charas join an event by `Event_AddChara` between `Event_Prepare` and the request. `Actor_FindEventChara` (`0xfd850`, `(out sp, actor)`) gives an actor's chara; `ActorRegistry_FindByKey` the actor of a LEB key.

The game's own path is an owner's: an area linked to an NPC (its LEB entry's first relationship section, link index 0–3) is entered, and the NPC's component plays its row's event: `NpcComponent_OnTrigger` (`+0x330`) → `NpcComponent_StartTriggerEvent` (`+0x1c0`) → `NpcComponent_EventForCondition` (`+0x180`, the `eventTriggers` entry of condition 10 + link index whose `additionalConditions` hold) → `NpcComponent_StartEvent` (`+0x170`) → `NpcComponent_MakeEvent` (`+0x178`): `Event_Make` on the row's flowchart (`eventInfo.eventAsset`), `NpcComponent_SetupEvent` (`+0x310`: `Event_Prepare`, `Event_AddChara` for each linked actor whose chara names one of the flowchart's actors, then its own), then the request. The Ballad's area in `Field_01G` (`AreaOcarina`) owns the orchestra that way and links the egg, the instruments and the owl; in `Ending_12O` the Wind Fish owns the ending and links its `AreaEventBox`, the owl and the instruments. The module adds the egg to the opening it starts by its LEB key (`AP::Bridge::RequestEvent`); measured 2026-10-28, the egg then cracks, breaks and sets its switch as after the Ballad.

## Open questions

- Whether an event with no camera action moves the camera.
- Which actions besides the item-get enable the event cameras, and what the second camera is for.
- Unknown fields of the event types (`game/event.hpp`):
  - `ActorBinding` `+0x10` and `+0x28` (8 bytes after each count and capacity).
  - `FlowchartContext` `+0x48` (8 bytes after the node capacity).
  - `Flow` `+0x10` (0x10), `+0xb8` (0x70), `+0x130` and `+0x1b0` (8 each).
  - `RunningEvent` `+0xa0` (u32) and `+0x138` (8).
  - `EventRequest` `+0x70` (0x10; `Flow_Start` takes it as the metadata pack's owner).
  - `EventManager` `+0x0` (8, before its vtable).
  - `EventChara` `+0x58` (8, the component's own), `+0x30c` (0x4c), `+0x504` (float, 1 when made) and `+0x518` (8).
