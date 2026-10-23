# Link's player actor (game 1.0.0)

Link's actor, his state machine, his character controller and his animation controller, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/actors/player.hpp`, which lists every field; this file gives the evidence and what its comments leave out. "Guess" marks what the code suggests but was not followed to the end. TESTING.md, Reverse engineering, has Link's model, shapes, tunic, item shape groups and animation controller as the module uses them; this file does not repeat them.

## Player

`Player` (`Player_vtable` `0x1c17b70`, 0x1f90 bytes; `Framework::player`): the actor table's factory `Player_Create` (`0xd24890`) → `0x83da50` → `Player_MakeShared` (`0x83daf0`: `operator_new(0x1fb0)`, the control block 0x20, the player at `+0x20`). `Player_Ctor` (`0xd26910`): `Actor_Ctor`, the vtable, then zeroes from `+0x2a8` (0x1f0 bytes), `+0x1130` (0x208) and `+0x1340` (0x248). Destructor `0xd2b030`, deleting `0xd2bc20`.

`Player_Setup` (`0xd2bc50`, virtual `+0x20`) makes the components, each under the component name the header's comments give, and loads `Playerlink.bfres` and `tailor/Player.drsb`. `Player_InitParts` (item shape groups) and `Player_InitAnimator` (the call `0xd1a4e0` that makes the layered animator) are labels inside it.

| Virtual | |
|---|---|
| `+0x28` | `0xd31de0` |
| `+0x38` | `Player_Cleanup` (`0xd31df0`, guess): clears the signals' listener sets, updates the locators, drops `eventEntity` |
| `+0x48` | `Player_DropCarried` (`0xd32720`): clears both halves of `carried`, `kCarrying`, the TailorPlayer's NoLift and the animation's upper slot (`PlayerAnimation_ClearUpper`); resets `locItemAL`'s local transform; clears `putTarget.valid` |
| `+0x68` / `+0x70` | IsA / TypeId (`0xd32730` / `0xd328c0`) |
| `+0x78` | Register (`0xd32920`) |
| `+0xa0` | `0xd32a50`: does nothing (ACTORS.md, Classes) |
| `+0xb0` | `Player_OnEventStart` (`0xd32a60`): `eventKind` from the running event's actor `+0x180` (1 → 2, 2 → 1, 3 → 4, 4 → 5, 5 → 6), `kInEvent`, `damageFlashTime` 0, the DamageColComp off (its virtual `+0xa8`), the EventChara told (`0x81aac0`) |
| `+0xb8` | `Player_OnEventEnd` (`0xd32d30`): destroys `eventEntity`, clears `eventKind` and `kInEvent`, the DamageColComp on |
| `+0xe0` | `Player_OnRoomChanged` (`0xd32f60`, ACTORS.md, Classes): `sideView` from the room data `+0xb9` == 1, the TailorPlayer's `SideView` / `TopView` |
| `+0x110` | `0xd33470`: in state 22 plays animation key 0x34 or 0x16b by `sideView` |
| `+0x118` | the fall (`0xd336c0`) |

**Components.** The makers called in `Player_Setup`: `modelComp` `SkeletalModelComponent_Make` (model `link`); `animator` `0xd1a4e0` (`LayeredAnimation_Make` with 2 layers, then `AnimationLayers_SetBoneMask(layers, 1, "spine_a")`); `materialAnim` `0x3b48e0`; `playerCollision` `PlayerCollision_Make` (`0x93f4a0`); `damageCollision` `0x869f0` (its `+0x330` takes the damage lambda `Player_OnDamage`); `eventChara` `0x60eb0` (chara `Link`); `camera` `0xd42630`, given the shaker, its `+0x515` set by the update; `cameraShaker` `0xc6320`; `boneEffect` `0x1fe170` (bones `clavicle_L`, `head`); `damageColor` `Player_MakeDamageColor`; `waterEvent` `0x35dfc0`; `eventSensor` `0xd3f840` (sensor `player:EventSensor`); `itemSensor` `0xd42290` (`player:ItemSensor`); `ladderInfo` `0x93ce20`; the locators `SkeletalModel_MakeLocator`; `hookshot` `0xd80c00` (the HookShot actor, model `HookShot`); `boomerang` `0xd63850` (`linkBoomerang`); `tailor` `TailorPlayer_MakeLayered` (flags NoLift, TopView, SwordInvisible, ShieldInvisible); `tailorWaterSurface` `0x9790e0`; `animPowerUp` and `animDefence` `MaterialAnim_Bind`; `facial` is the FacialComp `PlayerAnimation_Make` takes. `boneClavicleL` and `boneHead` come from `0xd3fec0 (skeleton, bone name)`, `shapeBody` from the model component's virtual `+0xb8`, `selfWeak` from the actor's weak self (`0xd35050`).

**parts** (`PlayerParts_vtable` `0x1c17ca8`, base `PartsVisibility_vtable` `0x1c11a20`; 0x40 bytes): `+0x8` the model component (shared), `+0x18` an `unordered_map` from s32 group to `{vector<s16> shapes +0x18, u8 visible +0x30}` (`PartsVisibility_AddShape`, `PartsVisibility_SetGroup`; TESTING.md, Player `+0x498`).

**Owned objects.** `bottle`: a PlayerBottle (vtable `0x1c19918`; `+0x8` a PlayerItemModel, `+0x10` a PartsVisibility of
vtable `0x1c19938`; `PlayerBottle_Init`). `bow`: a PlayerItemModel (0x38 bytes, vtable `0x1c19638`; `+0x28` its model
component, which the damage handler reads; `PlayerBow_Init`). `owned_1ba8`: `{u16, u32}`, whose first byte blocks
interacting (`Player_CanInteract`).

- `attackCols` (0x2b0 bytes, `Player_AttackCols_Ctor` `0xd1f2f0`, made by `Player_InitAttackCollisions` `0xd40720`):
  one collision component per attack (SwordAttackColComp, SideViewAttackColComp, SwordChargingAttackColComp,
  SwordPokeAttackColComp, JumpSwordAttackColComp, MagicPowderColComp, ShieldAttackComp, TreadOnAttack,
  BottleColComp and others); `Player_AttackCols_Update` (`0xd21980`) switches them on and off each frame.
- `knockback` (`PlayerKnockback`, 0x30 bytes; `PlayerKnockback_Set` `0xd23340`, `PlayerKnockback_Scale` `0xd24800`):
  `velocity`, which `Player_Move` adds × dt while `state` is 2, the frame counts, the hit's `kind` (1, 2, 4, 5).
- `actionTransitions` (`PlayerActionTransitions`, 0x1c0 bytes, `PlayerActionTransitions_Ctor` `0xd92b50`): the
  transitions into the input-driven states (State machine).
- Actors Link spawns: `arrows` (PlayerArrow, actor id 0x183, `Player_SpawnArrow` `0xd521f0 (player, index)`),
  `magicRodBalls` (PlayerMagicRodBall, 0x1b5, a pool: `Player_FreeMagicRodBall` `0xd515e0` gives the first free
  slot, `Player_MagicRodBall` `0xd51790` spawns into it), `swordBeam` (PlayerSwordBeam, 0x1fc, `Player_SpawnSwordBeam`
  `0xd55000`). `Player_Cleanup` and `Player_Reset` (`0xd532d0`) destroy them.

**Signals** (`signals`, `Signal`s of 0x58 bytes, ACTORS.md, Nodes; `PlayerSignal` names them by index):

| Index | Class | Argument | Emitted by |
|---|---|---|---|
| 0, 1 | vtables `0x1b5e890` / `0x1b5e878` | | not found |
| 2 `kShovelStart` | `0x1b59608` / `0x1b59698` | none | the Shovel state's enter (`0xdda850`) |
| 3 `kShovelEnd` | constructor `0xd27b50` (vtable `0x1c17d18`) | u8 | the Shovel state's exit (`0xddaf40`) |
| 4 | constructor `0xd27be0` | | not found |
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
non-null pointer means "holds something" only because the game always clears both. `carryNoDrop` (guess): when set
(or the carried actor's `+0x290`), dropping it without a throw does not hand it back to the scene root.
`putTarget` (`PlayerPutTarget`, vtable `0x1c17cf8`, likely): a position taken from an actor's spawn position while
carrying (`0xd264d0`), checked by Locomotion (`0xd26590`), aimed at by Throw (`0xde7fd0`); `valid` is cleared by
`Player_DropCarried`, Throw and Carry.

**holder** (`Player_SetHolder` `0xd588f0`, `Player_ReleaseHolder` `0xd53b00`): the actor holding Link; setting it
sets `kHeld` (and bit 24), changes `playerCollision`'s filter and zeroes `holdOffset` and `holdOffsetTarget`; the
update moves `holdOffset` towards `holdOffsetTarget` (likely).

**companion** (`Inventory.Companion`, `+0x9d`: BowWow, Marin, Ghost, Rooster; ACTORS.md, Companion interface):
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
- `lowerLevelHoles`: while not empty, a fall into an abyss runs `AreaHoleLowerLevel_StartEvent` on the first entity
  instead of the damage and respawn (likely). `slowAreas`: while not empty, Link moves at 0.75 (guess).
- `bodyContacts` (`PlayerContact`, 0x70) and `bodyTouches` (`PlayerTouch`, 0x60): this frame's reports of the
  physics body's `+0x80` and `+0xd8` signals (`Player_OnBodyContact` `0xd4ffb0`, `Player_OnBodyTouch` `0xd4e7d0`);
  a touch's other actor goes into `touchedActors`. `Player_ClearContacts` (`0xd343e0`) empties both.

**Respawn.** `Player_Respawn` (`0xd337a0 (player, Coords*, bool)`) puts Link at the coordinates. The update
(`Player_UpdateAnimation`) grows `airTime` while no event runs, Link is off the ground (`groundQuery.flags` bit 0)
and not on a ladder or in `fallAbyss`; above 1.5 s it calls `Player_Respawn(player, &respawnCoords, false)`. A
respawn zeroes `moveDelta`, `hit.attacker`, `hit.pending` and `handItems`, writes the handoff's `variant` and `flag`
and sets `pendingState` to 0xff; `respawnKind` (0 ground, 1 the side-view point, 3 the reset; compared with 1),
`onGround` and `sideView` pick its branches.

**Damage.** Health is the inventory's: `Player_OnDamage` (`0xd4c690`, a lambda on the DamageColComp, the player at
its `+0x8`) subtracts from `Health` (quarter hearts, clamped to `Inventory_MaxHealth` `0xdf8260`), halves the damage
with the Guardian Acorn (item 0x6f) or the blue tunic (`UpgradeItems.ClothesBlue`), doubles it with `HeroMode`,
clears `KillCountNoDamage`, and sets `damageFlashTime` from the damage colour animation's length; LASR's damage patch
(`0xd4c754`) is inside it. `Player_SetHit` (`0xd249c0 (hit, kind, attacker WeakPtr*, hit data*)`) writes `hit`
(`PlayerHit`, 0x60): the hit data's `attack` (its `+0xd` the attack type) and `defence` (a reaction per attack type),
`kind`, `attacker`, `direction` from the attacker's position, `pending` (cleared once taken, `0xd58c80`), the
knockback scales `scaleA` (kind 2) and `scaleB` (kinds 1, 4, 5), and `reaction0`/`reaction1` from the defence.

**Input** (`input`, `PlayerInput`, 0x70 bytes): rebuilt each frame by `Player_UpdateAnimation`. `checks` are
`PlayerActionCheck`s (0x40: u8 action, `Function check(Player*, Controller*)` at `+0x10`) that
`Player_RegisterActionChecks` (`0xd3f1d0`) registers; each frame `actions[a]` is set when its check holds and the
control mode allows it, and cleared unless `holdActions`. The stick: `stickX`, `stickY`, `stickLength` (up to 1)
and `stickRotation` (camera-relative, a yaw quaternion). Buttons (controller `+0x10` virtual `+0x10`: held,
previous; `+0x234` disables input): bit 0 A, 1 B (sword), 2 X (`EquipX`), 3 Y (`EquipY`), 4 and 6 dash, 5 and 7
shield. `pressedMaskB0D00` and `pressedMask40200` are the fishing's masks; `pressedBottle[i]` the button of bottle i.

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

Item actions 2 to 15 are on Y or X: their numbers are the equip ids, `MainItems` bit numbers. Action 6 is never set.
`Player_ControlMode` (`0xd3e1d0`): `controlModeOverride` while `kControlModeOverride`; else 1 or 2 from the scene
(Framework `+0x4a8` → `+0x208`); else 4 while `kHeld`; else 0. Mode 1 allows only actions 0, 3 and 17; 3 and 5 none
(3 also clears the stick and the A and B bools); 4 all but `kDash`.

**Other fields.**

- `itemCooldown` counts down to 0 each frame and the item states (bomb, boomerang, bow, carry; `0xd9d420`) read it.
  `itemRequest` (-1 none) and `itemRequestTaken`: the Bow's helper (`0xd9d380`) sets the request, the Bomb state
  takes it (guess).
- `clingers` (`PlayerClingers`, `Player_InitClingers` `0xd252e0`): six locators on the `head` bone and the actors
  stuck to Link (likely the Gels): node `{weak actor +0x18, f32 limit +0x28, f32 time +0x2c}`; the update adds the
  frame time (more when struggling) and at the limit detaches the actor and emits `kClingerReleased`; with any,
  Link walks at 0.25. `Player_ClearClingers` (`0xd3ec60`) drops them all (also in deep water with Flippers).
- `fairy`: an ItemFairy (actor id 0xaf) Link touched (`0xd494d0`), which the Bottle state catches and clears.
- `inWaterArea` (likely): set and cleared by lambdas (`0xd4b6dc`, `0xd4b630`); with deep-water ground and Flippers
  it starts swimming (`0xd4ce50`). `recoilFlags`: set by attack-hit lambdas when the state accepts a recoil (bit 0
  hit, 1 during a dash, 2 from the second source).
- `lookAt` (0x100 bytes, `PlayerLookAt_Ctor` `0xd27af0`, `PlayerLookAt_Update` `0xd1d340`): the head's look-at, given
  the `head` locator (`0xd1d230`); the bone callback turns `boneHead` by its `+0x34` (likely). Its fields are open.
- `shieldAngle` (a game angle, 0x20000000 = 45°, clamped to ±45°) and `shieldArmMatrix` (its 3×3 multiplies a bone's
  matrix in the bone callback `0xd4c184`): `Player_AimShield` (`0xd591f0`) and the Guard state.
- `handoff` (`PlayerStateHandoff`, `+0x1ad4`..`+0x1b38`): values a state's exit leaves for the next state's enter,
  which clears them: `kind` (0, 1, 2), `variant` (the Jump exit 1, Damage's `+0x140`, Fall's `+0x280`), `count`,
  `flag`, `value` (the Jump exit's `+0x218`, Damage's `+0x184`), and a `payload` of 0x40 bytes read only when
  `payloadType` is 4 (DemoEvent's and Ladder's exits).
- `moveTarget` (`PlayerMoveTarget`, 0x40): the request `Player_MoveTo` (`0xd555b0`) copies before state 16
  (`moveTarget`); event actions build it from `mode`, `distance`, `actor` and `animName`. `blowAway`
  (`PlayerBlowAway`, 0x13 bytes copied): the request `Player_BlowAway` (`0xd559c0`) copies before state 23.
- `pendingState`: a state to enter at the next chance outside events (`0xdabc58`), 0xff none: 0xe ladder, 0xf,
  0x22 fishing.
- `itemGroupBits`: a bit per item shape group, which `Player_ShowItemGroupBit` (`0xd567c0`) shows unless flags bit 1;
  `handItems`: bit 0 the sword shown, bit 1 the shield.
- `modelFlag`: `Player_SetModelFlag` (`0xd54df0`) passes it to Link's, the bow's and the bottle's model components
  (virtual `+0xf0`, the instance's `+0x26a`; MODELS.md); its meaning is open.
- `ocarinaAreaName` and `ocarinaArea`: `Player_SetOcarinaArea` (`0xd59b00 (player, StringView* name, WeakPtr*)`),
  whose only caller is in AreaOcarina's code (likely). `forcedAngle` (guess): a game angle used instead of the
  computed one while `kForcedAngle`.
- `tempEntity` is destroyed by the update while alive; `field_1e84` counts down to 0 each frame, and `0xd57db0` is
  true while it is above 0 (no positive writer found).

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
(`StateTransition_Check` `0xd8d480`, virtual `+0x20`). A `SelectTransition` (`SelectTransition_vtable` `0x1c1b3f8`,
0x60 bytes) adds a `select` function at `+0x30`: when the conditions hold, `state = select(arg)`
(`SelectTransition_Check` `0xd8d840`). `stateHooks` are five StateTransitions into the states 22, 11, 21, 1 and 0
(demoEvent, damage, fallAbyss, jump, locomotion). `actionTransitions` holds `toJump`, `toSwordAttack`, `toGuard`,
`toDash`, `toSwordChargeAttack` and two SelectTransitions (state 0xff until selected); its constructor gives each a
condition, `0xd931d0` sets the selects' functions, and states add conditions (`0xdad340`, `0xda54c0`, `0xdb7530`).
`states[id]` holds state id's object for ids 0 to 35; slots 36 to 63 stay null.

The `fishing` state is the owner TESTING.md's fishing rod rows call the rod and lure.

## Character controller

**CharCtrlComp** (`CharCtrlComp_Ctor` `0x10b6cf0`, `CharCtrlComp_vtable` `0x1c50038`, 0x1d0 bytes; destructor `0x10b7700`, deleting `0x10b7980`; about 90 classes use it): a Node with a second base at `+0x148` (`vtable2`, `0x1c50138`, whose methods use `this - 0x148`), in Node's tail padding (MODELS.md, Skeletal model component).

- `filter` is a 0x20-byte pool object: `+0x0`..`+0xc` filter words (bit 31 set, bit 0 disabled), `+0x10` the physics controller, `+0x18` from the maker's params `+0x50`.
- `body`: `+0x8` the controller's implementation, `+0x38` its shape set (`+0x20` u8 count, `+0x28` shapes, 0x10 each, up to 64), `+0x458`/`+0x459` added flags.
- `owner` is a weak pointer from the maker's third argument; `ignoreObject` is the owner's `+0xc0`, wrapped (vtable
  `0x1c4fbd8`) as the move query's ignore filter (likely).
- `CharCtrlComp_Move` (`0x10b5f10`, virtual `+0xd0`, `(result*, ?, f32 dt, component, Vec4* delta)`): while `useGravity`, `fallVelocity` grows by `gravity` × dt up to `maxFallSpeed`, and goes back to `gravity` × dt on the ground (result bit 2); the step, `pendingMove` plus the fall, is clamped to `maxMove`; the body moves; `lastMove` is the position after minus before (w 0); `pendingMove` is zeroed; the node's `coordsNew` is written; `moving` is set during it. PlayerCollision overrides it (`0x10b8050`).
- Virtuals: `+0xc0` shape i (`0x10b5d70`), `+0xd0` Move. Second base: `+0x120` `0x10b6520`, `+0x128` set the filter (`0x10b6540`), `+0x138` SetEnabled (`0x10b65f0`, `enabled`), `+0x148` shape count, `+0x150` shape i.

**PlayerCollision** (`PlayerCollision_Make` `0x93f4a0 (out, owner, name, ?, params)`: `operator_new(0x290)`;
`PlayerCollision_vtable` `0x1c501f0`, second `0x1c502f0`; `PlayerCollision_Dtor` `0x10b7700`) is Link's root component,
so his position is its node's coordinates. `listener` (0x40 bytes, vtable `0x1c50360`) is registered with the maker's
params `+0x58` (`0x93f5ac`); Move
accumulates a vector at its `+0x10` and reads `+0x30`, `+0x34`, `+0x3c`. `field_274` is a float moved from the
params' `+0x20` (Move uses `v * v - 1`); `gravitySign` is -1 while `gravity.y` ≤ 0, else 1. `contacts` and
`touches` have the element types of Player's `bodyContacts` and `bodyTouches` (likely); `vector_208` is emptied at
each Move; `0x10b7680` reads the last contact and the byte `+0x28a`. `field_284` are bools Move reads (`+0x284` 1
from the constructor and 0 from `Player_Setup`, `+0x287` 1 from `Player_Setup`, `+0x288` and `+0x289` cleared after
a Move, `+0x28a` cleared at each Move).

## Animations

**PlayerAnimation.gsheet** (`PlayerAnimationSheet_ptr`): 416 rows of 0x48 bytes (`SheetData` count and row size). The row fields are the sheet's own field table (`gsheet.py` on the RomFS file) and the code: `fileName` goes to `AnimationSet_Make`; `PlayerAnimation_SetSlot` loops an animation whose `policy` is `kPlayerAnimLoops`; `Player_UpdateAnimation` plays `nextKey` when the layer ends; with `rootMotionStepOffset`, `PlayerAnimation_Play` sets the animator's virtual `+0xc8` to 1.0 on slot 0 (else 0); `facialEye`, `facialMouth` and `facialEar` are the sheet's `FacialAnim` (Eye, Mouse, Ear); `PlayerAnimation_Play` looks `facialName` up in the FacialComp (its `+0xf8`). `+0x4` and `+0x34` are alignment.

**PlayerAnimController** (`PlayerAnimation_Make` `0xd4aca0`: `operator_new(0xb0)`, the control block 0x18; `PlayerAnimation_Ctor` `0xdeb320 (controller, weak LayeredAnimation*, FacialComp sp*, model component sp*)`; `PlayerAnimation_vtable` `0x1c17220`). Make takes the material animation component too and does not keep it.

- `animation` is locked from the player's animator (`0xdeb6e0`); `PlayerAnimation_SetSlot` reads its layers.
- `anims`: nodes of 0x40 bytes, `+0x10` key, `+0x18` the sheet data (shared), `+0x28` the row, `+0x30` the animation bound to the model; `Player_InitAnimations` registers every row (`PlayerAnimation_Register`).
- `layerAnims` and `slots` are sized by the animator's `requestedLayers`; slot i plays on layer i.
- Every play goes through `PlayerAnimation_Play` (`0xdeca80 (controller, key, slot, blend)`), from frame 0; `PlayerAnimation_SetSlot` hands the animation to `LayeredAnimation_SetSlot`. After a `nextKey` play the update calls `model`'s virtual `+0x150`.
- `facialEnabled` (1): a play changes the face (`0x121590`, `0x120a30`) only while it is set, and only on slot 1 or with slot 1 empty.

## Open questions

- What the conditions of `stateHooks` and `actionTransitions` test.
- What `eventEntity` holds.
- Signals 0, 1 and 4: no emitter found.
- Who inserts into `lowerLevelHoles` and `slowAreas` (likely the area actors).
- The fields of `PlayerMoveTarget`, `PlayerBlowAway`, `PlayerKnockback` and `lookAt`, and the payloads of
  `PlayerContact` and `PlayerTouch`.
- `field_1b38`, `field_1b80`, `field_1ea8`: written only by the constructor; `field_1e84`: no positive writer.
- `PlayerCollision`'s `field_278`..`field_284`; the buttons of the fishing's masks 0xb0d00 and 0x40200.
