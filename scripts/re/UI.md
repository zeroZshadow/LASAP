# Game UI (game 1.0.0)

The game's UIs, pause menu pages and maps, from static analysis of the main module. Offsets are module offsets (what `InstallAtOffset` and `scan.py` take); `las.sym` names them at their runtime addresses (module offset + 0x8504000). Object offsets are bytes from the object's start. Field names are those of `game/ui.hpp`, `game/menu.hpp`, `game/map.hpp` and `game/state_machine.hpp`, which list every field; this file gives the evidence and what their comments leave out. TESTING.md, Reverse engineering, has the pause menu header and pages, the HUD pass and the text boxes' glyphs as the module uses them; this file does not repeat them.

## Framework

**LayoutWrapper** (`LayoutWrapper_vtable`, `0x1c27230`, 0x78 bytes; `LayoutWrapper_Build` `0xe59100`, destructor `0xe59470`): every UI, pause page, map marker and layout part. The vtable starts at the object's `+0x0` (no RTTI); the game has its own type ids (statics, the wrapper's `0x1cadf70`) for `IsType`. The build zeroes `+0x8`..`+0x6f` and sets `active` 1, `openPending` 0, `activeOnOpen` 1 and `archiveSet`.

| Virtual | |
|---|---|
| `+0x0` / `+0x8` | `IsType(type id*)` / its type id |
| `+0x10` / `+0x18` | destructor / deleting destructor |
| `+0x20` / `+0x28` / `+0x30` | setup / update (each frame while `active`) / open |
| `+0x38` | on close: empty in the base (`0xe597c0`); the UI manager calls it for the ids in its close list |
| `+0x40` | `(out shared_ptr)` the layout, `{+0x18, +0x20}` |
| `+0x48` / `+0x50` | the layout / the root pane (`0xe59810`: the layout's `root`) |
| `+0x58` | `LayoutWrapper_GetAnimation` (`0xe59830`): `(out Animation, wrapper, StringView* name)`, the animator of `<layout>_<name>.bflan` (`ui2d_Layout_CreateAnimator` `0x1158da0`) |
| `+0x60` | UI id, -1 for wrappers that are not UIs |

`active` (`+0x70`) is every wrapper's flag: the UI manager runs the update while it is set and closes the UI once it is
clear after an update. `openPending` opens a UI requested again at the next update (`active` from `activeOnOpen`, the
layout's `calculate` and `draw` on, virtual `+0x30`). `LayoutWrapper_AttachTo` keeps the parent's layout
(`parentLayout`, weak, from the parent's virtual `+0x40`, and `attachLayout`), the pane (`attachPane`) and the root's
own parent (`homePane`), and clears `homeKeep` and the layout's `calculate`; the detach (`0xe59ac0`) puts the root back
under `homePane` and sets `calculate` again. Parts and map markers override `+0x40`..`+0x58` (`0xe5ae10`..`0xe5b020`): a
part without a layout of its own (`layout` null) answers from the pane it wraps (`+0x88`). `owner` is the page a part is
bound to (`Part_Bind` writes it, `0xf343c4`); `Part_GetLayoutShared` (`0xe5ae10`, virtual `+0x40` of `Part` and every UI
class derived from it) answers from `owner`'s virtual `+0x40` when the wrapper's own layout is gone.

| Function | |
|---|---|
| `0xe7fe70` `LayoutWrapper_FindPane` | `(out {layout sp, count, pane}, wrapper, const char* name, length)`: a pane by name, searched recursively; `0xe68e20` the same for a text box |
| `0xe59d40` `LayoutWrapper_AttachTo` | `(child, parent wrapper, pane)`: the child's root becomes the pane's last child, and leaves the HUD pass |
| `0xe5a0a0` `LayoutWrapper_AttachBefore` | `(child, parent wrapper, pane, sibling)`: as AttachTo, inserted before the sibling (ui2d `InsertChild` `0x1166c10`; `AppendChild` `0x1166bd0`, `RemoveChild` `0x1166c40`) |
| `0xf343a0` `Part_Bind` | `(part, page, StringView* name)`: a wrapper over an `L_` part pane of the page's layout; `owner` the page |

**Parts** (`Part`, `Part_vtable` `0x1c27338`, 0x90 bytes): a wrapper and the pane handle of the `L_` part pane it is bound to (`Part_Bind`); their constructors write `+0x70`..`+0x73` as 1, 0, 1, 0. The guide's slots, the message window's parts, the System page's choices and the map pages' parts are parts.

**Layouts** (`Layout`: the game's layout, vtable `0x1c520c8`, 0x78 bytes, around `nn::ui2d::Layout`,
`ui2d_Layout_vtable` `0x1c844d8`, 0x60 bytes, `ui2d_Layout_Ctor` `0x1156c80`): `UiManager_BuildLayout` (`0xed9050`)
allocates one (aligned to 0x100), `Layout_BuildPartsLayout` (`0x10d7e00`, virtual `+0x90`) one per parts pane, and
`Layout_Dtor` (`0x10d7ed0`) releases `archive`. `Layouts_Update` (`0x10dce60`) animates and calculates the layouts whose
`calculate` is set, `HudPass_Draw` (`0x10d8e00`) draws those whose `draw` is set; a UI's open sets `calculate` while the
wrapper has no `attachPane`. `ui2d_Layout_SetTagProcessor` (`0x11597d0`, `+0x68`) sets every text box's `tagProcessor`;
`GameTextBox_Make` (`0x10d7cd0`, `+0x88`) builds the `txt1` panes. `field_58` is a heap table `{s32 count, s32 count2,
void** entries}` that `0x1158450` builds during a parts build from the build context's two lists (`+0x40`/`+0x48`,
`+0x58`/`+0x60`) and the ui2d Layout destructor frees (`0x1158540`, `0x1156d2c`; likely); what its entries are is
unknown.

**Panes** (`Pane`: `nn::ui2d::Pane`, `ui2d_Pane_vtable` `0x1c84e20`; 0xd8 bytes, its data ending at `+0xd2`): `ui2d_Pane_Calculate` (`0x1167140`, virtual `+0x98`) recomputes `globalAlpha` and `globalMatrix` of every visible pane each time its layout calculates and sets `kPaneCalculated` (cleared for an invisible pane) and `flagsEx` bit 4; `ui2d_Pane_Draw` (`0x1167e90`, `+0xa0`) draws a pane with `kPaneVisible`, `kPaneCalculated` and `flagsEx` bit 4. A `globalAlpha` written by hand thus holds for the current frame only, unless the layout's `calculate` is off. `ui2d_Pane_Finalize` (`0x1166890`, `+0x18`) deletes the children without `kPaneUserAllocated` and frees a heap copy of `extUserData` (`flagsEx` bit 2). `systemFlags` has a bit per system data entry type in `extUserData`; bit 17 drives a per-pane path in the calculation and the draw. Virtuals: `+0x60` FindPaneByName(name, recursive) (`0x1166e00`, comparing 24 characters), `+0x70` FindMaterialByName, `+0x80`..`+0x90` bind and unbind animations, `+0xa8` DrawSelf, `+0xb8` LoadMtx (`0x1168160`).

**Text boxes** (`TextBox`: the game's GameTextBox, `GameTextBox_vtable` `0x1c52940`, 0x1a0 bytes, around
`nn::ui2d::TextBox`, `ui2d_TextBox_vtable` `0x1c85270`, 0x158 bytes, `ui2d_TextBox_Ctor` `0x116f9a0`).
`GameTextBox_Make` sets `onSetString` to a default functor (`0x1c52a98`) and `notDrawn`. The game's four setters
(`GameTextBox_SetString16` `0x10de0b0`, `GameTextBox_SetString8` `0x10de1c0`, `GameTextBox_SetString16Len` `0x10de2d0`,
`GameTextBox_SetString8Len` `0x10de3f0`; virtuals `+0x100`..`+0x118`) call `onSetString` before the ui2d setter.
`field_191` is zeroed by `GameTextBox_Make` (a u16 write with `glyphsReady`) and set to 1 before a game string is set,
by the game's string setters (`0x10de6d8`, `0x10de79c`; `TextBox_SetString` `0x10de620`) and many UI sites
(`MessageText_SetLabel` among them); nothing in GameTextBox, ui2d or the UI code reads it. `bits`: 0–1 line alignment, 2
text dirty (set by the setters, by colour changes and by `UiManager_Update` on queued boxes once their glyphs are drawn;
cleared by `ui2d_TextBox_Calculate`), 3 shadow, 6 width limit (always set by the constructor), 7 per-character
transform, 8 round half extents, 12 vertical centre; 4, 5, 9–11 and 13–14 come from the resource. `tagProcessor` is
null, or set by the layout for a text box whose resource asks for extended tags (its flag bit 7). Virtuals beyond the
pane's: `+0xe0` InitString, `+0xe8`/`+0xf0` AllocateStringBuffer, `+0xf8` FreeStringBuffer, `+0x100`..`+0x118` the
setters, `+0x120` `ui2d_TextBox_GetTextDrawRect` (`0x1171270`, the rectangle through `x8`), `+0x128`/`+0x130` the text
writer's setup. TESTING.md, Text box glyphs, has the glyph queue.

**Tag processor** (`TagProcessor`, `TagProcessor_Ctor` `0x9038e0`, vtable `0x1bda908`, 0x118 bytes; its base nn::font
TagProcessorBase, whose constructor `0x111f1c0` sets only the vtable): draws the button tags of the text boxes that
point at it. It is embedded in the pause guide (`NaviGuide::tags`), the message window (`tagProcessor`), the Items page
(`+0x4030`) and the dungeon map (`tagProcessor`, for `T_PanelDgnName_00`), whose setups point their text boxes at it. A
text box of a layout built with `LayoutWrapper_Build` has none. `rubyFont` (`ui/jpn_ruby.bffnt`, loaded only when the
message manager's `+0x20` is 1, Japanese) and `pictogramFont` (`ui/pictogram.bffnt`) are `FontHandle`s, `{resource,
nn::font::Font*}` as `UiManager_LoadFont` (`0xeda2a0`) returns them. `color` is the message colour 0 (`0x10af170(msbp
+0x80, 0)` in the constructor; 0xff000000 before that). `colorIndex` (-1 at construction) picks the text's top and
bottom colours in `TagProcessor_ApplyColor` (`0x904860`, virtual `+0x30`): -1 keeps the writer's colour, 0 is `color`, n
the message colour n (`0x10af170(msbp +0x80, n)`). `scale` is set by `0x904a00` from its argument's `+0xc`/`+0x10`.
`pad_40` is zeroed by the constructor; none of the processor's functions (`0x903e00`..`0x904a00`) reads it.

**Animators** (`Animator`: `nn::ui2d::GroupArrayAnimator`, `ui2d_GroupArrayAnimator_vtable` `0x1c84a18`; `ui2d_Layout_CreateAnimator` makes 0x58 bytes and a pointer per group): `ui2d_Animator_UpdateFrame` (`0x115da80`, virtual `+0x18`) advances `frame` by `speed` × the step as `playType` says and sets `playState`; the layout's update applies the frame, so a frame set shows at the next layout update (the map markers pick their texture this way, `SetMark`). Virtuals: `+0x20` enable (`ui2d_AnimTransform_SetEnabled`), `+0xb8` `ui2d_Animator_Play(speed, playType)` from the start (from the end for a negative speed), `+0xc0` PlayAuto (the play type from the data), `+0xc8` PlayFromCurrent, `+0xd0` StopAt(frame), `+0xd8` Stop, `+0xe0` / `+0xe8` StopAtStart / StopAtEnd. `Animator_FrameCount` (`0x114c840`) reads the resource's u16 `+0x8`.

**UI manager** (`Manager`, the static `UiManager` at `0x1c971c0`, 0xab80 bytes, 16-aligned, the next static at
`0x1ca1d40`; `UiManager_Ctor` `0xecc610`, run by `LayoutWrapper_Build` on first use; destructor `0xec03e0`): `factories`
are 44 `std::function`s at `+0x110 + id*0x30` (ids 0 to 0x2b; each one's call target at `+0x130 + id*0x30`); `uis` the
UI list (nodes `{prev, next, UI, count}`); `requests` (`UiManager_Request` `0xed9b30`; nodes with the request and the
s32 id at `+0x20`); `closes` the ids `UiManager_Close` (`0xecff70`) queues, whose layouts the next update stops before
it calls the UI's virtual `+0x38`; `removals` (`UiManager_Remove` `0xeda0e0`). `UiManager_Update` (`0xece160`) makes a
requested UI that is missing with its factory, sets it up (virtual `+0x20`) and lists it. `archives`: per archive set s,
slots s, s + 2 and s + 4 are searched for a layout: 0 Game.arc (its resource accessor at the archive's `+0x80`), 1
StartUp.arc, 2 GameRegion.arc, 3 StartUpRegion.arc, 4 GameRegionCommon.arc, 5 StartUpRegionCommon.arc; 6 is DgnTex.arc
(the dungeon map's icons read it, `0xf386ac`) and 7 PanelTex.arc. `UiManager_LoadGameArchives` (`0xed5450`) loads the
`ui/Game.arc` set into the resource group `gameArchivesLoad` (`0xed5770`; FRAMEWORK.md, Resource groups), whose
completion lambda (`0xed5890`, which needs `owned_0` set) fills slots 0, 2, 4, 6 and 7; `startUpArchivesLoad` is the
StartUp set's group (lambda `0xed2c70`). The glyph queue: TESTING.md, Text box glyphs. `font` is an nn::font
ScalableFont (`+0x10` its TextureCache, `glyphCache`, set by its Initialize `0x111ded0` from `UiManager_InitFonts`
`0xecb470`; `+0x18` the font size, 0x28 at construction; `+0x32` the face; the font's RegisterGlyphs `0x11211b0` takes
these; likely); `glyphCache` is the nn::font TextureCache that holds the glyphs (Initialize `0x10de850`; likely).
`glyphResetRequest` (its writer is not found): 1 unlocks every lock group, 2 clears the cache; `UiManager_ResetGlyphs`
(`0xed10d0`) applies it once the worker is idle, then registers the common characters again and requeues every UI's
text. `glyphWorker` is a one-thread `ThreadPool` (FRAMEWORK.md, Scheduler and phases; its thread factory `0xecd020`).
`graphicsCallbackId` (-1 none) is the graphics callback `UiManager_UnregisterCallback` (`0xecc4f0`) unregisters when it
is 1 or more (`Graphics_UnregisterCallback`). `lockGroups` are 32 `LockGroup`s (`id` and `lockGroup` -1 at construction);
`UiManager_UpdateGlyphs` (`0xed0130`) passes a group's `pending` characters and `lockGroup` to RegisterGlyphs.
`owned_0`, `sp_10`, `vector_70` (released by the destructor) and `field_ab70` (0xff, not read) have no known role.
`UiManager_Find` has typed copies per UI class that return a lock handle `{vtable, mutex, locked, ?, UI, count}`:
`0xe85520` Gm_Navi (5), `0xe79560` Gm_MapCursor (0x1c), `0xe789b0` Dg_DgnPlayer (0x1b).

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
| 0xf | `Gm_Map` | overworld map page | 0x29, 0x2a | `Ld_Throbber`, `Ld_ThrobberSave` | `Throbber`, `ThrobberSave` (the autosave popup) |
| 0x11 | `Gm_GameOver` | `GameOver` | 0x2b | `Ld_Spicy` | `Spicy` |

`Gm_Picture` (`Picture`) reads its id from `+0x838`. No class answers 0x10, 0x12, 0x13 or 0x16 to 0x19 through a plain getter.

**Layout files.** `scripts/re/bflyt_tree.py <layout>` prints a layout's pane tree from the RomFS dump (`--list` lists the archive's layouts and animations). Which function builds which layout: `scan.py xstr '\.bflyt$'`.

## State machines

UIs, pages, markers, parts and some actors (Link, the fishing rod, the pond fish) run a state machine object, `StateMachine<N>` of N states (`game/state_machine.hpp`). The base vtable `StateMachine_vtable` (`0x1b52ef0`) has `+0x0` the destructor (`StateMachine_Dtor` `0x4f9a0`, which destroys `onChange`), `+0x10` the state count and `+0x18` a state's name, both pure; each machine class implements them. The code is a template per N, so every machine class has its own `ChangeState` and update; `scan.py sms` lists them.

| Offset | |
|---|---|
| `+0x0` | vtable |
| `+0x8` | `framesInState`: cleared by a change, counted by the update |
| `+0xc` | `field_c`: cleared by a change (the constructors write u16 0xff00 at `+0xc`: no state) |
| `+0xd` / `+0xe` | `state` (0xff none) / `previous` |
| `+0x10` | `onChange`: a libc++ `std::function` (its call target at `+0x30`), told `(state, name)` after each change |
| `+0x40` | `owner`: `this` for the member pointers |
| `+0x48` | `states`: per state 0x30 bytes, enter, update and exit, each an Itanium member pointer `{function, adjustment}` (on ARM the virtual bit is in the adjustment); with the state's bit set, the entry's first word is a state object instead (its vtable `+0x10` enter, `+0x20` exit, called with the owner) |
| `+0x48 + N*0x30` | the bit set object: its vtable (one per N), then `objectStates`, a bit per state (u8 up to 8 states, u16 up to 16, u64 up to 64) |
| `+0x58 + N*0x30` | `names`: N name pointers |

A machine is 0x58 + N × 0x38 bytes, rounded up to 16. `ChangeState` runs the old state's exit with the new state, sets `state` and `previous` and clears `framesInState` and `field_c`, runs the new state's enter with the old state, then calls `onChange`. A setup fills the table, so the state names are in the setup's code.

| Owner | N | Machine at | ChangeState |
|---|---|---|---|
| Navi (UI 5) | 6 | `+0x80` | `Navi_ChangeState` `0xee7dc0` |
| Title (UI 0x1d) | 6 | `+0x80` | `Title_ChangeState` `0x87f1b0`: Deactive (`Title_Setup`; the intro plays), LogoIn, BtnIn, WaitLR, GameStart, End |
| ThrobberSave (UI 0x2a) | 6 | `+0x80` | `ThrobberSave_ChangeState` `0xf85d20` |
| Throbber (UI 0x29) | 4 | `+0x80` | `Throbber_ChangeState` `0xf85230` |
| GmSystem (UI 10) | 6 | `+0xf0` | |
| MessageWindow (UI 0x24) | 15 | `+0x1260` | `MessageWindow_ChangeState` `0xef0f40` |
| MapMarker | 4 | `+0x90` | `MapMarker_ChangeState` `0xf3a8c0` |
| MapCursor (UI 0x1c) | 5 | `+0x80` | `MapCursor_ChangeState` `0xea0750` |
| DungeonMap (UI 0xe) | 16 | `+0xf0` | `0xe782e0` |
| OverworldMap (UI 0xf) | 17 | `+0xf0` | `0xe941d0` |
| Link (`Player`) | 64 | `+0x4e0` | `PlayerSM_ChangeState` `0xd34660` (PLAYER.md) |

A machine 16-aligned after an 8-byte-aligned field leaves 8 bytes of padding before it (Navi, ThrobberSave and MapCursor at `+0x78`), and one whose size is rounded up leaves its tail unused (MapMarker's `+0x1c8`).

## Input

The game's controller is `*(Framework+0x518)` (a shared pointer, count `+0x520`): `+0x10` the pad (virtual `+0x10` returns `{u32 held, u32 held last frame}`, so `held & ~last` is a press), `+0x228` the left stick (virtual `+0x18` returns `{f32 x, f32 y}`), `+0x234` u8 locked (no input). Button bits are the physical buttons of `HidButtonTable.gsheet` (`mButtonType`): 0 A, 1 B, 2 X, 3 Y, 4 R, 5 L, 10 Plus, 11 Minus, 14 to 17 up, down, right, left. The maps read A (pin), X (memories) and Y held (place names / dungeon info) this way. Per-function flags: `+0xb1` and `+0xd1` zoom out / in, `+0x171` and `+0x181` floor up / down (the maps' zoom and the dungeon floors).

## Datasheets

| Function | |
|---|---|
| `0x83a1e0` `Datasheet_Load` | `(out shared_ptr, resource group, StringView* path)`: the path in the group's file map (FRAMEWORK.md, Resource groups) |
| `0xfa6130` `Datasheet_RowById` | `(sheet, s32 id, row size)`: the row whose `mId` is id (a hash map); rows at `*(sheet+0x70)+0x20`, count `+0x28` |
| `0xf9d320` `Datasheet_RowAt` | `(out {sheet sp, count, row}, sheet, index)` |
| `0xf9d260` `Datasheet_FindRowShared` | `(out {sheet sp, count, row}, sheet, StringView* key)`: by the key column (`Datasheet_FindRow`) |
| `0xf9de10` `Conditions_Check` | `(conditions, u32 type, StringView* arg, actor)`: type 0 true, 1 event flag (`!` negates), 2 `name:value` counter, 3 companion (`None`, `BowWow`, `Marin`, `Ghost`, `Rooster`), 4 to 7 actor parameters; the sheets' condition arrays are `{u32 type, string}` elements of 0x18 bytes |

A loaded sheet (`Sheet` in `game/sheet.hpp`; `Datasheet_Create` `0xfa64f0`, a make_shared of 0x110 bytes,
`Datasheet_vtable` `0x1c30d40`) is a resource (`Resource_Ctor` `0xfb8e40`; virtual bases with vtables at `+0x100`
and `+0x108`): `self` (shared-from-this), the resource info (`+0x18`..`+0x40`), `path` (the resource manager's key,
by which the destructor unregisters it), `keep` (the file buffer), `data` (the `gsht` data, checked for version 1)
and `dataSize`, `keyField` (-1 without a key column, when `Datasheet_RowById` and `Datasheet_FindRow` throw;
`Datasheet_KeyField` `0xfa5f10`), and three hash maps: `map_88` (by a u32, not traced), `rowsById` (id to row index)
and `rowsByKey` (key string to row index).

`UiDatasheets_Load` (`0x867f34`) loads the UI sheets into holders of shared pointers:

| Holder | Sheets |
|---|---|
| `0x1cc3a18` | `UiFieldMapIcons` `+0x0` (rows 0x50: `mNameLabel` `+0x8`, `mType` `+0x18`, show flag `+0x20`, x/y/z `+0x30`), `UiFieldMapLocationName` `+0x10` (0x28: label `+0x8`, flag `+0x18`), `UiFieldMapMask` `+0x20` (0x50: area `+0x4`, conditions A `+0x8` and B `+0x18`, `UI_FieldMapTraverse` flag `+0x28`, `UI_FieldMapOpen` flag `+0x38`), `UiDungeonArea` `+0x30` (0x20, by area name: `mFloor` `+0x10`, `mAnimFloorIndex` `+0x14`, `mIsBoss` `+0x18`), `UiDungeonIcon` `+0x40`, `UiDungeonStair` `+0x50` |
| `0x1cc3ad0` | `UiMemory` `+0x0` (0x78), `UiMemoryHearts` `+0x10` (0x10: x/y/z `+0x4`), `UiMemoryShells` `+0x20` |
| `0x1cc3960` | `UiItemMenuCursorMoveTable` |

## Text

`MessageManager_Find` (`0x903250`, manager `*0x1cb2db8`): `(out {msbt sp, count, s32 label index}, manager, StringView* "File:Label")` finds a label in a loaded `<File>.msbt` (index -1 when missing). `String_Format` (`0x8ed5c0`) formats `{0}`. The overworld map names an icon by `Place:<mNameLabel>`; `MessageText_SetLabel` sets a text box from a label.

## Pause menu

TESTING.md, Pause menu header and Pause menu pages, has how the header changes pages.

**Navi** (UI 5, `Gm_Navi.bflyt` at priority 5; `Navi_Ctor` `0xee4a40`, `Navi_Dtor` `0xee4fb0` (deleting `0xee6200`),
setup `0xee6230`, update `0xee6f80`, open `0xee7830`; 0x1350 bytes, which its factory (`0xec8b44`) allocates with a
control block of 0x20; vtable `0x1c2aa18`, holding only the LayoutWrapper slots, followed by the machine's
`Navi_MachineVtable` `0x1c2aa90`). The setup fills the machine (None, Open, WaitKey, Selected, CloseOldUi, Close), the
`pageIds` (`0xf`, or `0xe` in a dungeon; 6; 10, or `0xb` with level index 8) and the text and pane handles.
`Navi_PaneReady` (`0xeeccc0`) hangs a requested page under its anchor (`anchors[i]`'s pane) and keeps it in `pages[i]`,
and restores the page's state; CloseOldUi saves it, and `Navi_Dtor` hands `target`, `overworldZoom` and `pinType` to the
HUD (`+0x61dc`, `+0x6370`, `+0x6371`):

- `overworldZoom` and `dungeonZoom`: the maps' zoom indexes (overworld `+0x8178`, dungeon `+0x2cec`).
- `mapCursor`, valid while `mapCursorSaved`: the last map page's cursor (overworld `+0x81a8`, dungeon `+0x2d18`).
- `pinType`: the pin type selected in the pin bar, which both map pages keep (overworld `+0x8161`, dungeon
  `+0x2cd5`): set from the hovered pin's code % 10, stepped 0 to 8 by the type buttons
  (`OverworldMap_PinTypeButtons` `0xe9e5a0 (u8* previous, u8* type)`, the previous one at `+0x8162` / `+0x2cd6`).
- `memoryTab`, `memoryListPos` and `panelListPos`: the Memories UI's tab and the list positions of its first tab
  and of its Chamber Dungeon panels tab (three bytes each, the list's virtuals `+0x98` / `+0x90`), which its open
  (`0xee13b0`) restores and its close (`0xee17a0`, `0xee18e0`) saves.
- `itemCursor`: the Items page's cursor (`+0x4020`).
- `inputOn`: the update reads L and R only while it is set (1 at construction; the Open update `0xee8650` sets it).

**NaviGuide** (`Guide_Ctor` `0xf55ea0`, run by Navi's constructor; `Guide_vtable` `0x1c2e538`, `Guide_Dtor` `0xe5e070`;
bound to `L_Operation_00` by Navi's setup). `Guide_Setup` (`0xf56540`, virtual `+0x20`) binds `L_Btn00`..`08` and
`L_BtnAttention00` (setting `hasAttention` when found), reads `area` (`N_Operation_00`'s rectangle plus its translate)
and `spacing` (the pane's extended user data), sets every `kinds` entry to 0x1c and points the slots' text boxes at
`tags`. `Guide_Set` (`0xf571b0`) clears and fills `captions[slot]` and sets `relayout`; `Guide_Clear` (`0xf57ae0`) hides
the slots through each one's layout (virtual `+0x48`, root pane `+0x18`). `Guide_Update` (`0xf568f0`, `+0x28`, from
Navi's update) runs `Guide_Layout` (`0xf56c30`: the shown slots placed right to left from `area[2]`, `spacing` apart)
while `relayout` is set, and ends the attention (`Guide_EndAttention` `0xf56a90`: slot `attentionIndex` back in its
place, its look by `enabled`) once `attentionEnding` is set and both its animations have stopped. While `attention` is
set, the attention slot stands in for slot `attentionIndex`. A **GuideSlot** (`GuideSlot_vtable` `0x1c2e620`,
`GuideSlot_Setup` `0xf581d0`, `GuideSlot_Update` `0xf58450`, destructor `0xe5e400`): `enabled` picks Normal or Disable;
the attention slot's `GuideSlot_SetupAttention` (`0xf58510`) binds `attentionInOut` and `attentionLoop`, which its
update plays once the first ends. `Guide_StartAttention` (`0xf57fb0`) puts the attention slot over a slot and plays
`attentionInOut` forward (`GuideSlot_AttentionIn` `0xf586d0`, which sets `attentionIn`); `Guide_StopAttention`
(`0xf58150`) plays it backwards (`GuideSlot_AttentionOut` `0xf58740`, clearing `attentionIn`) and sets
`attentionEnding`.

**MenuPage** (`Page_vtable` `0x1c2a950`, 0xf0 bytes): each page's constructor builds the base inline; `Page_Dtor`
(`0xee4310`: the vtable reset, the page out of its parent and back into the HUD pass, `setGuide` destroyed, `out` and
`in` released, the wrapper's destructor) ends the destructors of Gm_Item, the maps, the Memories and GmSystem.
`GmSystem_Dtor` (`0xec4330`) is Gm_System's own, which PdPage's (`0xec42c0`) calls, so a hook on it sees every Gm_System
and PdPage page go. `Page_SetupBase` (`0xee4590`) binds `in` and `out` and sets `canClose`. `setGuide`
(`void(const u32& slot, const u32& kind)`) sets a slot of the Navi guide to a kind (`Guide_Set`; kind 0x1c hides
the slot): `Navi_PaneReady` stores a functor over the Navi (`Navi_GuideCallback` `0xeee3c0`, vtable `0x1c2ab88`) in
each page it hangs, and the map pages copy it into the Memories UI (`DungeonMap_MemoryCreated` `0xe86470`).

| Page virtual | |
|---|---|
| `+0x68` / `+0x70` | `Page_PlayIn` (`0xee4750`: Out off, In on, played at 1.0) / `Page_PlayOut` (`0xee47b0`) |
| `+0x78` / `+0x80` | In forward from its start / In backwards |
| `+0x88` / `+0x90` | In / Out to its end |
| `+0x98` / `+0xa0` | `Page_InEnded` (`0xee4970`) / `Page_OutEnded` (`0xee49b0`) |
| `+0xa8` / `+0xb0` | close (calls `+0x70`; Gm_Item's `0xeb6070`) / closed (calls `+0xa0`) |

**GmSystem** (UI 10, `Gm_System.bflyt`; `GmSystem_Ctor` `0xf82b00`, 0xd70 bytes, its factory `0xec8dbc`;
`GmSystem_vtable` `0x1c2fe50`; the machine's vtable `0x1c2ff18`: count `0xf82eb0`, names `0xf82ec0`). `choices` are
three `SystemChoice` parts (`L_SystemChoice_NN`, `SystemChoice_vtable` `0x1c2c440`, 0x168 bytes,
setup `0xf354b0`: the animations On, Off, Loop, Decide, Normal, Disable, Set and ToggleOnOff and `text`
`T_List_00`, `UI:SystemChoiceSave`, `Load`, `RubyOn`). `choiceCount` is 3 when the message manager's `+0x20` is 1
(Japanese), else 2 with the third choice hidden; `rubyOn` is copied from the map pins' section `+0x762`. KeyWait
(`0xf839e0`) moves `cursor` (`previousCursor` the one it left) and plays the choices' On, Off and Loop; A on RubyOn
goes to ChangeRuby (toggles `rubyOn` and the section's byte, plays ToggleOnOff), on Save or Load to Decide, which
requests `choiceUis[cursor]` (0x1f save, 0x20 load, `Cm_FileOperate`) once Decide ends; WaitUiClose waits until
that UI is gone. The Load screen's machine (`file` in the trace: Open, KeyWait, Decide, Close, Confirmation,
ConfirmationCancel, ConfirmationToKeyWait, ConfirmationDecide, Load, LoadEnd) starts the load with
`FileOperate20_StartLoad` (`0xebf530`): `SetGameState(4)`, which in a level is a level jump (`LevelManager_LevelJump`
with no level name), and the pause header removed with `UiManager_Remove`, without its Close state. `systemOp` is the controls list (`SystemOp`, `L_SystemOp_00`, `GmSystem_SystemOpCtor` `0xf78af0`,
`SystemOp_vtable` `0x1c2f980`, 0x678 bytes, to the end of the page): nine `SystemOpEntry` parts (`L_Op_00`..`08`,
0xa8 bytes, `text` `T_Operation_00` with the labels `UI:SystemOperatingItem`, `Map`, `Guard`, `ItemX`, `ItemY`,
`Dash`, `Action`, `Attack`, `Move`); its setup sets the layout's Set frame by the items owned. PdPage (UI 0xb,
`PdPage_vtable` `0x1c2c068`) derives from GmSystem and adds a machine at `+0xd70`.

## Items page (UI 6, `Gm_Item`)

`GmItem_Ctor` `0xeb1350` (0x4250 bytes), `GmItem_Dtor` `0xeb43a0` (ending in `Page_Dtor`), setup `0xeb52b0`, update `0xeb5f40`, open `0xeb5ff0`, close (virtual `+0xa8`) `0xeb6070`. Machine at `+0xf0` (change `0xeb60b0`): Deactive, In, Loop (`0xeb7cd0`), Out, and the song pop-up's OcarinaPopIn, PopLoop, PopDecide, PopOut. `canClose` (`+0xe0`) allows B, L and R (cleared while the pop-up is open). Its tag processor is at `+0x4030`, a 0x108-byte text buffer at `+0x4148`.

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

## Autosave popup (UI 0x2a, `Ld_ThrobberSave`)

**ThrobberSave** ("Now Saving", archive set 1, StartUp.arc; `ThrobberSave_Ctor` `0xf855f0`, destructor `0xec5a90`, `ThrobberSave_Setup` `0xf85800`, `ThrobberSave_Update` `0xf85c60`; 0x2a0 bytes, its factory `0xec587c`; vtable `0x1c30098`, the machine's `0x1c30110`). The setup sets `text` (`T_Info_00`) to `UI:SystemSaving` with `MessageText_SetLabel` and puts `out` at its end. `ThrobberSave_Show` (`0xf895c0 (bool auto)`) from None or Out goes to InAuto with `timer` 0.5 s, or to In; LoopAuto counts `timer` down and slides out at zero (`ThrobberSave_ChangeState`). The module's toasts use it (DESIGN.md, Toasts).

**Throbber** (UI 0x29, `Ld_Throbber.bflyt`; `Throbber_Ctor` `0xf84cb0`, destructor `0xec5e60`, `Throbber_Setup` `0xf84e90`, `Throbber_Update` `0xf85170`, `Throbber_ChangeState` `0xf85230`; 0x210 bytes, its factory `0xec5ca4`; vtable `0x1c2fff0`): a machine of 4 states at `+0x80` (vtable `0x1c30068`: None, In, Loop, Out), the animations In `+0x1c0`, Loop `+0x1d8`, Out `+0x1f0`; `+0x208` unknown. The module has no type for it.

## Message window (UI 0x24, `Cm_Message`)

**MessageWindow** (`MessageWindow_Ctor` `0xeee6b0`, `MessageWindow_Dtor` `0xeefbb0`, `MessageWindow_Setup` `0xeeffc0`, `MessageWindow_Update` `0xef0bc0`, UI id `0xef0f30`; 0x1d20 bytes, its factory `0xec9554`; `MessageWindow_vtable` `0x1c2abd0`, followed by `MessageWindow_MachineVtable` `0x1c2ac48`). TESTING.md, `0xef1960`, has its states as the test build reads them.

- `inOutChoice[n − 2]` plays for a choice of n options: `MessageWindow_Selecting` (`0xef2300`) takes buttons once that animation's animator has stopped (`speed`, `+0x3c`, is 0). Its animator pointer is at window `+0x58 + choiceCount × 0x18` (`inOutChoice[choiceCount − 2]` `+0x10`).
- The parts: `talk` (vtable `0x1c2e310`, with `talkName`), `getItem` (`0x1c2e0d0`), `system` (`0x1c2e250`), `telop` (`0x1c2e3d0`) and `modeless` (`0x1c2e190`, with `modelessName`; its setup `0xf54eb0`); `text` is the one in use, whose virtual `+0x28` the update calls.
- The states' enters get the previous state and their exits the next (`0xef1560`). The Opening enter (`0xef1300`) requests UI 0x24 only while `resumeState` is 0; TimerWaiting and EventWaiting (`0xef2b90`, `0xef2d70`) keep the state to go back to in `resumeState`, and every open clears it.
- `MessageWindow_OpenTalker` (`0xef7300`) and `MessageWindow_PlaceBalloon` (`0xef7a30`, which opens a modeless message
  before it places it) set `msbt`, `label` and `page`. `MessageWindow_SetPage` (`0xef43b0`) reads the message from
  `msbt` `+0x90` and fills `pageText`, `choiceText`, `choiceCount`, `choiceDefault`, `choiceCancel`, `pageLetters` and
  `pageEnd` (3: more follows, and Reading `0xef1960` asks `0xef6080` for the next page; otherwise a kind it computes;
  likely), and copies `pageColor` into the tag processor's `colorIndex`. `MessageText_SetText` gets `pageText`.
  `pageColor` is the colour the next page starts in: the page's tag visitor (`MessageWindow_CollectTag` `0xef5b30`)
  stores a colour tag's value in it (and sets `fadeIn` and `follow` from tags 0xe and 0x10), and the opens reset it to
  0. `pageLineBreaks` is the page's count of line and page breaks, which nothing reads.
- `MessageWindow_NextState` (`0xef6270`) runs `pendingTags` (node `+0x10` kind, `+0x14` value, `+0x148` the letter it applies at) up to the letters shown and returns the next state: tag 7 sets `timer` (TimerWaiting, counted down by `0xef2ba0`), 0xd `waitButtons` (ButtonWaiting, `0xef2e90`), 0xf `fadeOut` (the page ends in FadeOut instead of TextFadeOut); it also sets the text's `lettersTarget` and `lettersFade`.
- `result` is the chosen option + 1, 0xff none (Selecting's A, the PreSelecting enter, ButtonWaiting). `choiceCursor` is an s32 (`MessageWindow_Selecting` reads and writes it so); `MessageWindow_PreSelectingEnter` (`0xef20e0`) copies `choiceDefault` into it; B picks `choiceCancel`. `decided` makes Selecting wait for the option's Decide.
- The counters: `followWait` `{1/6, 0, 6}` for the balloon's follower (`0xef1210`); `keyWait` `{1/6, 0, 6}` in KeyWaiting (`MessageWindow_KeyWaiting` `0xef1c40`) and `{0.1, 0, 10}` in PreSelecting; `fadeWait` `{0.25, 0, 4}` in TextFadeOut (`0xef1ab0`).
- `fadeIn` goes to the text's virtual `+0x68` from Opening and FadeIn (`0xef1300`, `0xef1560`), then 0; `modelessPos` and `modelessHalfSize` are `W_Mes_01` of the modeless part, which `MessageWindow_PlaceBalloon` clamps the balloon with; `follow` makes the update move the balloon after a world point by `balloonOffset`.
- `kind` indexes the 20 window kind names (`MessageWindow_KindNames` `0x1c2ac68`: MessageWindow, Dialog_Marin,
  Dialog_Owl, Dialog_Telephone, Dialog_Monologue, Telop_Bottom, Telop_Center, Telop_Top, System_1line to
  System_3line, System_ItemGet, System_KeyItemGet, Modeless_Marin, Modeless_Ghost, Modeless_BowWow,
  Modeless_Versatile, MessageWindow_Localize, Dialog_Monologue_Localize, MessageWindow_Stalfos); 0x14 none, at
  construction. `MessageWindow_SetKind` (`0xef3590`) sets it and the tag colour, hides the five parts, shows the
  kind's and picks its text box (`MessageText_SelectText` `0xf54060` with the second box for kinds 0xc and 0x10 to
  0x12, `MessageText_SelectStalfosText` `0xf54190` for 0x13); `MessageWindow_OpenTalker` tests 9. `kindOfStyle` is
  the kind per message project style (`MessageWindow_MapStyles` `0xef4020` finds each kind name in the project's
  styles), from which the opens pick `kind`. `handle` is the opener's request handle, released by DrawEnd and Closing
  (`0xef10c0`, `0xef3120`, `0xef33f0`; likely). `closeAtEnd` (1 at construction) sends the last page to Closing, else
  DrawEnd.

**MessageText** (`MessageText_vtable` `0x1c2df20`, the base of the five parts; destructor `0xeef6b0`):
`MessageText_Update` (`0xf534e0`, virtual `+0x28`) takes `lettersShown` from the `letters` animation (`Typing`), whose
frame is the letters × `framesPerLetter` ×
`speed`, playing forward at most `revealRate` × `speed` a step; `MessageText_PlayIn` (`0xf53650`, `+0x68`, `(part,
count)`) plays `in` and disables `out`; `MessageText_PlayOut` (`0xf536f0`, `+0x70`) plays `out` and the feed's Out;
`MessageText_SetText` (`0xf53840`, `+0x80`) sets `text`'s string and `speed` = `letterFrames` / the letters
(`letterFrames`: the letters `T_Mes_00` holds in the layout, `framesPerLetter` Typing's frames / that);
`MessageText_ShowLetters` (`0xf53b60`) shows the first letters at once, and `MessageText_Hurry` (`0xf53c80`, B) sets
`revealRate` 2.0. `MessageText_Setup` (`0xf52d10`) binds the text boxes `mainText` (`T_Mes_00`), `altText`
(`T_Mes_01`, optional: System_KeyItemGet, Modeless_Versatile and the two `_Localize` kinds) and `stalfosText`
(`T_MesStalfos_00`, optional); `text` is a copy of the one in use. `mesPosPane` (`N_MesPos_00`, which the balloon's
follower moves) and `normalPane` (`N_Normal_00`) are reset by `MessageWindow_OpenTalker`. `kindAnims` are the window
kind's animations (`ItemGet`, `KeyItemGet`, ...; setups `0xf54d10`, `0xf551d0`; the first played at the setup).
`feed` is the next-page icon (`MessageFeed`, `L_MesFeed_00`, `MessageFeed_vtable` `0x1c2e058`, setup `0xf548c0`,
update `0xf54c50`; 0xf8 bytes) with In, Out, Loop and Decide and `shown` (set when KeyWaiting's enter plays In,
cleared by Out); its In-to-Loop handover runs while `hasFeed` (the layout has `L_MesFeed_00`) is set.

**MessageChoice** (`MessageChoice_vtable` `0x1c2dfe0`; `MessageChoice_Setup` `0xf54280`, update `0xf546b0`, destructor `0xeef4c0`): Selecting plays `on` and `off` by `hovered` and waits for `decided` and the end of `decide`.

## Map cursor (UI 0x1c, `Gm_MapCursor`)

One UI drawn over both map pages; each page's open requests it (`UiManager_Request(0x1c)`), and pages find it with
`0xe79560`. `MapCursor_Make` (`0xec2c50`) makes it, 0x310 bytes (vtable `0x1c28370`, setup `0xe9fdc0`, destructor
`0xe9fa40`). Fields: `+0x80` the name bubble's machine (`MapCursorMachine_vtable` `0x1c283e8`; Deactive, On, Loop, Off,
HideName; change `0xea0750`, run by its update `0xea0690`; On's enter is `MapPopName_SetText` `0xea0960`, which sizes
the bubble only while `nameHidden` is clear); animations OnName `+0x238`, OffName `+0x250`, In `+0x268`, Out `+0x280`,
On `+0x1f0`, Loop `+0x208`, Off `+0x220` (animator at `+0x10` of each); `T_MarkerName_00` text box `+0x2a8`;
`W_window_00` `+0x2c0`; `W_Cursor_00` `+0x2f8`, its size copied to `+0x300`/`+0x304` (the hover radius and the cursor's
size for clamping). `nameHidden` (`+0x2e0`) and `nameHideRequest` (`+0x2e1`) are two u8 flags (the constructor sets the
first); the handle at `+0x2c8` (`field_2c8`) is never set.

- `0xea0f50` `MapCursor_ShowName (cursor, MessageRef*)`: shows the name with the message's text, clears `+0x2e0`, then `0xea1000`.
- `0xea1000` `MapCursor_NameOn (cursor)`: clears `+0x2e1`, bubble state On, `SE_SY_MAP_CURSOL_WIDE`.
- Setting `+0x2e0` and `+0x2e1` to 1 takes the name away (the maps do this when nothing named is hovered).

## Map markers

**Icon markers** (`Gm_MapMarker.bflyt`, `MapMarker_vtable` `0x1c2cfa8`, 0x250 bytes): made by `0xe9c870` `(out shared_ptr)` and set up as a part of a page by `0xf3ad80` (out of the HUD pass, then the setup `0xf3a490`). Its vtable's slots end at `+0x70`; the machine's vtable (`MapMarkerMachine_vtable` `0x1c2d020`) follows at `+0x78`. Fields: `+0x90` machine of 4 states (Deactive, On, Loop, Off; change `0xf3a8c0`, run by `MapMarkerMachine_Run` `0xf3a900`), then 8 bytes of alignment padding (`+0x1c8`), animations On `+0x1d0`, Loop `+0x1e8`, Off `+0x200`, SetMark `+0x218` (each `{layout sp, count, animator}`; SetMark's animator `+0x228`), the sheet `+0x230` (shared, kept alive) and row `+0x240` (a `UiFieldMapIcons` row), `+0x248` set when the marker loses the hover (`MapMarker_LoopUpdate` `0xf3aba0` then goes to Off). The SetMark frame picks the marker's texture and size (`ap_map_icons.py` adds frames).

**Pins** (`MapMarkUnit`, `Gm_MapMarkUnit.bflyt`, the player's map pins; vtable `0x1c2da60`, 0x140 bytes): made by
`MapMarkUnit_Make` `0xe828a0 (out, "Gm_MapMarkUnit.bflyt")`, set up by `MapMarkUnit_Setup` `0xf487c0`; the
animations On, Loop, Off, Normal, Disable, SetMark (its animator `+0x118`, frame max(code % 10 - 1, 0)) and Put;
`code` at `+0x138`; hover On (`0xf48f90`) and Off (`0xf48fe0`).

**Pin bar** (`MapMarkList`, `L_MapMarkList_00`, `MapMarkList_vtable` `0x1c2d9e8`, 0xe98 bytes, setup `0xf46ef0`):
the animations In, Loop, Out, Normal, Disable, SetPositionX and SetPositionY, the texts `pinCount` (`T_PinNum_00`),
`pinCapacity` (`T_PinNum_01`) and `pinSlash` (`T_PinSlash_00`), nine `MapMarkUnit`s `marks` (`marks[1]`..`[8]`
bound to `L_MapMark_01`..`08` by the in-list setup `0xf48340`; `marks[0]` not bound), the panes `markPanes`
(`L_MapMark_00`..`08`) and `cursor` (`MarkCursor`, `L_MarkCursor_00`, 0x100 bytes: On, Loop, Off, `W_Cursor_00`). The
pins' save data is behind `*0x1cc3228` (0x1f0 bytes, made on first use): six `std::function`s (objects at `+0xd0`,
`+0x100`, `+0x130`, `+0x160`, `+0x190`, `+0x1c0`, each called through its `+0x20`): get a slot's position (`0xede050
(function, slot)`), get its code (0 empty), set its position (`0xede0b0`), set its code (`0xede110`), the number of pins
placed (codes not 0) and the capacity. `0xedbfd0 MapPinStore_Bind (store)` binds them on each level load
(`LevelLoad_Finish`) for the dungeon index (`*0x1cc13c8`, kept at store `+0x18`); the pins' save section is `*0x1cc1370`
(store `+0x10` holds `0x1cc1370`):

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
| `+0x500` / `+0x540` | `map_500`, an `unordered_map` only made and destroyed; `field_540`, a handle never set |
| `+0xd30` | Y held: place names shown, no hover, no snapping |
| `+0xd38` | `maskRowCount`: the `UiFieldMapMask` rows |
| `+0xd40 + a*0x158`, `+0x3840 + a*0x18` | `fieldAreas` (`L_FldArea_<a>`, below) and `L_FldAreaMask_<a>` (32 areas) |
| `+0x3b40` | 30 pins; hovered pin `+0x3d20`, its index `+0x8158` (previous `+0x815c`) |
| `+0x3d40` | `memoryList`: the `UiMemory` rows shown (`{sheet sp, row}`) |
| `+0x3d60 + i*0x230` | `memoryParts`: `L_Memory_<i>` (24, `MemoryMarker`, below) |
| `+0x71e0` / `+0x72b8` | `linkMarker` (`MapPlayer`, `L_MapPlayer_00`) / `pinBar` (`MapMarkList`, `L_MapMarkList_00`; Map markers) |
| `+0x8150` / `+0x8154` | hovered icon marker's index into `+0x710` / previous (-1 none) |
| `+0x8161` / `+0x8162` | `pinType` / `previousPinType` (Pause menu, Navi) |
| `+0x8164`..`+0x81a0` | zoom: levels `{1, 3}` at `+0x819c`, index `+0x8178`, current and target scale `+0x8168`/`+0x8164`, `zoomProgress` `+0x816c` (0 to 1, 1 once a zoom ends), `+0x8170` scale of the cursor's speed and hover radius |
| `+0x81c0` / `+0x81c1` | `maskOpening`: the setup opened a traversed area's mask, so Open goes on to OpenMask; `cursorShown`: Open played the cursor's In |
| `+0x8198` / `+0x81a8` | cursor speed / cursor position |
| `+0x81b0` / `+0x81b8` | Link's position / Link not in the overworld (his marker hidden) |

World to layout (`B_Map_00` pane P): x = floor(P.width / 240 × x + P.x), y = floor(P.y − 2.7 × ((z − 8.25) × 0.79 + (32.25 − y) × 1.1)).

**Parts.** A field area (`FieldAreaPart`, `FieldAreaPart_vtable` `0x1c2d050`, 0x158 bytes, setup `0xf3b240`): the
animations On, Off, PlainOnOff, Active, Deactive, ZoomIn and ZoomOut, its `UiFieldMapMask` row (`maskRow`, the
sheet kept in `maskSheet`) and `traversed` (the row's `UI_FieldMapTraverse` flag); `FieldArea_Update` (`0xf3b810`)
plays On or Off by `traversed` and Deactive when the row's conditions hold, then does nothing more. A Memory's
marker (`MemoryMarker`, `L_Memory_<i>`, `MemoryMarker_vtable` `0x1c2ddd0`, 0x230 bytes): a `StateMachine<4>` at
`+0x90` (Deactive, On, Wait, Off; `MemoryMarker_ChangeState` `0xf516f0`) and On, Loop, Off, SetMark. Link's marker
(`MapPlayer`, `L_MapPlayer_00`, `MapPlayer_vtable` `0x1c2cd48`, 0xd8 bytes): On, Indoor, Sideview.

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
| `+0x6b8` / `+0x6d0` | DgnMapOn / DgnMapOff (no decompiled page function plays them) |
| `+0x730` | MapSymbolInOut (animator `+0x740`): `DungeonMap_UpdateDungeonInfo` (`0xe87d60`) plays it forward or back (virtual `+0xc8`, speed 1 or -1) as Y is pressed or let go |
| `+0x758`.. | panes (the pane of each `{sp, count, pane}` handle): `N_MapOrigin_00` `+0x758`, `P_Grid_00` `+0x770`, `N_MapPosition_00` `+0x788`, `N_MapPin_00` `+0x7a0`, `N_PinOffset_00` `+0x7b8`, `B_PointerLimit_00` `+0x7d0`, `B_ScrollLimit_00` `+0x7e8`, `B_MarkerLimit_00` `+0x800`, `N_MapIcon_01` `+0x818`, `N_MapIcon_00` `+0x830`; text `T_DgnName_00` `+0x848`, `T_PanelDgnName_00` `+0x860`; cells `+0x878 + cell*0x18` |
| `+0xe68 + cell*0x10` | the cell's chip (`Dg_DgnChip` visited, `Dg_DgnChipMask` with the map only) |
| `+0x1268` | vector of shared pointers: the room icons |
| `+0x1280` | 20 pins; hovered pin `+0x13c0`, its index `+0x2ccc` (previous `+0x2cd0`) |
| `+0x13d0`, `+0x1460`, `+0x14f0` | `L_DgnItem_00`..`02`: map, compass, stone beak in the sidebar |
| `+0x1580 + i*0x168` | `floorChoices`: `L_MapChoice_00`..`03`, the floor buttons (`MapChoice`, 0x168 bytes: `ListChoice_Setup` `0xf345d0`'s On, Off, Loop, Decide, Normal, Disable and `T_List_00`, then `N_Player_00` and `N_Boss_00`, the panes marking Link's and the boss's floor) |
| `+0x1b20` | the boss icon |
| `+0x1b30`, `+0x29c8`, `+0x2a98`, `+0x2b78` | `pinBar` (`L_MapMarkList_00`), `L_MapStart_00` (entrance), `linkMarker` (`L_MapPlayer_00`), `timeCount` (`L_TimeCount_00`: `TimeCount`, 0x150 bytes, In, Out, SetTime, `T_Time_00`..`02`, `T_Sign_00`/`01`) |
| `+0x2b70` | Link's map position |
| `+0x2cc8` | the dungeon index (`*0x1cc13c8`; 8 the Chamber Dungeon, which has no sidebar items) |
| `+0x2cd5` / `+0x2cd6` | `pinType` / `previousPinType` (Pause menu, Navi) |
| `+0x2cd8` | zoom scale (levels `{1, 2}` at `+0x2d0c`, index `+0x2cec`); `zoomFrom` (`+0x2cdc`, the level the zoom started from, likely) and `zoomProgress` (`+0x2ce0`, 0 to 1): `DungeonMap_UpdateZoom` (`0xe78d70`) lerps the scroll from `+0x2cf8` to `+0x2d00` and the cursor's scale by it |
| `+0x2d18` | cursor position |
| `+0x2d20` | Link in a side-view room |
| `+0x2d21`..`+0x2d32` | floors: count `+0x2d21`, top `+0x2d22`, shown `+0x2d23` (previous `+0x2d24`), floor art index `+0x2d28`, Link's `+0x2d30`, the boss's `+0x2d31`, the entrance's `+0x2d32` |
| `+0x2d40` | Y held: dungeon info, no hover |
| `+0x2d41`, `+0x2d43`, `+0x2d44` | `cursorRestored` (Navi's `mapCursor` set the cursor; else Open puts it on Link's marker), `cursorShown` (Open played the cursor's In), `sideViewMarkerShown` (Open placed Dg_DgnPlayer in a side-view room); `DungeonMap_OpenUpdate` `0xe79a20` |
| `+0x4f8` | `memories`: the Memories UI (`Gm_Memory`), set by `DungeonMap_MemoryCreated` (`0xe86470`) |

The dungeon (`*(scene+0x618)+0x208`, scene from `SceneHolder_Lock`): rooms `+0x1f8` (shared pointers to `Area`s, 0xd0
bytes: box `+0x20`/`+0x30`, the view box `viewMin`/`viewMax`
`+0x40`/`+0x50`, `active` `+0xc0`, `showForced` `+0xc1`, `viewKind` `+0xc4` (ACTORS.md, Rooms), id `+0x60`, zone
`+0x64`, its file's path `+0x68` as `level/Lv01TailCave/Lv01TailCave_03G.leb` with its length at `+0xb8`, row `+0xc2`,
column `+0xc3`), zones `+0x210`, virtual `+0x20` the room at a position.

**Setup**: per room visited, or on the map or compass: its chip under `B_<cell>`, then `0xe80950` `DungeonMap_MakeRoomIcons (page, room, dungeon, visited, map, compass)`: the boss icon (`UiDungeonArea` `mIsBoss`, until beaten), an icon per room actor and per `UiDungeonIcon` row of the room from `0xe832c0` `DungeonMap_MakeActorIcon (out, page, actor {u16 id, f32 x, y, z, switches}, room, visited, map, compass)` by actor id: `ObjTreasureBox` a chest (`Dg_DgnTresure`), `ObjLockDoor`/`ObjBossDoor`/`ObjClosedDoor` and the one-way and Panel doors, `ObjDungeonCrackedWall` and Panel cracked walls (`Dg_DgnBombWall`), `AreaUpstairs`/`AreaDownstairs` and Panel stairs, `ObjDungeonWarpHole`, `PanelJointOpeningStraight`; no icon for other actors. Each icon is placed by `0xe84b60` `DungeonMap_PositionIcon (x, y, z, page, out vec2, room sp*)` (world to map, `0xe827b0`: the cell's position plus the offset from the room's middle × 5.333; the result is the icon's translate, set through icon virtual `+0x80`), attached with `AttachBefore(icon, page, N_MapPin_00, N_PinOffset_00)` and pushed onto `+0x1268`. Then `0xe82250` `DungeonMap_ShowCellFloor (page, cell, room, area row)` per room (and on every floor change): the cell, its icons (icon vtable `+0x78`), Link's and the entrance markers and the pins (code / 10) are shown on their floor only. The floor art is the animation `Dgn_<mAnimFloorIndex>`.

**Room icons** (`DungeonMapIcon_vtable` `0x1c2cb38`, 0xd0 bytes, built by `0xf37fb0 (icon, layout name)`, initialised by `0xf36e60 (icon, actor, visited)`): open/closed animators `+0xa0`/`+0xb8`, `+0xc0` the area's id, `+0xc4` shown, `+0xc5` map, `+0xc6` compass, `+0xcc` visited. These fields and the animations belong to its base class DungeonMapPart (`DungeonMapPart_vtable` `0x1c2c638`, 0xd0 bytes, destructor `0xe71ff0`), which the page's `L_MapStart_00` (`+0x29c8`) is too. Virtuals `+0x80` `DungeonMapIcon_SetTranslate` (`0xf36d20`, `(vec2 in x0, icon)`) and `+0x88` `DungeonMapIcon_SetScale` (`0xf36d60`) move and scale the root pane. `0xf38040` (vtable `+0x70`, `DungeonMapIcon_Update (icon, actor)`) sets `+0xc4` and the icon's visibility when the compass is owned (`+0xc6`) or the actor's shown switch is set (with `+0xcc`), open by its open switch; `0xf380f0` (vtable `+0x78`, `(icon, bool on the floor)`) shows it while both `+0xc4` and the bool are set.

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
