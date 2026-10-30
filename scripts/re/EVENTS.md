# Events (game 1.0.0)

How an event takes the actors, Link among them, and how the running event and its flowchart are reached, from static analysis of the main module and a probe in the test build. Offsets are module offsets; `las.sym` names them at their runtime addresses (module offset + 0x8504000). The module's `game/event.hpp` types the structures below.

## The running event

The event manager (`EventManager_ptr`) keeps its requests in a deque of shared_ptrs (`+0x1b0` blocks of 256 elements of 0x10 bytes, `+0x1c8` the first element's index, `+0x1d0` the count; 0 while no event runs); the first is the running one. A request's `+0x60` is the event `Event_Make` built (400 bytes):

| Offset | |
|---|---|
| `+0x10` | the flow (0x1c8 bytes, made from the flowchart and the entry, `0x10924f0`); its `+0x128` is the flowchart |
| `+0x20` / `+0x28` | the entry's name as a shared string |
| `+0x30` | the entry's name (data, length), as `BgmManager_OnEventStart` compares it with `ZeldaScroll` |
| `+0x78` / `+0x80` | a hash map (buckets, bucket count) keyed by EventChara pointer: the actors in the event (`Event_HasChara`) |
| `+0x180` | a kind, which `Player_OnEventStart` turns into Link's `eventKind` |

The flowchart is the `.bfevfl` file loaded and relocated in place: the layout of the evfl library the patcher uses (`ResFlowchart`: `EVFL`, counts at `+0x10`, name `+0x20`, actors `+0x28` of 0x38 bytes, events `+0x30` of 0x28 bytes, the entry names' dictionary `+0x38`, entries `+0x40` of 0x20 bytes with the main event at `+0x1c`), with each offset a pointer and each string a u16 length and its characters. The probe found the same flowchart (`SinkingSword`, entry `diamond-island`) at that place for a LASR heart piece's event.

## What an event's start does to the actors

- `Actor_RegisterEvents` (`0x22a30`), run once an actor is ready, subscribes it to the event manager's start (`+0xe8`) and end (`+0x140`) signals. Their callbacks (`0x22dd0`, `0x22d20`) call the actor's virtual `+0xb0` and `+0xb8` unless the actor is being destroyed (entity `+0x43` bit 6). An actor registered while an event runs gets `+0xb0` at once.
- The base actor's `+0xb0` and `+0xb8` are `Actor_SuspendUpdates` and `Actor_ResumeUpdates`: the update task runs the class's update only while flags bits 8 to 10 are set, so every actor that keeps the base slots stands still for the whole event. The actor update phase (`Phase_ActorUpdate`) itself runs during events; only the play time stops. Event actions reach their actors through the event system, not through the actors' updates, so a suspended actor still acts in its event. NpcGhost overrides the slots; which other classes do is open.
- `Player_OnEventStart` (`+0xb0`) asks whether Link's EventChara (player `+0x2f8`) is one of the event's actors (`Event_HasChara`, `0x81aac0`). If it is: `eventKind` from the event's kind, `kInEvent` (flags bit 0), `damageFlashTime` 0 and the DamageColComp off. If not: `Actor_SuspendUpdates`, so Link stands still like the others. It clears flags bit 24 either way. `Player_OnEventEnd` undoes whichever happened, and destroys `eventEntity`.
- `kInEvent` takes Link into the demoEvent state (22): the first of his state hooks (PLAYER.md, State machine), checked each frame before the action transitions, has one condition (`0xd44940`): `kInEvent` set and the state neither 22 nor 16 (`moveTarget`); the current state's virtual `+0x60` must let him go. The demoEvent state's enter (`0xdac500`) plays an idle animation (unless the handoff says otherwise) and zeroes the knockback's velocity; its update (`0xdabee0`) moves him with `Player_Move` and a zero vector, so only gravity moves him; its exit is `0xdac8e0`. The state he was in (a dash, a swim) has ended, and after the event he starts again from `locomotion`.
- Link's control mode (`Player_ControlMode`) does not depend on events: the scene's mode, the override or `kHeld`. During an event the input is still read; the demoEvent state ignores it.
- `BgmManager_OnEventStart` pauses the sound effects that play and sets `eventQuiet` (new ones at 0.2) for every event but `ZeldaScroll` (FRAMEWORK.md).

## How an event gets its scene actors

An event made with `Event_Make` and handed to the event manager has the global actors (Link, `Timer`, `Camera`, `Audio`, `Fade`, ...) and no others: actions of a flowchart actor that is a room's actor (the Wind Fish's egg) reach nothing. Charas join an event by `Event_AddChara` (`0x80f7d0`, `(event, EventChara sp*)`), under the names the chara gives (`+0x358`, `+0x380`; a chara without `+0x378` is skipped), between `Event_Prepare` and the request. `Actor_FindEventChara` (`0xfd850`, `(out sp, actor)`) gives an actor's chara; `ActorRegistry_FindByKey` the actor of a LEB key.

The game's own path is an owner's: an area linked to an NPC (its LEB entry's first relationship section, link index 0–3) is entered, and the NPC's component plays its row's event: `NpcComponent_OnTrigger` (`+0x330`) → `NpcComponent_StartTriggerEvent` (`+0x1c0`) → `NpcComponent_EventForCondition` (`+0x180`, the `eventTriggers` entry of condition 10 + link index whose `additionalConditions` hold) → `NpcComponent_StartEvent` (`+0x170`) → `NpcComponent_MakeEvent` (`+0x178`): `Event_Make` on the row's flowchart (`eventInfo.eventAsset`), `NpcComponent_SetupEvent` (`+0x310`: `Event_Prepare`, `Event_AddChara` for each linked actor whose chara names one of the flowchart's actors, then its own), then the request. The Ballad's area in `Field_01G` (`AreaOcarina`) owns the orchestra that way and links the egg, the instruments and the owl; in `Ending_12O` the Wind Fish owns the ending and links its `AreaEventBox`, the owl and the instruments. The module adds the egg to the opening it starts by its LEB key (`AP::Bridge::RequestEvent`); measured 2026-10-28, the egg then cracks, breaks and sets its switch as after the Ballad.

## Open questions

- Which actor classes override `+0xb0` and keep updating during events.
- Whether an event with no camera action moves the camera.
- What the flow object's other fields hold.
