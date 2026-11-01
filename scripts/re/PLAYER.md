# Link's player actor (game 1.0.0)

Link's actor, his state machine, his character controller and his animation controller, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/actors/player.hpp`, which lists every field; this file gives the evidence and what its comments leave out. "Guess" marks what the code suggests but was not followed to the end. MODELS.md has the model and animation components he is made of.

## Player

`Player` (`Player_vtable` `0x1c17b70`, 0x1f90 bytes; `Framework::player`): the actor table's factory `Player_Create` (`0xd24890`) → `0x83da50` → `Player_MakeShared` (`0x83daf0`: `operator_new(0x1fb0)`, the control block 0x20, the player at `+0x20`). `Player_Ctor` (`0xd26910`): `Actor_Ctor`, the vtable, then zeroes from `+0x2a8` (0x1f0 bytes), `+0x1130` (0x208) and `+0x1340` (0x248). Destructor `0xd2b030`, deleting `0xd2bc20`.

`Player_Setup` (`0xd2bc50`, virtual `+0x20`) makes the components, each under the component name the header's comments give, and loads `Playerlink.bfres` and `tailor/Player.drsb`. `Player_InitParts` (item shape groups) and `Player_InitAnimator` (the call `0xd1a4e0` that makes the layered animator) are labels inside it.

| Virtual | |
|---|---|
| `+0x28` | `0xd31de0` |
| `+0x38` | `Player_OnDestroyed` (`0xd31df0`): ends in `Actor_OnDestroyed`; clears the signals' listener sets, updates the locators, drops `eventEntity` |
| `+0x48` | `Player_DropCarried` (`0xd32720`): clears both halves of `carried`, `kCarrying`, the TailorPlayer's NoLift and the animation's upper slot (`PlayerAnimation_ClearUpper`); resets `locItemAL`'s local transform; clears `putTarget.valid` |
| `+0x68` / `+0x70` | IsA / TypeId (`0xd32730` / `0xd328c0`) |
| `+0x78` | Register (`0xd32920`) |
| `+0xa0` | `0xd32a50`: does nothing (ACTORS.md, Classes) |
| `+0xb0` | `Player_OnEventStart` (`0xd32a60`, EVENTS.md): when Link's EventChara is in the running event (`Event_HasChara` `0x81aac0`), `eventKind` from the event's `+0x180` (1 → 2, 2 → 1, 3 → 4, 4 → 5, 5 → 6), `kInEvent`, `damageFlashTime` 0, the DamageColComp off (its virtual `+0xa8`); else `Actor_SuspendUpdates` |
| `+0xb8` | `Player_OnEventEnd` (`0xd32d30`): destroys `eventEntity`, clears `eventKind`; then clears `kInEvent` and turns the DamageColComp on, or `Actor_ResumeUpdates` |
| `+0xe0` | `Player_OnRoomChanged` (`0xd32f60`, ACTORS.md, Classes): `sideView` from the room data `+0xb9` == 1, the TailorPlayer's `SideView` / `TopView` |
| `+0x110` | `0xd33470`: in state 22 plays animation key 0x34 or 0x16b by `sideView` |
| `+0x118` | the fall (`0xd336c0`) |

**Components.** The makers called in `Player_Setup`: `modelComp` `SkeletalModelComponent_Make` (model `link`); `animator` `0xd1a4e0` (`LayeredAnimation_Make` with 2 layers, then `AnimationLayers_SetBoneMask(layers, 1, "spine_a")`); `materialAnim` `0x3b48e0`; `playerCollision` `PlayerCollision_Make` (`0x93f4a0`); `damageCollision` `0x869f0` (its `+0x330` takes the damage lambda `Player_OnDamage`); `eventChara` `0x60eb0` (chara `Link`); `camera` `0xd42630`, given the shaker, its `+0x515` set by the update; `cameraShaker` `0xc6320`; `boneEffect` `0x1fe170` (bones `clavicle_L`, `head`); `damageColor` `Player_MakeDamageColor`; `waterEvent` `0x35dfc0`; `eventSensor` `0xd3f840` (sensor `player:EventSensor`); `itemSensor` `0xd42290` (`player:ItemSensor`); `ladderInfo` `0x93ce20`; the locators `SkeletalModel_MakeLocator`; `hookshot` `0xd80c00` (the HookShot actor, model `HookShot`); `boomerang` `0xd63850` (`linkBoomerang`); `tailor` `TailorPlayer_MakeLayered` (flags NoLift, TopView, SwordInvisible, ShieldInvisible); `tailorWaterSurface` `0x9790e0`; `animPowerUp` and `animDefence` `MaterialAnim_Bind`; `facial` is the FacialComp `PlayerAnimation_Make` takes. `boneClavicleL` and `boneHead` come from `0xd3fec0 (skeleton, bone name)`, `shapeBody` from the model component's virtual `+0xb8`, `selfWeak` from the actor's weak self (`0xd35050`).

**parts** (`PlayerParts_vtable` `0x1c17ca8`, base `PartsVisibility_vtable` `0x1c11a20`; 0x40 bytes): `+0x8` the model component (shared), `+0x18` an `unordered_map` from s32 group to `{vector<s16> shapes +0x18, u8 visible +0x30}` (`PartsVisibility_AddShape`, `PartsVisibility_SetGroup`). The item shape groups: 0 sword, 1 shield, 2 ocarina, 3 shovel, 4 hookshot, 5 flippers, 6 magic rod, 7 boots, 8 second sword, 9 mirror shield; `Player_ShowItem` (`0xd3e390 (player, u8 item, bool)`) picks the sword and the shield by inventory `+0x9c` bit 0 (`SwordLv2`) and bit 1 (`MirrorShield`).

**Owned objects.** `bottle`: a PlayerBottle (vtable `0x1c19918`; `+0x8` a PlayerItemModel, `+0x10` a PartsVisibility of
vtable `0x1c19938`; `PlayerBottle_Init`). `bow`: a PlayerItemModel (0x38 bytes, vtable `0x1c19638`; `+0x28` its model
component, which the damage handler reads; `PlayerBow_Init`). Both are models of `Playerlink.bfres` (`Bow`,
`linkBottle`) from `ModelResource_Get`, made with `SkeletalModelComponent_Make` and hung on a bone locator
(`SkeletalModel_MakeLocator` `0x105c070 (out, model component, const char* bone, translate, rotate, scale)`, e.g.
`itemA_R`; Link's own are `locItemAL` `itemA_L`, `locItemAR` `itemA_R` and `locHead` `head`) with
`SkeletalModel_Attach` (`0xfb8320 (model component, locator sp*, bool, int* flags)`).

- `attackCols` (0x2b0 bytes, `Player_AttackCols_Ctor` `0xd1f2f0`, made by `Player_InitAttackCollisions` `0xd40720`):
  one collision component per attack (SwordAttackColComp, SideViewAttackColComp, SwordChargingAttackColComp,
  SwordPokeAttackColComp, JumpSwordAttackColComp, MagicPowderColComp, ShieldAttackComp, TreadOnAttack,
  BottleColComp and others); `Player_AttackCols_Update` (`0xd21980`) switches them on and off each frame.
- `knockback` (`PlayerKnockback`, 0x30 bytes; `PlayerKnockback_Set` `0xd23340`, `PlayerKnockback_Scale` `0xd24800`):
  `velocity` is the hit's direction × a speed from the attack's `+0xa` (0.065, 0.1075, 0.15, 0.0325) × 60, which
  `PlayerKnockback_Scale` scales by the hit data unless `state` is 1; `angle` and `facing` come from `atan2`.
  `PlayerKnockback_Update` (`0xd230c0 (knockback, player)`, kinds 1 to 5, from the update) turns `facing` towards
  `angle`, counts `moveFrame` to `moveFrames`, then `endFrame` to `endFrames` and sets `state` 1.
- `actionTransitions` (`PlayerActionTransitions`, 0x1c0 bytes, `PlayerActionTransitions_Ctor` `0xd92b50`): the
  transitions into the input-driven states (State machine).
- Actors Link spawns: `arrows` (PlayerArrow, actor id 0x183, `Player_SpawnArrow` `0xd521f0 (player, index)`),
  `magicRodBalls` (PlayerMagicRodBall, 0x1b5, a pool: `Player_FreeMagicRodBall` `0xd515e0` gives the first free
  slot, `Player_MagicRodBall` `0xd51790` spawns into it), `swordBeam` (PlayerSwordBeam, 0x1fc, `Player_SpawnSwordBeam`
  `0xd55000`). `Player_OnDestroyed` and `Player_Reset` (`0xd532d0`) destroy them.

**Signals** (`signals`, `Signal`s of 0x58 bytes, ACTORS.md, Nodes; `PlayerSignal` names them by index):

| Index | Class | Argument | Emitted by |
|---|---|---|---|
| 0 `kOcarinaStart` | vtables `0x1b5e890` / `0x1b5e878` | u8: the song (`EquippedSong`, inventory `+0xa1`) | the Ocarina state's enter (`PlayerState_Ocarina_Enter` `0xdd6b00`), before it runs the song's entry of `event/Common.bfevfl` (`ocarinaAreaName` overrides the entry) |
| 1 `kOcarinaEnd` | as 0 | u8: the song | the song event's completion (`PlayerState_Ocarina_SongDone` `0xdd7770`, a lambda the enter connects) |
| 2 `kShovelStart` | `0x1b59608` / `0x1b59698` | none | the Shovel state's enter (`0xdda850`) |
| 3 `kShovelEnd` | constructor `0xd27b50` (vtable `0x1c17d18`) | u8 | the Shovel state's exit (`0xddaf40`) |
| 4 `kDashCrash` | constructor `0xd27be0` | `(const Vec3*, WeakPtr*)`: Link's world position, the object he ran into (moved out by the slot) | the Dash state's inner `Crash` state's enter (`PlayerState_Dash_CrashEnter` `0xda7a70`), a Pegasus Boots bonk; Slime Eye listens (ACTORS.md, Slime Eye) |
| 5 `kFallAbyss` | constructor `0xd27c70` | `const Coords*` | the FallAbyss state's enter (`0xdb1150`): Link's world coordinates |
| 6 `kRespawn` | as 5 | `const Coords*` | after `Player_Respawn(player, &respawnCoords, 1)` (`0xd336d0`, `0xd59e50`, `0xdb1f00`) |
| 7 `kClingerReleased` | `0x1b5b288` / `0x1b5b270` | `{Actor*, ctrl}` | a clinging actor comes off (the update, `Player_ClearClingers`) |

**flags** (`PlayerFlags`): bit 0 in an event (`Player_OnEventStart` / `OnEventEnd`), 1 set by the constructor
(`Player_ShowItemGroupBit` does nothing while it is set), 2 carrying (`Player_DropCarried` clears it), 4
`kControlModeOverride`, 6 the ground update gives the tailor the ground's material in the air too, 7 the setup done, 8
`kHeld`: an actor holds Link (`holder`; it also blocks interacting, `Player_CanInteract`), 10 and 11 cleared each frame
(11 tells the camera), 12 `kForcedAngle`, 13 and 14 cleared by the reset, 15 to 17 and 23 written by a respawn (23 read
by `Player_Move`), 20 read by the DemoEvent state's enter, 24 set with `kHeld` outside states 1, 11, 18 and 22.

**carried** is a weak pointer (control block `+0x19b8`). `Player_DropCarried` locks it and clears both halves, so a
non-null pointer means "holds something" only because the game always clears both. `carryNoDrop`: the carried actor
was already a child of Framework `actorSystem` when the carry began (`0xd9f200`), which otherwise adds it there (not
with the actor's `+0x290` set). Unless either is set, the drop (`0xd347e0`) and the throw (`0xde7fd0`) move it into
the room at the level scene's `+0x228` (`Room::root`; without one they destroy it).
`putTarget` (`PlayerPutTarget`, vtable `0x1c17cf8`, set by the constructor at `0xd26bd4`): the carried actor's spawn
position, taken when the carry begins (`0xd264d0`; not for an actor being destroyed), checked by Locomotion
(`0xd26590`), aimed at by Throw (`0xde7fd0`); `valid` is cleared by `Player_DropCarried`, Throw and Carry.

**holder** (`Player_SetHolder` `0xd588f0`, `Player_ReleaseHolder` `0xd53b00`): the actor holding Link; setting it
sets `kHeld` (and bit 24), changes `playerCollision`'s filter and zeroes `holdOffset` and `holdOffsetTarget`; the
update (`0xd37774`) moves `holdOffset`'s x and z a tenth of the way to `holdOffsetTarget` each frame (at least
0.0001, never past it).

**companion** (`Inventory.Companion`, `+0x9d`: BowWow, Marin, Ghost, FlyingCucco; ACTORS.md, Companion interface):
`Player_SpawnCompanion` (`0xd53c20`) makes it when `Companion` is not 0, `Player_SetCompanion` (`0xd53d40`) stores
it and sets `Companion` from its virtual `+0x20`, `Player_ReleaseCompanion` (`0xd53ed0`) sets `Companion` 0 and
calls its `+0x48`, `Player_DestroyCompanionActor` (`0xd538e0`) destroys its actor (virtual `+0x10`).

**Ground.** `Player_UpdateGround` (`0xd54180`) casts `groundQuery` down from Link each frame (`0x10d19c0`, `(length,
world, query, origin, dir, filter)`).
The hit gives `groundNormal` (up when not grounded), `groundAttribute`, `groundActor` (the collider's owner, a
platform) and, when grounded outside states 14 and 21 and not on kinds 5 to 8, `respawnCoords` (with `respawnKind`
0). `onGround` is the character controller's grounded result; `groundWalkable` the hit inside the field grid with a
normal's y above 0.5; `onGroundSurface` the hit's height within an epsilon of Link's (on the ray's ground, not on an
object); `neverGrounded` holds until the first ground update; `airTimer` counts seconds since grounded while
`prevMoveResult` (last frame's `moveResult`) is 0.

- `groundAttribute`: bits 0-5 the material (the TailorPlayer's footsteps; materials 0x15 and 0x16 slow Link to
  0.75), bits 6-9 the kind: 6 (`0x180`) pulls Link to the centre of the 1.5 m grid cell under him (`pullVelocity`,
  `pullStart`, `pullCenter`, `pullBlend`, `pullTime`; `Player_Move` `0xd57260`), 7 (`0x1c0`) deep water (swimming
  with `ExtraItems.Flippers`, `onGround` and `onGroundSurface`); kinds 5 to 8 are never respawn ground. On a ladder
  (state 14) it is the ladder's `+0x58`.
- `Player_Move` adds the knockback and the pull, then calls `playerCollision`'s Move (virtual `+0xd0`): `moveDelta`
  is the displacement handed to it (states read it × 1/dt as Link's velocity), `moveResult` its result byte.
- `RayQuery` (0xb8 bytes, `RayQuery_vtable` `0x1c50d18`; a generic ray-cast query, Shadow Link's object has one
  too): `callback` at `+0x50` (`RayCallback_vtable` `0x1c50d38`) holds the hit record at its `+0x8` (attribute index
  `+0x10`, position `+0x18`, normal `+0x24`, distance `+0x30`), `hit` points at it; `flags` bit 0 is a hit, and
  `hitPosition`, `hitNormal` and `collider` are cached from the hit on first use (flags bits 3, 4, 2).
- `lowerLevelHoles`: AreaHoleLowerLevel's Link-enter code (`0x71e20`) inserts the area's entity
  (`0xd21ba0 (player + 0x1630, entity sp*)`), its leave code (`0x70dc0`) erases it (`0xd22240`); `0xd22550 (player +
  0x1630)` (from `0xd33710` and `0xda35a8`) clears both sets. While not empty, FallAbyss's exit (`0xdb1f00`)
  runs `AreaHoleLowerLevel_StartEvent` on the first entity instead of the damage and respawn. `slowAreas`: the
  module computes its address only in the constructor, the destructor and `0xd22550`, so it stays empty and the
  0.75 move never applies.
- `bodyContacts` (`PlayerContact`, 0x70) and `bodyTouches` (`PlayerTouch`, 0x60): this frame's hits of
  `playerCollision`'s controller (Character controller; `Player_OnBodyContact` `0xd4ffb0`, `Player_OnBodyTouch`
  `0xd4e7d0`); a touch's other actor goes into `touchedActors`. `Player_ClearContacts` (`0xd343e0`) empties both.

**Respawn.** `Player_Respawn` (`0xd337a0 (player, Coords*, bool)`) puts Link at the coordinates. The update
(`Player_UpdateAnimation`) grows `airTime` while no event runs, Link is off the ground (`groundQuery.flags` bit 0)
and not on a ladder or in `fallAbyss`; above 1.5 s it calls `Player_Respawn(player, &respawnCoords, false)`. A
respawn zeroes `moveDelta`, `hit.attacker`, `hit.pending` and `handItems`, writes the handoff's `variant` and `flag`
and sets `pendingState` to 0xff; `respawnKind` (0 ground, 1 the side-view point, 3 the reset; compared with 1),
`onGround` and `sideView` pick its branches.

**Damage.** Health is the inventory's: `Player_OnDamage` (`0xd4c690`, a lambda on the DamageColComp, the player at
its `+0x8`) subtracts from `Health` (quarter hearts, clamped to `Inventory_MaxHealth` `0xdf8260`), halves the damage
with the Guardian Acorn (item 0x6f) or the blue tunic (`UpgradeItems.ClothesBlue`), doubles it with `HeroMode`,
clears `KillCountNoDamage`, and sets `damageFlashTime` from the damage colour animation's length. LASR's Damage setting
(`damageModifier`) rewrites the health subtractions: hits (`0xd4c754`, in `Player_OnDamage`), traps (`0xd7c8c8`), falls
and drowning (`0xdb1f74`) and Blaino (`0xd96950`). `Player_SetHit` (`0xd249c0 (hit, kind, attacker WeakPtr*, hit data*)`) writes `hit`
(`PlayerHit`, 0x60): the hit data's `attack` (its `+0xd` the attack type) and `defence` (a reaction per attack type),
`kind`, `attacker`, `direction` from the attacker's position, `pending` (cleared once taken, `0xd58c80`), the
knockback scales `scaleA` (kind 2) and `scaleB` (kinds 1, 4, 5), and `reaction0`/`reaction1` from the defence.

**Input** (`input`, `PlayerInput`, 0x70 bytes): rebuilt each frame by `Player_UpdateAnimation`. `checks` are
`PlayerActionCheck`s (0x40: u8 action, `Function check(Player*, Controller*)` at `+0x10`) that
`Player_RegisterActionChecks` (`0xd3f1d0`) registers; each frame `actions[a]` is set when its check holds and the
control mode allows it, and cleared unless `holdActions`. The stick: `stickX`, `stickY`, `stickLength` (up to 1)
and `stickRotation` (camera-relative, a yaw quaternion). Buttons are `PadButton` bits (framework.hpp) of the controller's pad
(`+0x10`, virtual `+0x10`: held, previous; `+0x234` disables input): B the sword, X `EquipX`, Y `EquipY`, L or ZL dash,
R or ZR shield. `PadState_Update` (`0x8813b0`, pad vtable `0x1bc4c68`) builds the held word from `GetNpadState`
(full key, handheld or joy-con dual): nn::hid's bits 0-3 stay, L, R, ZL, ZR, Plus, Minus, the stick clicks and the
D-pad move, and the stick bits come from the axes against the dead zone (`+0x40`). The fishing's masks are
`0xb0d00` (`pressedLeftUpDown`) and `0x40200` (`pressedRight`).

| Action | | Action | |
|---|---|---|---|
| 1 `kSword` | B pressed, SwordLv1 | 11 `kBow` | arrows > 0 |
| 2 `kRocsFeather` | | 12 `kHookshot` | |
| 3 `kA` | A pressed | 13 `kBoomerang` | none in flight |
| 4 `kDash` | held bit 4 or 6, Pegasus Boots | 14 `kMagicRod` | |
| 5 `kShield` | held bit 5 or 7, Shield | 15 `kSwordHeld` | B held |
| 7 `kMagicPowder` | powder > 0 | 16 `kSwordReleased` | B released |
| 8 `kOcarina` | not on ground kinds 5, 6, 8, nor 7 without Flippers | 17 `kThrow` | A pressed while carrying |
| 9 `kShovel` | | 18-20 `kBottle0` + i | bottle i owned, no fairy in it |
| 10 `kBomb` | bombs > 0 | 21 `kSwordNotHeld` | |

Item actions 2 to 15 are on Y or X: their checks compare `EquipY`/`EquipX` with the item's `MainItems` bit number
(9 Roc's Feather, 13 to 15 the bottles). Action 6 is never set.
`Player_ControlMode` (`0xd3e1d0`): `controlModeOverride` while `kControlModeOverride`; else 1 or 2 from the scene
(Framework `+0x4a8` → `+0x208`); else 4 while `kHeld`; else 0. Mode 1 allows only actions 0, 3 and 17; 3 and 5 none
(3 also clears the stick and the A and B bools); 4 all but `kDash`.

**Other fields.**

- `comboTime` counts down to 0 each frame. Placing a bomb (the Bomb state's enter `0xd98b70`) sets it with
  `comboBomb`; shooting an arrow (`PlayerBow_Combo` `0xd9d380 (player, s8 arrow slot, bool* attached)`) sets it with
  `comboArrow`, or, while a bomb's `comboTime` runs, attaches that bomb to the arrow. The Bomb state's enter attaches
  the bomb to `comboArrow`'s arrow while its `comboTime` runs; its update (`0xd98920`) shoots a bomb arrow on `kBow`
  while a bomb's `comboTime` runs (Bow's `kind`, State handoff). A combo ends both (`comboTime` 0, `comboArrow` -1).
- `clingers` (`PlayerClingers`, `Player_InitClingers` `0xd252e0`): six locators on the `head` bone and the actors
  stuck to Link, the Gels (their `MiniZolCling` state's enter `0x79d940` is the only caller of the adder
  `0xd25a90`): node `{weak actor +0x18, f32 limit +0x28, f32 time +0x2c}`; the update adds the
  frame time (more when struggling) and at the limit detaches the actor and emits `kClingerReleased`; with any,
  Link walks at 0.25. `Player_ClearClingers` (`0xd3ec60`) drops them all (also in deep water with Flippers).
- `fairy`: an ItemFairy (actor id 0xaf) Link touched (`0xd494d0`), which the Bottle state catches and clears.
- `inWaterArea`: set by Link's water event's enter (`+0x58`, `0xd4b6e0`), cleared by its leave (`+0xb0`, `0xd4b630`);
  with deep-water ground and Flippers it starts swimming (`0xd4ce50`). `recoilFlags`: set by attack-hit lambdas when
  the state accepts a recoil (bit 0 hit, 1 during a dash, 2 from the second source).
- `lookAt` (`PlayerLookAt`, 0x100 bytes, `PlayerLookAt_Ctor` `0xd27af0`, `PlayerLookAt_Update` `0xd1d340`): `0xd1d230`
  sets `head`, the angle limits and `mode` 1. Link's look-at hook (EventChara hook 12, `0xd455e0 (u8 mode, Vec*
  target)`) stores `target` and `mode` 2 for mode 2, keeps 1, else 0; `EventChara_LookAndTurn` passes 2 for a valid
  look target (EVENTS.md). The update calls `LookAt_Update` (`0xf9fdc0 (controller, Vec4* target, s32 facing, Vec4
  head position, f32 rate)`) on the controller at `+0x20`, with Link's facing angle and the head locator's position:
  in `mode` 2 with `target`, the wider limits, `flags` bits 0 and 2 and rate 0.5; in `mode` 0 with a point ahead of
  Link, bit 0 clear and rate 0.5; in `mode` 1 with rate 0.03 and bit 0 clear, or, while layer 0 plays `wait` or
  `idle_bare` (rows 103, 36) and neither `sideView` nor `InEvent` or `Carrying` is set, bits 0 and 2 with the camera
  system's actor (`CameraSystem_ptr`, its node's position) as the target. With bit 0, `LookAt_Turn` (`0xf9f840
  (controller, facing, target angle, 4°, rate)`) takes the target's angle from the head relative to the facing: within
  ±`angleLimit`, `turnSpeed` += (4° − `turnSpeed`) × rate and `turn` moves toward it by |difference| × rate, at most
  `turnSpeed`, at least 0.1°; `pitch` then moves toward the target's elevation clamped to `angleMin`..`angleMax` (step
  |difference| × rate within 0.1° to 4°). Beyond the limit `pitch` goes toward 0; `TurnsToLimit` turns `turn` to the
  limit, else `turn` holds for `holdLimit` frames, then, unless `HoldsTurn`, eases to 0 (step |`turn`| × rate / 2
  within 0.1° to 4°) with `turnSpeed` × (1 − rate). Without bit 0 `pitch` eases to `pitchRequest`, `turn` to 0 (step
  |`turn`| × rate within 0.1° to 4°) and `turnSpeed` × (1 − rate). Then `LookAt_UpdateMatrices` (`0xf9ec50`)
  recomputes the matrices unless `KeepsMatrices`, with `turnShare` and `pitchShare` (`LookAt_Update` inlines the same
  code). The matrices have no reader. `holdRate` is stored by `LookAt_Turn` (`0xf9f91c`) and the constructors only.
  The controller is also in EnemyBossblin, EnemyHinox, EnemyMasterStalfon, EnemyMoriblinSpear, EnemyMoriblinSword and
  EnemyTotakeke, whose step (`0xf9fb90`) calls `LookAt_Turn` and `LookAt_UpdateMatrices`.
- `shieldAngle` (a game angle, 0x20000000 = 45°, clamped to ±45°) and `shieldArmMatrix` (its 3×3 multiplies a bone's
  matrix in the bone callback `0xd4c184`): `Player_AimShield` (`0xd591f0`) and the Guard state.
- `handoff` (`PlayerStateHandoff`): State machine, State handoff.
- `moveTarget` (`PlayerMoveTarget`, 0x40): the request `Player_MoveTo` (`0xd555b0`) copies before state 16
  (`moveTarget`); event actions build it from `mode`, `distance`, `actor` and `animName`. The action at `0xd6ee70`
  (params `mode`, `distance`) sets `pendingState` 14 (ladder) and passes mode 2, speed 4.5 and a target `distance`
  from Link along a constant axis (negated for its `mode` 2). `flags` are read by the update (`0xdd4950`) after
  `Player_Move`: it faces Link toward the state's `+0xa0` (the direction to the goal at the enter), easing half the
  remaining angle a frame, or at once with `KeepGoalDirection`, which also sets `+0xc1` at once (measured 2026-11-01).
  The talk and examine sequences' walks set it (`0x1017ec`, `time` 0.9), and `PlayerChara_TurnTo` (`0xd45870`) in
  state 16. The state's enter (`0xdd5c20`) copies `timeout` to its `+0xb8`, which the update counts down; once negative, or once arrived (`+0xc0` and `+0xc1`), its exit transition
  holds and its virtual `+0x60` lets demoEvent in.
- `blowAway` (`PlayerBlowAway`, 0x13 bytes copied): the request `Player_BlowAway` (`0xd559c0`) copies before state 23.
- `eventEntity`: made by `Player_ItemGetSequence` (`ItemGet_MakeCarryModel` `0xd79db0`, from `modelComp`) unless
  `keepCarry`: `0xd7a150` makes the item-get's `ItemGetHolder` (ITEMS.md), the node the gotten item's model hangs from
  in its item-get pose, and attaches it to Link as component `CarryModel`. `Player_OnEventEnd` destroys it.
- `pendingState`: a state to enter at the next chance outside events (`0xdabc58`), 0xff none: 0xe ladder, 0xf,
  0x22 fishing.
- `itemGroupBits`: a bit per item shape group, which `Player_ShowItemGroupBit` (`0xd567c0`) shows unless flags bit 1;
  `handItems`: bit 0 the sword shown, bit 1 the shield.
- `modelFlag`: `Player_SetModelFlag` (`0xd54df0`) passes it to Link's, the bow's and the bottle's model components
  (virtual `+0xf0`, the instance's `+0x26a`; MODELS.md); its meaning is open.
- `ocarinaAreaName` and `ocarinaArea`: `Player_SetOcarinaArea` (`0xd59b00 (player, StringView* name, WeakPtr*)`),
  whose only caller (`0x7fd70`) is a callback AreaOcarina's setup (`0x7e560`) installs. `forcedAngle`: a game angle
  used instead of the computed one while `kForcedAngle` (`0xd4d914`); only the constructor writes it (0).
- `tempEntity` is destroyed by the update while alive; `unused_1e84` counts down to 0 each frame (`0xd376f0`,
  `Player_UpdateAnimation`); the constructor and `Player_Reset` (`0xd53368`) write 0. Its only other reader, `0xd57db0`
  (true while it is above 0 or the state's virtual `+0x80` returns false), has no caller.
- `swimDepth`: `Player_SetSwimDepth` (`0xd58770 (player, depth)`) offsets the damage collision's two shapes and the
  item sensor's shape by the depth's value (table `0x1703b90`) when it changes. Swim's enter and update set `Float`
  (anim `float`) or `Dive` (`dive`, `u_swim_wait`), its exit `None`; the Damage state sets `Float` and `None`, the
  Dash crash `Crash`. NpcComponent's touch virtual (`+0x338`, `0x957400`) raises trigger condition 1 instead of 0
  while it is `Dive`.

## Model

`modelComp` (model `link` of `Playerlink.bfres`) draws every item (the rod, ocarina, shovel, hookshot, flippers, both
swords and both shields) unless their material bits are cleared (MODELS.md, Shape draw gate). Its 20 shapes, by index:
0 magic rod, 1 ocarina, 2 shovel, 3–5 arm, belt, body, 6 boots, 7–9 face (ear, eye, mouth), 10 flippers, 11 hair, 12
hookshot handle, 13 shield, 14–15 mirror shield, 16–17 sword, 18–19 second sword. The bow, the bottle, the arrows, the
hookshot's chain and the boomerang are other models or actors.

**Tunic.** The material animation `clothes` swaps `MI_body`'s albedo: `MI_body_alb.0` green, `.1` red, `.2` blue, at
frames 0, 1 and 3. `materialAnim` (7 slots) holds it on slot 4 at rate 0, its frame set each frame from inventory
`+0x9c` (bit 6 red, bit 7 blue, red first). Slot 5 carries `powerUp` or `defence` (`Player_SetBuffGlow` `0xd3ea40
(player, bool defence)`), slot 6 the mirror shield's.

## State machine

`machine` is a `StateMachine<64>` (UI.md, State machines) at `+0x4e0`: `PlayerSM_vtable` `0x1c17cc8` (`+0x10` `PlayerSM_StateCount` `0xd2af90`: 64; `+0x18` `PlayerSM_StateName` `0xd2afa0`), changed by `PlayerSM_ChangeState` (`0xd34660 (machine, u8 state)`). Its state is `Player::machine.state` (`+0x4ed`), the state names start at `+0x1138`, its bit set object at `+0x1128` (vtable `0x1c14df0`, the bits at `+0x1130`).

Every one of Link's states is an object: `Player_Setup` gives entry id the object `states[id]` (`+0x1340`, owned, ids 0 to 35 used), sets its bit and makes the player the owner. A state object (0x30 bytes and up): `+0x0` vtable, `+0x8` a second vtable, `+0x10` a vector, `+0x28` u8 its id; its virtual `+0x40` returns the name, `+0x10` enters and `+0x20` exits (called with the owner). Each creator (`0xd42cd0`..`0xd43ce0`) does `operator_new(size)` and stores the vtable (`PlayerState_<Name>_vtable` in `las.sym`) and the id. Link is in state 0 while free and in 22 (`demoEvent`) during talk, events and cutscenes; `talk` (4) is a state of its own.

| Id | Name | Size | Creator | Id | Name | Size | Creator |
|---|---|---|---|---|---|---|---|
| 0 | `locomotion` | 0x370 | `0xd42cd0` | 18 | `death` | 0x30 | `0xd43530` |
| 1 | `jump` | 0x240 | `0xd42d40` | 19 | `resurrection` | 0x90 | `0xd435a0` |
| 2 | `swordAttack` | 0xd0 | `0xd42dc0` | 20 | `throw` | 0x80 | `0xd43610` |
| 3 | `fall` | 0x2c0 | `0xd42e40` | 21 | `fallAbyss` | 0x80 | `0xd43680` |
| 4 | `talk` | 0x58 | `0xd42ec0` | 22 | `demoEvent` | 0xb0 | `0xd43700` |
| 5 | `open` | 0x60 | `0xd42f30` | 23 | `blownAway` | 0xf0 | `0xd43770` |
| 6 | `push` | 0x58 | `0xd43010` | 24 | `boomerang` | 0x80 | `0xd437e0` |
| 7 | `pull` | 0x90 | `0xd42fa0` | 25 | `bow` | 0x60 | `0xd43850` |
| 8 | `carry` | 0x160 | `0xd43080` | 26 | `hookshot` | 0x310 | `0xd438c0` |
| 9 | `dash` | 0x320 | `0xd43100` | 27 | `magicRod` | 0xa0 | `0xd43940` |
| 10 | `guard` | 0x2a0 | `0xd43180` | 28 | `bomb` | 0xa0 | `0xd439c0` |
| 11 | `damage` | 0x1a0 | `0xd43200` | 29 | `warp` | 0x58 | `0xd43a30` |
| 12 | `ocarina` | 0xa0 | `0xd432e0` | 30 | `swim` | 0x1d0 | `0xd43aa0` |
| 13 | `magicPower` | 0x60 | `0xd43270` | 31 | `forcePlayAnim` | 0x90 | `0xd43b20` |
| 14 | `ladder` | 0x320 | `0xd43350` | 32 | `shovel` | 0xe0 | `0xd43b90` |
| 15 | `swordChargeAttack` | 0xd0 | `0xd433d0` | 33 | `bottle` | 0x1c0 | `0xd43c00` |
| 16 | `moveTarget` | 0xd0 | `0xd43450` | 34 | `fishing` | 0x4e0 | `0xd43c80` |
| 17 | `swordPoke` | 0xa0 | `0xd434c0` | 35 | `doPush` | 0x58 | `0xd43ce0` |

**Transitions.** A `StateTransition` (`StateTransition_vtable` `0x1c1b3d0`, 0x28 bytes: u8 `state` `+0x8`, a vector of
`conditions` `+0x10`, appended by `StateTransition_AddCondition` `0xd8d5d0`) is taken when every condition holds
(`StateTransition_Check` `0xd8d480`, virtual `+0x10`). A `SelectTransition` (`SelectTransition_vtable` `0x1c1b3f8`,
0x60 bytes) adds a `select` function at `+0x30`: when the conditions hold, `state = select(arg)`
(`SelectTransition_Check` `0xd8d840`). Each frame the update checks the five `stateHooks` in order, then the
transitions in the current state's vector `+0x10`, and takes the first that holds and that the current state's
virtual `+0x60 (state, player, u8 target)` does not refuse; a target of 0xff changes nothing. A state's constructor
fills that vector with its own transitions and the shared ones of `actionTransitions` (Fall `0xdad340`, Dash
`0xda54c0`, Guard `0xdb7530`). The `actionTransitions` constructor gives each transition one condition and sets the
selects' functions (`Function_Swap` `0xd931d0`). Dash, Swim and other states swap their own `gate` in on enter and
an empty one on exit (`Function_Swap` `0xda8be0`). `states[id]` holds state id's object for ids 0 to 35; slots 36
to 63 stay null.

| Transition | Into | Condition | Holds when |
|---|---|---|---|
| `stateHooks[0]` | 22 | `PlayerHook_InEvent` `0xd44940` | `kInEvent`, and the state is neither 22 nor 16 |
| `stateHooks[1]` | 11 | `PlayerHook_Damage` `0xd448a0` | `hit.pending` with `hit.kind` 0 |
| `stateHooks[2]` | 21 | `PlayerHook_FallAbyss` `0xd44580` | not in 21 or 22 and not carrying the flying cucco, and: on ground of kind 5; on the surface (`onGroundSurface`) of kind 7 without Flippers; on the surface of kind 8; or in side view in water (`0x13c850` above Link's position) on kind 8 or without Flippers |
| `stateHooks[3]` | 1 | `PlayerHook_Recoil` `0xd444a0` | `recoilFlags` bit 0; clears `recoilFlags` and sets `handoff.jumpKind`: with bit 2 `Jump` (`DJump` with bit 1) and `handoff.value` 1 while `heldFeather`, else 0.5; without bit 2 `Recoil` (`DJumpRecoil` with bit 1) |
| `stateHooks[4]` | 0 | `PlayerHook_HeldStart` `0xd44410` | flags bit 24, which it clears |
| `toJump` | 1 | `0xd93bd0` | `kRocsFeather`; not on ground kind 6; no clingers; not carrying, or carrying the flying cucco |
| `toSwordAttack` | 2 | `0xd93ad0` | `kSword`; not carrying; no clingers |
| `toGuard` | 10 | `0xd939d0` | `kShield`; not carrying; no clingers |
| `toDash` | 9 | `0xd938d0` | `kDash`; not carrying; no clingers |
| `toSwordChargeAttack` | 15 | `0xd937b0` | `kSwordReleased` with `swordCharge` `charging` and `charged`; not carrying |
| `toInteract` | select `0xd93400` | `0xd936c0` | `kA` and `Player_CanInteract`; the state by the interaction target's kind (`+0x1a0`: the Npc row's `doAction.type` − 1, `NpcComponent_MakeDoAction` `0x9506a0`): 1 open, 2 doPush, 3 pull, 4 carry, other talk (0 talk, 5 examine, 6 take) |
| `toItem` | select `0xd93390` | `0xd93530` | an item action (6 to 14, 18 to 20); not carrying; no clingers; the state of the first set action in the order 6, 7, 8, 9, 11, 10, 12, 13, 14, 18, 19, 20 |

"Carrying the flying cucco" is `Player_CarryingCucco` (`0xd8e250`): `kCarrying` and the carried actor is
NpcFlyingCucco (0x181). `toItem`'s states come from a static map (`PlayerAction_ItemStateFind` `0xd92a70`, nodes
`+0x10` action, `+0x11` state): 6 locomotion, 7 magicPower, 8 ocarina, 9 shovel, 10 bomb, 11 bow, 12 hookshot, 13
boomerang, 14 magicRod, 18 to 20 bottle.

The `fishing` state is the owner of the fishing rod and its lure (ACTORS.md, Fishing).

**State handoff.** Exits write `handoff` by the next state's id, conditions as they are taken; each next state reads
its own fields:

| Next state | Reads | Writers |
|---|---|---|
| 0 locomotion | `kind` 0 with `kSwordHeld`: starts the sword charge; side view: `variant` > 0 faces 90°, < 0 270°; `flag` 1: its `+0x351`; `angle` not 0: its virtual `+0x70` with it, which eats that apple | Jump's and Fall's exits: `kind` 1, their `+0x23c` / `+0x2b4` (their virtual `+0x70` keeps the apple to eat after landing) as `angle` when not negative; Jump's condition (its `+0x221`): its `+0x234` as `variant`; SwordAttack's exit: its `+0xc4` as `variant` |
| 1 jump | `jumpKind`; `variant` ≥ 1 clears its `+0x228` (else not `sideView`): with it the update keeps the landing height `+0x218` on the ground query's hit (`+0x1650`); `value` > 0: its `+0x214`; `angle` turns Link; `flag` > 0: `velocityX`, `velocityZ` | `PlayerHook_Recoil`; Ladder's exit: `variant` 1, in side view `angle` ±90° by the stick; Locomotion's and Dash's conditions: `Hop`, `flag` 1 and their velocity; the conditions of Fall (its `+0x2b1`), Guard (`+0x288`), Jump (`+0x22b`) and Hookshot (`+0x2ac`, `0xdbd8a0`): `Hop`; Swim's: `LeaveWater` |
| 3 fall, 11 damage | `variant` into Fall's `+0x280` / Damage's `+0x140`: frames in the air without moving, at 90 a respawn (`0xd59e50`); with `kind` not 0, `value` into Fall's `+0x2a4` / Damage's `+0x184` | each other's exits; Jump's exit into 11: `kind` 1, its `+0x218` as `value` |
| 14 ladder | `kind` 1 with `payloadType` `Ladder`: its `+0x314` back; `variant` 1: `ladder_wait` | DemoEvent's exit |
| 22 demoEvent | `kind` 1 keeps the animation, else (unless flags bit 20) `Player_IdleAnimation` (`0xd8e020 (player, bool)`, `variant` not 0: `u_swim_wait` in top view); `angle` 1 with `payloadType` `Ladder`: keeps the payload for its exit; `flag`: copies `states[21]`'s (FallAbyss) `+0x10`..`+0x77` | Ladder's exit (also `pendingState` 14); FallAbyss's exit: `flag` 1; Carry's, Damage's, FallAbyss's, MoveTarget's, Fishing's exits: `kind` 1; Swim's, MoveTarget's: `variant` 1 |
| 30 swim | `kind` not 0: `enter_water`; `variant`; `angle` not 0 | the Flippers conditions (deep-water surface): `kind` 1; Ladder's exit: `kind` 1; Fall's, Jump's, SwordAttack's exits: `variant` 1; MoveTarget's (mode 3): `angle` 1 |
| 29 warp | `kind` not 0: `warp_d_ed`, else `warp_ed` | none by a constant: no transition, `PlayerSM_ChangeState` call or `pendingState` write uses 29, and no select returns it |
| 2, 15 sword attacks | `kind` not 0 in side view: the sword shown, the shield hidden | Swim's exit in side view |
| 17 swordPoke | `kind`'s low byte, when ≤ 2 | SwordChargeAttack's exit: its `+0xc1` (`kind` not 0 on its enter); Guard's condition (its `+0x290`): its `+0x28c`, which its attack-hit virtual (`+0x50`, `0xdbb130`) sets while the sword charges: 1 for a hit of kind 0 (the first byte the hit lambda `0xd49bc0` passes), else 0 (`0xd49990` passes 1); Swim's condition (its `+0x1c9`): 2 |
| 25 bow | `kind`'s low byte into its `+0x58`, the arrow slot: bit 7 clear spawns that arrow, else a free slot (`0xd5a150`) | Bomb's condition (its `+0x90`): the bomb arrow's slot, its `+0x94` |
| 9 dash | `kind` not 0: its `+0x31a`, the shield shown and `attackCols` `+0x1f8`; else with `SwordLv1` the sword shown and `+0x1c8` | Guard's condition: 1 while its inner state (`+0x3d`: Guard, ShieldHit, Ending) is Guard; none from ShieldHit |
| 10 guard | `kind` 0 with `kSwordHeld` after state 1, 2, 15 or 17: starts the sword charge | SwordAttack's exit: `kind` 1 |

**State virtuals.** `+0x10` (`PlayerState_BaseEnter` `0xd8d370`) calls `+0x28` (enter, with the previous state) and `+0x20` (`PlayerState_BaseExit` `0xd8d430`) calls `+0x30` (exit, with the next state); `+0x18` is the update. The Dash state (9) runs an inner state machine whose state 3 `Crash` (enter `0xda7a70`) is the Pegasus Boots bonk: it plays the bonk animation and emits signal 4.

**Swim** (30; update `0xdddd20`, enter `0xddf710`, exit `0xde0670`): moves Link only through `Player_Move` (in the update and in `PlayerState_Swim_Move` `0xde0bb0`, which passes the animation's root motion), so his position is the character controller's. `PlayerState_Swim_Move` sets the controller's `useGravity` to 0, seeds the state's velocity (`+0x190`, a Vec4; side view: `(lastMove.x, 0, -lastMove.y)` scaled, at most 1.75) and keeps `+0x1c7` (in water): in side view (`sideView`) a water query (`0x13c850`) at Link's position plus a y offset, in top view deep-water ground (`groundAttribute` kind 7, `onGround`, `onGroundSurface`) with Flippers (inventory `+0xac` bit 0). The state keeps no position of its own; `Node_SetWorldRotation` (`0xfb72f0`) on Link's root turns him only while `input.stickLength` is above 0.325, `swordCharge` is not charging and the state's `+0x1c6` is clear, so not every frame marks the node.

## Character controller

**CharCtrlComp** (`CharCtrlComp_Ctor` `0x10b6cf0`, `CharCtrlComp_vtable` `0x1c50038`, 0x1d0 bytes; destructor `0x10b7700`, deleting `0x10b7980`; about 90 classes use it): a Node with a second base at `+0x148` (`vtable2`, `0x1c50138`, whose methods use `this - 0x148`), in Node's tail padding (MODELS.md, Skeletal model component).

- `filter` is a 0x20-byte pool object: `+0x0`..`+0xc` filter words (bit 31 set, bit 0 disabled), `+0x10` the physics controller, `+0x18` from the maker's params `+0x50` (in the inlined constructor at `0x10b4bd0`, the world's `controllerQueryFilter`).
- The physics controller is a PhysX 3.4 character controller, made by `CharCtrl_CreateController` (`0x10c8db0 (?,
  desc)`): the desc's position (`+0x0`, floats) and its `+0x10`, `+0x14`, `+0x18`, `+0x1c`, `+0x24`, `+0x28` go to
  the PxControllerDesc's position (doubles), `slopeLimit`, `invisibleWallHeight`, `maxJumpHeight`, `contactOffset`,
  `density`, `scaleCoeff`; `+0x58` picks the desc class (vtable `0x1c4fb08` for 1, `0x1c4fb38` for 0). Its
  `reportCallback` and `behaviorCallback` are the two bases of a 0x18-byte object (vtables `0x1c4fa98`, `0x1c4fad0`;
  `+0x10` the desc's `+0x48`), the latter only when `+0x48` is set. `CctHitReport_OnShapeHit` (`0x10b0970`) emits a
  `PlayerContact`, `CctHitReport_OnControllerHit` (`0x10b0cf0`) a `PlayerTouch`, onObstacleHit nothing. The behaviour
  callback (`CctBehavior_Shape` `0x10b0770`, `CctBehavior_Controller` `0x10b08e0`) locks the shape's userData object
  or reads the controller's userData and asks the desc's object; obstacles get 0.
- `body`: `+0x8` the controller's implementation, `+0x38` its shape set (`+0x20` u8 count, `+0x28` shapes, 0x10 each, up to 64), `+0x458`/`+0x459` added flags.
- `world` is the physics world (ITEMS.md, Physics worlds); `controllerFilter` is its `controllerFilter`, wrapped
  (vtable `0x1c4fbd8`) into Move's controller filters (`0x10b6074`).
- `CharCtrlComp_Move` (`0x10b5f10`, virtual `+0xd0`, `(result*, ?, f32 dt, component, Vec4* delta)`): while `useGravity`, `fallVelocity` grows by `gravity` × dt up to `maxFallSpeed`, and goes back to `gravity` × dt on the ground (result bit 2); the step, `pendingMove` plus the fall, is clamped to `maxMove`; the body moves; `lastMove` is the position after minus before (w 0); `pendingMove` is zeroed; the node's `coordsNew` is written; `moving` is set during it. It starts by putting the controller at the node's world position; PlayerCollision overrides it (`0x10b8050`) without that step: Link's Move starts from the controller's own position and overwrites `coordsNew` with the result.
- **Teleport.** The constructor connects `CharCtrlComp_OnTransformChanged` (`0x10b6b60`) to the node's `transformChanged` (`0x10b50bc`, handle `+0x1d0`). Outside a Move (`moving` 0) it compares the controller's foot position (physics controller virtual `+0x30`, PhysX `getFootPosition`, doubles in d0-d2) with the node's world position (`Node_UpdateWorld` first) and, when they differ, calls `setFootPosition` (virtual `+0x28`, `(ctrl, const double xyz[3])`) with it and zeroes `fallVelocity` and `lastMove`. `Node_MarkDirty` emits `transformChanged` only when `needsCoordUpdate` is clear, so writing `coordsNew` and setting `needsCoordUpdate` by hand moves the controller only if a later `Node_MarkDirty` (Link's rotation, `Node_SetWorldRotation`) runs before the next Move; while he swims with the stick neutral none does, and the next Move puts Link back (measured 2026-11-01).
- Virtuals: `+0xc0` shape i (`0x10b5d70`), `+0xd0` Move. Second base: `+0x120` `0x10b6520`, `+0x128` set the filter (`0x10b6540`), `+0x138` SetEnabled (`0x10b65f0`, `enabled`), `+0x148` shape count, `+0x150` shape i.

**PlayerCollision** (`PlayerCollision_Make` `0x93f4a0 (out, owner, name, ?, params)`: `operator_new(0x290)`;
`PlayerCollision_vtable` `0x1c501f0`, second `0x1c502f0`; `PlayerCollision_Dtor` `0x10b7700`) is Link's root component,
so his position is its node's coordinates; about 90 other makers inline the same constructor. The maker's params hold
the controller desc at `+0x10`: the maker moves the desc's slope limit into `slopeLimit` and zeroes it, so PhysX
applies none and Move uses `1 / (v * v) - 1` (the tangent squared), and puts `&behavior` in the desc's `+0x48`.
`PlayerCollision_Init` (`0x10b79b0`, virtual `+0x20`) connects to the body's hit signals (`contacts`, `touches`) and
sets `behavior.owner`. `gravitySign` is -1 while `gravity.y` ≤ 0, else 1.

- `behavior`: `PlayerCollisionBehavior_Shape` returns `Slide` unless the shape (`PhysicsShape`) is `walkable`, then
  its `behavior`; it returns `Slide` again for the shape and actor of the last `contacts` entry while
  `lastContactGround` is clear. `PlayerCollisionBehavior_Controller` returns `Slide`.
- `PhysicsShape_Ctor` (`0x10c45c0 (shape, PxShape*)`) sets the PxShape's userData (`+0x10`) to it and takes
  `extents` from the geometry (type 3, a box, and others). About 40% of shapes have a `tag` (measured 2026-10-31); its readers check
  its class with IsA first. AttackColComp and DamageColComp give each shape they make (`0xd3ee8`, `0xe2ac0`) an
  `AttackColShapeTag` or `DamageColShapeTag` with the shape description's `+0x50`, which hits use in place of the
  comp's attack or defence (ACTORS.md, Hit records). `CollisionShapeTag`: the collision file maker
  (`CollisionComp_Make` `0x8264d0`) tags each triangle-mesh shape whose file entry has `+0x10` 0, with `noDashCrash`
  clear; DeguZol (`0x2d12a4`, `0x2d14c8`) and ObjRaft (`0xc3b114`) tag their shapes with it set. The Dash state's
  contact check (`PlayerState_Dash_OnContacts` `0xdaa280`, vtable `+0x38`) skips a `noDashCrash` shape as it skips
  a heightfield, so Link does not bonk on it. Actors change their collision's shapes through its
  virtuals `+0xb8` (count) and `+0xc0` (shape by index): EnemyArmos (`0x1a6c98`), DeguZol (`0x2d128c`, `0x2d14b0`)
  and ObjInvisibleWall (`0xbc7e38`) set `walkable`, ObjRaft (`0xc3aed4`) clears `behavior`.
- `PlayerCollision_OnContact` (`0x10b94f0`, the `contacts` slot `PlayerCollision_Init` connects) sorts each contact by
  its normal's y against `slopeLimit`: within it on a `walkable` shape it is ground (`touchedGround`,
  `lastContactGround`, `minGroundUp` and `groundNormal`), else a wall (`touchedWall` when steeper; `maxWallUp`, and its
  normal's x and z into `wallNormals`); the `touches` slot does the wall part for controllers (`0x10b8f1c`).
- `force` and `frameForce`: `PlayerCollision_Make` (`0x93f4a0`) and the constructors inlined in the other makers zero
  them, and Move copies `ZeroVec_ptr` into `frameForce`; no other code stores into them, also in the 86 subclasses
  (their vtables share `PlayerCollision_Init`; their Moves call `0x10b8050`).
- `velocity`: while the controller's actor has a mass (PxRigidDynamic virtual `+0x108` > 0), Move adds `(force +
  frameForce) × dt ×` its inverse mass (`+0x110`), zeroes `frameForce`, scales `velocity` by `1 − dt ×` its linear
  damping (`+0x1d0`, not below 0) with y 0, and adds `velocity × dt` to the step.

With `clipWallPush`, Move first takes from the step its push into each `wallNormals` wall beyond `wallSkin`.
`followSlope` acts with `touchedGround` and `useGravity` and recomputes the vertical step from `slopeLimit`;
`slideOffWall` acts with `touchedWall` and not `touchedGround`, and divides the second move by `1 - maxWallUp²` (which
must be positive). `PlayerCollision_Make` sets `projectOnGround` and clears the other three; `Player_Setup` clears
`projectOnGround` and sets `clipWallPush` (`0xd2bfb0`).

## Animations

**PlayerAnimation.gsheet** (`PlayerAnimationSheet_ptr` `0x1cbaf70`, `**` the sheet): 416 rows of 0x48 bytes (`SheetData` count and row size). The row fields are the sheet's own field table (`gsheet.py` on the RomFS file) and the code: `fileName` goes to `AnimationSet_Make`; `PlayerAnimation_SetSlot` loops an animation whose `policy` is `kPlayerAnimLoops`; `Player_UpdateAnimation` plays `nextKey` when the layer ends; with `rootMotionStepOffset`, `PlayerAnimation_Play` sets the animator's virtual `+0xc8` to 1.0 on slot 0 (else 0); `facialEye`, `facialMouth` and `facialEar` are the sheet's `FacialAnim` (Eye, Mouse, Ear); `PlayerAnimation_Play` looks `facialName` up in the FacialComp (its `+0xf8`). `+0x4` and `+0x34` are alignment.

**PlayerAnimController** (`PlayerAnimation_Make` `0xd4aca0`: `operator_new(0xb0)`, the control block 0x18; `PlayerAnimation_Ctor` `0xdeb320 (controller, weak LayeredAnimation*, FacialComp sp*, model component sp*)`; `PlayerAnimation_vtable` `0x1c17220`). Make takes the material animation component too and does not keep it.

- `animation` is locked from the player's animator (`0xdeb6e0`); `PlayerAnimation_SetSlot` reads its layers.
- `anims`: nodes of 0x40 bytes, `+0x10` key, `+0x18` the sheet data (shared), `+0x28` the row, `+0x30` the animation bound to the model; `Player_InitAnimations` registers every row (`PlayerAnimation_Register`).
- `layerAnims` and `slots` are sized by the animator's `requestedLayers`; slot i plays on layer i. Slot 0 is the whole
  body, slot 1 an upper-body overlay: layer 1 is masked to `spine_a` and its children and carries only `idle_carry`,
  `bottle_use` and `eat`. A slot entry (`+0x78`, 0x18 apart) keeps the row playing at `+0x10`; a layer's bound
  animation leads to its frame control by `+0x8` → `+0x0` → `+0x8` (f32 frame, `+0xc` rate).
- Every play goes through `PlayerAnimation_Play` (`0xdeca80 (controller, key, slot, blend)`), from frame 0; `PlayerAnimation_SetSlot` hands the animation to `LayeredAnimation_SetSlot`. After a `nextKey` play the update calls `model`'s virtual `+0x150`.
- `facialEnabled` (1): a play changes the face (`0x121590`, `0x120a30`) only while it is set, and only on slot 1 or with slot 1 empty.
