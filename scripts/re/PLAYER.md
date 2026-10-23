# Link's player actor (game 1.0.0)

Link's actor, his state machine, his character controller and his animation controller, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/actors/player.hpp`, which lists every field; this file gives the evidence and what its comments leave out. "Guess" marks what the code suggests but was not followed to the end. TESTING.md, Reverse engineering, has Link's model, shapes, tunic, item shape groups and animation controller as the module uses them; this file does not repeat them.

## Player

`Player` (`Player_vtable` `0x1c17b70`, 0x1f90 bytes; `Framework::player`): the actor table's factory `Player_Create` (`0xd24890`) → `0x83da50` → `Player_MakeShared` (`0x83daf0`: `operator_new(0x1fb0)`, the control block 0x20, the player at `+0x20`). `Player_Ctor` (`0xd26910`): `Actor_Ctor`, the vtable, then zeroes from `+0x2a8` (0x1f0 bytes), `+0x1130` (0x208) and `+0x1340` (0x248). Destructor `0xd2b030`, deleting `0xd2bc20`.

`Player_Setup` (`0xd2bc50`, virtual `+0x20`) makes the components, each under the component name the header's comments give, and loads `Playerlink.bfres` and `tailor/Player.drsb`. `Player_InitParts` (item shape groups) and `Player_InitAnimator` (the call `0xd1a4e0` that makes the layered animator) are labels inside it.

| Virtual | |
|---|---|
| `+0x28` | `0xd31de0` |
| `+0x38` | `Player_Cleanup` (`0xd31df0`, guess): clears the signals' listener sets, updates the locators, drops `eventEntity` |
| `+0x48` | `Player_DropCarried` (`0xd32720`): clears both halves of `carried`, `kCarrying`, the TailorPlayer's NoLift and the animation's upper slot (`PlayerAnimation_ClearUpper`); resets `locItemAL`'s local transform; clears `field_1980` |
| `+0x68` / `+0x70` | IsA / TypeId (`0xd32730` / `0xd328c0`) |
| `+0x78` | Register (`0xd32920`) |
| `+0xa0` | `0xd32a50`: does nothing (ACTORS.md, Classes) |
| `+0xb0` | `Player_OnEventStart` (`0xd32a60`): `eventKind` from the running event's actor `+0x180` (1 → 2, 2 → 1, 3 → 4, 4 → 5, 5 → 6), `kInEvent`, `damageFlashTime` 0, the DamageColComp off (its virtual `+0xa8`), the EventChara told (`0x81aac0`) |
| `+0xb8` | `Player_OnEventEnd` (`0xd32d30`): destroys `eventEntity`, clears `eventKind` and `kInEvent`, the DamageColComp on |
| `+0xe0` | OnRoomChanged (`0xd32f60`, ACTORS.md, Classes) |
| `+0x110` | `0xd33470`: in state 22 plays animation key 0x34 or 0x16b by `field_1b38[1]` |
| `+0x118` | the fall (`0xd336c0`) |

**Components.** The makers called in `Player_Setup`: `modelComp` `SkeletalModelComponent_Make` (model `link`); `animator` `0xd1a4e0` (`LayeredAnimation_Make` with 2 layers, then `AnimationLayers_SetBoneMask(layers, 1, "spine_a")`); `materialAnim` `0x3b48e0`; `playerCollision` `PlayerCollision_Make` (`0x93f4a0`); `damageCollision` `0x869f0` (its `+0x330` takes the damage lambda `Player_OnDamage`); `eventChara` `0x60eb0` (chara `Link`); `camera` `0xd42630`, given the shaker, its `+0x515` set by the update; `cameraShaker` `0xc6320`; `boneEffect` `0x1fe170` (bones `clavicle_L`, `head`); `damageColor` `Player_MakeDamageColor`; `waterEvent` `0x35dfc0`; `eventSensor` `0xd3f840` (sensor `player:EventSensor`); `itemSensor` `0xd42290` (`player:ItemSensor`); `ladderInfo` `0x93ce20`; the locators `SkeletalModel_MakeLocator`; `hookshot` `0xd80c00` (the HookShot actor, model `HookShot`); `boomerang` `0xd63850` (`linkBoomerang`); `tailor` `TailorPlayer_MakeLayered` (flags NoLift, TopView, SwordInvisible, ShieldInvisible); `tailorWaterSurface` `0x9790e0`; `animPowerUp` and `animDefence` `MaterialAnim_Bind`; `facial` is the FacialComp `PlayerAnimation_Make` takes. `boneClavicleL` and `boneHead` come from `0xd3fec0 (skeleton, bone name)`, `shapeBody` from the model component's virtual `+0xb8`, `selfWeak` from the actor's weak self (`0xd35050`).

**parts** (`PlayerParts_vtable` `0x1c17ca8`, base `PartsVisibility_vtable` `0x1c11a20`; 0x40 bytes): `+0x8` the model component (shared), `+0x18` an `unordered_map` from s32 group to `{vector<s16> shapes +0x18, u8 visible +0x30}` (`PartsVisibility_AddShape`, `PartsVisibility_SetGroup`; TESTING.md, Player `+0x498`).

**Owned objects.** `bottle`: a PlayerBottle (vtable `0x1c19918`; `+0x8` a PlayerItemModel, `+0x10` a PartsVisibility of vtable `0x1c19938`; `PlayerBottle_Init`). `bow`: a PlayerItemModel (0x38 bytes, vtable `0x1c19638`; `+0x28` its model component, which the damage handler reads; `PlayerBow_Init`). `owned_1578`: 0x30 bytes `{Vec4 position, f32 0, f32 -1, f32 -1, u16 0xff00 at +0x20}`; `owned_1580`: 0x1c0 bytes (constructor `0xd92b50`); `owned_1ba8`: `{u16, u32}`, whose first byte blocks interacting (`Player_CanInteract`); `owned_3a8` is updated each frame (`0xd21980`).

**Signals** (`signals`, 0x58 each: `+0x18` the listener set, `+0x30` u32, `+0x38` mutex): 0 and 1 vtables `0x1b5e890` / `0x1b5e878`, 2 `0x1b59608` / `0x1b59698`, 3 constructor `0xd27b50` (vtable `0x1c17d18`), 4 `0xd27be0`, 5 and 6 `0xd27c70`, 7 `0x1b5b288` / `0x1b5b270`. Which events they carry is open.

**flags** (`PlayerFlags`): bit 0 in an event (`Player_OnEventStart` / `OnEventEnd`), 1 set by the constructor, 2 carrying (`Player_DropCarried` clears it), 7 the setup done, 8 no interacting (`Player_CanInteract`), 10 and 11 cleared each frame (11 tells the camera), 15 to 17 and 23 written by a respawn.

**carried** is a weak pointer (control block `+0x19b8`). `Player_DropCarried` locks it and clears both halves, so a non-null pointer means "holds something" only because the game always clears both.

**Respawn.** `Player_Respawn` (`0xd337a0 (player, Coords*, bool)`) puts Link at the coordinates. The update (`Player_UpdateAnimation`) grows `airTime` while no event runs, Link is off the ground (`groundQuery` `+0x40` bit 0) and not on a ladder or in `fallAbyss`; above 1.5 s it calls `Player_Respawn(player, &respawnCoords, false)`. `respawnBlocked` stops the timer; a respawn zeroes `field_17a0`, `weak_17c8`, `field_1800`'s first byte and `field_1b92`, writes `field_1ad0` (`+0x1ad8`, `+0x1ae0`) and sets `field_1b94` to 0xff; `field_1b70` (compared with 1), `field_1784` and `field_1b38[1]` pick its branches.

**Damage.** Health is the inventory's: `Player_OnDamage` (`0xd4c690`, a lambda on the DamageColComp, the player at its `+0x8`) subtracts from `Health` (quarter hearts, clamped to `Inventory_MaxHealth` `0xdf8260`), halves the damage with the Guardian Acorn (item 0x6f) or the blue tunic (`UpgradeItems.ClothesBlue`), doubles it with `HeroMode`, clears `KillCountNoDamage`, and sets `damageFlashTime` from the damage colour animation's length; LASR's damage patch (`0xd4c754`) is inside it. It passes `hitInfo` to `0xd249c0` (guess: the hit's data).

**Other fields.** `itemCooldown` counts down to 0 each frame and the item states (bomb, boomerang, bow, carry; `0xd9d420`) read it. `moveFlags` bits 6 to 9 are a 4-bit kind; `(moveFlags & 0x3c0) == 0x1c0` with `ExtraItems.Flippers` gates the update (likely). `timedActors` nodes hold `{weak actor +0x18, f32 limit +0x28, f32 time +0x2c}`: the update advances the time and acts at the limit; `field_1958` is cleared when the map is empty. `tempEntity` is destroyed by the update while alive. `groundQuery` (vtable `0x1c50d18`, a sub-object of vtable `0x1c50d38` at its `+0x50`) is a query class many classes use. `field_1770` is copied from `*0x1c93f10` in the setup; `object_18d0` is registered with the model component (`0xd252e0`); `object_19d0` (constructor `0xd27af0`) is given the `head` locator (`0xd1d230`); `field_1e84` counts down each frame; `list_1850` is cleared on a reset (`0xd532c8`).

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

`stateHooks` are five objects (vtable `0x1c1b3d0`, 0x28 bytes: u8 state `+0x8`, a vector of callbacks `+0x10`, built by `0xd8d5d0`) for the states 22, 11, 21, 1 and 0; what their callbacks do is open. The `fishing` state is the owner TESTING.md's fishing rod rows call the rod and lure.

## Character controller

**CharCtrlComp** (`CharCtrlComp_Ctor` `0x10b6cf0`, `CharCtrlComp_vtable` `0x1c50038`, 0x1d0 bytes; destructor `0x10b7700`, deleting `0x10b7980`; about 90 classes use it): a Node with a second base at `+0x148` (`vtable2`, `0x1c50138`, whose methods use `this - 0x148`), in Node's tail padding (MODELS.md, Skeletal model component).

- `filter` is a 0x20-byte pool object: `+0x0`..`+0xc` filter words (bit 31 set, bit 0 disabled), `+0x10` the physics controller, `+0x18` from the maker's params `+0x50`.
- `body`: `+0x8` the controller's implementation, `+0x38` its shape set (`+0x20` u8 count, `+0x28` shapes, 0x10 each, up to 64), `+0x458`/`+0x459` added flags.
- `owner` is a weak pointer from the maker's third argument; `field_170` is the owner's `+0xc0`, passed to the move.
- `CharCtrlComp_Move` (`0x10b5f10`, virtual `+0xd0`, `(result*, ?, f32 dt, component, Vec4* delta)`): while `useGravity`, `fallVelocity` grows by `gravity` × dt up to `maxFallSpeed`, and goes back to `gravity` × dt on the ground (result bit 2); the step, `pendingMove` plus the fall, is clamped to `maxMove`; the body moves; `lastMove` is the position after minus before (w 0); `pendingMove` is zeroed; the node's `coordsNew` is written; `moving` is set during it. PlayerCollision overrides it (`0x10b8050`).
- Virtuals: `+0xc0` shape i (`0x10b5d70`), `+0xd0` Move. Second base: `+0x120` `0x10b6520`, `+0x128` set the filter (`0x10b6540`), `+0x138` SetEnabled (`0x10b65f0`, `enabled`), `+0x148` shape count, `+0x150` shape i.

**PlayerCollision** (`PlayerCollision_Make` `0x93f4a0 (out, owner, name, ?, params)`: `operator_new(0x290)`; `PlayerCollision_vtable` `0x1c501f0`, second `0x1c502f0`; `PlayerCollision_Dtor` `0x10b7700`) is Link's root component, so his position is its node's coordinates. `listener` (vtable `0x1c50360`) is registered with the maker's params `+0x58` (`0x93f5ac`); `field_274` is the params' `+0x20`; `gravitySign` is -1 while `gravity.y` ≤ 0, else 1; `vector_1d8`'s elements (0x70 bytes) hold shared and weak pointers, and `0x10b7680` reads the last one and the byte `+0x28a`.

## Animations

**PlayerAnimation.gsheet** (`PlayerAnimationSheet_ptr`): 416 rows of 0x48 bytes (`SheetData` count and row size). The row fields are the sheet's own field table (`gsheet.py` on the RomFS file) and the code: `fileName` goes to `AnimationSet_Make`; `PlayerAnimation_SetSlot` loops an animation whose `policy` is `kPlayerAnimLoops`; `Player_UpdateAnimation` plays `nextKey` when the layer ends; with `rootMotionStepOffset`, `PlayerAnimation_Play` sets the animator's virtual `+0xc8` to 1.0 on slot 0 (else 0); `facialEye`, `facialMouth` and `facialEar` are the sheet's `FacialAnim` (Eye, Mouse, Ear); `PlayerAnimation_Play` looks `facialName` up in the FacialComp (its `+0xf8`). `+0x4` and `+0x34` are alignment.

**PlayerAnimController** (`PlayerAnimation_Make` `0xd4aca0`: `operator_new(0xb0)`, the control block 0x18; `PlayerAnimation_Ctor` `0xdeb320 (controller, weak LayeredAnimation*, FacialComp sp*, model component sp*)`; `PlayerAnimation_vtable` `0x1c17220`). Make takes the material animation component too and does not keep it.

- `animation` is locked from the player's animator (`0xdeb6e0`); `PlayerAnimation_SetSlot` reads its layers.
- `anims`: nodes of 0x40 bytes, `+0x10` key, `+0x18` the sheet data (shared), `+0x28` the row, `+0x30` the animation bound to the model; `Player_InitAnimations` registers every row (`PlayerAnimation_Register`).
- `layerAnims` and `slots` are sized by the animator's `requestedLayers`; slot i plays on layer i.
- Every play goes through `PlayerAnimation_Play` (`0xdeca80 (controller, key, slot, blend)`), from frame 0; `PlayerAnimation_SetSlot` hands the animation to `LayeredAnimation_SetSlot`. After a `nextKey` play the update calls `model`'s virtual `+0x150`.
- `facialEnabled` (1): a play changes the face (`0x121590`, `0x120a30`) only while it is set, and only on slot 1 or with slot 1 empty.

## Open questions

- `stateHooks`: what the callbacks of states 22, 11, 21, 1 and 0 do.
- What `eventEntity`, `weak_400`, `weak_410`, `weak_458`, `weak_1708`, `weak_17c8`, `weak_1990`, `weak_19c0`, `sp_1b98` and `weak_1f20` hold.
- `pad_1588`..`groundQuery` and `moveFlags`..`object_1960`: typed containers and flags without a known meaning.
- Which events the eight `signals` carry.
- Whether `airTime` grows only off the ground (the condition reads `groundQuery` `+0x40` bit 0).
