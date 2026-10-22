# Game UI (game 1.0.0)

The game's UIs, pause menu pages and maps, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. TESTING.md, Reverse engineering, has the pause menu header and pages, the HUD pass and text boxes; this file does not repeat them.

## Framework

**LayoutWrapper** (`LayoutWrapper_vtable`, `0x1c27230`): every UI, pause page, map marker and layout part. The vtable starts at the object's `+0x0` (no RTTI); the game has its own type ids (statics) for `IsType`.

| Virtual | |
|---|---|
| `+0x0` / `+0x8` | `IsType(type id*)` / its type id |
| `+0x10` / `+0x18` | destructor / deleting destructor |
| `+0x20` / `+0x28` / `+0x30` | setup / update (each frame) / open |
| `+0x40` | `(out shared_ptr)` the layout, `{+0x18, +0x20}` |
| `+0x48` / `+0x50` | the layout (`+0x18`; its root pane at layout `+0x18`) / the root pane |
| `+0x58` | `(out {layout sp, count, animator}, wrapper, StringView* name)`: the animator of `<layout>_<name>.bflan` (`0xe59830`, ui2d `0x1158da0`) |
| `+0x60` | UI id, -1 for wrappers that are not UIs |

Wrapper fields: `+0x18`/`+0x20` the layout and its count; `+0x30`/`+0x38` the parent's layout (weak) once attached; `+0x40`/`+0x48` the parent's layout (shared); `+0x50` the pane it hangs under. Parts and map markers override `+0x40`..`+0x58` (`0xe5ae10`..`0xe5b020`): a part without a layout of its own (`+0x18` null) answers from the pane it wraps (`+0x88`).

| Function | |
|---|---|
| `0xe7fe70` `LayoutWrapper_FindPane` | `(out {layout sp, count, pane}, wrapper, const char* name, length)`: a pane by name, searched recursively; `0xe68e20` the same for a text box |
| `0xe59d40` `LayoutWrapper_AttachTo` | `(child, parent wrapper, pane)`: the child's root becomes the pane's last child, and leaves the HUD pass |
| `0xe5a0a0` `LayoutWrapper_AttachBefore` | `(child, parent wrapper, pane, sibling)`: as AttachTo, inserted before the sibling (ui2d `InsertChild` `0x1166c10`; `AppendChild` `0x1166bd0`, `RemoveChild` `0x1166c40`) |
| `0xf343a0` `Part_Bind` | `(part, page, StringView* name)`: a wrapper over an `L_` part pane of the page's layout |

**Animators** (the `+0x58` result's third field): virtual `+0x20` enable `(bool)`, `+0xc0` play `(f32 speed)`, `+0xc8` play `(f32 speed, ?)` with a negative speed backwards, `+0xd0` set the frame, `+0xe0`/`+0xe8` to the start / end (guess); the frame is the float at `+0x20`, `+0x3c` the rate (0 once stopped, guess); the frame count `0x114c840 (animator)`. Setting the frame shows that frame at the next layout update; the map markers pick their texture this way (`SetMark`).

**UI manager** (the static at `0x1c971c0`, `UiManager_ptr`): 44 factory slots (`+0x130 + id*0x30`, ids 0 to 0x2b), the UI list `+0x950`, requests `+0x968`, removals `+0x998`. `UiManager_Find` has typed copies per UI class that return a lock handle `{vtable, mutex, locked, ?, UI, count}`: `0xe85520` Gm_Navi (5), `0xe79560` Gm_MapCursor (0x1c), `0xe789b0` Dg_DgnPlayer (0x1b).

**State machines.** UIs, pages, markers and parts run a machine object; the code is a template per number of states N, so each class has its own `ChangeState` and update. Machine fields: `+0x8` u32 frames in the state, `+0xc` u8, `+0xd` u8 state (0xff none), `+0xe` previous state, `+0x30` listener (its virtual `+0x30` is told each change), `+0x40` owner; from `+0x48` per state 0x30 bytes: enter, update, exit as Itanium member pointers `{function, adjustment}`; then a bitset (a set bit: that state's entry is a `std::function` instead), then N name pointers. A setup fills the table, so the state names are in the setup's code. `ChangeState` runs the old state's exit and the new state's enter.

**UI ids** (vtable `+0x60`; setup, update and open are vtable `+0x20`..`+0x30`, named in `las.sym` as `<Name>_Setup` etc.):

| Id | Layout | Class | Id | Layout | Class |
|---|---|---|---|---|---|
| 0 | `Cm_HUD` | `CmHud` | 0x14 | `Gm_CraneGame` | `CraneGame` |
| 1 | `Pd_Puzzle` | Chamber Dungeon | 0x15 | `Gm_RaftGame` | `RaftGame` |
| 2 | `Pd_Panel` | Chamber Dungeon | 0x1a | `Gm_Memory` | `Memory` |
| 3 | `Pd_PanelResult` | Chamber Dungeon | 0x1b | `Dg_DgnPlayer` | Link's marker on the maps |
| 4 | `Pd_Dlg` | Chamber Dungeon dialog | 0x1c | `Gm_MapCursor` | `MapCursor` |
| 5 | `Gm_Navi` | pause header (`Navi`) | 0x1d | `Tl_Title` | `Title` |
| 6 | `Gm_Item` | Items page (`GmItem`) | 0x1e | `Ed_Ending` | `Ending` |
| 7 | `Gm_Map` | warp map (`WarpMap`) | 0x1f, 0x20 | `Cm_FileOperate` | file screens |
| 8 | `Gm_Navi` | warp map header (`WarpNavi`) | 0x21 | `Gm_Event` | `EventUi` |
| 9 | `Gm_Map` (hidden) | warp outside the field (`WarpDecision`) | 0x22 | `Ld_Fade` | `Fade` |
| 0xa | `Gm_System` | System page (`GmSystem`) | 0x23 | `Ed_TheEnd` | `TheEnd` |
| 0xb | | Chamber Dungeon's third page | 0x24 | `Cm_Message` | message window |
| 0xc | `Cm_FileOperate` | file menu | 0x25 | `Cm_Dlg` | warp question (`WarpSet`) |
| 0xd | `Tl_Entry` | name entry | 0x26 | `Cm_Dlg` | dialogs |
| 0xe | `Dg_DgnMap` | dungeon map page | 0x27, 0x28 | `Pd_Dlg` | Chamber Dungeon dialogs |
| 0xf | `Gm_Map` | overworld map page | 0x29, 0x2a | `Ld_Throbber`, `Ld_ThrobberSave` | loading |
| 0x11 | `Gm_GameOver` | `GameOver` | 0x2b | `Ld_Spicy` | `Spicy` |

`Gm_Picture` (`Picture`) reads its id from `+0x838`. No class answers 0x10, 0x12, 0x13 or 0x16 to 0x19 through a plain getter.

**Layouts.** `scripts/re/bflyt_tree.py <layout>` prints a layout's pane tree from the RomFS dump (`--list` lists the archive's layouts and animations). Which function builds which layout: `scan.py xstr '\.bflyt$'`.

## Input

The game's controller is `*(Framework+0x518)` (a shared pointer, count `+0x520`): `+0x10` the pad (virtual `+0x10` returns `{u32 held, u32 held last frame}`, so `held & ~last` is a press), `+0x228` the left stick (virtual `+0x18` returns `{f32 x, f32 y}`), `+0x234` u8 locked (no input). Button bits are the physical buttons of `HidButtonTable.gsheet` (`mButtonType`): 0 A, 1 B, 2 X, 3 Y, 4 R, 5 L, 10 Plus, 11 Minus, 14 to 17 up, down, right, left. The maps read A (pin), X (memories) and Y held (place names / dungeon info) this way. Per-function flags: `+0xb1` and `+0xd1` zoom out / in, `+0x171` and `+0x181` floor up / down (the maps' zoom and the dungeon floors).

## Datasheets

| Function | |
|---|---|
| `0x83a1e0` `Datasheet_Load` | `(out shared_ptr, resource manager, StringView* path)` |
| `0xfa6130` `Datasheet_RowById` | `(sheet, s32 id, row size)`: the row whose `mId` is id (a hash map); rows at `*(sheet+0x70)+0x20`, count `+0x28` |
| `0xf9d320` `Datasheet_RowAt` | `(out {sheet sp, count, row}, sheet, index)` |
| `0xf9d260` `Datasheet_FindRowShared` | `(out {sheet sp, count, row}, sheet, StringView* key)`: by the key column (`Datasheet_FindRow`) |
| `0xf9de10` `Conditions_Check` | `(conditions, u32 type, StringView* arg, actor)`: type 0 true, 1 event flag (`!` negates), 2 `name:value` counter, 3 companion (`None`, `BowWow`, `Marin`, `Ghost`, `Rooster`), 4 to 7 actor parameters; the sheets' condition arrays are `{u32 type, string}` elements of 0x18 bytes |

`UiDatasheets_Load` (`0x867f34`) loads the UI sheets into holders of shared pointers:

| Holder | Sheets |
|---|---|
| `0x1cc3a18` | `UiFieldMapIcons` `+0x0` (rows 0x50: `mNameLabel` `+0x8`, `mType` `+0x18`, show flag `+0x20`, x/y/z `+0x30`), `UiFieldMapLocationName` `+0x10` (0x28: label `+0x8`, flag `+0x18`), `UiFieldMapMask` `+0x20` (0x50: area `+0x4`, conditions A `+0x8` and B `+0x18`, `UI_FieldMapTraverse` flag `+0x28`, `UI_FieldMapOpen` flag `+0x38`), `UiDungeonArea` `+0x30` (0x20, by area name: `mFloor` `+0x10`, `mAnimFloorIndex` `+0x14`, `mIsBoss` `+0x18`), `UiDungeonIcon` `+0x40`, `UiDungeonStair` `+0x50` |
| `0x1cc3ad0` | `UiMemory` `+0x0` (0x78), `UiMemoryHearts` `+0x10` (0x10: x/y/z `+0x4`), `UiMemoryShells` `+0x20` |
| `0x1cc3960` | `UiItemMenuCursorMoveTable` |

## Text

`MessageManager_Find` (`0x903250`, manager `*0x1cb2db8`): `(out {msbt sp, count, s32 label index}, manager, StringView* "File:Label")` finds a label in a loaded `<File>.msbt` (index -1 when missing). `String_Format` (`0x8ed5c0`) formats `{0}`. The overworld map names an icon by `Place:<mNameLabel>`; `MessageText_SetLabel` sets a text box from a label.

## Items page (UI 6, `Gm_Item`)

Setup `0xeb52b0`, update `0xeb5f40`, open `0xeb5ff0`, close (virtual `+0xa8`) `0xeb6070`. Machine at `+0xf0` (change `0xeb60b0`): Deactive, In, Loop (`0xeb7cd0`), Out, and the song pop-up's OcarinaPopIn, PopLoop, PopDecide, PopOut. `+0xe0` allows B, L and R (cleared while the pop-up is open).

| Field | |
|---|---|
| `+0x310` / `+0x328` | `T_Name_00` / `T_Info_00` |
| `+0x358 + i*0x280` | `L_List_00`..`11`, the 4 × 3 item grid; slot to `MainItems` bit at `0x17026a4` (powder, feather, bombs, shovel / ocarina, hookshot, bow, rod / boomerang, three bottles). Slot: item `+0x240` (0xff empty), icon frame `+0x244`, icon animator `+0x1c0`, count part `+0x120`, Y/X/no badge `+0xa0`/`+0xb8`/`+0xd0` |
| `+0x2158 + i*0xf0` | `L_Inst_00`..`07`, the instruments |
| `+0x28d8 + i*0xf0` | `L_Key_00`..`04`: Tail, Slime, Angler, Face, Bird keys |
| `+0x2d88 + i*0xf8` | `L_ListEq_00`..`05`: sword, shield, tunic, bracelet, boots, flippers |
| `+0x3358`, `+0x3478` | `L_Key_06` seashells, `L_Key_05` chamber stones (counts in `T_ItemNum_00`/`01`) |
| `+0x3598`, `+0x3688`, `+0x3780`, `+0x3870`, `+0x3918` | Sleepy Mushroom, Secret Medicine, seashell sensor, trade item (frame item - 1, 20 empty), figurine |
| `+0x39c0` | `L_PopList_00`, the ocarina's song pop-up |
| `+0x3e70` / `+0x3f60` | `L_Cursor_00` / `L_StHeartContainer_00` |
| `+0x4020` | cursor index (0 to 38) |

Cursor indexes (`0xeb6230` MoveCursor): 0–11 the grid, 12 seashells, 13 sensor, 14 chamber stones, 15 mushroom, 16 figurine, 17–24 instruments, 25–29 keys, 30–35 equipment, 36 trade item, 37 medicine, 38 heart container; item id per index at `0x17026b0`. Loop moves the cursor by the `UiItemMenuCursorMoveTable` row of the index (`+4` up, `+8` down, `+0xc` right, `+0x10` left; 39 no move). `0xeb6590` ShowItemInfo sets `UI:ItemName_<key>` and `UI:ItemExplanation_<key>` (key from `0xeb6e30`); `0xeb6b30` Refresh redraws the grid from the inventory. Y or X on the grid: `0xebaf90 (page, cursor, 0 Y / 1 X)` writes `Inventory+0xa2`/`+0xa3` (swapping with the other button) and updates the HUD's slot. On the ocarina the song pop-up opens instead (`0xf77540`); its decide (`0xf77a80`) writes `Inventory+0xa1`. A on the sensor toggles `*0x1cc1370 + 0x763`. In the Chamber Dungeon, items are greyed out through the PanelDungeon manager (`*0x1cbdfb8`).

## Memories (UI 0x1a, `Gm_Memory`)

A page the overworld map requests in MemoryStart; its creation callback (`0xe9df20`) keeps it at map `+0x3d30`, fills its list (`0xf4e500`) from the unlocked `UiMemory` rows at map `+0x3d40` and copies the map's button guide. Open `0xee13b0` (restores the tab and list state from Navi), close `0xee17a0` (saves them), machine at `+0xf0` (change `0xee1b00`): Wait, ChangeTab, ShowMessage, Close. Tabs at `+0x8e90 + i*0x260` (4, or 3 without Chamber Dungeon panels): `L_Content_00` the memories list (`+0x2a0`), `L_Content_01`/`02` collection counters, `L_Content_03` the panels. A on a memory shows its `mMessageLabel_n` lines with `mTalkerLabel` in the message window under `N_Message_00`. The map's MemoryUpdate (`0xe9b0b0`) ends it on B or X while `+0xe0` is set.

## HUD (UI 0, `Cm_HUD`)

Constructor `0xea35c0`, setup `0xea6990`, update `0xea7990`, open `0xea84f0` (builds every heart and counter from the inventory, then state Wait). Machine at `+0x80` (change `0x7f3380`): Wait, Open, InGame (`0xeaa800` `Hud_Update`), CraneIn, CraneOut, WaitEndCrane, FishingStart, FishingUpdate, FishingEnd, Close.

| Field | |
|---|---|
| `+0x340`.. | animations (handles): In `+0x340`, Out `+0x358`, CraneIn `+0x370`, CraneOut `+0x388`, Set `+0x3a0` (its animator `+0x3b0`: the pause menu header sets its frame to the page index), FishingIn `+0x3b8`, FishingOut `+0x3d0`, ItemIn `+0x3e8`, ItemOut `+0x400`, KeyUpDown `+0x418` (moves the keys below a second heart row when max health > 40) |
| `+0x430 + i*0x300` | `L_Life_<i>` (20 hearts, `Cm_HeartGauge`, two rows of 10) |
| `+0x4030` / `+0x4258` | `L_SetItem_00` (Y, `Inventory+0xa2`) / `L_SetItem_01` (X, `+0xa3`): cached item `+0x210`, icon frame `+0x214` |
| `+0x4480` | `L_Rupee_00` (machine at part `+0x90`, shown value part `+0x2ec`) |
| `+0x4790` | `L_Interact_00` (the A prompt: Talk, Open, Push, Hold, Lift, Examine, Take, Play; state `+0x4898`) |
| `+0x48b0` / `+0x4b00` | `L_ShellRadar_00` / `L_Compass_00` |
| `+0x4d68 + i*0xc0` | `L_Leaf_<i>` (5 golden leaves, in `A_Leaf_00`) |
| `+0x5140 + i*0xc0` | `L_Key_<i>` (21 small keys, in `A_Key_00`); `+0x6100` `L_BossKey_00`. Key, leaf and boss key parts: In animator part `+0xa0`, Out `+0xb8` |
| `+0x61e0` / `+0x62a8` / `+0x6378` | `L_Location_00` / `L_DgnLocation_00` / `L_TimeCount_00` |
| `+0x61c0` / `+0x61c2` | drawn health / drawn max (quarter hearts) |
| `+0x61c4` / `+0x61c5` | hearts update during events / hearts frozen |
| `+0x61cf` / `+0x61d0` / `+0x61d1` | leaves / keys / boss key shown |
| `+0x61dc` | the page index the pause menu opens on |

**Hearts.** A heart part has 17 animation handles at `+0x160 + k*0x18`: the fills between quarters (k 0 to 7, guess), Life4 8, Life0 9, In 10, Out 11, Normal 12 (guess), Caution 13, RevivalOn 14, RevivalOff 15, LifeUp 16; `+0x2f8` its fill animation's index. `0xf40e90 HudHeart_SetFill (heart, quarter, bool up)`. InGame moves the drawn health one quarter toward the health each time the current heart's fill animation stops, not during events (unless health is 0 or `+0x61c4`). With the drawn health at the drawn max and health above it (`0xeabae0`), it reveals one heart (In, Life4, LifeUp), jumps the drawn health to the health and the drawn max to the real max (`0xdf8260`); no other code raises the drawn max until the next open, so two containers at once, or health not above the old max, leave hearts in Out.

**Counters.** Keys: `DungeonItems[level]` byte 0 (up to 21), boss key byte 1 bit 3, hidden in the overworld; the boss key goes once the level's boss door switch is set (`0xf886c0`). Golden leaves (overworld, until `GoldenLeafComplete`): the bits of `Inventory+0xc2`; a drop plays Out on the key parts, not the leaf parts (a bug in the game). Item slots: Bomb, Bow and Powder show their counts (`0xf459c0`), the ocarina its song, bottles a fairy.

## Map cursor (UI 0x1c, `Gm_MapCursor`)

One UI drawn over both map pages; each page's open requests it (`UiManager_Request(0x1c)`), and pages find it with `0xe79560`. Fields: `+0x80` the name bubble's machine (Deactive, On, Loop, Off, HideName; change `0xea0750`, run by its update `0xea0690`); animations OnName `+0x238`, OffName `+0x250`, In `+0x268`, Out `+0x280`, On `+0x1f0`, Loop `+0x208`, Off `+0x220` (animator at `+0x10` of each); `T_MarkerName_00` text box `+0x2a8`; `W_window_00` `+0x2c0`; `W_Cursor_00` `+0x2f8`, its size copied to `+0x300`/`+0x304` (the hover radius and the cursor's size for clamping).

- `0xea0f50` `MapCursor_ShowName (cursor, MessageRef*)`: shows the name with the message's text, clears `+0x2e0`, then `0xea1000`.
- `0xea1000` `MapCursor_NameOn (cursor)`: clears `+0x2e1`, bubble state On, `SE_SY_MAP_CURSOL_WIDE`.
- Setting `+0x2e0` and `+0x2e1` to 1 takes the name away (the maps do this when nothing named is hovered).

## Map markers

**Icon markers** (`Gm_MapMarker.bflyt`, `MapMarker_vtable` `0x1c2cfa8`, 0x250 bytes): made by `0xe9c870` `(out shared_ptr)` and set up as a part of a page by `0xf3ad80` (out of the HUD pass, then the setup `0xf3a490`). Fields: `+0x90` machine of 4 states (Deactive, On, Loop, Off; change `0xf3a8c0`), animations On `+0x1d0`, Loop `+0x1e8`, Off `+0x200`, SetMark `+0x218` (each `{layout sp, count, animator}`; SetMark's animator `+0x228`), the sheet `+0x230` and row `+0x240` (a `UiFieldMapIcons` row), `+0x248` set when the marker loses the hover (On/Loop then go to Off). The SetMark frame picks the marker's texture and size (`ap_map_icons.py` adds frames).

**Pins** (`Gm_MapMarkUnit.bflyt`, the player's map pins): made by `0xe828a0 (out, "Gm_MapMarkUnit.bflyt")`, set up by `0xf487c0`; type code at `+0x138`, SetMark animator `+0x118` (frame max(code % 10 - 1, 0)), hover animations On `+0xa0` (`0xf48f90`) and Off `+0xd0` (`0xf48fe0`). The pins' save data is behind `*0x1cc3228` (0x1f0 bytes, made on first use): six `std::function`s (objects at `+0xd0`, `+0x100`, `+0x130`, `+0x160`, `+0x190`, `+0x1c0`, each called through its `+0x20`): get a slot's position (`0xede050 (function, slot)`), get its code (0 empty), set its position (`0xede0b0`), set its code (`0xede110`), the number of pins placed (codes not 0) and the capacity. `0xedbfd0 MapPinStore_Bind (store)` binds them on each level load (`LevelLoad_Finish`) for the dungeon index (`*0x1cc13c8`, kept at store `+0x18`); the pins' save section is `*0x1cc1370` (store `+0x10` holds `0x1cc1370`):

| Dungeon index | Positions (vec2) | Codes (u8) | Capacity |
|---|---|---|---|
| 0xff (field) | section `+0x0 + slot * 8` | section `+0x690 + slot` | 30 |
| 0–7, 9 (9 uses set 8) | section `+0xf0 + (set * 20 + slot) * 8` | section `+0x6ae + set * 20 + slot` | 20 |
| 8 (Chamber Dungeon) and above 9 | store `+0x1c + slot * 8` (not saved; PanelEdit clears them) | store `+0xbc + slot` | 20 |

A pin's code is its type (1 and up); a dungeon pin's is floor × 10 + type, the floor counted from 1.

Placing a pin (both maps): A off a pin goes to PutMarkPin, which takes the first empty slot, makes the pin at the cursor (`0xf48ee0` places it) with the last type picked, and goes to StartEditMarkPin: the pin bar (`Gm_MapMarkList`, the page's `L_MapMarkList_00`) opens at the cursor; in EditMarkPin L/R pick the type (0 removes), A confirms, B cancels. EndEditMarkPin's enter saves the slot (the hovered index) through the store: position, then code, or code 0 and the pin dropped. With every slot used, the `*Empty` states run the same bar on a temporary pin and save nothing. The overworld stores the cursor position as is (`N_MapPin_00` units); the dungeon stores cursor − `N_MapPosition_00` − `N_PinOffset_00` (the pin's position under `N_PinOffset_00`).

## Overworld map (UI 0xf, `Gm_Map`)

Vtable `0x1c281b0`: setup `0xe8f9d0`, update `0xe93800`, open `0xe93fd0`, destructor `0xe8e680`. Machine at `+0xf0` (change `0xe941d0`): Deactive, Open, KeyWait (`0xe95090`), PutMarkPin, StartEditMarkPin, EditMarkPin, EndEditMarkPin, PutMarkPinEmpty, StartEditMarkPinEmpty, EditMarkPinEmpty, EndEditMarkPinEmpty, MemoryStart, MemoryUpdate, MemoryEnd, MemoryFromDungeon, Close, OpenMask.

| Field | |
|---|---|
| `+0x528`.. | animations (`{sp, count, animator}` handles; here and below the handle's start): MapNameIn `+0x528`, MapSymbolInOut `+0x558`, FldMaskOpen `+0x570`, MemoryIn `+0x588`, MemoryOut `+0x5a0`, StoryInOut `+0x5b8`, heartInOut `+0x5d0`, SellInOut `+0x5e8` |
| `+0x600` / `+0x618` | vectors of the heart piece and seashell markers (Memories) |
| `+0x640` | `N_MapCore_00` (the scrolled map); this and the other pane fields are the pane of a `{sp, count, pane}` handle |
| `+0x658` | `N_MapPin_00`: parent of the icon markers, the pins and Link's marker |
| `+0x670` | `B_Map_00`: world to layout (below) |
| `+0x6a0` / `+0x6b8` / `+0x700` | `B_PointerLimit_00` (cursor bounds), `B_ScrollLimit_00`, `B_MarkerLimit_00` |
| `+0x6d0` / `+0x6e8` | `N_heartMarker_00` / `N_SellMarker_00` |
| `+0x710` | vector of shared pointers: the icon markers (`+0x708` the sheet's row count) |
| `+0x730 + i*0x18` | `T_MapName_<i>` (place names, `Place:<label>` when their flag is set) |
| `+0xd30` | Y held: place names shown, no hover, no snapping |
| `+0xd40 + a*0x158`, `+0x3840 + a*0x18` | `L_FldArea_<a>` and `L_FldAreaMask_<a>` (32 areas; `0xf3b810` shows an area and its mask by its `UiFieldMapMask` row) |
| `+0x3b40` | 30 pins; hovered pin `+0x3d20`, its index `+0x8158` (previous `+0x815c`) |
| `+0x3d60 + i*0x230` | `L_Memory_<i>` (24) |
| `+0x71e0` / `+0x72b8` | `L_MapPlayer_00` (Link) / `L_MapMarkList_00` (the pin bar) |
| `+0x8150` / `+0x8154` | hovered icon marker's index into `+0x710` / previous (-1 none) |
| `+0x8164`..`+0x81a0` | zoom: levels `{1, 3}` at `+0x819c`, index `+0x8178`, current and target scale `+0x8168`/`+0x8164`, `+0x8170` scale of the cursor's speed and hover radius |
| `+0x8198` / `+0x81a8` | cursor speed / cursor position |
| `+0x81b0` / `+0x81b8` | Link's position / Link not in the overworld (his marker hidden) |

World to layout (`B_Map_00` pane P): x = floor(P.width / 240 × x + P.x), y = floor(P.y − 2.7 × ((z − 8.25) × 0.79 + (32.25 − y) × 1.1)).

**Setup**: place names; Link's marker; the pins (`AttachBefore(pin, page, N_MapPin_00, Link's root)`); the icon markers: per `UiFieldMapIcons` row by id, a marker with the row, SetMark frame `mType`, kept and pushed onto `+0x710` only when its show flag is set (the AP hook `0xe917c8` is after the push), attached like the pins; the field areas, which reveal an area once traversed (`EventFlags_Set` `0xdfa7b0` of its Open flag); the heart pieces (inventory `+0x90` bit i, frame 5) and seashells (`+0x88`, frame 6) markers, hidden until the Memories show them.

**KeyWait** (`0xe95090`) each frame:
1. `0xe9e7f0 (page, true)` moves the cursor: with the stick it speeds up (4.25 to 9); idle, it eases toward the hovered pin, else the hovered icon marker (cursor += 0.3 × (target − cursor)), not while Y is held; clamped to `B_PointerLimit_00`.
2. A (`0xe9e220`): on a pin `StartEditMarkPin`, else `PutMarkPin` (a beep while Y is held). The zoom buttons, else X: `MemoryStart`.
3. Pin hover: the nearest shown pin within `cursor+0x300 × 0.2 × +0x8170`.
4. Icon hover: the same over the shown markers in `+0x710`. On a change the old marker gets `+0x248`; the new one state On and its name: `MessageManager_Find("Place:" + row label)` and `MapCursor_ShowName`; nothing hovered takes the name away.
5. Y held (`+0xd30`) shows the place names and hides the cursor's name.

The icon markers, their hover, name and snapping exist only in this page's setup, KeyWait and cursor movement.

## Dungeon map (UI 0xe, `Dg_DgnMap`)

Vtable `0x1c27b18`: setup `0xe748a0`, update `0xe780d0` (runs the machine, places Link's marker UI 0x1b), open `0xe78100` (requests the cursor, and Dg_DgnPlayer in a side-view room), destructor `0xe73970`. Machine at `+0xf0` (change `0xe782e0`): Deactive, Open, KeyWait (`0xe7a0f0`), PutMarkPin, StartEditMarkPin, EditMarkPin, EndEditMarkPin, PutMarkPinEmpty, StartEditMarkPinEmpty, EditMarkPinEmpty, EndEditMarkPinEmpty, ChangeFloor, MemoryStart, MemoryUpdate, MemoryEnd, Close.

Pane tree (`bflyt_tree.py Dg_DgnMap`): `N_MapOrigin_00` (zoom and scroll) > `P_Grid_00` (the 8 × 8 cells `B_00`..`B_63`, 80 × 64 each, `B_<row*8+col>`) and `N_MapPosition_00` > `N_MapIcon_00`, `N_MapIcon_01`, `N_MapPin_00` > `N_PinOffset_00` (at y 256). The room icons hang in `N_MapPin_00` before `N_PinOffset_00`; the pins in `N_PinOffset_00`, so they draw over the icons.

| Field | |
|---|---|
| `+0x4d0` | `unordered_map<u32 room id, s32 cell>` |
| `+0x508 + i*0x18` | animations `Dgn_00`..`Dgn_16`: the floor art, one per `UiDungeonArea` `mAnimFloorIndex` (0 D1, 1 D2, 2/3 D3, 4 to 6 D4 to D6, 7 to 10 Eagle's Tower 1F to 4F, 11 D8, 12 the Egg, 13 the Color Dungeon, 0xf the collapsed tower's 4F) |
| `+0x6b8` / `+0x6d0` | DgnMapOn / DgnMapOff (Y held: the dungeon info) |
| `+0x758`.. | panes (the pane of each `{sp, count, pane}` handle): `N_MapOrigin_00` `+0x758`, `P_Grid_00` `+0x770`, `N_MapPosition_00` `+0x788`, `N_MapPin_00` `+0x7a0`, `N_PinOffset_00` `+0x7b8`, `B_PointerLimit_00` `+0x7d0`, `B_ScrollLimit_00` `+0x7e8`, `B_MarkerLimit_00` `+0x800`, `N_MapIcon_01` `+0x818`, `N_MapIcon_00` `+0x830`; text `T_DgnName_00` `+0x848`, `T_PanelDgnName_00` `+0x860`; cells `+0x878 + cell*0x18` |
| `+0xe68 + cell*0x10` | the cell's chip (`Dg_DgnChip` visited, `Dg_DgnChipMask` with the map only) |
| `+0x1268` | vector of shared pointers: the room icons |
| `+0x1280` | 20 pins; hovered pin `+0x13c0`, its index `+0x2ccc` (previous `+0x2cd0`) |
| `+0x13d0`, `+0x1460`, `+0x14f0` | `L_DgnItem_00`..`02`: map, compass, stone beak in the sidebar |
| `+0x1580 + i*0x168` | `L_MapChoice_00`..`03`: floor buttons |
| `+0x1b20` | the boss icon |
| `+0x1b30`, `+0x29c8`, `+0x2a98`, `+0x2b78` | `L_MapMarkList_00` (pin bar), `L_MapStart_00` (entrance), `L_MapPlayer_00` (Link), `L_TimeCount_00` |
| `+0x2b70` | Link's map position |
| `+0x2cc8` | the dungeon index (`*0x1cc13c8`; 8 the Chamber Dungeon, which has no sidebar items) |
| `+0x2cd8` | zoom scale (levels `{1, 2}` at `+0x2d0c`, index `+0x2cec`) |
| `+0x2d18` | cursor position |
| `+0x2d20` | Link in a side-view room |
| `+0x2d21`..`+0x2d32` | floors: count `+0x2d21`, top `+0x2d22`, shown `+0x2d23` (previous `+0x2d24`), floor art index `+0x2d28`, Link's `+0x2d30`, the boss's `+0x2d31`, the entrance's `+0x2d32` |
| `+0x2d40` | Y held: dungeon info, no hover |
| `+0x4f8` | the Memories UI (id 0x10, guess) |

The dungeon (`*(scene+0x618)+0x208`, scene from `SceneHolder_Lock`): rooms `+0x1f8` (shared pointers; room: box `+0x20`/`+0x30`, id `+0x60`, zone `+0x64`, its file's path `+0x68` as `level/Lv01TailCave/Lv01TailCave_03G.leb` with its length at `+0xb8`, row `+0xc2`, column `+0xc3`), zones `+0x210`, virtual `+0x20` the room at a position.

**Setup**: per room visited, or on the map or compass: its chip under `B_<cell>`, then `0xe80950` `DungeonMap_MakeRoomIcons (page, room, dungeon, visited, map, compass)`: the boss icon (`UiDungeonArea` `mIsBoss`, until beaten), an icon per room actor and per `UiDungeonIcon` row of the room from `0xe832c0` `DungeonMap_MakeActorIcon (out, page, actor {u16 id, f32 x, y, z, switches}, room, visited, map, compass)` by actor id: `ObjTreasureBox` a chest (`Dg_DgnTresure`), `ObjLockDoor`/`ObjBossDoor`/`ObjClosedDoor` and the one-way and Panel doors, `ObjDungeonCrackedWall` and Panel cracked walls (`Dg_DgnBombWall`), `AreaUpstairs`/`AreaDownstairs` and Panel stairs, `ObjDungeonWarpHole`, `PanelJointOpeningStraight`; no icon for other actors. Each icon is placed by `0xe84b60` `DungeonMap_PositionIcon (x, y, z, page, out vec2, room sp*)` (world to map, `0xe827b0`: the cell's position plus the offset from the room's middle × 5.333; the result is the icon's translate, set through icon virtual `+0x80`), attached with `AttachBefore(icon, page, N_MapPin_00, N_PinOffset_00)` and pushed onto `+0x1268`. Then `0xe82250` `DungeonMap_ShowCellFloor (page, cell, room, area row)` per room (and on every floor change): the cell, its icons (icon vtable `+0x78`), Link's and the entrance markers and the pins (code / 10) are shown on their floor only. The floor art is the animation `Dgn_<mAnimFloorIndex>`.

**Room icons** (`DungeonMapIcon_vtable` `0x1c2cb38`, 0xd0 bytes, built by `0xf37fb0 (icon, layout name)`, initialised by `0xf36e60 (icon, actor, visited)`): open/closed animators `+0xa0`/`+0xb8`, `+0xc0` room id, `+0xc4` shown, `+0xc5` map, `+0xc6` compass; `0xf38040` (vtable `+0x70`, `DungeonMapIcon_Update (icon, actor)`) sets `+0xc4` and the icon's visibility when the compass is owned (`+0xc6`) or the actor's shown switch is set (with `+0xcc`), open by its open switch; `0xf380f0` (vtable `+0x78`, `(icon, bool on the floor)`) shows it while both `+0xc4` and the bool are set.

`N_MapPosition_00` moves after the setup (y 48 in Tail Cave, at runtime); `N_PinOffset_00` is at 0 then, not the file's 256. The cursor (`+0x2d18`) is in `N_MapOrigin_00`'s units: a room icon at translate t under `N_MapPin_00` is at t + `N_MapPin_00` + `N_MapPosition_00` there.

**KeyWait** (`0xe7a0f0`) each frame: `0xe86750 (page, true)` moves the cursor (eases toward the hovered pin only; `+0x2d08` the speed, 4.25 while the stick is idle); A (`0xe86c10`, `bool ()`: the rising edge of A, the page not passed): on a pin (`0xe871e0`) StartEditMarkPin, else PutMarkPin; zoom (`0xe874a0`); floor buttons (`0xe87670`) to ChangeFloor (`0xe7d8a0`: the floor buttons, `DungeonMap_ShowCellFloor` per room, the floor art animations); X (`0xe87840`) MemoryStart; then, whatever was pressed, `0xe87a20` the pin hover (nearest shown pin within `cursor+0x300 × 0.2 × page+0x2ce4`; the pins' positions compared with cursor − `N_MapPosition_00` − `N_PinOffset_00`); `0xe87d60` Y held.

The dungeon page has no icon markers, no hover over its room icons and no names on the cursor; it shares the cursor UI, the pins' classes and the marker functions with the overworld page.

## Warp map (UI 7, `Gm_Map`)

The map the warp holes and the ocarina's Manbo's Mambo open in the field: the event actor `WarpUI` (`Show` `0x7e8ec0`, `GetResult` `0x7eaf30`, `JumpToSelectedLocation` `0x7ea4c0`) requests UI `0xede170()`: 7 in the field, else 9 (`WarpDecision`, the same class with the map hidden, which asks through the `Cm_Dlg` dialog 0x25 and warps to the level's `OcarinaWarpPoint`). The page hangs in its own header, WarpNavi (UI 8, `Gm_Navi`, title `UI:WarpFieldTitle`), not in the pause menu. The warp itself is `GameControl_SetLevelJump(Field, row.mLocator)` in `JumpToSelectedLocation`.

Warp points: `WarpPoint.gsheet` (holder `*0x1cb24f8`, rows by `mIndex` via `0x825970`; row `mIndex` `+0x10`, `mLocator` `+0x18`, `mNameLabel` `+0x28`, x/y/z `+0x40`), unlocked by bit `mIndex` of `Inventory+0xb8` (`0xdf8670 (key)`, the event action `Link.AddWarpPoint`). Each is a marker of its own class (`Gm_MapWarpPoint.bflyt`, vtable `0x1c2fa70`; make `0xf9b100`, SetupPart `0xf79c10`, hover on/off `0xf7a0d0`/`0xf7a150`, row `+0x110`), placed with the overworld's world-to-layout formula and attached under `N_Warp_00` (in `N_MapCore_00`); vector `+0x5fd8` by `mIndex`, locked points hidden.

Machine at `+0xf0` (change `0xf98c00`): FieldIn, DungeonIn, Move, Dialog, ZoomIn, ZoomOut, Out. Move (`0xf99840`) each frame:
1. `0xf9b650 (page, true)` moves the cursor (`+0x5ff0`) as the overworld page does: with the stick it speeds up, idle it eases toward the hovered point (cursor += 0.3 × (target − cursor)); clamped to `B_PointerLimit_00`.
2. Without the stick, the d-pad (controller `+0x171`/`+0x181`/`+0x191`/`+0x1a1`: up, down, right, left) jumps the cursor onto the nearest shown point that way (`0xf9c970`).
3. `0xf9c370` hover: the first shown point within 48 (squared distance ≤ 2304, no zoom factor) into `+0x62e0`; hover on/off and `Place:<mNameLabel>` on the map cursor.
4. B cancels (Out with index -1); A on a hovered point zooms in on it (zoom `{1, 2}`, `0xf98ac0`) and asks `UI:WarpFieldDecision` / `UI:WarpFieldCancel` on `L_MapChoice_00` (Dialog). Out writes the index into the result (`+0x62e8`, index `+0x60`).

## Markers made by the module

A map marker works on any map page without a `UiFieldMapIcons` row: of the marker's fields only the page reads its row (`+0x240`); the marker's own code reads `+0x248` (hover lost). `MapMarker_Make` (`0xe9c870`), `MapMarker_SetupPart` (`0xf3ad80`) and `LayoutWrapper_AttachBefore` make one under a page's pane; its update (vtable `+0x28`) runs its machine (On goes to Loop once its animation ends) and `MapMarker_ChangeState` (`0xf3a8c0`, machine `+0x90`) plays On or Off; the SetMark animator (`+0x228`) frame picks its look. The map tracker's dungeon dots (DESIGN.md, Map tracker, Dungeon dots) are made this way.
