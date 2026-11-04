# Trendy Game (game 1.0.0)

The crane game in the `CraneGame` level: the session and its price, the prizes and how they are chosen and placed, the crane, the moving parts and the prize physics, from static analysis. Offsets are module offsets; `las.sym` names them at their runtime addresses (module offset + 0x8504000).

Everything in the machine is PhysX 3.4 in the room's physics world (ITEMS.md, Physics worlds; filter words as ITEMS.md, Collision filters). The crane, the belt, the floors and the fence take every constant from literals in the code: no gsheet row, LEB parameter or bfres user data. The prizes take theirs from `CranePrize.gsheet`, `CranePrizePhysicsTraits.gsheet` and the collision file in their model.

| Actor | Id | Factory | CraneGame_01A (x y z) | Role |
|---|---|---|---|---|
| `NpcGameShopOwner` | 0x153 | `0x9b4d10` | 12.30 0 6.75 | the session: talk, payment, replay, Marin's play, final prize |
| `ObjCraneGameButton` | 0x145 | `0xae3a10` | 6.35 0 5.25 | starts a paid play |
| `AreaCraneGame` | 0x209 | `0x4eff0` | 7.50 2.30 3.00 | makes and places the prizes, judges them won, runs the play |
| `CranePrize` | 0x213 | `0x13f290` | (made by AreaCraneGame) | a prize |
| `ObjCrane` | 0x144 | `0xad5380` | 3.75 4.30 4.28 | the crane |
| `ObjGameBeltConveyor` | 0x1ca | `0xb956a0` | 3.75 0.70 4.50 | carries prizes to the exit |
| `ObjMovingFloorCrane` | 0x1fb | `0xc12880` | 4.85 0.70 1.50; 10.12 0.70 1.50 | the upper shelf's two sliding floors |
| `ObjCraneFence` | 0x218 | `0xae1a50` | 5.37 −1.13 6.75 | the wall beside the prize tray |

## Session and price

The shopkeeper (flowchart `GameShopOwner.bfevfl`) runs the sessions; the button (`CraneGameButton.bfevfl`) starts a paid play.

### Price

The price lives only in the flow's event parameters: `Link.CheckRupee {amount}` (`EventQuery_CheckRupee` `0xd7df70`: amount ≤ Inventory `+0x80`) and `Link.AddRupee {amount}` (`EventAction_AddRupee` `0xd7c590`: rupees + amount, clamped to 0..9999).

| Who plays | Price | Events (check / take) | Text |
|---|---|---|---|
| Link, before `BowWowFigureGet` | 10 | `Event2`/`Event6`, `Event13`/`Event58`, `Event160`/`Event173` | `Npc:QuestGameShop`, `Npc:ReplayGameShop` |
| Link, once `BowWowFigureGet` is set | 5 | `Event119`/`Event129`, `Event133`/`Event137`, `Event184`/`Event192` | `Npc:QuestGameShopCheap` |
| Marin | 10 | `Event61`/`Event60` | `SubEvent:WantPlayGameMarin1..3` |

One payment buys one crane cycle (one grab). Paying sets the owner's LEB switch 0, level flag 1 (ACTORS.md, Switches); the play's end event `SecondPlay` clears it. Level flags clear on a level jump, so a payment not played is lost when Link leaves.

### Links and switches

| Owner link | Actor | Use |
|---|---|---|
| 0 | `AreaEventBox` (7.50 0.75 6.18) | trigger condition 10: entry `Game_talk` (Marin's play) while `Conditions.gsheet` `MarinGameEvent` holds (Marin follows, `MarinGame` clear) |
| 1 | `AreaCraneGame` | owner `+0x2d8`/`+0x2e0` |
| 2 | `ObjCraneFence` | `PopCraneFence` / `DepopCraneFence` |
| 3 | `AreaLevelOpen` (the exit, to `Field_12D`) | trigger condition 13: `ExitGameShop` while switch 0 is on, `ForgetGameShop` while switch 1 is on |

Owner switch 0 = level flag 1 (paid, not played). Owner switch 1 = level flag 2, AreaCraneGame's LEB switch 0, which the area sets while a won prize lies in the tray. The button links the owner (button `+0x408`) and AreaCraneGame. Its `Npc.gsheet` row has `doAction` type 6 at 1.8 m and no flowchart; its code makes its own chara `CraneGameButton` (`CraneGameButton_MakeEventChara` `0xae87f0`).

### Flow entries (`GameShopOwner.bfevfl`)

Return values (`FlowControl.SetReturnValue`) go to `GameShopOwner_OnEventFinished` (`0x9bb840`): 0 `GameShopOwner_EndPlay`, 1 `GameShopOwner_StartPlay`, 2 `GameShopOwner_StartMarinPlay`; others do nothing.

| Entry | Started by | What it does | Returns |
|---|---|---|---|
| `talk` | talk | Paid (switch 0): `PlayGameShop`. No prize left (`CheckPrizeEmpty` false): `CloseGameShop`. Else HUD CraneIn with the rupee counter, the offer at 10 or 5: Play with enough rupees → pay, switch 0 on, `PlayGameShop`, HUD CraneOut, `PopCraneFence`; No → `NoGameShop`; too poor → `NoneMoneyToolShop` | −1 after paying, else 0 |
| `talk_button` | sub flow of the button's `examine` when not paid | as `talk`; with level flag 5 (set by the button) it says `PlayGameShopDirectly` and clears it | −1 / 0 |
| `SecondPlay` | the owner's update, when a play ends | switch 0 off. Prizes still in the claw (`CheckPrizeHookedInArms`): `TroubleGameShop`, fade, `MoveHookedInArmsPrizes`, fade. No prize left: `ResumeCamera`, `CloseGameShop`, `DepopCraneFence`. Else `ReplayGameShop` "Challenge again?": pay → `ContinueGameShop`; No → `NoGameShop`, `ResumeCamera`, `DepopCraneFence`; too poor → `ResumeCamera`, `DepopCraneFence` | 1 after paying (the next play starts at once), else 0 |
| `Game_talk` | link 0's area | Marin walks to the owner and asks; both answers lead to the payment. 10 rupees → pay, `PopCraneFence`, flag `MarinGame` on; too poor → `NoneMoneyToolShop` | 2 after paying, else 0 |
| `Angry` | the owner's update, when Marin's play ends | `AngryGameCreek`, `RequestLevelJump Field/Field_12D` | 0 |
| `ExitGameShop` | the exit, switch 0 on | "Leaving without even playing?": Play → Link steps back; Don't → out to `Field_12D`, the payment lost | |
| `ForgetGameShop` | the exit, switch 1 on | "Don't leave your prize behind!": Take it → Link steps back; Don't → out, the prize left | |
| `BowWowFigureGet` | sub flow of `PrizeCommon` `get` | the final prize: `QuestGameShop10`, the chamber stone (`PanelDungeonPiece` index 24), `QuestGameShopPanelService`, flag `BowWowFigureGet` on (price 5 from then on) | |

`CraneGameButton.bfevfl` `examine`: switch 0 on → Link walks to 1.2 m of the button, HUD out, menu off (`Hud.SetOpenMenuEnable false`), `CraneGameButton.StartManualPlay` (`CraneGameButton_StartManualPlay` `0xae8380`: `GameShopOwner_StartPlay` while switch 0 is on) with `SE_MAP_CRANE_START`. Off → level flag 5 on, sub flow `GameShopOwner<talk_button>`, then the same start once paid.

### Owner actor

| Offset | |
|---|---|
| `+0x2a8` | NPC component (`"Npc"`, `GameShopOwner_Init` `0x9b5880`) |
| `+0x2d8`/`+0x2e0` | AreaCraneGame (shared) |
| `+0x2e8` | play state: 0 none, 1 Link plays, 2 Marin plays, 3 play over |
| `+0x2f0` | grip machine (`GameShopOwner_GripChangeState` `0x9b6c20`): None 0, Idle 1, Gripped 2, Fall 3, Land 4 (`fall_ed`), End 5; Marin's crane grips the owner |

`GameShopOwner_Update` (`0x9b6560`, per frame): in play state 2 it ticks the grip machine; in state 1 or 2, once AreaCraneGame is gone or back in Sleep, it closes the crane UI (state 1, `GameShopOwner_CloseCraneUi` `0x9b68f0`), starts `SecondPlay` (state 1) or `Angry` (state 2) and sets state 3.

- `GameShopOwner_StartPlay` (`0x9bbae0`): `AreaCraneGame_StartPlay`, play state 1, the crane camera (`GameShopOwner_ApplyCraneCamera` `0x9bade0`: distance 15.1, pitch −0.829 rad), scene `minigame` 1, UI 0x14 `Gm_CraneGame` (`GameShopOwner_OpenCraneUi` `0x9bbe70`).
- `GameShopOwner_StartMarinPlay` (`0x9bbcb0`): `AreaCraneGame_StartMarinPlay`, play state 2, grip machine Idle, `minigame` 1, no UI.
- `GameShopOwner_EndPlay` (`0x9bb980`): `minigame` 0, `GameShopOwner_ResumeCamera` (`0x9baa40`: 23.5, −0.785 rad) unless Marin played, play state 0, grip None.

**Actions and queries** (`GameShopOwner_FindAction` `0x9ba0b0`, `GameShopOwner_FindQuery` `0x9b99c0`):

| Name | Body | Effect |
|---|---|---|
| `CheckPrizeEmpty` | `0x9b9ef0` | true while prizes remain (the name is inverted): `GameShopOwner_RemainingPrizes` (`0x9b9f40`) = AreaCraneGame's out-of-area + placed − won list counts > 0 |
| `CheckPrizeHookedInArms` | `0x9b9c30` | `AreaCraneGame_HookedPrizeCount` (`0x59580`: live prizes in ObjCrane `+0x6d0`) > 0 |
| `PopCraneFence` / `DepopCraneFence` | `0x9bb540` / `0x9bb130` | `ObjCraneFence_Pop` / `_Depop` on the linked fence (`GameShopOwner_FindFence` `0x9bb1c0`) |
| `ApplyCraneCamera` / `ResumeCamera` | `0x9bad90` / `0x9ba9f0` | the camera above |
| `ResetRemainPrizes` | `0x9ba830` → `GameShopOwner_ResetRemainPrizes` `0x9ba880` | each placed prize back onto its place (`AreaCraneGame_PlaceOnFloor` / `PlaceOnRail`); no flow uses it |
| `MoveHookedInArmsPrizes` | `0x9ba670` → `GameShopOwner_MoveHookedInArmsPrizes` `0x9ba6c0` | each prize k left in the claw to (3.5, 2 + 2k + its place height, 7.0) with zero velocity, in the tray outside the game area, so the next update counts it won |

### HUD and UI

- `Hud.ShowRupee`, `Hud.PlayCraneInAnim` and `Hud.PlayCraneOutAnim` (`0x7f25b0`, `0x7f3200`, `0x7f3500`): CmHud state 4 CraneIn runs into 6 WaitEndCrane, which keeps the rupee counter following Inventory `+0x80`, so the HUD counts the payment down; CraneOut returns to InGame (UI.md).
- `Gm_CraneGame` (UI 0x14) has no counter or timer: `CraneGame_Setup` keeps the ObjCrane (`+0x3d0`) and the parts `L_CatcherBtn_00`/`01`; `CraneGame_Update` animates them from the crane's mover (`+0x6a0`, Movers). The button's model has the nodes `CraneGameButton_A` and `CraneGameButton_X`, lit from the same mover (`0xae63d0`..`0xae6690`; Marin's button presses at `+0x43c`..`+0x43e`).

### Marin

- She comes into `CraneGame` only while `MarinGame` is clear (`RoomManager_SpawnCompanion`; no other companion comes in). `Game_talk` sets `MarinGame` once paid, so her play happens once per save; too few rupees and it comes up again on the next entry.
- `Marin.bfevfl` `MarinPlayCrane` presses the button (`Marin_PressCraneRight` `0x9f5b90`, `Marin_PressCraneBoth` `0x9f58d0`, `Marin_PressCraneLeft` `0x9f5e50`, `Marin_EndPlayCraneGame` `0x9f55d0`). Her crane drives to the shopkeeper and grips him (ObjCrane, Marin's play); `Angry` puts Link out at `Field_12D`. She wins no prize.

## Prizes

AreaCraneGame makes every prize when it is set up, puts each on its place, judges which leave the game area and runs the play's machine. A prize is a CranePrize actor, made from code only, that lies passive in the physics and starts its own get event when Link touches it outside the game area.

### Rows

`CranePrizeSheet_Row` (`0x148410 (out, sheet, index)`, the row at out `+0x10`), `CranePrizeFeatured_Row` (`0x1485e0`), `CranePrizePhysicsTraits_Find` (`0x1484c0 (out, sheets, StringView* symbol)`). `CranePrizeSheets_Set` (`0x148240`, from `Datasheets_Load`) fills `CranePrizeSheets_ptr`: `+0x0` CranePrize, `+0x10` CranePrizePhysicsTraits, `+0x20`/`+0x30` the featured groups 1 and 2.

| CranePrize row | Field |
|---|---|
| `+0x00` | `symbol` (pointer, u32 length) |
| `+0x10`, `+0x20` | `getEvent.messageEntry`, `getEvent.jingle` |
| `+0x30`, `+0x40` | `landingSe.seInsideGameArea`, `seOutsideGameArea` |
| `+0x50` | `collision.traits` |
| `+0x60` | `collision.centerOfGravity` |
| `+0x70` f32 | `modelScale` |
| `+0x78`, `+0x80` u32 | `layouts` (0x20 bytes each), their count |
| `+0x88` u32 | `getCount` |
| `+0x8c`, `+0x8d`, `+0x8e` u8 | `options.isFeatured`, `canGetButInsideGameArea`, `isAutoSave` |

Layout: `+0x0` s32 `itemIndex`, `+0x8` conditions (0x18 bytes: u32 category, string) and `+0x10` their count, `+0x18` `place.type`, `+0x1c` `place.index`. `CranePrizeLayout_ConditionsHold` (`0x573e0`) runs `Conditions_Check` on each with no actor (category 1 event flag, 2 item owned). Featured row: `+0x4` `cranePrizeId`, `+0x8` `layoutIndex`.

`CranePrizeParams` (`0x1b5fe08`): 21 pointers, one per row, to hard-coded 0x54-byte blocks.

| Offset | |
|---|---|
| `+0x00` | landing SE: least contact impulse that plays it (0.1 for all) |
| `+0x04` | landing SE: impulse above `+0x0` for full volume (0.5 for all) |
| `+0x08` | height above the place: 0.375 rupees, heart piece; 0.35 recovery heart, bomb, shield, chamber stone; 0.275 magic powder; 0.4 Yoshi Doll; 0.425 seashell; −0.025 to 0.15 the figurines |
| `+0x0c`..`+0x2c` | a pose (position, rotation in degrees, scale): the get holder's vtable `+0xa8`, the item-get pose |
| `+0x30`..`+0x50` | a second pose: the holder's vtable `+0xb0` |

### Places

| `place.type` | Places | Position |
|---|---|---|
| 1 | 3, lower shelf: AreaCraneGame's rail links (LEB `z`, actor `+0x118`) | the linked rail point: CraneGame_01A x 5.99, 8.24, 10.49, y 0.75, z 3.16 |
| 2 | 2, upper shelf: AreaCraneGame's links (LEB `x`, actor `+0x100`) | the linked ObjMovingFloorCrane (index 0 at x 4.85, index 1 at x 10.12), at the floor's position (`0xc15710`) |
| 0 | none | nothing places it: RupeeSilver, the only row of type 0, never appears |

A missing link or point leaves the prize at AreaCraneGame's root. Each prize starts `CranePrizeParams +0x8` above its place and falls onto it. The places are data of the level.

### Prize selection

`CraneGame_BuildFeaturedPrizes` (`0x53f30`) has one caller, `AreaCraneGame_Setup`: the prizes are chosen once per entry to CraneGame_01A, never after a play or a win.

1. `CraneGame_NextFeaturedPrize(out, group)` (`0x54ac0`) for group 0 (`CranePrizeFeaturedPrizeGroup1`: vanilla the Yoshi Doll, then the ten figurines in chain order) and group 1 (`CranePrizeFeaturedPrizeGroup2`: heart piece 25, seashell 42, chamber stone 3, heart piece 26, seashell 43). It walks the group in order and skips each row that `CranePrize_Has(cranePrizeId, 1, layout itemIndex)` owns. At the first row not owned it stops: that row if its layout's conditions hold, else nothing for the group. Each group shows at most one prize, and a first unowned row whose conditions fail hides the rest of its group.
2. `CraneGame_FillPlaces` (`0x54e50 (this, u8 prize[], s32 itemIndex[], placeType, featured)`) for type 1, then type 2. The featured prizes come first, group 0 before group 1: each takes its layout's place when it is empty, with no second layout. Then the non-featured rows (`+0x8c` clear) in sheet order: each layout of that type whose place is empty and whose conditions hold takes it. `CranePrize_Has` is not asked for them.
3. Each filled place: `AreaCraneGame_SpawnRailPrize` (`0x551f0`) for type 1, `AreaCraneGame_SpawnFloorPrize` (`0x55860`) for type 2.

**Shared places.** A figurine wins a place a group-1 prize also wants, and the group-1 prize does not show that visit. The one such place is type 2 index 1 (upper right): Kuribo, Teresa, Gesso, Pakkun and BowWow against heart piece 26. Group 1 uses three places in turn: heart piece 25 and seashell 42 type 1 index 1, the chamber stone and seashell 43 type 2 index 0, heart piece 26 type 2 index 1.

| Place | Featured (group) | Otherwise (sheet order, first whose conditions hold) |
|---|---|---|
| type 1 index 0 (lower left) | vanilla: Yoshi Doll (0) | RupeeRed |
| type 1 index 1 (lower middle) | heart piece 25, seashell 42 (1) | RecoveryHeart |
| type 1 index 2 (lower right) | SmallBowWow, Togezo, PukuPuku, Heiho, Pokey (0) | RupeePurple |
| type 2 index 0 (upper left) | chamber stone 3, seashell 43 (1) | RupeeRed while `GetMagicPowder` is clear, MagicPowder |
| type 2 index 1 (upper right) | Kuribo, Teresa, Gesso, Pakkun, BowWow (0); heart piece 26 (1) | Bomb with `SurfHarp`, Shield |

The mod's sheets (LASR's `crane_game.py`, `ap_option_edits.crane_rows`): the Yoshi Doll is out of group 0 and not featured, with `!TradeYoshiDollGet`, and the lower-left RupeeRed needs `TradeYoshiDollGet`, so the doll sits there until won; SmallBowWow lacks `HintYosshi`, BowWow `ShadowClear`; Shield needs `ShieldFoundFlag`; the upper-right Bomb and a red rupee before it follow the `shuffled_bombs` option.

### AreaCraneGame

`AreaCraneGame_Factory` (`0x4eff0`), vtable `AreaCraneGame_vtable` (`0x1b52d68`), priority 0x1c. CraneGame_01A: scale (12, 5, 4.5), LEB switch 0 = level flag 2.

`AreaCraneGame_Setup` (`0x4fb40`):

- `GameAreaComp` (`BoxCollision_Make` `0x53640`) into `+0x3f0`: a static box, half extents the actor's scale halved (CraneGame_01A x 1.5..13.5, y −0.2..4.8, z 0.75..5.25), filter `0x4018` (layer 24 with layer 9).
- Loads `actor/AreaCraneGame.bfres`; `AreaCraneGame_MakeAirWall` (`0x57f80`) builds `ItemAirWallComp` (`+0x400`) from its only content, the collision mesh `CraneGame.cidb`, filter `0x4001`.
- `CraneGame_BuildFeaturedPrizes`; the state machine at `+0x2b0` in state 0.

| Offset | |
|---|---|
| `+0x100`, `+0x118` | links and rail links (ACTORS.md): the type 2 and type 1 places |
| `+0x2b0` | state machine: `+0xd` state, `+0x8` frames in it, states `{enter, update, exit}` from `+0x48` (0x30 each) |
| `+0x3f0` | `GameAreaComp` |
| `+0x400`, `+0x410` | `ItemAirWallComp`, the bfres |
| `+0x420`, `+0x430` | ObjCrane, ObjGameBeltConveyor (`AreaCraneGame_FindCraneAndBelt` `0x547b0`, in Init) |
| `+0x440` | spawned prizes: `{CranePrize sp, u32 kind (0 rail, 1 floor), u32 place}`, 0x18 bytes each |
| `+0x458` | placed prizes, same entries, moved from `+0x440` once the prize is ready |
| `+0x470` | won prizes (weak): `AreaCraneGame_CountWon` (`0x51cf0`) adds a prize once and is true the first time |
| `+0x488` | prizes out of the game area (weak) |
| `+0x4a0` f32 | Judging timer, seconds |
| `+0x4a4` u8 | prizes newly won in this play, cleared on entering CraneActive |

**Update** (`AreaCraneGame_Update` `0x50a60`, per frame from the task `AreaCraneGame_Register` `0x50580` posts):

1. Drops expired entries from `+0x470` and `+0x488`.
2. An overlap query (`0x10ce240`) of the `GameAreaComp` box, query word `0x4001` (layer-9 shapes), up to 16 hits. A placed prize whose body is not among the hits has left the game area: it is counted won (`AreaCraneGame_CountWon`, `+0x4a4`), switched to its outside collision (`CranePrize_SetOutsideArea`), added to `+0x488` and taken from `+0x458`.
3. A box at (6.0, 0.0, 6.75) with half extents (5.0, 0.2, 1.75), the floor in front of the machine: placed prizes overlapping it are counted won too, and stay placed.
4. Moves ready prizes from `+0x440` to `+0x458`.
5. Sets LEB switch 0 (level flag 2) while `+0x488` is not empty: a won prize waits in the tray.
6. Runs the machine (`AreaCraneGame_RunState` `0x51fb0`) and clears ObjGameBeltConveyor `+0x2b8`.

**States** (`AreaCraneGame_ChangeState` `0x52a30`):

| # | State | Enter | Update | Exit |
|---|---|---|---|---|
| 0 | Init | | `0x54260`: finds the crane and the belt, Sleep | |
| 1 | Sleep | `0x543a0`: clears Link `+0x1b8c` bit 4 | | `0x544a0`: Link `+0x1ea9` = 3 (`Player_ControlMode`: no input), `+0x1b8c` bit 4 |
| 2 | CraneActive | `0x545b0`: `+0x4a4` 0 | `0x545c0`: Judging once ObjCrane is back in Sleep (`+0x2bd` 0) | |
| 3 | Judging | `0x54620`: timer 0 | `0x54630`: Sleep after 0.1 s, held while the belt pushed a prize this frame (`+0x2b8`), at most 4 s | |

Sleep → CraneActive: `AreaCraneGame_StartPlay` (`0x590b0`: `ObjCrane_StartPlay`, a play report at Framework `+0x5d8`) and `AreaCraneGame_StartMarinPlay` (`0x591a0`: finds Marin, `ObjCrane_StartMarinPlay` with the owner as the target), from the owner's StartPlay and StartMarinPlay. `AreaCraneGame_ResetRemainPrizes` (`0x59700`) and `AreaCraneGame_MoveHookedInArmsPrizes` (`0x59840`) are the area's side of the owner's actions.

### Spawning a prize

`AreaCraneGame_SpawnRailPrize` / `SpawnFloorPrize` `(this, place, u8 prize type, s32 itemIndex)`: actor id 0x213, keyless, in AreaCraneGame's zone, LEB parameter 0 the CranePrize row index, parameter 1 the layout `itemIndex`; `Actor_Create` under the room; a closure on the prize's ready signal (`+0x2c0`) runs `AreaCraneGame_PlaceOnRail` (`0x56e20`) or `AreaCraneGame_PlaceOnFloor` (`0x56360`); the entry goes to `+0x440`. The placers put the prize at its place plus `CranePrizeParams +0x8` in y, `CranePrize_SetInsideArea`, and zero its velocities.

### CranePrize

`CranePrize_Factory` (`0x13f290`), `CranePrize_Ctor` (`0x13f9d0`, class bits `0x200`, make_shared block 0x3b0), vtable `CranePrize_vtable` (`0x1b5f9e8`), type id static `TypeId_CranePrize` (`0x1cab5f0`). It posts no per-frame task: physics and contact callbacks only.

| Offset | |
|---|---|
| `+0xbc` | LEB parameter 0: the CranePrize row index |
| `+0xc4` | LEB parameter 1: the layout `itemIndex` |
| `+0x2a8`, `+0x2b8` | the row holder and the row |
| `+0x2c0` | ready signal |
| `+0x320` | `CollisionComp` |
| `+0x330`, `+0x340`, `+0x350` | `SkeletalModelComp`, `SeComp`, `EventCharaComp` `Item` |
| `+0x360`, `+0x370` | `actor/Prize<symbol>.bfres`, `event/PrizeCommon.bfevfl` |
| `+0x380`, `+0x384` | the two contact connections |
| `+0x388` u8 | get requested |

**Setup** (`CranePrize_Setup` `0x13fce0`): a row index past the sheet destroys the actor. Else it loads the model (`CranePrize_LoadModel` `0x140f10`) and the flowchart (`CranePrize_LoadFlowchart` `0x140ff0`); `CranePrize_OnLoaded` (`0x1413f0`) then builds the components and emits the ready signal:

- `CranePrize_MakeCollision` (`0x1415f0`): `CollisionComp` from the model's `<symbol>.cidb` (Prize bodies), each shape scaled by `modelScale`; without one a sphere of radius 0.5 (`0x143880`). Then `CranePrize_ApplyPhysicsTraits` and `PhysicsBody_SetMassAndCom`.
- `CranePrize_MakeModel` (`0x141fd0`): model `symbol`, scale `modelScale`. `CranePrize_MakeSe` (`0x142290`), `CranePrize_MakeEventChara` (`0x142410`).
- `CranePrize_MakeShellBeacon` (`0x1424f0`): `ShellBeaconComp` for row 9 (SecretSeashell) only, so the seashell sensor reacts to it.

**Inside and outside.** `CranePrize_SetInsideArea` (`0x1449e0`) sets filter `0x18040c9`; with `canGetButInsideGameArea` (Pakkun only) `0x18048c9` and `CranePrize_ConnectInsideContacts` (`0x144b20`). `CranePrize_SetOutsideArea` (`0x147430`) sets `0x218048c9` and `CranePrize_ConnectOutsideContacts` (`0x1474e0`): the game area box becomes solid to the prize, so it cannot fall back in, and Link's capsule and sensor meet it.

- Touch handlers (`CranePrize_OnTouchInside` `0x146250`, `_OnTouchOutside` `0x1478a0`): Link (`CranePrize_IsPlayer` `0x146400`) → `CranePrize_RequestGet`.
- Contact handlers (`CranePrize_OnContactInside` `0x147070`, `_OnContactOutside` `0x147c00`): Link → `CranePrize_RequestGet`; else, on a qualifying contact (`0x145590`) whose impulse exceeds `CranePrizeParams +0x0`, the landing SE at volume `min(1, (impulse − +0x0) / +0x4)`, `seInsideGameArea` or `seOutsideGameArea`.

A prize can be got only once it has left the game area box; Pakkun also inside it.

**Get** (`CranePrize_RequestGet` `0x1464f0`, once): the flowchart's entry `get` with Link and the `Item` chara, args `prizeKey` = `symbol`, `itemCount` = `getCount`, `itemIndex` = `+0xc4`. `PrizeCommon` `get`:

1. `Item.Deactivate`.
2. `Link.PrizeGetSequenceByKey(prizeKey)` (`Player_PrizeGetSequenceByKey` `0xd6fc70` → `Player_PrizeGetSequence` `0xd6fd10`): the item-get with the text of `CranePrize_GetMessage` (`0x148690`: `messageEntry`, or `HeartPieceComplete` for a heart piece that completes a container), autosave by `isAutoSave`, the prize's model held in the `CranePrizeParams` poses (`CranePrizeGetHolder_vtable` `0x1b5f748`).
3. `Inventory.AddPrizeByKey(prizeKey, itemCount, itemIndex)` (`EventAction_AddPrizeByKey` `0x7e1c70`) → `CranePrize_Add(CranePrize_TypeByKey(prizeKey), count, index)`: the one place a prize becomes owned.
4. `Inventory.HasPrize(19, 1)` (BowWow, the last figurine): owned with `BowWowFigureGet` clear → sub flow `GameShopOwner<BowWowFigureGet>`, the final prize. Then `Item.Destroy`.

A prize that is not picked up stays where it lies; leaving the level destroys it with nothing added. The next entry chooses the prizes again from what is owned, so an unowned featured prize comes back.

The mod's `PrizeCommon` (LASR's `prize_common.py`) adds a Yoshi Doll branch after `Item.Destroy`: `Inventory.HasItem {itemType 30}` (`EventQuery_HasItem` `0x7e2b10`: the trade slot `+0x9a`), `CheckFlag`/`SetFlag` of `LensFoundFlag`, `SetWarashibeItem` (`EventAction_SetWarashibeItem` `0x7e2a10`). LASR passes `LensFoundFlag` as the Int 271, where `EventFlow_ParseString` takes only a String and throws. The branch runs only with the Yoshi Doll in the trade slot, which the module's `AddItemID` hook never puts there (it sets the getting flag `TradeYoshiDollGet` instead). The mod's `GameShopOwner.bfevfl` gives the final prize through `GenericItemGetSequenceByKey` and `AddItemByKey PanelDungeonPiece` index 24 (LASR's `trendy_game.py`).

## The crane (ObjCrane)

A kinematic anchor that code moves over the machine, a dynamic claw body hanging from it on a vertical slider joint, and four arm bodies on motor-driven hinges. A catch is physics only: no random numbers, no grip roll and no scripted carry in the player's play. Marin's play carries its target by code.

`ObjCrane_Factory` (`0xad5380`) → `ObjCrane_Ctor` (`0xad5ac0`, make_shared 0x720, class bits `0x1001`), vtable `ObjCrane_vtable` (`0x1bf8518`), type id static `TypeId_ObjCrane` (`0x1cab5b0`). `ObjCrane_Setup` (`0xad5ff0`), `ObjCrane_OnDestroyed` (`0xad7ed0`), `ObjCrane_Register` (`0xad8070`). `ObjCrane_Update` (`0xad8550`): the machine (`ObjCraneSM_Update` `0xad85e0`), the vertical drive (`+0x5e0`), the skeletal model marked dirty, the contact flag `+0x6f1` cleared.

| Offset | |
|---|---|
| `+0x2b0` | `StateMachine<12>` (`ObjCraneSM_vtable` `0x1bf8650`, `ObjCrane_ChangeState` `0xad8630`): state `+0x2bd` |
| `+0x5b0` | `CraneRootComp`: kinematic anchor at the actor's root |
| `+0x5c0` | `BodyComp`: the claw head |
| `+0x5d0` | slider joint root ↔ body (`PrismaticJoint_Make`) |
| `+0x5e0` | vertical drive, `std::function<void()>` |
| `+0x610 + 0x10·i` | `ArmComp{i}`, i 0..3 (`+0x280` its hinge to the body) |
| `+0x650`, `+0x660`, `+0x670`, `+0x680` | `SkeletalModelComp` (`Crane`), `BoneEffectComp`, `LightComp`, `SeComp` |
| `+0x690` | loop SE handle |
| `+0x6a0` | mover (Movers) |
| `+0x6b0`, `+0x6c0` | carried target (weak) and its offset from the body: Marin's play only |
| `+0x6d0` | `vector<weak_ptr<Actor>>`: CranePrize actors still in the claw after the release |
| `+0x6e8` f32 | state timer, seconds |
| `+0x6ec` s32 | release retries, 0 at each start |
| `+0x6f0` u8 | Marin plays: every SE takes its `_MARIN` variant |
| `+0x6f1` u8 | the body touched something this frame (the body's contactFound signal, `+0x470`) |
| `+0x6f2`/`+0x6f3` u8 | the target's grip / release sent |

### Rig

| Part | Values |
|---|---|
| `CraneRootComp` | kinematic |
| `BodyComp` | made with `{0.4, 0.15, 0.4}`; mass 50; mass-space inertia 0 (it does not rotate); friction 0, combine multiply; restitution 0.1, combine average; gravity off |
| Slider | prismatic joint root ↔ body, the root's frame turned 90°; limit [−2.2, 0] m, enabled |
| Arms | four `ArmComp{i}`, mass 0.2, hinged on the body at radius 0.257 m at 45°, 135°, 225°, 315°; limit [0, 0.5767] rad (0..33°, 0 closed), limit and drive enabled |
| Model | `base_01` follows `BodyComp`, `arm_01`..`arm_04` follow `ArmComp0`..`3` |

`ObjCrane_SetArmDrive` (`0xad8db0 (v, force)`): each hinge's drive velocity −v rad/s and force limit `force`; `ObjCrane_OpenArms` (`0xadad30`).

| Arm setting | Drive velocity | Force limit |
|---|---|---|
| open (CatchOpen, ReleaseOpen) | +5 rad/s | 100 |
| grip (CatchClose, kept through Ascent and ReturnToHome) | −3.14 rad/s | 1 |
| close (Setup, ReleaseClose) | −5 rad/s | 5 |

The vertical drive changes the body's y velocity toward a target, at most 20·dt a frame (20 m/s²): +1.5 m/s up (Setup, Ascent; `0xade550`), −1.0 m/s down (Descent; `0xadcaa0`), 0 hold (after Descent; `0xadc7a0`).

### States

Times are seconds in the state. SE names are `SE_MAP_CRANE_*`.

| # | State | Does | Next |
|---|---|---|---|
| 0 | Sleep | | (StartPlay, StartMarinPlay) |
| 1 | Preparation | | 2 after 0.5 |
| 2 | Adjust | light on; the mover runs | 3 when the mover is done |
| 3 | CatchOpen | arms open, `ARM_OPEN` | 4 after 1.5 |
| 4 | Descent | drive down, loop `MOVE3` | 5 (below) |
| 5 | WaitALittleAfterDescent | | 6 after 2.0 |
| 6 | CatchClose | light off, grip drive, `ARM_CLOSE1`; at 0.3 a carried target gets its state 2 (Gripped) | 7 after 2.0 |
| 7 | Ascent | drive up, loop `MOVE4`; a carried target follows the body | 8 at the slider's top |
| 8 | ReturnToHome | the return mover; `HIT` at the end | 9 when the mover is done |
| 9 | WaitALittleAtHome | | 10 after 0.5 |
| 10 | ReleaseOpen | arms open, `ARM_OPEN`; at 0.3 a carried target gets its state 3 (Fall) | 11 after 1.5 |
| 11 | ReleaseClose | close drive, `ARM_CLOSE2` | 0 after 1.5 |

**Descent** (`ObjCrane_DescentUpdate` `0xad95f0`) goes on while the slider is above −2.2 m, the body touched nothing this frame, each arm hinge's `0x10c0de0` value is below 20 in length and fewer than 5 s have passed. The claw stops at the bottom (2.2 m below the top), on contact, when an arm is pushed, or after 5 s. Ascent has no time limit.

**Release check** (`ObjCrane_ReleaseCloseUpdate` `0xada060`, after 1.5 s): a sphere overlap of radius 0.6 m at the body's position − 0.6 m in y refills `+0x6d0` with the CranePrize actors it meets. `+0x6ec` counts up; a negative old value with prizes in the list would go back to ReleaseOpen, but every start writes 0, so there is one release per play. The list feeds `CheckPrizeHookedInArms` and `MoveHookedInArmsPrizes`.

### Movers

A mover (`+0x6a0`) moves the actor's root kinematically; `CraneRootComp` follows the root and drags the body and arms by their joints. Base `CraneMover_Ctor` (`0xadb0c0`); the axis mover at its `+0x20` (`CraneAxisMover_Ctor` `0xadbd50`, `StateMachine<3>` Sleep, Move, Brake): Move (`CraneAxisMover_Move` `0xadbe80`) accelerates to the top speed and brakes once the distance left is within speed·dt + speed²/(2·acceleration); Brake (`CraneAxisMover_Brake` `0xadc050`) slows to Sleep. Mover vtables (`CranePlayerMover_vtable` `0x1bf8cb8`, `CraneMarinMover_vtable` `0x1bf8d78`, `CraneReturnMover_vtable` `0x1bf89d0`): `+0x10` start, `+0x18` update, `+0x20` done, `+0x28`/`+0x30` horizontal / vertical axis not yet used, `+0x38`/`+0x40` its button held. The UI and the button's lights read these.

| Mover | Make | Acceleration | Top speed | Path |
|---|---|---|---|---|
| player | `CranePlayerMover_Make` `0xadf050` | 7 m/s² | 3 m/s | each axis once, either order, while its button is held |
| return | `CraneReturnMover_Make` `0xadae60` | 7 m/s² | 1.9 m/s | z back to spawn z, then x back to spawn x |
| Marin | `CraneMarinMover_Make` `0xae0c40` | 7 m/s² | 3 m/s | x to the target's x (after 0.2 s), z to the target's spawn z (after 0.4 s) |

**Player mover.** Horizontal is A or D-pad Right (`Crane_HorizontalHeld` `0xae0200`: held & `0x201`), vertical X or D-pad Up (`Crane_VerticalHeld` `0xae0040`: `0x804`). `CranePlayerMover_AwaitButtonPressed` (`0xadf320`) waits for a fresh press of an unused axis, ends once both are used, and after 1.0 s idle opens the message `UI:CraneOperation`. Horizontal moves toward spawn x + 8.0 (`0xadfa30`), vertical toward spawn z − 2.73 (`0xadfc60`); releasing brakes. The claw travels only away from home: CraneGame_01A x 3.75..11.75, z 4.28..1.55. Released at top speed it slides on 0.64 m. There is no time limit.

Home, where the claw releases, is the spawn position (3.75, 4.30, 4.28), above the belt.

### Starting a play

- `ObjCrane_StartPlay` (`0xadee60`): `+0x6ec` 0, `+0x6f0` 0, a player mover, no target, Preparation.
- `ObjCrane_StartMarinPlay` (`0xae0970 (crane, target sp*, Marin*)`): `+0x6f0` 1, a Marin mover aimed at the target, the target carried (`+0x6b0`), Marin's `MarinPlayCrane` (`Marin_StartPlayCrane` `0x9f6fc0`), light on, Preparation. CatchClose sends the target's grip machine Gripped, ReleaseOpen Fall.

A play ends when the crane is back in Sleep; AreaCraneGame's CraneActive update polls it. The crane calls nothing in AreaCraneGame or the HUD.

## Moving parts and prize physics

None of the three parts reads the game state: they run the same before, during and after a play and during events.

### Collision layers

| Body | Word | Layer | Collides with |
|---|---|---|---|
| ObjGameBeltConveyor `FloorComp` | `0x81025081` | 1 | 2, 7, 9, 12, 19, 26 |
| ObjMovingFloorCrane `FloorComp` | `0x4082` | 2 | 2, 9 |
| ObjCraneFence `FenceComp`, Deactive | `0x4001` | 1 | 9 |
| ObjCraneFence `FenceComp`, Active | `0x4801` | 1 | 6, 9 |
| AreaCraneGame `GameAreaComp` | `0x4018` | 24 | 9 |
| CranePrize inside the game area | `0x18040c9` | 9 | 1, 2, 9, 18, 19 |
| CranePrize inside, `canGetButInsideGameArea` (Pakkun) | `0x18048c9` | 9 | 1, 2, 6, 9, 18, 19 |
| CranePrize out of the game area | `0x218048c9` | 9 | 1, 2, 6, 9, 18, 19, 24 |

Layer 6 is Link's. Prizes meet each other, the belt, the floors and the fence; Link only an Active fence; the game area box only prizes already out.

### Physics helpers

- `BoxCollision_Make` (`0x53640 (out sp, actor, name, parent sp*, world*, Vec4* halfExtents, bool* static)`): a box BodyCollision, shape flags `0xb` (simulation, scene query, visualization), a `PxRigidStatic` or `PxRigidDynamic`.
- `PrismaticJoint_Make` (`0x10c98a0 (out sp, PxPhysics*, actor a, frame a, actor b, frame b)`): `PxPrismaticJointCreate`; `0x10c0fa0` builds its linear limit, `PrismaticJoint_EnableLimit` (`0x10c1450`) turns it on.
- `PhysicsBody_SetMassAndCom` (`0x10c4310`): `PxRigidBodyExt::setMassAndUpdateInertia`. `PhysicsBody_AddImpulseAtPos` (`0x10c43a0`): `PxRigidBodyExt::addForceAtPos`, impulse.
- PxCombineMode: 0 average, 1 min, 2 multiply, 3 max; PhysX takes the higher mode of the two materials.

### ObjGameBeltConveyor

Vtable `ObjGameBeltConveyor_vtable` (`0x1c025a8`), no model (the belt's look is the map's), no update task. `ObjGameBeltConveyor_Setup` (`0xb95ea0`): `FloorComp` (`+0x2a8`), a static box with half extents (1.5, 0.035, 0.75) m (CraneGame_01A x 2.25..5.25, top y 0.735, z 3.75..5.25), friction 0, restitution 0.1; `SE_MAP_CRANE_BELT_MOVE` from the start.

- **Prizes.** `ObjGameBeltConveyor_OnContact` (`0xb96bb0`) runs each physics step a dynamic body stays in contact. It drives the body's z velocity toward +1.0 m/s: impulse m·(1 − vz), clamped to ±m·0.5·|g|·dt (4.905 m/s²), spread over the contact points. x and y are left alone. The belt has no friction, so this push is all that moves a prize on it: toward +z, out of the machine. It sets `+0x2b8`, which AreaCraneGame clears each frame and its Judging state waits on.
- **Link.** `ObjGameBeltConveyor_OnPlayerHit` (`0xb969a0`): 1 m/s toward +z added to his controller's move while he touches it; sets `+0x2b9`.

### ObjMovingFloorCrane

`ObjMovingFloorCrane_Ctor` (`0xc12fc0`), vtable `ObjMovingFloorCrane_vtable` (`0x1c09190`), type id static `0x1cab5e0`, model `MovingFloorCrane`.

| Offset | |
|---|---|
| `+0x2b0` | state machine (2 states, `ObjMovingFloorCrane_ChangeState` `0xc14760`) |
| `+0x380` | `RootComp`: static anchor |
| `+0x390` | `FloorComp`: the floor, dynamic |
| `+0x3a0` | prismatic joint `RootComp` ↔ `FloorComp` |
| `+0x3b0` | `SkeletalModelComp` on `FloorComp`, local (0, −0.11, 0) |
| `+0x3c0` | drive, `std::function<void()>` |
| `+0x3f0` f32 | state timer, seconds |

`ObjMovingFloorCrane_Setup` (`0xc13350`):

| Part | Values |
|---|---|
| `FloorComp` | dynamic, mass 10, mass-space inertia 0 (never turns), gravity off |
| Shapes (half extents; centre) | slab (1.125, 0.11, 0.75) at (0, 0, 0); rims (0.425, 0.025, 0.2) at (±0.7, 0.11, 0) and (1.125, 0.025, 0.275) at (0, 0.11, ±0.475): a 0.55 × 0.4 m pocket at the top's centre |
| Material | friction 0.5, combine multiply; restitution 0.1 |
| Joint | along world x, limit ±1.5 m |

| # | State | Drive |
|---|---|---|
| 0 | MoveToRight, 4.7 s | `ObjMovingFloorCrane_DriveRight` (`0xc15480`): x velocity toward +0.8 m/s |
| 1 | MoveToLeft, 4.7 s | `ObjMovingFloorCrane_DriveLeft` (`0xc15180`): toward −0.8 m/s |

Each drive changes the x velocity by at most 10·dt a frame (10 m/s²); `ObjMovingFloorCrane_Update` (`0xc146b0`) runs the machine, then the drive. The period is 9.4 s: each 3.0 m crossing takes about 3.75 s and the floor rests about 0.95 s at the limit. Both floors start in the same frame and move in step, 5.27 m apart (CraneGame_01A floor 1 x 3.35..6.35, floor 2 x 8.62..11.62, z 0.75..2.25, top y 0.81). A prize rides by friction (0.6 × 0.5 = 0.3); the reversal and the stop at the limit exceed 0.3·g = 2.9 m/s², so prizes slip there.

### ObjCraneFence

`ObjCraneFence_Ctor` (`0xae2190`), vtable `ObjCraneFence_vtable` (`0x1bf8e38`), model `CraneFence`. The wall between the prize tray and Link's side: `ObjCraneFence_Setup` (`0xae2490`) makes `FenceComp` (`+0x380`), a static box with half extents (0.1, 2.25, 1.5) m centred 2.25 m up (CraneGame_01A x 5.27..5.47, z 5.25..8.25).

| # | State | Enter | Update |
|---|---|---|---|
| 0 | Deactive | `ObjCraneFence_DeactiveEnter` (`0xae3750`): filter `0x4001` (prizes only) | `+0x3b0` set → Active; else the model sinks at 1 m/s to 0 |
| 1 | Active | `ObjCraneFence_ActiveEnter` (`0xae35c0`): filter `0x4801` (prizes and Link) | `+0x3b0` clear → Deactive; else the model rises at 1 m/s to 0.75 |

Each enter sets the filter and re-sets the actor's global pose. `ObjCraneFence_Pop` (`0xae3970`) / `_Depop` (`0xae39c0`) set `+0x3b0` to 1 / 0 with `SE_MAP_CRANE_FENCE_ON` / `_OFF`; `ObjCraneFence_Reset` (`0xae3470`) starts it Deactive.

### Prize bodies

`CranePrize_MakeCollision` reads the embedded file `<symbol>.cidb` of `Prize<symbol>.bfres` and makes a dynamic `CollisionComp` from its shapes. `CranePrize_ApplyPhysicsTraits` (`0x143c30`) applies the `CranePrizePhysicsTraits` row that `collision.traits` names, then `PhysicsBody_SetMassAndCom` sets its mass with the row's `centerOfGravity` (body frame, not scaled); the inertia comes from the shapes.

**cidb.** `u32 4`, `"CIDB"`, `u32 0x10`, `u32 0`, `+0x10` u32 shape count, entry offsets from `+0x14` (each + 8). Entry: `+0x0` u32 parameter bytes, `+0x4` u32 4, `+0x8` u32 type, `+0xc` u32 `0xffffff00`, `+0x10` position, `+0x1c` rotation (Euler, radians), `+0x28` parameters: 1 box (half extents), 2 capsule (radius, half height), 3 sphere (radius).

| Prize | Shapes (centre; size, before `modelScale`) |
|---|---|
| RupeeRed, RupeePurple | capsule (0, 0, 0) r 0.337 h 0.204; box (0, −0.377, 0) (0.074, 0.221, 0.081) |
| RupeeSilver | box (0, −0.373, 0) (0.226, 0.059, 0.134); capsule (0, −0.015, 0) r 0.224 h 0.439 |
| RecoveryHeart | sphere (0, −0.196, 0) r 0.045; capsule (−0.018, 0.059, 0) r 0.221 h 0.099 |
| MagicPowder | box (0, −0.2, 0) (0.191, 0.060, 0.150); capsule (0, 0.2, 0) r 0.314 h 0.149 |
| Bomb | sphere (0, 0.001, 0) r 0.348; box (0, 0.32, 0) (0.114, 0.197, 0.129) |
| Shield, PanelDungeonPiece | box (0, −0.221, 0) (0.203, 0.111, 0.108); capsule (0, 0.097, 0) r 0.315 h 0.049 |
| YoshiDoll | sphere (0, 0.331, 0.229) r 0.214; box (0, 0.074, 0) (0.206, 0.402, 0.188) |
| HeartPiece | box (−0.007, −0.198, 0) (0.125, 0.059, 0.071); capsule (0.002, 0.046, 0) r 0.209 h 0.124 |
| SecretSeashell | box (0.012, −0.377, −0.025) (0.160, 0.039, 0.178); capsule (0.005, 0.028, 0) r 0.192 h 0.433 |
| SmallBowWow | sphere (0, 0.35, 0.05) r 0.431; box (0.033, 0.582, −0.272) (0.383, 0.255, 0.276) |
| Kuribo | box (0, 0.313, 0) (0.433, 0.257, 0.319); sphere (0.054, 0.827, −0.14) r 0.579 |
| Togezo | box (0, 0, 0) (0.329, 0.123, 0.267); sphere (0, 0.395, 0) r 0.404 |
| Teresa | sphere (0, 0.639, 0.052) r 0.584; two boxes |
| PukuPuku | sphere (0.02, 0.496, 0) r 0.473; two boxes |
| Gesso | box (0, 0.46, 0) (0.626, 0.136, 0.239); capsule (−0.006, 0.71, 0) r 0.5 h 0.678; box (0, −0.03, 0) (0.342, 0.134, 0.197) |
| Heiho | capsule (0, 0.841, 0) r 0.5 h 0.707; box (0, 0.067, 0) (0.357, 0.089, 0.274) |
| Pakkun | four boxes and a sphere (0, 2.92, 0) r 0.653: about 3.6 tall |
| Pokey | spheres at y 0.433, 1.27, 2.03 (r 0.516, 0.542, 0.593) |
| BowWow | sphere (0, 0.66, 0.02) r 0.851 |

**Traits.** `mass` → `setMass`; `drag` → linear damping; `angularDamp` → angular damping; `bounciness` → restitution; `staticFriction`, `dynamicFriction` → the materials, friction combine always multiply; `bounceCombine` → restitution combine; `axisLock.rotationX/Y/Z` → the angular lock flags.

| Row | mass | drag | angularDamp | bounciness | friction (static, dynamic) | bounceCombine | Used by |
|---|---|---|---|---|---|---|---|
| Sample | 0.3 | 0.3 | 0.5 | 0.1 | 0.6, 0.6 | 0 | MagicPowder, Shield, YoshiDoll, SecretSeashell, Kuribo, Togezo, Gesso, Heiho, Pakkun, Pokey, PanelDungeonPiece |
| Rupee | 0.5 | 0.3 | 0.5 | 0.1 | 0.6, 0.6 | 0 | the three rupees |
| Heart | 1.2 | 0.3 | 0.5 | 0.1 | 0.6, 0.6 | 0 | RecoveryHeart, HeartPiece |
| Bomb | 1.0 | 0.1 | 0.5 | 0.1 | 0.6, 0.6 | 0 | Bomb |
| SmallBowWow | 1.0 | 0.2 | 0.5 | 0.1 | 0.6, 0.6 | 0 | SmallBowWow |
| Teresa | 0.2 | 0.5 | 0.7 | 0.1 | 0.6, 0.6 | 0 | Teresa |
| PukuPuku | 0.3 | 0.5 | 0.5 | 0.1 | 0.6, 0.6 | 0 | PukuPuku |
| Pakkun | 0.7 | 0.3 | 0.5 | 0.1 | 0.6, 0.6 | 0 | none |
| BowWow | 1.0 | 0.1 | 0.2 | 0.1 | 0.6, 0.6 | 0 | BowWow |

No row locks an axis. With multiply, a prize has friction 0.36 against another prize, 0.3 against a moving floor and 0 against the belt and the claw.

### The way out

The game area box covers the machine's inside. The claw releases above the belt (x 2.25..5.25), which carries the prize past z 5.25, out of the box and into the tray, closed off from Link's side by the fence at x 5.37. AreaCraneGame's next update finds the prize out of the box and judges it won.

## Open questions

- The overlap query keeps 16 hits and prizes have 1 to 5 shapes each: whether more than 16 shapes inside the box make prizes still inside count as out (whether the hit callback `0x1b52f68` continues the query).
- The floor-front box of AreaCraneGame's update (Update step 3): what it is for, when it overlaps the game area box only at z 5.0–5.25.
- AreaCraneGame `+0x4a4`, the prizes won in a play: no reader found.
- Layers 18 and 19 in the prize mask, and 7, 12, 19, 26 in the belt's: which bodies (the map's `CraneGame.cidb`, the claw, the arms).
- `0x145590`: which contacts play a prize's landing SE (a shape type `& 0x1f` = 1, or an ObjMovingFloorCrane).
- `getEvent.jingle`: its reader.
- `CranePrizeParams +0x30`: which of the get holder's poses vtable `+0xb0` serves.
- ObjCrane: the `BodyComp` argument `{0.4, 0.15, 0.4}` (half extents or size), the `ArmComp` shapes (`0xad8ee0`), whether `CraneRootComp` has a shape, the slider's frames (where the body hangs at position 0, the axis of the 90° turn).
- `0x10c0de0`: which joint quantity the Descent stop measures (constraint force assumed).
- Whether a wedged claw body can keep Ascent, which has no time limit, from reaching the slider's top.
- ObjMovingFloorCrane `RootComp` (`0xade850`): its shape; the joint's frames, taken as identity at the actor. What the pocket in the floor's top holds against: the prizes placed at the floor's position (`0xc15710`).
- Whether a prize on a moving floor can be pushed off into a gap at a limit stop (the map collision not checked).
- The belt's `+0x2b9`: no reader found.
- cidb entry `+0x4` and `+0xc`; the shape types are read off the parameter counts, not the parser.
- The owner component's `+0x2e0` object and its `+0x288`, which end the grip machine's Fall in Marin's play.
- The crane camera's Link `+0x318` fields (distance, pitch, `+0x504`, `+0x4f0`, `+0x4c0`).
- Who sets the button's Marin control `+0x43c`.
- Whether evfl catches `EventFlow_ParseString`'s exception or the game aborts (the mod's Yoshi Doll branch).
- The play report at Framework `+0x5d8` (`0xdf46a0`, field `Type`).
