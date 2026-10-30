# Actors (game 1.0.0)

How the game's actors are made, run, stopped and removed, and what decides whether a room's actor appears, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. "Guess" marks what the code suggests but was not followed to the end. Field names are those of `game/actor.hpp` and `game/actors/npc.hpp`. TESTING.md, Reverse engineering, has the item-get models, NPC components, skeletons and animation; this file does not repeat them. MODELS.md has the model and animation components, PLAYER.md Link's actor, FRAMEWORK.md the framework and its phases.

## Actor table

`ActorTable` (`0x1bcc898`): 0x275 entries of 0x58 bytes, one per actor id (the LEB actor ids; 45 are empty). `scan.py actors [regex]` lists them.

| Offset | |
|---|---|
| `+0x0` | factory `(shared_ptr* out, shared_ptr* owner, params*)`; null for an empty id |
| `+0x8`..`+0x40` | up to 8 resource names: `.bfres` (from `actor/`), `.drsb` (`tailor/`), `.bfevfl` (`event/`) |
| `+0x48` u8 | load group: rooms make their actors one group at a time, lowest first. `0x00` MapStatic, `0x01` MapKinematic, `0x03` MapFixture, `0xc9` EnemySpark, `0xc8` every other |
| `+0x49` u8 | priority (actor `+0xac`): the key of the actor's tasks, which run in ascending order. `0x01` AreaOuterWall, `0x02` Player, `0x03` Link's projectiles, `0x10` panel joints, MapSkySea and ObjSirenInstrument, `0x11` enemies and most objects, `0x12` pots, bombs, fish, horse heads and the tile wall, `0x1c` NPCs and most Area actors, `0x1e` Tag actors and FixedCameraActor |
| `+0x4a` u8 | resource count |
| `+0x4b` u8 | owned by the actor system instead of the room (Map*, PanelJoint*, ObjDungeonRoof, PanelDungeonRoof): made with the actor system as owner and moved into the room root when the room runs |
| `+0x50` | name |

## Classes

Vtables start at the object's `+0x0` (no RTTI before them); `+0x68 IsA(type*)` and `+0x70 TypeId()` use the game's own type ids. Classes: Entity (`0x1c313e8`) → Component (`0x1c31358`) and the engine actor base (`0x1c31198`, guess) → the game Actor (`Actor_vtable`, `0x1b50f28`, base slots `+0x0`..`+0x120`, shared by 486 actor classes) → each class (NpcGhost `0x1be37c0`, Player `0x1c17b70`, EnemyOctorok `0x1b97418`, ObjTreasureBox `0x1c12020`). Slots from `+0x128` are class-specific.

**Entity slots** (actors and components):

| Slot | Actor base | |
|---|---|---|
| `+0x00` | `0x1da40` | `Priority()`: actor `+0xac` |
| `+0x08` | `0xfa7690` | `Detach`: leave the parent now (`Entity_RemoveChild(parent, name, length, true)`, else `Actor_DestroyNow`); a component leaves its actor's map |
| `+0x10` / `+0x18` | `0x1da50` / `0x1dd70` | destructor / deleting destructor |
| `+0x20` | `0x1dda0` | `Setup`, once, after `Register`: `MakeRoot` (`+0xa8`), then `PlaceAtSpawn` (`+0x90`) |
| `+0x28` | empty | `Start`, run by the start task at the next flush |
| `+0x30` | empty | `OnDestroyRequested` |
| `+0x38` | `0x1de40` | `OnDestroyed`, the last call of `DestroyNow`: drops the event-manager handles (`+0x294`, `+0x298`) and the zone subscriptions (`+0xd8`), records the actor's removal (`+0xa2` bit 2, Rooms), erases its key from the actor registry |
| `+0x40` / `+0x48` | empty | `OnActivated` / `OnDeactivated` |
| `+0x50` | `0xfa7af0` | `OnActiveChanged`: `+0x40` or `+0x48`; the actor's passes its active state to its root and components (their bit 1) |
| `+0x58` | `0xfa7d40` | `Destroy(bool immediate)`: request (Destruction) |
| `+0x60` | `0xfa7d80` | `DestroyNow` |
| `+0x78` | `0x1e480` | `Register`: posts the class's per-frame tasks |

**Actor slots** (`+0x80`..`+0x120`):

| Slot | Base | |
|---|---|---|
| `+0x80` / `+0x88` | empty | added to / removed from a parent (`Entity_AddChildBody`, `Entity_EraseChild`) |
| `+0x90` | `0x1e580` | `PlaceAtSpawn`: the root's coordinates from the spawn transform (`+0x260`), `+0xe8(0)`, `+0xa6` = 1 |
| `+0x98` | `0x1e630` | a closure over the components, called by `+0x108` while `+0xa2` bit 4 (guess: refresh after re-entering its zone) |
| `+0xa0` | `0x1e6d0` | `Entity_Destroy`; the room manager's `+0x278` signal calls it (callback `0x1f520`); Player's does nothing |
| `+0xa8` | `0x1e6f0` | `MakeRoot(out, actor)`: a node component named `Root` (`0x20d10`) when `+0x80` is empty |
| `+0xb0` / `+0xb8` | tail-call `+0xc0` / `+0xc8` | suspend / resume (guess: around events; NpcGhost's set and clear `+0x2c2` while an event runs) |
| `+0xc0` / `+0xc8` | `0x1e7d0` / `0x1e8d0` | clear / set flags bits 8-10 (its update tasks) and run closures over the components |
| `+0xd0` | `0x1e980` | subscribe to the zone signals (Rooms); suspends the actor at once when it is made outside the current zone |
| `+0xd8` | `0x1edf0` | unsubscribe |
| `+0xe0` | `0x1f090` | `OnRoomChanged(int* new, int* old)` (Rooms); Player's (`0xd32f60`) keeps Link out of zone suspension |
| `+0xe8` / `+0xf0` | `+0x100` / `+0xf8` | zone entered / left; `+0xf8`, `+0x100` empty hooks for classes |
| `+0x108` / `+0x110` | | after a suspension / the zone signal for the actor's own zone |
| `+0x118` | `0x1f220` | `Entity_Destroy`: the room's fall check (below y = -100) calls it; Player's (`0xd336c0`) handles the fall |

## Actor fields

`Entity_Ctor` (`0xfab900`, `(entity, {name, length, bool enabled}*)`) and `Actor_Ctor` (`0x1bdb0`, `(actor, ActorArgs*)`; the factories build `ActorArgs {+0x0 name, +0x8 length, +0x10 u8 1, +0x18/+0x20 owner sp, +0x30 params copy}`). `Entity_Ctor` leaves the parent (`+0x48`) to the subclass constructors (the engine actor base, Component), which set it from the arguments' owner. `Actor_Ctor` writes through `+0x2a0`, and the Actor is 0x2a8 bytes: NpcGhost's second base and Player's first field start at `+0x2a8`. Destructor `Actor_Dtor` (`0x1da50`).

| Offset | |
|---|---|
| `+0x08`/`+0x10` | weak self pointer (shared-from-this) |
| `+0x18`, `+0x38` | name (`char[0x20]`, NUL-terminated; 0x20 characters or more throw, and the factories return null for a name of 0 or more than 31), its length |
| `+0x40` | flags (below); start as 5 \| enabled |
| `+0x48`/`+0x50` | parent (weak) |
| `+0x58`..`+0x78` | components: a libc++ `unordered_map` by FNV-1a of the name (node 0x30 bytes `{next, hash, name, length, sp}`, the name pointing at the component's own; `Actor_AttachComponent` `0xfaa8a0`) |
| `+0x80`/`+0x88` | root component (shared_ptr) |
| `+0x90` | component iteration depth |
| `+0x98` | LEB key (params `+0x0`) |
| `+0xa0` u16 | actor id (params `+0x8`) |
| `+0xa2` u16 | zone state: bit 0 suspended outside its zone, bit 1 flags bit 2 when it was suspended, bit 2 record the removal on destroy, bit 4 refresh on re-entry (`+0x98`) |
| `+0xa4` u16 | class bits: bit 0 zone-scoped (suspended while Link is in another zone), bit 1 destroyed when a dungeon room reloads (keyless actors). NpcGhost ORs `0x805` (`0x9bcbb4`), Octorok `0x103`, ObjTreasureBox `0x1000` |
| `+0xa6` u8 | placed at its spawn |
| `+0xa8` u32 | serial (counter `0x1b51050`, never 0) |
| `+0xac` u32 | priority (`ActorTable +0x49`) |
| `+0xb0` u32 | zone (params `+0xc`: the LEB actor's room id; -1 none) |
| `+0xb4` | zone of the room that owns it (-1 none; `Room_AdoptActor` writes it); `+0xb8` the zone its removal is recorded under |
| `+0xbc`..`+0xfc` | the 8 LEB parameters `{u32 value, u32 type}`: 2 float, 3 int, 4 LEB string offset (`LebRoom` virtual `+0x50`), 5 pooled string |
| `+0x100` | links (LEB `x`, record `+0x87`): owned 0x38-byte objects (`ActorLink_vtable` `0x1b51088`): `+0x8` the target actor (weak, the registry's actor of the key of `LebRoom` virtual `+0x28` of the link's index), `+0x18` the link's 2 parameters, `+0x28` the `LebRoom` (shared) |
| `+0x118` | rail links (LEB `z`): owned 0x30-byte objects (`ActorRailLink_vtable` `0x1b51068`): `+0x8` the record's 0x18-byte entry, `+0x20` the `LebRoom` (shared) |
| `+0x130`, `+0x1b0`, `+0x230` | the bfres, drsb and flowchart resources |
| `+0x240`/`+0x248` | the room's LEB (`LebRoom`) |
| `+0x250` | its LEB record; the actor reads its switches there |
| `+0x260`, `+0x270`, `+0x280` | spawn position (record `+0x14` plus the area's offset), rotation (quaternion from the Euler degrees at record `+0x20`), scale (record `+0x2c`) |
| `+0x290`..`+0x2a0` | signal handles: `+0x290` the room manager's `+0x278` signal, `+0x29c` the scene's `+0x708` (room changed), `+0x2a0` the scene's `+0x7b8` (`Actor_SubscribeZones` `0x1e980`, `Actor_UnsubscribeZones` `0x1edf0`); `+0x294`, `+0x298` the event manager's `+0xe8` and `+0x140` signals (`Actor_RegisterEvents` `0x22a30`) |

**Params** (0x1c0 bytes; `ActorParams_Copy` `0x31100`): `+0x0` u64 LEB key, `+0x8` u16 actor id, `+0xc` u32 zone,
`+0x10` 8 LEB parameters, `+0x50`/`+0xd0` 8 bfres / drsb resources, `+0x150` flowchart, `+0x160`/`+0x168` `LebRoom` sp,
`+0x170` LEB record, `+0x180` position, `+0x190` rotation, `+0x1a0` scale, `+0x1b0`/`+0x1b8` a weak pointer the room's
params builder zeroes and nothing reads. Destructors `0x1cf70` (`+0xd0`), `0x1d190` (`+0x50`), `0x1d3b0` (`+0x10`,
releases type-5 strings).

**LEB actor record** (`LebActorRecord`; the one-letter names as LASR's `Tools/leb.py`): `+0x0` u64 key, `+0x8` name offset, `+0xc` the actor id, `+0x10` u32 room (zone) id, `+0x14` position, `+0x20` rotation, `+0x2c` scale, `+0x38` 8 parameters, `+0x78` u8 switch types [4], `+0x7c` u16 switch indices [4], `+0x84` `e`, `+0x85` `k`, `+0x86` `b` (no reader found), `+0x87` `x`, `+0x88` `y`, `+0x89` `z`; 0x90 bytes, then three lists in this order: `x` links of 0x14 bytes (`{2 parameters, u32 actor index}`), `z` rail links of 0x18 bytes (`{2 parameters, u32 rail, u32 point}`) and `y` u32 actor indices (`Actor_Ctor` reads the first two, `Room_OnLebActor` `0x8e53b8` the third). `LebRoom` (`LebRoom_vtable` `0x1bcc2f8`, 0x98 bytes, `LebRoom_Ctor` `0x8eab00`) virtuals: `+0x18` room id, `+0x20` actor count, `+0x28` record i (`LebRoom_ActorAt` `0x8eb0e0`), `+0x30`/`+0x38` rails, `+0x40`/`+0x48` points, `+0x50` a string by offset (`LebRoom_String` `0x8eb1c0`).

## Nodes

A **Node** (`Node_Ctor` `0xfb55a0 (node, ComponentArgs*)`, `Node_vtable` `0x1c31a68`, `Node_Dtor` `0xa9270`) is a component that is a scene-graph node: an actor's root, a model component, a character controller. `Actor_MakeNode` (`0x20d10`) allocates 0x150 bytes; the data ends at `+0x148` (MODELS.md, Skeletal model component). `Actor_AttachComponent` (`0xfaa8a0 (actor, component, parent node)`) attaches one: under the parent node (a weak `parent`, the child appended to the parent's `children`, shared pointers), or as the actor's root with a null parent. `Actor_GetOrMakeRoot` (`0xb6dc0`) returns the root or makes it.

- The constructor sets `coordsNew` and `coords` to a zero position (`ZeroVec_ptr`), the identity rotation and unit scale, and builds `transformChanged`.
- `Node_MarkDirty` (`0xfb6aa0`) and `Node_UpdateWorld` (`0xfb6310`) as TESTING.md, Reverse engineering, describes them. A node with an `attachment` (a bone attachment: `+0x10` locator, `+0x30` position, `+0x40` rotation, `+0x50` scale, `+0x60` dirty) and no parent has the world local × the attachment's; `Node_MarkDirty` skips children with one.
- Its vtable has the Entity slots only; `+0x50` `Node_OnActiveChanged` (`0xfb58c0`) sets bit 1 of each child node from its own active state, raising the actor's `componentIterDepth` around the walk.

A **signal** (`Signal`, 0x58 bytes) has one layout for every argument type. `transformChanged` and NPC components'
`readySignal` (a make_shared of 0x78 bytes, the signal at its `+0x18`) take a bool (`SignalBool_vtable` `0x1b59008`);
the tasks' and some of Player's take none (`Signal_vtable` `0x1b59608`, FRAMEWORK.md). The bool signal's virtuals:
`+0x10` `SignalBool_Clear` (`0xa95e0`), `+0x18` `SignalBool_IsOpen` (`0xa9630`), `+0x20` `SignalBool_Connect`
(`0xa9650`: a `std::function`, returns a u32 handle, 0 when closed; the `*Sub` fields keep such handles), `+0x28`
`SignalBool_Disconnect` (`0xa97f0`), `+0x30` `SignalBool_Emit` (`0xa9970`: copies the slots under the lock, calls each
with a bool).

| Offset | |
|---|---|
| `+0x8` | `connectorVtable`: a second base (`0x1b59098`) whose slots forward to `owner` |
| `+0x10` | `owner`: the signal itself |
| `+0x18` | `slots`: libc++ `std::map<u32 handle, std::function<void(bool)>>` (node 0x60: key `+0x20`, function `+0x30`) |
| `+0x30` | `nextHandle`: Connect returns it and counts up from 1; 0 is closed (Clear sets it) |
| `+0x34` | `oneShot`: 1: Emit drops every slot and closes the signal after calling them |
| `+0x38` | `mutex`: an nn::os mutex (0x20 bytes) |

## Creation

`Actor_Create` (`0x8f5ea0`, `(shared_ptr* out, shared_ptr* owner, params*)`; every spawn goes through it):

1. Null when the id's factory is null.
2. Copies the params. With a non-zero key: null when the registry holds the key, or the owner has a child of the actor's name (`ActorName_Make` `0x23830`: the LEB name, else `"{0:S22}:{1:X8}"` of the table name and a counter; `Entity_FindChildInMap` `0xfb3470` on the owner's maps `+0x10` and `+0x38`).
3. Loads each table resource (`Resource_Load`) and takes it with `Resource_TakeModel` `0x57d50`, `Resource_TakeTailor` `0xdbb60`, `Resource_TakeFlowchart` `0x3d9f0`; each waits (`Resource_Wait`) until the file is loaded. A spawn from code waits on the game thread for files not loaded yet.
4. Calls the factory.
5. When the actor lives and is not being destroyed: its key into the registry (`ActorRegistry_ptr`, `0x1cb2d00`, `unordered_map<u64, shared_ptr<Actor>>`, strong references; `ActorRegistry_FindByKey` `0x8f8200`), and a task on the Framework's scheduler (`Task_Post` `0x2d7430`, `ActorCreate_ReadyTask` `0x8f7860`) that waits until flags bit 4 is set and the actor's component of type `0x1b511e0` reports ready (`+0x58`), then `Actor_RegisterEvents` (`0x22a30`) and virtual `+0xd0` (zone subscription).

A factory (Octorok `0x54e0a0`, TreasureBox `0xc9e820`, NpcGhost `0x9bc540`): builds the name and checks it and the owner's maps, `make_shared` of the class (the control block at `+0x0`, the actor at `+0x20`; NpcGhost 0x2f0 bytes), `Actor_Ctor` then the class constructor, the weak self pointer, then unless `flags & 0x40000008`: `Register` (`+0x78`), `Setup` (`+0x20`), bit 3; `Entity_Start` (`0xfab470`: a start task) and `Entity_AddChild(owner, sp)` (`0xfb3290`, bit 1 from the owner). Failing that, `Entity_Destroy` and null. At the next flush the start task calls `Start` (`+0x28`) and sets bit 4; the actor is updated from the frame after.

`Entity_AddChild` (`0xfb3290`, `(container, sp*)`): the child needs bit 3 and a name of 1 to 31 characters not already in the container's maps `+0x10`, `+0x38`, `+0x60`. `Entity_AddChildBody` (`0xfb1250`) takes it out of another container, adds it (a strong reference), sets its parent, calls its `+0x80`, sets bit 1 from the container's `+0x140` and calls the container's listener (`+0x88`). The actor system `*(Framework+0x498)` is such a container (children map `+0x10`, active `+0x140`); each room's root is another.

## Flags (`+0x40`)

| Bit | |
|---|---|
| 0 | enabled (`Entity_SetEnabled` `0x2a560`) |
| 1 | parent active: copied from the parent container's `+0x140` when added, cleared when erased; for components the actor's active state |
| 2 | in its zone, not frozen (Rooms); set at construction |
| 3 | initialized: `Register` and `Setup` ran |
| 4 | started: the start task ran `Start` |
| 8-12 | member of the update, late-update, (unused), render-calc and render queues: each task runs only while its bit is set; `+0xc0` clears 8-10, `+0xc8` sets them |
| 30 | destroy requested |
| 31 | destroyed |

Active: `(flags & 0x40000017) == 0x17` (enabled, parent active, in its zone, started, not being destroyed). Each change of a bit of that mask calls `+0x50` when the active state flips. A task runs while `(flags & (0x40000017 | its bit)) == (0x17 | its bit)`.

## Entity manager and the frame

`EntityManager_ptr` (`0x1cc6d98`; 0x6e0 bytes, `EntityManager_Ctor` `0xfac070`): seven task queues of 0xf8 bytes (`TaskQueue_Add` `0x321e0 (queue, key*, std::function*, u32 priority)`, `TaskQueue_Remove` `0xe4670`, `TaskQueue_Run` `0xfacab0`; tasks run in ascending priority, one per key; changes while a queue runs are deferred), `+0x6d0` the count of initialized entities, `+0x6d8` flushing.

| Queue | Bit | Run by | |
|---|---|---|---|
| `+0x008` | | `EntityManager_Flush`, first | start tasks (`EntityManager_PostStart` `0xfae2d0`, task `0xfae690`) |
| `+0x100` | 8 | update phase `0x15b80` | per-frame update (286 actor classes, 55 component classes) |
| `+0x1f8` | 9 | update phase `0x14210` | late update (the skeletal model copies its node's transform into its model instance) |
| `+0x2f0` | 10 | none found | |
| `+0x3e8` | 11 | render phase `0xd6c0` | render-side calculation (instanced models) |
| `+0x4e0` | 12 | render phase `0xd420` | render-side |
| `+0x5d8` | | `EntityManager_Flush`, second | destroy tasks (task `0xfae970`) |

A class's `Register` sets the queue's bit and posts its task with `Priority()` (NpcGhost `0x9be370`: `flags |= 0x100`, `TaskQueue_Add(manager + 0x100, &self, task, Priority())`); the update task calls the class's update only while `(flags & 0x40000117) == 0x117` (NpcGhost task `0x9be5f0` → `NpcGhost_Update` `0x9be630`; Player `0xd35580` → `0xd355c0`). Update and draw are not vtable slots. ObjTreasureBox's `Register` posts nothing.

`Framework_FrameStep` (`0x17e30`) runs the update phases (Framework `+0x2a8`, added by `Framework_Init` `0x9410` with `Framework_AddUpdatePhase` `0xc0e0`; render phases `+0x3a0`, `Framework_AddRenderPhase` `0xc290`; FRAMEWORK.md, Scheduler and phases, lists them all) in ascending priority:

| Priority | Lambda | |
|---|---|---|
| `0x61a8` | `0x15d80` | game control (guess) |
| `0x6d60` | `0x14510` | UI manager |
| `0x7530` | `0x15b80` | actor update (queue `+0x100`) and the play time while no event runs; nothing while Framework `+0x510` or `+0x511` is set |
| `0x9c40` | `0x14630` | node world update, effects |
| `0xb3b0` | `0x140f0` | sound, BGM |
| `0xbb80` | `0x14210` | late update (queue `+0x1f8`); nothing while `+0x510` or `+0x511` |
| `0xbd74` | `0x143f0` | HUD |
| `0xc350` | `0x11960` | the room manager's `+0x58` (unload rooms), then the render phases (queues `+0x4e0`, `+0x3e8`, graphics), or while Framework `+0x4e0` counts down a flush instead |

Pause (a menu opening; a level jump sets `+0x510`): the two actor phases do nothing, the render phases still run, so frames are drawn with nothing updated. `EntityManager_Flush` (`0xfae0e0`) runs from a graphics callback (`0x18930`, registered by `0xaa40`; guess: once the frame's GPU work is done): start tasks, then destroy tasks.

## Destruction

- `Entity_Destroy` (`0xfabf70`): while flushing, `Detach` (`+0x08`) now; else `Destroy(false)` (`+0x58`).
- `Actor_Destroy` (`0xfa7d40`): `Actor_DestroyComponents` (`0xfa8410`) with the same `immediate`, then `Entity_RequestDestroy` (`0xfaba60`): sets bit 30, `+0x50` when it was active, a destroy task (a strong reference) into queue `+0x5d8` unless immediate, `+0x30`.
- `Entity_RemoveChild` (`0xfb00c0`, `(container, name, length, immediate)`): not immediate (and not flushing) requests as above; immediate destroys the components, erases the child (`Entity_EraseChild` `0xfb04e0`) and calls `Actor_DestroyNow`.
- The flush runs the destroy tasks by priority (components, priority 0, before their actors); each calls `Detach`.
- `Actor_DestroyNow` (`0xfa7d80`): the root and every component go (`+0x60` each), the actor leaves every queue, bit 31, `+0x50` when it was active, `OnDestroyed` (`+0x38`).
- The container's map entry, the actor's component map, the root and each queued task hold strong references; the destructor runs with the last one, the memory goes with the last weak reference. Weak pointers then lock to null; code holding a raw pointer checks `flags & 0xc0000000`.

## Rooms

**Room** (`Room_vtable` `0x1bcc100`, 0x250 bytes, `Room_Ctor` `0x8def30 (room, LevelData*, area sp*, bool)`; one per loaded area, in the room manager's `+0x210`/`+0x218`): `+0x10` state machine, `+0x180` the LEB, `+0x1a0` level data, `+0x1a8` area, `+0x1b8` root (the container of the room's actors; `+0x140` its active flag), `+0x1c8` actor-system-owned map actors, `+0x1e0` actors kept by a reload, `+0x1f8` queued entries (0x128 bytes: `+0x8` id, `+0xa` group, `+0x10` LEB sp, `+0x20` record, `+0x28` 8 resources), `+0x210` entries loading, `+0x228` components to wait for, `+0x240` zone id, `+0x249` its zone is in the removal ring, `+0x24a` abort (a level jump), `+0x24b` entries started per frame (2; 0xff for a room made while the level loads).

| State | Enter / update / exit | |
|---|---|---|
| Prepare | – / `0x8df740` / – | LebLoad, or Run when aborted or the area is no longer wanted |
| LebLoad | `0x8df770` / `0x8df900` / – | loads the area's LEB (area `+0x68`), builds a `LebRoom` (`0x8eab00`: actor, rail, point, infomation), `Room_OnLebActor` per actor, sorts the queue by group |
| AssetLoad | `0x8e0ca0` / `0x8e1610` / – | starts the loads of one group's entries, `+0x24b` per frame; once an entry's files are loaded, its params from the record and `Actor_Create` with the root as owner (the actor system for `ActorTable +0x4b`). LASR's `InterceptActorLoad` sits here (`0x8e177c`) |
| InitializeActor | – / `0x8e2870` / `0x8e2b60` | waits for the components in `+0x228`; then AssetLoad for the next group, or Run |
| Run | `0x8e2bf0` / `0x8e2d50` / – | enter: `Scene_ReleaseActors` moves the map actors into the root. Update, every 4th frame: actors below y = -100 get `+0x118`, other entities `Entity_Destroy` |

**`Room_OnLebActor`** (`0x8e4da0`, Room vtable `+0x20`) decides whether a LEB actor is made, by presence and the removal memory, not by switches: a map actor already alive (`+0x1c8`), a key in the registry (the live actor is adopted instead: `Room_AdoptActor` `0x2b8e0` moves it into the root, sets `+0xb4` and its spawn position), a name already in the root, and, for a room whose zone is in the removal ring (`+0x249`), the removal memory (Persistence). A record with links (`y` > 0) and group above `0xc4` is queued in group `0xc6` (no `x` links) or `0xc7`.

**Field streaming.** `RoomManager_Update` (`0x8529b0`) in state 1 calls `RoomManager_UpdateRooms` (`0x85c280`): every
area's `active` cleared, the areas in the camera's load box
marked (at most 0x37), a Room made for the area at Link's position when there is none (`RoomManager_CreateRoom`
`0x854fd0`, vtable `+0x60`), `+0x230` Link's zone. One room below Run advances per frame. A room whose area left the box
is unloaded by vtable `+0x58` (`RoomManager_UnloadRooms` `0x854cd0`, from the last update phase): its actors destroyed a
few per call, then the room deleted (`Room_Dtor` `0x8e4610`). Room actors are destroyed, not kept; they come back with
the room, unless the removal memory holds them.

**Activity.** Two flags stop actors:
- Bit 1 by room: `UpdateRooms` (`0x85c83c`) sets a room root's `+0x140` while the camera window around Link (its
  extents by Link's area's `viewKind`, a table at `0x1701ed4`) overlaps the area's `viewMin`..`viewMax`, a tracked
  node lies inside that box, or the area's `showForced` is set (each update copies it to Room `+0x24d` and clears
  it); on a change it sets bit 1 of each child (`Entity_ForEachChild` `0xfafd20`, functor `0xfb3220`). Actors of the
  actor system (Link, factory spawns from code) are not in a room and never get this.
- Bit 2 by zone (a zone is one camera room). An actor subscribes in `+0xd0` (`0x1e980`) to the room manager's `+0x278`, the scene's `+0x708` (room changed → `+0xe0`) and `+0x7b8` (`0x1f2e0`). For a zone-scoped actor (`+0xa4` bit 0) of the zone Link left, `0x1f2e0` saves bit 2 into `+0xa2` bit 1, sets `+0xa2` bit 0 and clears bit 2. `Actor_OnRoomChanged` (`+0xe0`, `0x1f090`) restores bit 2 from `+0xa2` bit 1 when Link enters its zone. An actor made outside the current zone is suspended at once. Zone -1 (`+0xb0`) is never suspended.
- Bit 2 by freeze: `ActorTree_Freeze` (`0x7d5910`, functor `0x7d5b30`) clears it on every enabled actor of a tree that is not of type `0x1cab3f8` (`IsA`; Player's own type id is `0x1cab518`, so guess: a base class of Link's) and lists them, `ActorTree_Unfreeze` (`0x7d5f60`) sets it again; about a hundred classes call the pair. Actors of the actor system are not in those trees: a factory-spawned NpcGhost in zone -1 keeps bit 2 through a dungeon scroll (measured 2026-10-23).
- Bit 2 by `GameControl.LoadZone(zoneID)` (`0x7ffc50` → `0x866d90`): set on every direct child of the rooms of that zone.

**Dungeon rooms.** The scene's update (`0x836270`) follows Link's room (scene `+0x800` zone, `+0x804` room index); on a
change it fires the room signal (`+0x6f8`, the actors' `+0x708` callback, with new and old) and, on a zone change,
`+0x6a0` and `RoomManager_VisitZone` (`0x865e90`). In a dungeon (level data vtable `+0x48` == 3) `UpdateRooms` reloads a
running room whose area's `showForced` differs from the room's copy (Room `+0x24d`) while
the area's `active` is set (`Room_Reload` `0x8e5e60`): its actors are kept (`+0x1e0`; keyless ones with `+0xa4` bit 1
are destroyed, `0x8e62a0`) and the room goes back to LebLoad, where `Room_OnLebActor` skips the actors still present.
Actors of a dungeon room Link left are kept.

**Level jump.** `LevelManager_LevelJump` (`0x864d50`, state 1 only) saves Link's position, stores the destination, aborts every room (`+0x24a`), resets the event and message managers, state 4. State 4 (`0x864a30`) waits for no running event, sets Framework `+0x510`, waits until every room is in Prepare or Run, then vtable `+0x80` (`RoomManager_LevelJumpTeardown` `0x857660`): the BGM fades (90 frames), Link is disabled, every room is deleted (its actors with it), the level data is released (and the removal memory with it). Only the actor system's children survive (Link, the hookshot, actors spawned from code with that owner); they load their models again in the next level. The next level's rooms come up from the one at Link's position, one per frame, each group by group.

## Persistence

**Switches.** An actor reads its own LEB switches (slot 0-3: type `+0x78+slot`, index `+0x7c+2·slot`); the room never does. `ActorSwitch_IsOn` (`0x23490`), `_IsOff` (`0x23660`), `_On` (`0x22df0`), `_Off` (`0x22fd0`), `_Set` (`0x231b0`, LASR's `SetActorSwitch`), `ActorSwitch_Check` (`0xf886c0`), each `(record, slot)`:

| Type | Flag |
|---|---|
| 0 | level flag: bit `index` of 256 at EventFlags `+0x20` (`LevelFlags`, `0x1cc1140`; the bit table at its `+0x58`), not saved; a level jump clears them, a zone change in Field keeps them (`DBG peek l`, 2026-10-29) |
| 1 | global event flag: `GlobalFlags.gsheet` index, the saved flags (EventFlags `+0x268`); `EventFlags_Set`/`Check` resolve names to the same index |
| 2 | constant: on when `index` is not 0; setting does nothing |
| 3 | Chamber Dungeon bit (PanelDungeon `+0x16a0`) |
| other | IsOn false, IsOff true, setting does nothing |

Usual meanings: switch 0 "appear when" or "opened" (ItemSmallKey: enabled while on, `0x8cc280`), ObjTreasureBox's opened flag its LEB switch 1 (slot 0 a constant): `TreasureBox_Init` (`0xc9a0d0`) reads it, opened while on, and `TreasureBox_Open` (`0xc9b4b0`) sets it, each through slot 0 of the record at `+0x250` (how that maps to LEB switch 1 not looked into). It reads the flag again while its opening event runs, closing with it off, but not afterwards; switch 1 "got" (the small key sets it when collected, `0x8cc4bc`). Event actions on level flags: `EventFlags.SetLevelFlag` (`0x7e1420`), `CheckLevelFlag` (`0x7e14c0`) and the `…FromLevelEditorArgument` versions (`0x1041e0`, `0x104620`), whose flag index is the actor's LEB parameter `argumentIndex`.

**Removal memory** (defeated enemies and other actors without switches; LevelData `+0x8`, `LevelData_Ctor` `0x8ed720`): a hash set of keys (for the level) and a ring of 5 slots `{s32 zone, vector<u64> keys}` (current slot LevelData `+0xd8`). An actor that sets `+0xa2` bit 2 (enemies when they die, e.g. `0x2461fc`) has its key added in `OnDestroyed` (`RemovedSet_Add` `0x8e6ac0`: the slot of its zone `+0xb8`, else the hash set). `RoomManager_VisitZone` gives the zone Link enters a slot when it has none (the oldest, cleared). `Room_OnLebActor` for a room whose zone is in the ring (`RemovedSet_Has` `0x8e71e0`): without links, made when `e` is 0, else unless removed; with links, the highest `k` among linked actors not removed decides: 1 made, 2 made unless removed itself, 0 as without links. Defeated enemies stay away while their zone is among the last 5 visited, and come back after 5 other zones or a level change. A second set at LevelData `+0xe0` (current slot `+0x1b0`) is used by the drop code (`0x89c2f4`; guess: one drop per zone visit).

**Appearing.** Each class checks its switches in its own setup or state code and destroys itself, disables itself (bit
0) or picks a state (about 200 callers of `ActorSwitch_IsOn`). NPCs and items: `NpcComponent_CheckLayout` (`0x947c90`,
NpcComponent virtual `+0x80`): the first of the Npc row's
`layoutConditions` (`+0x160`, 0x20 each, `layoutID` at `+0x18`) that holds (`Conditions_Check`) gives its
`layoutID`, true when it equals the component's `layoutId`. `NpcComponent_CheckPop` (`0x947d70`, virtual `+0x88`):
every entry of the row's `popConditions` (`+0x148`, 0x18 each; `depopEnable` at `+0x158`) holds. `ItemActor_CanAppear`
(`0x8c4510`) adds the inventory check (TESTING.md).

## NPCs

**NpcActor** (NpcGhost; `NpcGhost_Ctor` `0x9bcb50 (out, ActorArgs*)`: make_shared of 0x2f0 bytes, the actor 0x2d0 bytes
at `+0x20`, `classBits |= 0x805`; `NpcGhost_Dtor` `0x9bccb0`; `NpcGhost_vtable` `0x1be37c0`). `npc` is set by NpcGhost's
setup (`0x9bcda0`), which connects to the component's
`readySignal`; `param0Max` is set there too. `NpcGhost_OnEventStart` (`0x9bd4c0`, virtual `+0xb0`) sets `inEvent`
and reads the component's `eventChara`, `NpcGhost_OnEventEnd` (`0x9bd680`, `+0xb8`) clears it, and `NpcGhost_Update`
(`0x9be630`) skips while it is set. The update counts `timer` down by the frame time while `scenarioRequested` is
clear; at 0 it calls `NpcGhost_ScenarioTimer` (`0x9bee20`) and starts again at 40. That callback starts the event
`Scenario:Lv5GhostGoGrave` (and sets the event flag `TakeGhostGrave`) or `Scenario:Lv5GhostGoHouse1` and sets
`scenarioRequested`; the event's callbacks (`0x9bfb48`, `0x9bfd5c`) clear it. Only NpcGhost's layout is checked;
other NPC classes may keep their component elsewhere.

**Companion interface.** `companionVtable` (`+0x2a8`, `NpcGhost_vtable2` `0x1be3948`, offset to top −0x2a8 at
`0x1be3938`) is a second base: the interface of the companions Link takes along. Four classes have it at `+0x2a8`;
its slot `+0x20` returns the companion kind that `Conditions_Check` type 3 names: NpcBowWow 1 (`0x1bdb6e8`),
NpcMarin 2 (`0x1be6ba0`), NpcGhost 3, NpcFlyingCucco 4 (`0x1be1440`, "Rooster"). NpcGhost's slots (thunks that
subtract 0x2a8):

| Slot | | |
|---|---|---|
| `+0x10`, `+0x18` | `0x9bd6a0`, `0x9bd6e0` | the actor as a shared_ptr (from the weak self pointer) |
| `+0x20` | `0x9bdff0` | the companion kind |
| `+0x28`, `+0x30` | `0x9be000`, `0x9be030` | the NPC component (`npc`) as a shared_ptr |
| `+0x38` | `NpcGhost_FollowLink` `0x9bdd20` | while `following`: the root to Link's world position |
| `+0x40` | `NpcGhost_StartFollowing` `0x9bd780` | into the actor system (Framework `+0x498`), event actor name `companion`, `timer` 40, class bit 0 cleared (no zone suspension), `following` set |
| `+0x48` | `NpcGhost_StopFollowing` `0x9bda40` | `timer` 0, class bit 0 set, into the current room's root (destroyed when none), `following` cleared |
| `+0x50`, `+0x58` | `0x9be090`, `0x9be0a0` | `following`, `param0Max` |

**NpcComponent** (`NpcComponent_Ctor` `0x943e90 (component, ComponentArgs*, StringView* row key, parent node sp*)`, `NpcComponent_vtable` `0x1bdc570`, `NpcComponent_Dtor` `0x944b50`; 0x2e0 bytes, the size `NpcGhost_MakeNpcComponent` (`0x9c0870`) and the plain maker (`0x943b40`) allocate). NpcGhost's component class (vtable `0x1be3ad8`) adds no fields.

- `owner` is a strong pointer to the owning actor, null unless it is of the type at `0x1cab3e8` (its virtual `+0x68` IsA).
- `parentNode` is the constructor's fourth argument, the actor's root (NpcGhost: `Actor_GetOrMakeRoot` with `"root"`), on which the model is made.
- `row` is the Npc.gsheet row (TESTING.md, Npc row); `NpcComponent_Setup` (`0x945510`, virtual `+0x20`) destroys a
  component without one. The setup reads `layoutId` (`NpcComponent_LayoutId` `0x94ace0`, virtual `+0x1d0`: the owner's
  int LEB parameter the row's `actorArgs.layoutID` (`+0x140`) names, counted from 1), which `NpcComponent_CheckLayout`
  compares (Persistence, Appearing), and `identifier` (`NpcComponent_Identifier` `0x94ad40`, `+0x1d8`: the string LEB
  parameter the row's `actorArgs.identifier` (`+0x144`) names, type 4 or 5), checks `NpcComponent_CheckLayout`, starts
  the resource loads and makes `readySignal`.
- `NpcComponent_OnResourcesLoaded` (`0x968db0`) takes `flowchart`, `modelResource` (the bfres) and `tailorResource` (the drsb), runs `NpcComponent_BuildComponents` (`0x957d80`) and emits `readySignal`.
- `animationCache`: `NpcAnimation_Get` (`0x9670d0`) binds the animations of `modelResource` (the animation set) and keeps them by name.
- `eventSubA` and `eventSubB` are handles on the event manager's `+0xe8` and `+0x140` signals; `task` is a
  `Task_Post` task (`0x947f38`); `event_280` an event of the flowchart (`0x80c290 (out, flowchart, name)`, virtual
  `+0x178`; likely).
- Animation settings: `NpcComponent_AnimSetting` (`0x955640`, virtual `+0x2c0`) finds the row's
  `graphics.animSettings` entry by animation name (`NpcAnimSetting` 0x20 bytes: name, `blendTime` frames, `lipsync`,
  `cull`) and caches it in `animSettingCache` (key a `SharedString`, value an `AnimSettingRef` `{sheet sp, entry*}`).
  `NpcComponent_PlayAnimation` (`0x948a70`, virtual `+0xc8 (out, blend, component, StringView* name, bool force)`)
  keeps the playing animation's entry in `animSetting` (`animSettingSheet` keeps the sheet alive; default the static
  entry at `0x1702058`, blend time 0) and blends by its `blendTime` when the blend argument is negative; virtual
  `+0x160` (`0x94a050`) tests its `lipsync`.
- Event triggers: `NpcComponent_ConnectTriggers` (`0x959370`) connects each row `eventTriggers` entry of condition
  10-13 through `NpcComponent_ConnectTrigger` (`0x9595d0`, `(component, link index 0-3)`): the linked area actor
  (AreaEventBox, AreaEventSphere, AreaEventBoxWithoutJump: its signal at `+0x2b8`; AreaLevelOpen: a function at
  `+0x2f0`) goes into `triggerLinks` (`NpcTriggerLink`, 0x20 bytes: link index, area sp, handle), then the linked
  areas' enabled bits are cleared. A component built before it started sets `triggersPending`, and
  `NpcComponent_Start` (`0x946a30`, virtual `+0x28`) connects them then; a started one connects at once.
  `NpcComponent_OnDestroyed` (`0x946ae0`, `+0x38`) disconnects them and clears both caches.
- `popRejected`: `NpcComponent_OnDestroyRequested` (`0x946a60`, `+0x30`) and the resources' callbacks emit
  `readySignal` without a build when `NpcComponent_CheckPop` fails; `0x96872c` then hides the NPC (likely).
- `collisionMaterial`: the collision shape's material word (`NpcComponent_CollisionMaterial` `0x949e30`, virtual
  `+0x130`): bits 0-5 the row's `collision.material` (`+0x1c4`), bits 14-17 1 when `attribute` (`+0x218`) bit 2,
  bit 19 when its bit 1 is clear.
- `groundCache` (`GroundQueryCache`, 0x30 bytes, vtable `0x1b51a70`: `+0x10` the last position, `+0x20` the
  attribute, `+0x24` valid): `NpcComponent_GroundAttribute` (`0x957750`, virtual `+0x348`) returns `groundAttr`'s
  attribute when it lives, else `GroundQuery_Cached` (`0x307a0`) from the root's world position straight down, which
  reuses the cached attribute while the position stays within an epsilon and casts a ray otherwise.

`NpcComponent_BuildComponents` makes each component through a virtual of its own:

| Field | Virtual | Maker | Made |
|---|---|---|---|
| `model` | `+0x200` | | always |
| `animator` | `+0x208` | `NpcComponent_MakeAnimator` `0x94b310` | a SkeletalAnimation, or a LayeredAnimation (the ghosts'; MODELS.md) |
| `collision` | `+0x210` | `0x94b350` | with the row's `collision.shape` not 0 |
| `damageCollision` | `+0x218` | | with `damageCollision.enable` |
| `eventChara` | `+0x220` | | |
| `doAction` | `+0x228` | | with `doAction.type` not 0 |
| `ocarinaReaction` | `+0x230` | `0x950910`: OcarinaReaction, a 0x1b0-byte Node (`0x5d4760`) | with `ocarina.enable` (`+0x200`), `ocarina.distance` (`+0x204`) |
| `lookAtTarget` | `+0x238` | `0x950c30`: LookAtTarget, a 0x150-byte Node (`0x95fa80`) | with `lookAtOffsetY` (`+0x170`) not 0 |
| `tailorPlayer` | `+0x240` | | with `graphics.drsb` set |
| `waterEvent` | `+0x248` | `0x951310`: WaterEvent, 0x160 bytes (`0x35dfc0`) | with `graphics.waterChannel.enable` (`+0x98`), `offsetY` (`+0x9c`) |
| `groundAttr` | `+0x250` | | with a collision |
| `tailorWaterSurface` | `+0x258` | `0x9519f0`: TailorWaterSurface, a 0x190-byte Node | with `waterChannel.enable`, `limitDepth` (`+0xa0`) |
| `facial` | `+0x260` | `NpcComponent_MakeFacial` `0x952380` | with `graphics.facial` naming a Facial.gsheet row |
| `aimControl` | `+0x268` | `0x952cc0`: AimControl, 0x130 bytes (`0xc8e00`) | with `graphics.interestIK` (`+0xb8`) set |
| `materialAnim` | `+0x270` | `NpcComponent_MakeMaterialAnimator` `0x9535f0` | |
| `shellBeacon` | `+0x278` | `0x954310`: ShellBeaconComponent, a 0x150-byte Node (`0x87140`) | with `shellSensor` (`+0x208`) entries |

TailorWaterSurface holds copies of `tailorPlayer` (its `+0x148`) and `waterEvent` (`+0x158`). Virtual `+0x288`
(`0x954520`) is the `RailTrailer` maker.

**FacialControl** (`FacialControl_Ctor` `0x11cdf0 (component, ComponentArgs*, StringView* row key, std::function*, u32
priority)`, `FacialControl_vtable` `0x1b5e008`; 0x150 bytes, which `NpcComponent_MakeFacial` allocates): `priority` 100
from the maker; `onChangeA` is the constructor's function, `onChangeB` the maker's (`0x120150`); `sheet` keeps
Facial.gsheet (`FacialSheet_ptr` `0x1cc3c40`) alive; `row` is `Datasheet_FindRow(sheet, key, 0x50)`.
`NpcComponent_MakeMaterialAnimator` counts no facial slots while
`facial` is null or its entity's flags have `kDestroyRequested`.

- `FacialControl_Setup` (`0x11d4c0`, virtual `+0x20`) builds `expressionsByLabel` (the expressions' indices by
  `label`, FNV-1a; only expressions whose `otherFrames` count equals `others`' count), pushes one 0 into `frames` per
  material animation slot (eye if set, mouth if set, then each other), makes `eyeBlink` (EyeBlinkControl, 0xc0 bytes,
  virtual `+0x98` `0x11ec10`) when virtual `+0x88` (`0x11eb50`: `eye` set and an expression with
  `eyeBlinkSettings`) holds, and `lipSync` (LipSync, 0x100 bytes, `+0xa0` `0x11f510`) when `+0x90` (`0x11ebb0`:
  `mouth` set and an expression with `lipSyncSettings`) holds.
- `FacialControl_SetExpression` (`0x120a30`, `(facial, index)`) sets `expression`, ignoring an index out of range.
- `FacialControl_Update` (`0x11e4d0`, virtual `+0x80`) advances each of `frames` modulo the expression's lengths
  (`eyeLength`, `mouthLength`, `otherLengths`); nothing while `frozen`.

**FacialRow** (a Facial.gsheet row, 0x50 bytes, the row size `FacialControl_Ctor` asks for; the schema's names `mKey`,
`mMaterialAnimNames` `{mEye, mMouth, mOthers}` at `+0x10`/`+0x20`/`+0x30`, `mExpressions` at `+0x40`): the material
animation component gets `SlotCount()` slots, one per `others` animation and one for each of `eye` and `mouth` that
is set, and one more after them, the slot `NpcComponent_PlayMaterialAnimation` uses. `expressions` are
`FacialExpression` (0x60 bytes: `label`, `eyeFrame`, `mouthFrame`, `otherFrames` `+0x18`, `eyeLength` `+0x28`,
`mouthLength`, `otherLengths` `+0x30`, `eyeBlinkFrame` `+0x40`, `eyeBlinkSettings` `+0x48`, `lipSyncCloseFrame`
`+0x50`, `lipSyncOpenFrame`, `lipSyncSettings` `+0x58`); a settings pointer (null for none) is a `FacialSettings`
(0x20 bytes: key, then the open and the close random range as `{min, range}`).

## Enemy damage

**EnemyReactionComp** (`EnemyReactionComp_vtable` `0x1b5be00`, 0x310 bytes, `EnemyReactionComp_Ctor` `0xeeb70 (component, ComponentArgs*, config*)`; base ReactionComp `0x1b5ed60`, `ReactionComp_Ctor` `0x12fae0`): an enemy's health and reactions. Each class makes it with its own name (`DeguArmos:EnemyReactionCom`, `EnemyReactionComp`, `EnemyReaction`, ...) and keeps it in a field (DeguArmos `+0x1480`); the vtable identifies it. Fields: `+0x160` the class's config (`+0xc` u16 max life), `+0x168` s32 life, `+0x16c` u16 max life, `+0xc4`/`+0xc6` s16 frames since the last hit and the invincibility limit, `+0xca` u16 mask (bit k: reaction k off), `+0xcc` u16 pending reactions (bit k), `+0xce + k` u8 the actor state for reaction k (0xff none), `+0xdb` current reaction (0 none), `+0xdc` the previous one. Reactions rank by index; 6 damage, 11 dead. Virtuals: `+0x88` life, `+0x90` set life, `+0x98` max life, `+0xa0` set max life, `+0xa8` add, `+0xb0` subtract (clamped at 0), `+0xb8` dead (life < 1), `+0xc0` refill, `+0xc8` kill (life 0), `+0xe0` update (`0xec310`, then the base's `0x1300a0`).

- The base update requests reaction 11 (`+0xcc` bit 11) while the comp is dead, the current reaction is below 11, mask bit 11 is clear and `+0xd9` (reaction 11's state) is set. Regular enemies map reaction 11 to their dead state in their setup (`strb #0xd9`; the Octorok kin at `0x5303a4`: state 12); each class's update takes the highest pending reaction (`ReactionComp_Take` `0x1310e0 (comp, current state)`, the state to change to) and changes state.
- `ReactionComp_Request` (`0x130ef0 (comp, u8 kind, u8*, attacker*, hit*)`): sets the bit when the kind outranks the current one, its mask bit is clear and it has a state; `ReactionComp_Clear` (`0x131030 (comp, kind)`).
- **The hit.** The collision step (`0xd51a4`) builds a hit (about 0x3a8 bytes on its stack) and calls `Hit_Dispatch` (`0xb4990 (hit*, u16* result, attacker*)`), which calls `DamageColComp_OnHit` (`0xe5490 (damage comp, attacker*, hit*)`): the damage comp's damage lambda (`std::function` at `+0x330`, called `(attacker*, hit*)`, returning the result: low byte 0 hit, 1 guarded, 2 and 3 other), else the defence's reaction for the attack type (`Defence_Reaction` `0xb3d40 (u8 type, defence*)`: the type's reaction byte 0 gives 3, 1, 2 or 7-11 give 1, others 0 unless defence `+0x76` is 1). `Hit_Dispatch` then emits the damage comp's signal for the result: 0 `+0x3b8`, 1 `+0x360`, 2 `+0x410`, each `(attacker*, hit*)`. Hit: `+0x0` and `+0x10` weak pointers (`+0x10` the damage comp), `+0x20` the attack (`+0x8` u16 power, `+0xc` u8, `+0xd` u8 attack type), `+0x28` the defence (an f32 multiplier per attack type `+0x8`..`+0x58`, a u8 reaction per type `+0x5c`..`+0x71`), `+0x30` the position (the hit effect's), `+0x70` the direction (XZ). The attacker is a hit record (Hit records, below). A damage comp's maker (`0x869f0`) keeps its params' defence at `+0x278`.
- Bosses decide death in their hit handler instead (no reaction 11 state). Armos Knight (`DeguArmos:DmgColCom` at `+0x1470`, the only damage comp, one shape on bone `n_hara`; defence `0x16f03a0`, every multiplier 1, `+0x76` 0): its damage lambda (`0x2763c0` → `DeguArmos_HitVerdict` `0x26af10 (actor, attacker*, hit*)`) guards (1) in states 2-6 (Attack .. AttackLanding), gives 2 in state 0 (AppearEvent), handles arrows (actor id 0x183) while `+0x172c` is set, else `Defence_Reaction`: only attack types 7 and 13 hit, all others are guarded or ignored. No direction test is in it. Result 1 goes to `DeguArmos_OnGuard` (`0x26b2b0`, the defence reaction), result 0 to `DeguArmos_OnDamage` (`0x26b360 (actor, attacker*, hit*, 0)`, through the lambda `0x275f90` on signal `+0x3b8`): nothing (returns 0) while the reaction comp's `+0xc4` < `+0xc6`; else `+0xc4` 0 and `+0xc6` 10 (10 frames until the next), the hit effect (`0x153de0` with hit `+0x20`, `+0x30`), subtracts defence[type] × power (at most 65535); dead in phase 2 (`+0x172d`): `DeguArmos_ChangeState` (`0x26b750 (actor + 0xff0, 9)`, `mDeadEventReactionState`, whose event is `DeguArmosEnd`) and returns 1; dead in phase 0 or 1: `DeguArmos_SetPhase` (`0x26b790 (actor, phase + 1)`: phase 0 life 16 and `+0x172c` 1, phases 1 and 2 life 8 and `+0x172c` 0); not dead or after a phase change: requests reaction 6 (state 10, Damage). States (`+0xffd`): 0 AppearEvent, 1 Move, 2 Attack, 3 AttackRise, 4 AttackTopWait, 5 AttackFall, 6 AttackLanding, 7 BraceAfterFall, 8 BraceAfterLanding, 9 DeadEvent, 10 Damage, 11 Defense, 12 Suspend.
- **Result signals.** A damage comp (`DamageColComp_Ctor` `0xe0f70`, vtable `0x1b5b8b0`, 0x4b0 bytes) has three signals, 0x58 bytes each: `+0x360` result 1, `+0x3b8` result 0, `+0x410` result 2. `Hit_Dispatch` emits one through its vtable `+0x30` with `(attacker*, hit*)`, the same pointers the damage lambda got; a slot's call operator (`std::function` `+0x30`) gets `(functor, attacker*, hit*)`. Classes connect slots through the object at signal `+0x10` (its vtable `+0x20`, a `std::function*`), in their constructor; most enemy classes connect to results 0 and 1 (Armos Knight, Master Stalfos, Moldorm, Genie, Angler Fish, Gohma, Dodongo, Hinox, Slime Eye, Evil Eagle, Lanmola, regular enemies).
- **Hit records.** The "attacker" every hit path passes is a hit record (0x1da8 bytes): `+0x0` and `+0x10` weak pointers, up to 8 entries of 0x3b0 bytes from `+0x20` (each a hit: weak pointers `+0x0` and `+0x10`, then the hit's data), u32 count `+0x1da0`. The collision step passes the attack side's record (`x21` = its object `+0x2f0`, `0xd52fc`) and hands `Hit_Dispatch` a copy of one of its entries as the hit (`0xd5348`). A reaction request (`ReactionComp_Request`) replaces the reaction comp's record (`+0x68`, a 0x1db0 allocation) with a copy of the attacker's (`ReactionRecord_Copy` `0xb5350 (record, attacker*)`: both weak pointers, the count and each entry's weak pointers and 0x384 bytes of data; it reads the count at `+0x1da0` whatever the attacker is) and stores the hit's index in it at `+0x70` (`ReactionRecord_Find` `0xb5ab0 (record, hit*)`: the first entry whose live weak pointers equal the hit's `+0x0` and `+0x10` (`HitEntry_Matches` `0xb4270`); it adds nothing, -1 when none matches). The reaction states and the guard path read that entry with a bounds check that throws `std::out_of_range` for an index of 8 or more (`0x7cc6a0`, `0x7ce1e8`, `0x7ce40c`, `0x7cefb4`, `0x131cf4`), which ends the game. A synthetic hit therefore needs an attacker record of 0x1da8 bytes whose count is at least 1 and whose entry 0 is the hit.
- **The hit's `+0x0` is the attack comp.** Attack collision comps (`AttackColComp_Ctor` `0xcecf0`, vtable `0x1b5aef8`, 0xf140 bytes; `AttackColComp_Make` `0x43220`, which attaches it to the actor by name, e.g. `DeguArmos:AttkColCom2` at DeguArmos `+0x1460`; Link's are named `SwordAttackColComp`, `SwordChargingAttackColComp`, `SwordRotationAttackColComp`, `JumpSwordAttackColComp`, ..., made by `Player_InitAttackCollisions`) keep the hit records (`+0x2f0`) the collision step reads. The Damage reaction state's enter (`0x7c72c0` → `0x7ce390` → `DamageReaction_Apply` `0x7ce520 (state, node, record, entry, ...)`) locks the entry's `+0x0`, checks it alive (`+0x43` bit 6) and calls `AttackCol_Vector` (`0x1cb110`): the `std::function` at `+0xf100` (its target `+0xf120`; none gives a constant vector). Any other object at the hit's `+0x0` is read 0xf120 bytes in, past its end.
- **After death.** `DeguArmos_OnDamage` has no dead check: with life 0 every hit past the invincibility frames takes the death branch again, and `DeguArmos_ChangeState` (`0x26b750`) does not skip the current state, so state 9 exits and enters again while `DeguArmosEnd` runs. Its own verdict lets hits through in state 9 too (only states 0 and 2-6 are refused). A caller that emits hits itself refuses them while the reaction comp is dead (virtual `+0xb8`); between phases life is refilled at once, so it is 0 only after the killing blow.

**Slime Eye** (Key Cavern's boss): three classes, each with a state machine at `+0xff0` (state `+0xffd`) and its own `ChangeState (machine, state)`.

- **EnemyDeguZol** (the LEB actor, the controller; no damage comp; `DeguZol_ChangeState` `0x2bc960`). States: 0 Sleep, 1 PlayerEnterEvent, 2 Phase0Wait, 3 Phase0ZolRain, 4 Phase1, 5 Phase2Wait, 6 Phase2Dive, 7 Phase2CombineReady, 8 Phase2CombineJumping, 9 Phase2CombineScaling, 10 End. Its setup ends in `DeguZol_SpawnParts` (`0x2bab30`): `Actor_Create` with key 0, its own zone and its parent as owner, of one `EnemyDeguZolPhase1` (id 0x257, kept at `+0x1328`) and two `EnemyDeguZolPhase2` (id 0x258, `+0x1338`, `+0x1348`), each `{ptr, ctrl}`; key 0 keeps them out of `ActorRegistry_ptr`. It gives Phase1 a weak pointer to itself (`+0x1820`) and two callbacks (`0x2f9ae0` into `+0x18c0`, `0x2f9b90` into `+0x18f0`).
- **The fall.** The BattleAreaCollisionComp's (`+0x12f0`) player-enter slot sets state 1, whose enter runs the event `DeguZolStart` and whose event end sets state 2; its exit puts Phase1 in 3 (the shadow on the floor). States 2 and 3 alternate: after 2 s a rain of `EnemyZolGreen` (id 0x1b, key 0, into the vector `+0x1310`), three a rain. The setup connects `DeguZol_OnPlayerCrash` (`0x2c0c50`, handle `+0x13c4`) to Player signal 4 (PLAYER.md, Signals), which the Pegasus Boots bonk emits wherever it happens: unless the state is 4 to 10, state 4. The arguments are not read. State 4's enter puts Phase1 in 4 (`PopBeforeEventWait`; 6 `PopCombinedShadow` after a recombination, `+0x1988`), which runs the event `DeguZolPop`, then 7, 8 `PopFreeFall` (the body at its spawn x/z, y 10, falls until grounded), 9 `PopLand`, 10 `Wait`.
- **EnemyDeguZolPhase1** (the body). States: 0 Sleep, 1 Phase0ShadowSilent, 2 Phase0ShadowAnimOneShot, 3 Phase0ShadowAnimLoop, 4 PopBeforeEventWait, 5 PopEvent, 6 PopCombinedShadow, 7 PopBeforeFallWait, 8 PopFreeFall, 9 PopLand, 10 Wait, 11 Move, 12 Damage, 13 Recovery, 14 Stretching, 15 Defense, 16 End. Six damage comps `DeguZol:P1Dmg[InitialOne|LeftEnd|CenterLeft|Center|CenterRight|RightEnd]`, each with a result-0 slot calling `DeguZolPhase1_OnDamage` (`0x2d6ac0`): nothing within the reaction comp's (`+0x17f0`) invincibility (10 frames), else state 12, or 15 at stage 4. The stage (`+0x1980`, `DeguZolPhase1_SetStage` `0x2ea810`, which also swaps the body shapes) rises by one in Damage's exit and falls by one in Recovery's. `DeguZolPhase1_OnStretchSensor` (`0x2ecda0`): a contact on the sensor comp `+0x17a0` whose actor passes a check (`0x2dc060`; guess: Link dashing) sets `+0x198a` (cleared at the end of every update) and state 14, whose enter calls `+0x18c0` (`DeguZol_PlaceHalves` `0x2bcd30`) and whose update goes to 12 while `+0x198a` holds and `|+0x1984|` passes a threshold (guess: the push), else to 10. Damage's enter with the stage above 3 calls `+0x18f0`, the split (`DeguZol_OnSplit` `0x2bc8a0`): the halves placed at the body's side bones and started (`0x3136a0`), Phase1 state 0, the controller state 5. So the split takes four hits on the P1Dmg comps (stage 4, before Recovery lowers it) and then the dash into the middle.
- **EnemyDeguZolPhase2** (a half; `DeguZolPhase2_ChangeState` `0x300e40`). States: 0 Sleep, 1 Wait, 2 MoveJump, 3 MoveRise, 4 MoveFreeFall, 5 MoveLand, 6 DiveJump, 7 DiveRise, 8 DiveFreeFall, 9 DiveLand, 10 CombineJump, 11 CombineRise, 12 CombineRised, 13 CombineScaling, 14 CombineScaled, 15 CombineFreeFall, 16 the damage reaction state, 17 the dead reaction state, 18 DeadLastOne, 19 End. One damage comp `DeguZol:P2Dmg` (`+0x15e0`) and an EnemyReactionComp (`+0x1600`, config `0x16f0aac`: life 16; reaction 6 → state 16, 11 → 17): a regular enemy's death. Its damage lambda (`DeguZolPhase2_HitVerdict` `0x2ff7b0`) takes `Defence_Reaction` and then guards (1) a hit whose position lies outside the eye's side (the XZ direction from the half to the hit against the one to the bone `+0x1670`). The result-0 slot (`DeguZolPhase2_OnDamage` `0x3001a0`) calls `EnemyReaction_ApplyHit` (`0x135c80`), so a hit of power ≥ life kills; in states 6-8 and 10-15 it drops the damage reaction but keeps the damage.
- **The end.** `DeguZol_Update` (`0x2bd310`) sets state 10 once the controller is in 5 to 10 and both halves are in 17 or 19. In state 5 (`0x2beff0`) the controller dives every 2 s; after 20 s with neither half dead it recombines them (state 7) into Phase1.

## For the module

- **Slime Eye from code** (static analysis, not run): the fall is `DeguZol_ChangeState(controller + 0xff0, 4)` while the controller's state is 2 or 3 (after `DeguZolStart`), the same as the bonk's slot does; emitting Player signal 4 (`player + 0x1d20`, vtable `+0x30`, `(signal, const Vec3*, WeakPtr*)`) reaches every listener. The split is `DeguZolPhase1_SetStage(body, 4)` then `DeguZolPhase1_ChangeState(body + 0xff0, 12)`, with the body in `Wait` or `Move`. The body and the halves have key 0, so the registry does not hold them: reach them through the controller's `+0x1328`, `+0x1338`, `+0x1348`. A half dies to its P2Dmg comp's result-0 signal with power ≥ its life, as `DBG hit` emits it; unless one dies within 20 s of the split, the two recombine.

- **Spawning from code** (the ghost puppets, `DBG puppet spawn`): the table's resources load synchronously on the game thread unless loaded already; a non-zero params key makes the actor registry-unique and keeps it alive through the registry; the name has at most 31 characters; owner the actor system keeps it across rooms and levels, and its `+0xb4` stays -1, so no room adopts it.
- **Ghost zones** (measured 2026-10-23, Tail Cave's entrance room and the room north of it): NpcGhost is zone-scoped (`+0xa4` bit 0, from its constructor's `0x805`), and an actor's zone is its params `+0xc`. One in Link's zone is suspended when he leaves the room (flags `0x11b`, `+0xa2` 3: bit 2 saved as set) and active again when he comes back; one that follows him into the next room stays suspended beside him. One in zone -1 stays active (`0x11f`) in every room and through the scroll. The module spawns every puppet in zone -1. A spawn in a level's first frames gets zone -1 from Link's `+0xb0`, which is still -1 then.

## Open questions

- What fires the scene's `+0x7b8` signal.
- Who sets `+0xa4` bit 0 for other classes (the class constructors write `+0xa4`).
- The type `0x1b511e0` of the component whose `+0x58` gates readiness, and who sets flags bit 4 for it.
- Queue `+0x2f0` (bit 10) has no runner in 1.0.0.
- The level load from state 6 to state 1, and level data vtable `+0x48` (level kind; 3 dungeon) and `+0x50`.
- LEB record `b` (`+0x86`): no reader found (actor, room and LEB code searched).
- The companion slots `+0x10` / `+0x18`: which two shared_ptr views of the actor they return.
- NpcComponent: the owner type (`0x1cab3e8`) its constructor checks; the row's `eventTriggers` conditions 0xe-0x11
  (virtual `+0x320`) and the names of conditions 10-13.
- FacialControl `frozen`: no writer found besides the constructor.
