# Items and inventory (game 1.0.0)

What adding and owning an item does in the main module, and the Chamber Dungeon's stones and panels, from static analysis. Offsets are module offsets; `las.sym` names them at their runtime addresses (module offset + 0x8504000). Item ids are `Items.gsheet`'s `itemID`, as the module's `itemsDict` (`Hooks/Common/inventory.hpp`) lists them.

## Adding and owning

`Inventory_AddItemID(item, count, index)` (`0x8d08e0`, hooked by the module) and `Inventory_HasItem(item, count, index)` (`0x8d1a30`) switch on the item id, 0 to 0x7e. `count < 1` adds nothing and owns everything. An id from 0x80 throws (a bitset's out_of_range), so the module's hook passes on ids below 127 only.

After every add, also those that change nothing, AddItemID sets the item's bit in the obtained bits (inventory `+0x124`, a bit per item id), and for the Boomerang (7), Seashell (22), Piece of Heart (58), Bottle (113) and Chamber Stone (114) it sends a play report (`PlayReport_ItemCommon`: SaveId, TotalTimePlayed, Chapter, Warashibe, then ItemId and Index). The getting flags (`Item_CheckGettingFlag`, `Item_SetGettingFlag`, both hooked) are something else: the event flag the item's `Items.gsheet` row names (row `+0x70`).

Inventory fields by name are in the module's `Game/Data/inventory.hpp`. Not named there: `+0xb0` the play time (save header `+0x20`, TotalTimePlayed) when the Sleepy Toadstool was picked, `+0xc0` a fairy per bottle slot, `+0x124` the obtained bits.

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
