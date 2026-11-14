# Entrances (game 1.0.0)

What moves Link to another level or locator, what the game keeps about the entrance he used, and what depends on it when
a level loads: the ground for entrance shuffles that lead the game's jumps from the seed file (DESIGN.md, Shuffled
dungeons and Entrances, **Patch**) with one static module. Offsets are module offsets; `las.sym` names them at their
runtime addresses (module offset + 0x8504000).

Each finding has a status and the verdict of an independent check by a Fable 5.2 subagent, given the addresses and game
files but not the conclusion:
- **covered**: the module's `jumps` hook leads it, or it needs nothing.
- **static data**: it needs a generic hook and a seed-file table (Plan).
- **open**: not established.

The `jumps` hook: `LevelJump` in `game/hooks/level.cpp` rewrites `(level, locator)` of a `LevelManager_LevelJump` call
by the seed file's `jumps` line for `(current level or *, level, locator)` (`AP::LeadJump`, `ap/ap_jumps.cpp`), before
the original runs. The current level is the last one `SetCurrentLevel` named.

## Funnels

Every level change and every move to a locator ends in one of three functions. A whole-text closure of their direct
callers and of the data and ADRP references to each finds no other way in.

| Funnel | Callers | Hooked | Fable |
|---|---|---|---|
| `LevelManager_LevelJump` `0x864d50` | `Event_ProcessLevelJump` (`0x81bdc4`), `SetGameState` in states 3 and 4 with an empty level and locator (the reload from the save), `FishingManager_OnEventResult` (`0xe1bc58`, code literal `Field`/`Field_09B`), five Chamber Dungeon UI functions (`ChamberUi_*`, `PdPage_OnChoice*`) | yes | confirmed |
| `LevelManager_WarpInLevel` `0x8654d0` (manager, locator, offset, facing) | `Event_ProcessLevelJump` only, for a pending jump with an empty level | **no** | confirmed |
| `SetGameState` `0x87b780` | `Event_ProcessLevelJump` (`$RETRY` 4, `$QUIT` 1, `$ENDING` 5), `FileOperate20_StartLoad`, `FileMenusScene_Update`, `EndingScene_Update`, `TitleScene_Update`, `Framework_MainLoop` | n/a | confirmed |

- `SetCurrentLevel` has one caller, `LevelLoad_Finish` on a full load. The reload after `LevelManager_WarpInLevel` is not
  one (`RoomManager_ToState2` passes false), so an in-level warp changes neither the save's location nor the hook's
  current level. Fable: confirmed.
- `LevelManager_LevelJump` copies the level and locator only when they are not empty, and calls
  `Inventory_StoreLinkPosition`. Fable: confirmed.

Event actions set the event's pending jump (`Event_SetLevelJump`), which `Event_ProcessLevelJump` runs at the request's
end. Fable: confirmed.

| Action | Target | Funnel |
|---|---|---|
| `GameControl.RequestLevelJump` `0x7ff160` | flow params `level`, `locator`, `offsetX`, `offsetZ` | LevelJump; an empty `level` warps in the level |
| `GameControl.RequestLevelJumpInCurrentLevel` `0x7ff2d0` | level = the save's `LevelName` | LevelJump |
| `GameControl.RequestWarp` `GameControl_RequestWarp` | empty level, params locator and offsets | WarpInLevel |
| `GameControl.RequestRestartWarp` `0x7ff580` | `RequestWarp`; in the Chamber Dungeon `RequestLevelJumpInCurrentLevel` | WarpInLevel |
| `GameControl.Retry` / `Quit` / `GoToEnding` | `$RETRY` / `$QUIT` / `$ENDING` | SetGameState |
| `WarpUI.JumpToSelectedLocation` `0x7ea4c0` | outside dungeons and Lv09 (`LevelIndex` 0xff): `Field` and the `WarpPoint.gsheet` row's `mLocator`; in a dungeon or Lv09 the in-level `OcarinaWarpPoint`; in the Chamber Dungeon the save's level and `OcarinaWarpPoint` | LevelJump / WarpInLevel |
| `<Actor>.RequestLevelJump(linkIndex)` `Actor_RequestLevelJump` | the linked `AreaLevelOpen`'s LEB params 0/1; no vanilla flow uses it | LevelJump |
| Chamber Dungeon actions (`PanelDungeon_JumpTo*`, three more) | literals, the Chamber's locator | LevelJump |

## Ways Link changes level

| Way | Target from | Funnel | Status | Fable |
|---|---|---|---|---|
| Door (`AreaLevelOpen`, `AreaLevelOpen_StartEvent`) | LEB p0 level, p1 locator, as `Area.bfevfl` args; also the same-level doors of Lv07 and Lv08 | LevelJump | covered | confirmed |
| Hole to another level (`AreaHoleLowerLevel`, `0x727f0`): EagleKeyCave's four, the well (`Field_11A`), `Field_02O` | LEB p0/p1; the well's entry `FallInCameraSet1` jumps to the literal `TownWell`/`TownWell_01A` | LevelJump | covered | confirmed |
| Hole in the level (LEB p0 empty): `Lv01TailCave_04G`, Eagle's Tower, Lv09 | LEB p1; with LEB p3 an int ≠ 0, offset = Link − hole (x, z) | WarpInLevel | static data | confirmed |
| Lanmola's pit (`AreaAntlion`, `AreaAntlion_StartFallOutAnt`) | `Field.bfevfl` `FallOutAnt`: literal `LanmolaCave`/`LanmolaCave_02A` | LevelJump | covered | confirmed |
| Stairs and ladders (`AreaUpstairs`/`AreaDownstairs`, `AreaStairs_StartEvent`): Tal Tal caves, Kanalet Castle, Richard's cave, every dungeon's side-view rooms | LEB p0 locator; `Stairs.bfevfl` entries all `RequestWarp` | WarpInLevel | static data | confirmed |
| Dungeon warp pad (`ObjDungeonWarpHole_StartWarp`) | LEB p0, or p1 while its switch 1 is on | WarpInLevel | static data | confirmed |
| Vacuum (`EnemyDeathBallVacuum_StartWarp`) | LEB p0 (`DeathballWarpPoint`) | WarpInLevel | static data | confirmed (target from LEB, not code) |
| Raccoon (`NpcRaccoon_StartEvent`) | `RequestWarp(Field_07D)`, offset computed at runtime | WarpInLevel | static data | confirmed |
| Field warp holes and Manbo's pond (`AreaWarpHole_StartEvent`, `ObjWarpManbo_StartEvent`), Manbo's Mambo outside dungeons | WarpUI: `Field` and the warp point's `mLocator` | LevelJump | covered | confirmed (from the funnel; their actor code not read) |
| Manbo's Mambo in a dungeon or Lv09 | `OcarinaWarpPoint` of the level | WarpInLevel | covered (not an entrance) | confirmed |
| Instrument (`Instrument_StartGetEvent`, `MusicalInstrument.bfevfl` `get`) | LEB p0 `Field`, p1 the Field locator outside the dungeon's door | LevelJump | covered | confirmed |
| Flow literals: `Bed` `BedRoomExamine` (DreamShrine), `CuccoKeeper` `cuccoStrike10`, `FairyQueen` gets, `Fisherman` `lure*jumpfirst` (FishingPond), `GameShopOwner` `Angry`/`ExitGameShop`/`ForgetGameShop`, `Marin` `beach_talk`, `BeachDate`, `CapeDate`, `Field` `cape_talk2`, `Lv09WindFishsEgg` `StairsUp` (Ending; started by `ObjLv9Stair`'s `Npc.gsheet` trigger when Link enters its linked `AreaEventBox`) | the flow | LevelJump | covered | confirmed |
| Rapids goal (`RaftShopMan` `normalGoal`/`timeAttackGoal`) | `RequestLevelJumpInCurrentLevel(Field_04P)` | LevelJump | covered | confirmed |
| Fishing's end (`FishingManager_OnEventResult`) | code literal `Field`/`Field_09B` | LevelJump | covered | confirmed |
| Arrival entries that warp: `PlayerStart` `FallInLv7`; `Stairs` `RunScrollWhiteLv09` (`Lv09Play_Update` once Link's z ≥ 108), `RunScrollLv06` (an `AreaUpstairs` entry), `MightPunchCrashOut` (`MightPunch_CrashOutTimer`) | literals | WarpInLevel | covered (not entrances) | confirmed |
| Warp menu (module), LASR's game-over Save & Quit | `RequestLevelJump(Field, Field_11C)` | LevelJump | covered (`*` line) | in game (warp menu) |
| Game-over Retry, file load, System Load | Save fields (Saved location) | SetGameState | covered | confirmed, in game |
| Pits, water, falls (`Player_Respawn`) | `respawnCoords` in the level | none | covered | confirmed |
| Owl statues, `ObjColorHole`, `ObjLv9Stair`, `AreaEagleTower`, `AreaReturnAbyssIgnoreGrid`, rapids areas, `ObjRaft` | no funnel call in their code | none | covered | confirmed for direct calls |

## Saved location

The save's `LevelName` (`+0x0`), `Locator` (`+0x40`), `RestartPos` (`+0xdc`) and `RestartDirection` (`+0x121`, bits 0–2):

- **Arrival.** `SetCurrentLevel` writes the level, the locator and `RestartPos` = the jump's offset (the world position for
  `$NONE`), not the direction. A led jump arrives under the led names: the save holds them. Fable: confirmed.
- **Retry** (game over, Continue). `$RETRY` runs `SetGameState(4)`, refills health to 3, 5, 7 or 10 hearts by the
  maximum, and writes `Locator` `$NONE` and `RestartPos`/`RestartDirection` from the scene's `retryB` when set, else
  `retryA` (Link's place at the end of the level's load). Fable: confirmed.
- **A save.** `SaveManager_Commit` copies the live save into the copy that is written (`SaveSnapshot`), whose
  `SaveWriter_Lambda` gives it `Locator` `$NONE` and the retry position (`SaveCurrent_SetRetryLocation`). Then the
  location providers override the copy: a save in `FishingPond` writes `Field`/`Field_09B`, one in `PanelEdit`
  `DanpeiHouse`/`DanpeiHouse_01A_a` (position 0). The live save keeps its arrival locator. Fable: confirmed.
- **Continue** (title, file menu, System Load). `SetGameState(4)` → `LevelScene_Start` starts the level from the save:
  `$NONE` places Link at `RestartPos`, with no arrival event. Fable: confirmed.
- So a continue and a retry put Link where he arrived through the led door, without a "last door" of their own.
  **Covered.** The FishingPond provider writes the vanilla way out: **static data** once the pond's way in is shuffled
  (Plan).

In game (scratch mod: Madam MeowMeow's door leads into the dog house, Tarin's and Ulrira's houses swapped; 2026-11-12):

| Step | Result |
|---|---|
| Madam's door | dog house; save `DogHouse`/`DogHouse_01A` |
| Death inside, Retry | the dog house at its arrival spot (15, 0.07, 8.06), not where Link died; `$NONE` |
| Its exit | outside Madam's door (`Field_11B_a`) |
| Death outside, Retry | that arrival spot, `$NONE` |
| Warp menu | outside Ulrira's door (`Field_12B`, `* Field Field_11C` line) |
| Tarin's door / its exit | Ulrira's house / outside Tarin's door |
| Save in the dog house, System Load of that file | the dog house at its arrival spot, `$NONE` |

## Arrival

- `LevelLoad_Finish`: locator `$NONE` places Link at the room manager's `+0x120` as a world position; any other locator
  goes through `LevelData_FindPlayerStart`, the `.lvb`'s `tagPlayerStart` names, plus the offset. **An unknown name
  places Link at the origin** (a static zero entry, angle 0x7fffffff), without an error. Fable: confirmed.
- The `.lvb` names equal the LEB `TagPlayerStart` p0 names in every level, with the same positions. A name need not be
  its room's (`Lv09WindFishsEgg_08D` stands in room 02A). Fable: confirmed.
- The arrival event: `RoomManager_FindArrivalStart` finds the loaded `TagPlayerStart` (0x18f) named by the locator, and
  `TagPlayerStart_StartEvent` starts its `PlayerStart.bfevfl` entry (LEB p1, default `default`), with the companion
  variant from `AreaLevelOpen_CompanionEntry`. None for `$NONE`. Fable: confirmed.
- `TagPlayerStart` LEB p7 `InitialPosition` is read by no code. It marks the `.lvb`'s first `tagPlayerStart`, which
  `LevelData_Load` keeps as the level's default start (`LevelData_DefaultPlayerStart`); only the dungeon map reads it,
  for its entrance marker. A dungeon's inside entrance does not move in a shuffle, so the marker stays right.
  Fable: confirmed.
- The arrival depends on the landing locator only. **Covered.**

## Load-time state

| State | Keyed by | Status | Fable |
|---|---|---|---|
| Place name and its first-show flag (`UI_FieldMapIcon_*`, `UI_FieldMapTraverse_*`, dungeons' marker flags): `PlaceName_OnZone`/`PlaceName_OnRoom`, `LocationName.gsheet` row by zone id range (`LocationName_RowByZone`) | zone | covered | confirmed |
| Dungeon banner (`Place:Lv<n>Dungeon` in `DungeonIn_Lv1..8`) | the landing `TagPlayerStart`'s entry | covered | confirmed |
| `LevelIndex`, dungeon items (compass, map, keys, beak), map pins | level name (`SetCurrentLevel`: Lv01..Lv08 0–7, PanelAdventure 8, Lv10ClothesDungeon 9, else 0xff) | covered | confirmed |
| BGM, zone audio | zone records, arrival entries' `PlayZoneAudio` | covered | not checked |
| `FieldPosition`, `IndoorPosition` (`Inventory_StoreLinkPosition`, also from the maps) | where Link left the level: the overworld map shows the door he took | covered | writes confirmed, readers not checked |
| Level flags, removal memory | cleared or released by the jump | covered | doc (ACTORS.md) |
| `LevelEnterTime`, `PreviousLevelTime` (`_Marin` exit talks compare it) | time in the level left | covered | confirmed |
| `AutoSaveLocation.gsheet` lookup | level | covered | not checked |
| No code reads the locator after arrival but `LevelLoad_Finish` and `RoomManager_FindArrivalStart`; no code keeps the previous level's name | | covered | confirmed |

## Door trigger

- `AreaLevelOpen_Setup` makes a static trigger box (`Collision`) the size of the LEB scale. With LEB p3 an int 1 it
  also makes a solid `WallCollision` box 0.05 larger: while Link cannot enter, the trigger is off and the wall stops
  him. Both are on only while LEB switch 0 holds (`AreaLevelOpen_HideUpdate`, `_ShowUpdate`), and the trigger is off
  while an event runs. `AreaLevelOpen_Update` fires when a live Link is inside and `Player_CanEnterArea` holds: no
  facing or height check beyond the box. LEB p7 is read by no code. Fable: confirmed.
- `Player_CanEnterArea` is false in death, resurrection, fallAbyss, demoEvent, blownAway and hookshot; in the air (jump,
  fall) only on a side-view ladder or carrying the flying rooster. Fable: confirmed with that list.
- The four `AreaLevelOpen`s under the floor (Lv07 03C, Lv08 03H and 08A, Lv10 04C) have switch 0 of type 4 (none),
  which `ActorSwitch_IsOn` reads as off: they never fire. Fable: confirmed.

## Doors that share a key

A `jumps` line leads every jump with its `(current level, level, locator)`:

| Key | Jumps | Consequence |
|---|---|---|
| (each dungeon, `Field`, the Field locator outside its door) ×8 | the entrance's exit and the instrument's warp | the instrument warps out of the door that leads in (wanted) |
| (`Lv10ClothesDungeon`, `Field`, `Field_08H`) | exit 08C, the `AreaLevelOpen` under 04C's floor, the Fairy Queen's four jumps | the same |
| (`CraneGame`, `Field`, `Field_12D`), (`CuccoHouse`, `Field`, `Field_01K_a`) | the door, the keeper's ejections | ejected out of the door that leads in |
| (`Lv09WindFishsEgg`, `Field`, `Field_01G`) | two exits, 02A and 08C | the egg stays |
| (`EagleKeyCave`, `Field`, `Field_01K_d`) | four holes | one landing |
| (`Field`, `Field`, `Field_04P`) vs (`RapidsRideTicket`, `Field`, `Field_04P`) | the rapids goal vs the ticket house's exit | apart only by the from-level: a `*` line on `Field_04P` would take both |

No connector has two exits on one key; QuadupletHouse has two exits in one room, to `Field_09C_a` and `Field_09C_b`.
The eight telephone boxes are eight levels. **Covered.** Fable: confirmed (it found the egg's second exit).

## Connectors and one-way doors

- Connector levels (more than one Field door): every exit has its own target, so a coupled shuffle needs one `jumps`
  line per door each way (`door_jumps`) and nothing more. Their inside stairs warp in the level and are not shuffled.
  **Covered.**
- One-way: EagleKeyCave's holes (→ `Field_01K_d`), the well (→ `TownWell_01A`; `TownWell`'s exit → `Field_11A`),
  `Field_02O` (→ `EagleTowerExit_01A_a`), Lanmola's pit (→ `LanmolaCave_02A`), the egg's exits (→ `Field_01G`), the
  Fairy Queen (→ `Field_08H`). All go through `LevelManager_LevelJump`, so a `jumps` line leads each. A drop led to a
  door's spot arrives with that spot's entry (Link walks out instead of falling). **Covered.**
- Warp points: `(any level, Field, <warp point locator>)`, from houses and caves too, so a shuffle of them takes `*`
  lines; the warp point's unlock bit stays by `mIndex`. **Covered.**

## Doors, companions and arrival events

- Companions (inventory `+0x9d`: 1 BowWow, 2 Marin, 3 the ghost, 4 the flying rooster; the actor at Player
  `+0x1b98`). Every level jump destroys the companion's actor (`RoomManager_LevelJumpTeardown` → `Player_Reset` →
  `Player_DestroyCompanionActor`) and leaves `+0x9d` as it is. On a full load `RoomManager_SpawnCompanion` makes it
  again beside Link only when the level's `.lvb` `config` byte 1 is set. It is clear in 19 levels: Lv01 to Lv10,
  ArmosShrine, BedRoom, CatfishsMawEnter, DreamShrine, FishingBoat, FishingPond, KanaletCastleEnter, MamboCave and
  SeashellHouse. So no companion is ever in a dungeon, whichever door leads in: it stays joined and comes back in the
  next level that allows it. **Covered**, by the level itself. Fable: confirmed (a second caller of
  `RoomManager_SpawnCompanion` is the ghost's own join in `RoomManager_Update`).
- In game (2026-11-12, Madam's door led into Tail Cave): BowWow, Marin, the ghost and the rooster are absent in Tail
  Cave with `+0x9d` kept, and come back out of Madam's door with Link.
- The door's `Area.bfevfl` entry (LEB p2) belongs to the overworld door. Its companion variants
  (`LevelDungeonIn_Ghost`, `_Marin`, `levelDownStairsJump_Ghost`, `_Marin`, `DungeonIn_Lv7_Ghost`) follow the door:
  the ghost refuses Tail Cave's door wherever it leads (in game). The randomizer skips Marin's and the ghost's events,
  so only BowWow and the rooster follow Link, and neither has a door variant. **Covered.** Fable: confirmed.
- Gates are the door actor's LEB switch (`TailCaveShutterOpened`, `AppearStairsFld*`, ...): they follow the door.
  **Covered.**
- Arrival entries that do story work follow the landing spot: `Field_11B_a` `ExitOutMadam_BowWow` (BowWow handed back
  with `BowwowClear`), `Field_01O` `Level7DungeonIn_FlyingCucco` (the rooster leaves after `Lv7InstrumentGet`),
  `Field_04F` `BowwowExitOut` (Owl `Fifth`), `Field_10D` `ExitOutShop` and `Shop_01A` `EntranceIn_Tool` (stealing),
  `KanaletCastleEnter_01B` (`GoldenLeaf1`), `GhostHouse_01A` (`Ghost3_Clear`), `DanpeiHouse_01A_a`, `MoriblinCave_02A`,
  `BoomerangCave_01A`, `HiddenZoraHouse_01A`, `Field_16F` (`MarinJoin`), the Marin date levels, the `_Marin` dungeon
  exits, `Opening` (the intro). With a shuffle they happen at the spot, whatever door led there: BowWow goes home when
  Link leaves through Madam's door. **Covered** for the module; the logic follows the spots. Fable: confirmed.

## Blocked landings

Overworld doors with something on or in front of the spot Link lands on. A led exit can land there while the door is
still closed from outside. Found in the RomFS (Fable: confirmed, with the wall distances and the Armos doors
corrected). The cases marked *tested* were run in game on 2026-11-12: a Mabe house's exit led onto the door with its
flag cleared, the enemies killed on landing.

| Landing | On or in front of it | Door gate | Flag (the obstacle's switch) | Closed | Flag set before landing |
|---|---|---|---|---|---|
| `Field_01E`, `01H`, `05K`, `06C`, `11K` | `ObjRock`/`ObjGrass` on the stairs | its flag | `AppearStairsFld02E`, `02H`, `05K`, `06C`, `11K`: lifting or cutting sets it, and the obstacle is not made while it is set | Link lands on the obstacle and steps off it, free (06C, 11K tested) | obstacle gone, Link comes up the stairs (all tested) |
| `Field_08G` | `ObjGrave` on the stairs | its flag | `AppearStairsFld08G`: pushing sets it | as above (tested) | grave pushed aside, stairs open (tested) |
| `Field_10C` | `ObjWeathercock` on the stairs | its flag | `WeathercockOpened`: pushing sets it | as above (tested) | weathercock moved aside, stairs open (tested) |
| `Field_15J` | `ObjMarthaStatue` on the stairs, another beside it | none | `MarthaStatueOpened` | beside the statue, free (tested) | statue moved aside, stairs open (tested) |
| `Field_14E` | nothing: Mamu's stairs are a closed pool | `MamuMazeClear` | — | beside the pool, free (tested) | not tested |
| `Field_10N`, `Field_11O` | `EnemyArmosDark`/`EnemyArmosLight` on the stairs; the landing is 1.5 off them | its flag | `AppearStairsFld10N`, `11O`: the Armos clears it when made and sets it when it wakes | free (both tested) | the Armos clears it again, the stairs stay shut (10N tested) |
| `Field_02D`, `02P_b`, `09G`, `09H`, `09N`, `13N_c`, `16E` | `ObjFieldCrackedWall` between the door and the landing | none | `FieldCrackedWall02D` … `16E`: bombing sets it | in front of the intact wall, free; the wall stops the way back (09H tested) | wall open, Link walks out of the doorway (all tested) |
| `Field_08F`, `Field_16J` | rocks or grass on three sides, 1.5 away, not removable | none | — | free (one side open) | — |
| Dungeon doors `Field_14D`, `12F`, `09M`, `03L_a`, `02A`, `01O`, `08H` | Tail Cave's and Key Cavern's gates, Face Shrine's gate, Angler's waterfall, Turtle Rock's head, Eagle's Tower, the Color Dungeon's grave puzzle (its push sets level flags; `AppearStairsFld08H` comes from elsewhere) | various | various | not tested | — |

Link never gets stuck: on covered stairs he is placed on top of the obstacle's collision and steps off it, and in front
of a cracked wall or the Armos he stands free. The door stays shut from outside until it is opened the vanilla way.
Setting the obstacle's flag before landing gives the vanilla arrival, except at the Armos doors. **Covered**: a
`jumps` line's optional sixth field is a flag `LeadJump` sets as it leads the jump, and `door_jumps` gives a jump out
onto a covered door its flag (`ap_entrances.OPENING_FLAGS`: the stairs' gates, `MarthaStatueOpened`, the cracked walls'
flags). Mamu's (`MamuMazeClear` opens the maze) and the Armos doors are left out. Tested in game 2026-11-12 with led
exits onto every door of `OPENING_FLAGS`: the flag is clear inside and set on landing, and Link comes up the stairs or
out of the doorway. The logic needs no change: the door is reached from inside, and from outside it stays a way in only with the
vanilla rule.

## Overworld map icons

`UiFieldMapIcons.gsheet` has 27 icons at fixed positions about 4.5–6 units north of their vanilla door: the shop, Tracy,
the witch, eight telephone boxes, the raft shop, the Trendy Game, Dampé, the Seashell Mansion, the library, the fishing
pond (no door; `Fisherman.bfevfl` sets its flag) and the ten dungeons. An icon shows once its flag is set, by the place
name of the zone Link enters. With a shuffle the flag is set inside, and the icon stays at the vanilla door. The eight
telephone boxes share `mNameLabel` `TelephoneBox_map` (flags `TelBox1..8`, not numbered as the levels: TelBox1 is
`TelephoneBox02_Mebe`); the module's `markers`, which find rows by `mNameLabel`, cannot tell them apart. **Static
data.** Fable: confirmed.

## Plan

The module stays as it is for every seed; a shuffle is data in the seed file.

1. **Keep `jumps`** for every door, drop, NPC ejection, instrument, warp point and the warp home: it covers everything
   in the tables above that reaches `LevelManager_LevelJump`, connectors and one-way doors included. A full shuffle of
   overworld doors (connectors, one-way drops, coupled or decoupled) needs only the generic additions 2 and 3, and
   covered doors take their opening flag (Blocked landings).
2. **`LeadJump` zeroes the offset** of a jump it leads. Holes with LEB p3 and the raccoon pass an offset relative to
   their own target. Generic, no data.
3. **`icons` section**: `<show flag> <x> <y> <z>` per moved icon. At `kMapSheetsLoaded` (where `markers` applies) the
   module moves the `UiFieldMapIcons` row with that show flag. The patcher gives an interior's icon its new door's place
   (the icon's vanilla offset from its door added to the new door). `markers` can become `icons` lines for the dungeon
   rows too.
4. **Patcher check**: every `jumps` target is a `tagPlayerStart` name of its level (an unknown one drops Link at the
   origin), and a `*` line's `(level, locator)` is no other jump's target (`Field_04P`). Static, in `verify_patch.py`.
5. Only for these shuffles:
   - **Fishing pond**: a hook on `SaveLocation_FishingPondFill` passes `(FishingPond, Field, Field_09B)` through
     `LeadJump`. No new data.
   - **In-level transitions** (stairs, ladders, dungeon warp pads, in-level holes): a hook on
     `LevelManager_WarpInLevel` with a `warps` section `<level> <locator> <new level> <new locator>`. Within the level
     it calls the original with the new locator. To another level it calls `LevelManager_LevelJump` (both run in the
     room manager's state 1 only).

No per-seed code is needed for any case found.

## Open questions

None bears on an entrance shuffle. Left over:
- Whether `RunScrollWhiteLv09` fires in vanilla: the egg's `.lvb` areas end at z 96, and `Lv09Play_Update` starts it at
  z ≥ 108 (rooms the egg's maze may build at run time). It warps in the level.
- `Player_CanEnterArea`'s ladder field (ladderInfo `+0x5a`) and the second writer of Player `+0x1b8c` bit 18
  (`0xd59b94`).
