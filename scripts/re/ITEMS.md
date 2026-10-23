# Items and inventory (game 1.0.0)

What adding and owning an item does in the main module, the Chamber Dungeon's stones and panels, and the saved data the inventory is part of, from static analysis. Offsets are module offsets; `las.sym` names them at their runtime addresses (module offset + 0x8504000). Item ids are `Items.gsheet`'s `itemID`, as the module's `kItemNames` (`game/hooks/inventory.hpp`) lists them.

## Adding and owning

`Inventory_AddItemID(item, count, index)` (`0x8d08e0`, hooked by the module) and `Inventory_HasItem(item, count, index)` (`0x8d1a30`) switch on the item id, 0 to 0x7e. `count < 1` adds nothing and owns everything. An id from 0x80 throws (a bitset's out_of_range), so the module's hook passes on ids below 127 only.

After every add, also those that change nothing, AddItemID sets the item's bit in the obtained bits (inventory `+0x124`, a bit per item id), and for the Boomerang (7), Seashell (22), Piece of Heart (58), Bottle (113) and Chamber Stone (114) it sends a play report (`PlayReport_ItemCommon`: SaveId, TotalTimePlayed, Chapter, Warashibe, then ItemId and Index). The getting flags (`Item_CheckGettingFlag`, `Item_SetGettingFlag`, both hooked) are something else: the event flag the item's `Items.gsheet` row names (row `+0x70`).

Inventory fields by name are in the module's `game/data/inventory.hpp` (`InventoryItems`; Saved data below).

| Id | Item | AddItemID | HasItem |
|---|---|---|---|
| 0, 2 | SwordLv1, Shield | `+0xa8` bit 0 / 1 | the bit |
| 1, 3 | SwordLv2, MirrorShield | also `+0x9c` bit 0 / 1 | `+0x9c` bit |
| 4 | Bomb | `+0xa8` bit 2, then `Inventory_AddBombs(count)` | `min(+0x9e, cap) >= count`: cap 0 without the bit, 30, 60 with Bomb_MaxUp |
| 5–9, 12, 15, 16 | Bow, HookShot, Boomerang, MagicRod, Shovel, RocsFeather, PegasusBoots, Ocarina | `+0xa8` bits 3–7, 9, 11, 12 | the bit |
| 10 | SleepyMushroom | `+0xac` bit 15 and the time at `+0xb0` | the bit |
| 11 | MagicPowder | `+0xa8` bit 8, `Inventory_AddPowder(count)` | `min(+0xa0, cap)`: 20, 40 with MagicPowder_MaxUp |
| 13, 14 | PowerBraceletLv1, Lv2 | `+0xa8` bit 10; Lv2 also `+0x9c` bit 2 | the bit; Lv2 `+0x9c` bit 2 |
| 17, 18, 19 | ClothesGreen, Red, Blue | `+0x9c` bits 6 (red) and 7 (blue): green clears both, red sets 6 and clears 7, blue the other way | green always owned |
| 20, 21 | Flippers, SecretMedicine | `+0xac` bit 0 / 1 | the bit |
| 22 | Seashell | `+0x88` bit `index`, index 0–49 | index < 0: popcount, else the bit |
| 23 | GoldenLeaf | `+0xc2` bit `index`, index 0–4 | as seashells |
| 24–28 | TailKey … BirdKey | `+0xac` bits 2–6 | the bit |
| 29–40, 42, 43 | trade items | `+0x9a` = 1–14 (41 is not an item): the one held, replacing the last | `+0x9a` equal |
| 44, 45, 47, 49 | Compass, DungeonMap, StoneBeak, NightmareKey | the current dungeon's entry (`DungeonItems[LevelIndex]` `+1`) bits 0, 1, 2, 3; nothing outside dungeons (`LevelIndex` 0xff) | the same entry |
| 48 | SmallKey | entry `+0` += count, 0–255 | entry `+0 >= count` |
| 50–57 | instruments | `+0xac` bits 7–14, then `Inventory_RecordChapter(instrument)` | the bit |
| 58 | HeartPiece | `+0x90` bit `index`, index 0–31; health to the maximum | as seashells |
| 59 | HeartContainer | `+0x98` bit `index`, index 0–8; health to the maximum | as seashells |
| 60 | Arrow | `Inventory_AddArrows(count)` | `min(+0x9f, cap)`: 0 without the Bow, 30, 60 with Arrow_MaxUp |
| 61, 62, 100 | Apple, GreenApple, Heart | health + 4, + 12, + count × 4, up to the maximum | Heart: health ≥ count × 4 |
| 101–108 | Rupee1, 5, 20, 50, 100, 300 | `+0x80` + 1, 5, 20, 50, 100, 300 (not × count), 0–9999 | rupees ≥ count × the value |
| 110, 126 | Fairy, BottleFairy | bottle slot `index` 0–2: `+0xc0` bit set while the bottle (`+0xa8` bit 13 + index) is owned, else cleared; index -1 adds nothing | index < 0: popcount of `+0xc0` |
| 111, 112 | DefenceUp, PowerUp | the scene's buff `+0x640` = 2 / 1, `+0x644` = 30.0 (its time, guess) | that buff active |
| 113 | Bottle | `+0xa8` bit 13 + `index`, index 0–2 | index < 0: owned bottles |
| 114 | PanelDungeonPiece | the chamber stone bit `index`, index 0–50 (Chamber stones) | the bit |
| 115 | PanelDungeonPieceSet | the set bit `index`, index 0–12 | the bit |
| 116 | ShellRader | `+0xac` bit 16 | the bit |
| 120–122 | songs | `+0x9b` bits 0–2; the selected song (`+0xa1`) becomes this one while none is | the bit |
| 123–125 | MagicPowder, Bomb, Arrow _MaxUp | `+0x9c` bits 3–5, then the count's add with 0 | the bit |
| 41, 46, 63–99, 103, 107, 109, 117–119 | | only the obtained bit | false |

What this means for received items:

- An index out of its range adds nothing but the obtained bit; a received Chamber Stone with index -1 is one (TODO.md).
- Dungeon items go to the dungeon Link is in; the module's hook sets `LevelIndex` to the item's dungeon around the call.
- A trade item replaces the one held; the module's hook adds none of 29–40 and sets their flags instead.
- Secret Medicine is a single bit: a second one while Link holds one is lost. LASR's pool has three.
- Heart containers take 9 indices: the 8 bosses and the Chamber Dungeon's reward for 12 clears (`MapPieceClearReward` index 8).

## Chamber stones

**Tables** (each `{u8* bits, s32 bytes, s32 bits}`): `PanelPieceBits` the stones owned (51 bits), `PanelPieceSetBits` the panel sets owned (13), and copies `PanelPieceBits2`, `PanelPieceSetBits2`: those Dampé has made into panels. `PanelNotifyBits` tells Dampé there is something new: AddItemID sets bit 0 for a new stone, bit 2 for a further stone or a set, bit 3 for a special stone (7, 13, 18, 19, 24, 32, the `SpecialAddition` panels).

**Stones and panels.** A `MapPieceList` row (a chamber panel; `PanelSheets` `+0x0`, rows 0x98 bytes) names its source in `mPanelItem` (`+0x18`): below 13 the set of that index (0–7 the dungeons, 9 the Color Dungeon), else the stone `mPanelItem - 13`. A dungeon's set holds its panels; each of the 33 stones (`PanelStone.gsheet`) gives one panel. The Items page counts the owned stones whose `PanelStone` row has `mItemMenuCount` (`GmItem_ChamberStoneCount`): 3, 4, 8, 10–13 and 20–26, the 14 found outside the Chamber Dungeon.

| Stone | Vanilla source (LASR location) | Panel |
|---|---|---|
| 3 | Trendy Game (`trendy-prize-4`) | `PanelLv02BottleGrotto_04H` |
| 4 | Seashell Mansion, 30 shells (`30-seashell-reward`) | `PanelLv03KeyCavern_04D` |
| 8 | Rapids race (`rapids-race-30`) | `PanelLv04AnglersTunnel_03F` |
| 10 | Town Tool Shop after the Coral Triangle (`shop-slot5-3rd`, `ShopPanelItem` 0) | `PanelLv06FaceShrine_02C` |
| 11, 12 | Fishing (`fishing-150`, `fishing-ol-baron`) | `PanelLv06FaceShrine_02D`, `PanelLv05CatfishsMaw_05C` |
| 13 | Seashell Mansion, 50 shells (`50-seashell-reward`) | `PanelSpecialAddition_02E` |
| 20–23 | Town Tool Shop after the Thunder Drum (`shop-slot5-4th` to `7th`) | `PanelLv08TurtleRock_04H`, `_05H`, `_04A`, `_05A` |
| 24 | Trendy Game (`trendy-prize-final`) | `PanelSpecialAddition_05E` |
| 25, 26 | Town Tool Shop after the Color Dungeon (`shop-slot5-1st`, `2nd`) | `PanelLv10ClothesDungeon_04D`, `_04A` |
| 0–2, 5, 6, 9, 14–17 | Chamber Dungeon clear rewards (`MapPieceClearReward`) | |
| 7, 18, 19, 27–32 | not traced | `SpecialAddition` and `Lv11OnlyDungeon` panels |

**Dampé** (`Danpei.bfevfl`): his talk checks `CheckNewDungeonSet` and `CheckNewPanelStone` (`PanelDungeon_NewStone`: notify bit 0), plays `RupeePanelGetSequence` and `ExEnemyPanelGetSequence` behind `CheckPanelAddStone` with the indices 13 and 24 (not decompiled), then `ProcessDungeonAndPanelStone` (`PanelDungeon_ProcessStones`): it adds each dungeon's set (`AddItemID(115, 1, dungeon)`) whose instrument Link owns, and set 9 once `Lv10DungeonClear` is set, copies every owned set and stone into the second tables and clears notify bits 0, 2 and 3. A stone added with its index thus becomes a panel at Dampé's next talk, as a found one does; a received instrument brings its dungeon's set the same way.

## Saved data

The save is the static `GlobalSave` (`0x1cbb200`). `GlobalSave_Reset` (`0xdfab90`, a new game; also on `$QUIT`) zeroes `+0x20`..`+0x5f00` (the file image) and fills its sections through pointer slots:

| Slot | Section |
|---|---|
| `+0x5f00` | the file header, 0xe0 bytes: version 0xe `+0x0`, the body's CRC32 `+0x4`, a time `+0x8`, `+0x32` 1, `+0x34` 4, `+0x88` u16 1970, `+0x8a`/`+0x8b` 1 |
| `+0x5f20` | the body, 0x5e00 bytes, which the CRC32 covers; `EventFlags_ptr` is this slot |
| `+0x5f80`, `+0x5fb0` | not mapped |
| `+0x5fc8` | a section of 0xe0- and 0x20-byte strides (`PanelQuery_Stone4`'s save `+0x5093` is in it) |
| `+0x6160` | `SaveHeader_ptr`: the save header (`SaveHeader`, 0x140 bytes) at GlobalSave `+0x53a0` (`0x1cc05a0`) |
| `+0x6168` | `Inventory_ptr`: the current data (`SaveCurrent`, `InventoryItems`, 0x160 bytes) at GlobalSave `+0x54e0` (`0x1cc06e0`) |
| `+0x6170` | `MapPinSection_ptr`: the map pins' section (0x690 bytes zeroed, then up to `+0x763`; UI.md, Map markers) |

The save files are `save:\savedata0.bin` to `3` (a table at `0x1cc1950`, 0x28 bytes per file), read and written by asynchronous file tasks (`0xdfd420`, `0xe00a80`, `0xe02730`, `0xe072b0`, `0xe07930`).

**SaveHeader** (0x140 bytes, the size `GlobalSave_Reset` clears):

- `knockCount`: bit 0 is what the event query `TestEnemyKnockCountOddEven` tests (lambda `0x96c680`, registered by `0x96c5a0`); the name is a guess.
- `deathCount`: one more, up to 0xffff, by a Link event action's lambda (`0xd6e650`, beside `AddDeadCounter`'s table; likely); not 0, it changes a choice in `0x82c870`.
- `totalTimePlayed`: microseconds; the actor update phase (`0x15be8`) adds the frame time × 1e6 while no event runs, saturating.
- `chapterCount` and `chapters`: `Inventory_RecordChapter` (`0x8d1830`) records each instrument: its index (0–7), bits (bit 0 item 9 obtained, bit 1 item 5 obtained, bit 2 inventory `+0x93` bit 0, bits 3 and up the Chamber Dungeon sets owned) and the rupees.

**InventoryItems** (`SaveCurrent`, 0x160 bytes): `GlobalSave_Reset` clears 0x160 bytes, and `SaveCurrent_LoadKeepLocation` (`0xdf8850`) copies 0x160 bytes from a loaded save, keeping `LevelName`, `Locator`, `RestartPos` and bits 0–2 of `RestartDirection`. A new game sets `LevelName` `MarinTarinHouse`, `Locator` `Opening`, `Health` 12, `EquipY` and `EquipX` 0xff, and picks `GridPuzzle` and `TilePuzzle` at random.

| Field | |
|---|---|
| `GridPuzzle` `+0x86` | seven 2-bit direction codes and bit 15. A grid puzzle (strings `grid`, `gridConfig`; `GridPuzzle_Step` `0x8f4430`) compares each step's direction with code `(index & 0xf) * 2` and plays `BGM_NAZOTOKI_SEIKAI` once done; `0x909c60` shows code + 1 as an arrow glyph. Guess: the Signpost Maze |
| `TilePuzzle` `+0xa6` | five 3-bit values, one of 16 patterns; the ShiningTile / `Panel4_ShiningPanel` puzzle (`0xc5c130`, `0xc58f50`) compares its tile index with the field at `index * 3` |
| `ExtraItems` `+0xac` | read and written as a u32 with `field_af` (`0x8d0ea8`) |
| `SleepyMushroomTime` `+0xb0` | `totalTimePlayed` when the Sleepy Toadstool was picked (`0x8d0ea8`); `0x8ca460` compares it plus 900 s |
| `WarpPoints` `+0xb8` | a bit per `WarpPoint.gsheet` `mIndex` (UI.md, Warp map) |
| `FishingLure` `+0xc1` | an event action sets its `type` − 1 (`0x993720`), `0xe1bbd0` sets 0, 1 or 2; the FishingLure actor (`0xb73330`) and the fishing play report's `RureType` (`0xdf42d0`) read it |
| `KeptItemKind` `+0xc3` | `KeptItem_Kind` (`0x9c5680`), the event actions TakeItem and ReturnItem; the table `0x16b3d70` maps it (`0x9c5d80`) |
| `FieldPosition` `+0xc4`, `IndoorPosition` `+0xd0` | `Inventory_StoreLinkPosition` (`0x8652b0`, from `LevelManager_LevelJump`) stores Link's position in the first when he leaves a level of type 3 and up (the overworld), else in the second (not with Player `field_1b38[1]`); the overworld and warp maps place Link with the first, the dungeon map reads the second (`0xe74870`) |
| `Figurines` `+0xe8` | bits; `CranePrize_Has` reads `+0xe9` |
| `ZoneId` `+0xec` | `Scene_UpdateZone` (`0x836fa8`) writes the entered zone's id; the warp UI's fallback (`0xf8ea50`) reads it |
| `RapidsBestTime` `+0xf0` | milliseconds, written at `0xdf045c`; a condition tests it for 0 (`0xc689e0`), a message formats it (`0xc69120`), `0xdf2360` compares it × 1000 with a microsecond timer. Guess: the rapids race |
| `FishSize`, `FishCaught` | per fish kind; the record message takes the largest size over all kinds but 5 (`0xc68cf0`), a condition sums the catches over all but 5 (`0xc68920`) |
| `RestartDirection` `+0x121` | bits 0–2: a direction passed with `RestartPos` into level jumps (`<< 29`, `0x83a470`) |
| `HintIndex` `+0x123` | `Hint_NextRow` (`0xc93310`) goes round a datasheet's eligible rows with it (default `Level2SwordComplete`); guess: the hints |
| `Obtained` `+0x124` | `Inventory_HasObtained` (`0xdf8740`); AddItemID sets the bits (Adding and owning) |
| `QueryTime` `+0x140` | an event query (`EventQuery_Timer140` `0x800c50`, parameters time, value, filename) holds while its time ≤ `QueryTime` / 1e6; its writer is not found |

No access found to `field_aa`, `field_af` (besides `ExtraItems`), `+0x134`..`+0x13f` and `+0x148`..`+0x15f`.
