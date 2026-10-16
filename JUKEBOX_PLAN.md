# Jukebox pane: plan

A fourth pause-menu pane, "Jukebox", next to Map, Items and System, reached with L/R. The player picks a BGM track from a list and it plays through the runtime override (`ap_music.cpp`, DESIGN.md "Music"). The RE facts behind this plan are in TESTING.md ("Pause menu" rows of the tables) and `scripts/re/las.sym` (`Navi_*`, `UiManager_*`, `GmSystem_*`, `Page_vtable`).

## How the pause menu works (game 1.0.0)

- **Header object.** `Gm_Navi.bflyt` is run by the Navi UI (vtable `Navi_vtable`, UI id 5). It owns the page order, the L/R switch, the title text and the page anchors. Its state machine at owner `+0x80` has the states Open (1), WaitKey (2), Selected (3), CloseOldUi (4) and Close (5).
- **Pages.** Index 0 is the map (UI id 0xf overworld, 0xe in a dungeon), 1 Items (6), 2 System (10, or 0xb in the Chamber Dungeon). The ids sit in a u32 table at Navi `+0x230`, the current index at `+0x23c` and the target at `+0x23d`. Plus opens on index 1 and Minus on index 0: the HUD stores the index at its `+0x61dc`, and `Navi_Open` copies it.
- **A page change.**
  1. `Navi_Update` turns an L or R press into `+0x1341` (1 L, 2 R).
  2. `Navi_WaitKey` steps `+0x23d` (the count 3 is a constant there), clears the old page's `+0x70`, plays `SE_SY_MENU_LR_MOVE` and goes to CloseOldUi.
  3. CloseOldUi closes the old page (page virtual `+0xa8`) while the header plays NextOut/BackOut. It saves the map's and the item cursor's state and removes the old page (`UiManager_Remove`). It then goes to Selected.
  4. Selected waits until the old page is gone and goes to Open, which asks the UiManager for the new page by id (`UiManager_Request`). The UiManager builds it through its factory slot for that id.
  5. When the page exists, `Navi_PaneReady` stores it at Navi `+0x12f0 + index*0x10`. It hangs the page's layout under the header's anchor pane for that index (`N_Map_00`, `N_Item_00`, `N_System_00`) with `LayoutWrapper_AttachTo`, and sets the button guide (`Navi_SetGuide`) and the titles (`Navi_SetTitles`).
  6. Open waits for the page's In animation (page virtual `+0x98`), then goes back to WaitKey.
- **Header text.** `Navi_SetTitles(navi, index)` fills three message texts: `T_Navih1_00` (the current page), `T_NaviBack_00` (the page under L) and `T_NaviNext_00` (under R). The labels are `UI:SubScreenTitleMap`, `Item`, `System` and `Memory`. Index 3 is an empty case and 4 is the map's Memories view. There are no tab panes, so a fourth page needs no layout change in the header.
- **One slot, different pages.** The game already puts different UIs in one slot: index 0 is the overworld map (0xf) or the dungeon map (0xe), and index 2 is System (10) or the Chamber Dungeon's page (0xb). `Navi_Setup` picks them when the menu opens.
- **Memories is not a page.** It is a sub-screen of the map:
  - the map's own code requests its UI (`UiManager_Request`: 0x1a, `Gm_Memory.bflyt`, from the overworld map; 0x10 from the dungeon map);
  - the map sets the header with `Navi_SetTitles(navi, 4)` and `Navi_SetGuide(navi, 4)`, and the header plays MemoryIn/MemoryOut;
  - inside Memories, L/R switch its own four tabs (`Gm_Memory`'s icons and `Cm_TabCursor`), not the header's pages.
- **Arrays sized 3.** The id table, the page slots at `+0x12f0`, the anchor handles at `+0x12a8` and the R/L wrap all assume three pages. Index 3 would read the current and target bytes as an id, and the next slot would land on the map's saved state at `+0x1320`.

## Approach

### The Jukebox as a mode of index 2

The game never sees a fourth index: the Jukebox is index 2 with a module flag, `g_JukeboxPage`. While the flag is set, the page the UiManager builds for index 2 is a Gm_System object that the module turns into the jukebox. Index, id table, slots, anchor (`N_System_00`), the HUD guide frame and the Plus/Minus logic all stay the game's own.

What the player sees is the order Map, Items, System, Jukebox, wrapping round. The module steers four moves; every other move is the game's:

| From | Button | Game does | Module does |
|---|---|---|---|
| System | R | 2 to 0 (Map) | a change from 2 to 2 with the flag set |
| Jukebox | L | 2 to 1 (Items) | a change from 2 to 2 with the flag cleared |
| Jukebox | R | 2 to 0 (Map) | clears the flag once the old page is gone |
| Map | L | 0 to 2 (System) | sets the flag before the new page is built |

- **Hook on `Navi_WaitKey`** (trampoline), run before the original. For the first two rows it takes `+0x1341` and does what the game's change does:
  - clears the old page's `+0x70` and sets `+0x1338` (direction: 1 for R, 0 for L);
  - goes to CloseOldUi (`Navi_ChangeState(+0x80, 4)`) and plays `SE_SY_MENU_LR_MOVE`;
  - notes the flag value the new page gets.

  The original never changes 2 to 2 itself, since it only changes when `+0x23c` differs from `+0x23d`.
- **Hook on the CloseOldUi update** (trampoline), run after the original. Once the original has removed the old page and gone to Selected, it applies the pending flag. Selected then finds no page for id 10 and goes to Open, which builds a new one.
- **Plus, Minus and B from the Jukebox** stay the game's: Plus goes to Items, Minus to the map, and B closes the menu. The flag is cleared when the old page is removed (CloseOldUi) and on Close (`Navi_CloseEnter`, trampoline).
- **Chamber Dungeon:** with level index 8 (third page id 0xb) the hooks pass everything through, so there is no Jukebox there.

### The page

The Jukebox page is the Gm_System object built for index 2, so the game's constructor, destructor, In/Out animations, layout attach and removal stay intact. Two hooks on its virtuals:

- **`GmSystem_Setup` (vtable `+0x20`):** the original runs first. With the flag set, the hook then:
  - hides `N_SystemChoice_00` (the choices and the controls chart);
  - builds the list layout and hangs it under the page's root with `LayoutWrapper_AttachTo`, so it slides and fades with `N_Page_00` like the other pages.
- **`GmSystem_Update` (vtable `+0x28`):** with the flag set, the original runs only while its machine (owner `+0xf0`, state byte `+0xfd`) is in Open, which plays the In animation. From KeyWait on, the module's update runs instead, so Gm_System's A (Save, Load, ruby text) never runs. The module's update keeps page `+0xe0` set: WaitKey closes the menu on B only while the page's `+0xe0` is set.
- **Destruction:** the module owns the list wrapper; a hook on the page's destructor (vtable `+0x10`, trampoline) deletes it before the original runs.

The fallback, if a game object of our own is ever needed (a page outside index 2), is a LayoutWrapper subclass with a copy of `Page_vtable`, made in the factory slot of an unused UI id (UiManager `+0x130 + id*0x30`, a libc++ `std::function`). The plan does not need it.

### Drawing the list

- **Layout:** `Gm_MemoryList.bflyt`, built standalone with `LayoutWrapper_Build` (Game.arc, set 0). It is the Memories list:
  - 11 `Gm_MemoryListItem` rows 48 px apart (7 visible, 2 spare above and below), each with `T_Item_00`, 29 characters;
  - the animations `Up`/`Down` (the rows move ±52 over 6 frames), plus `Reset` and `Set`;
  - `L_ListCursor_00` (Cm_ListCursor) and `L_ScrollBar_00` (Cm_ScrollBar, whose handle the code places);
  - `T_Panel_00`, which can show the playing track.
- **Rows:** the module keeps a window of 7 tracks and refills the row texts on each scroll; the row animations On/Off mark the cursor row. A first version may skip the Up/Down slide and only refill.
- **Text:** row strings go in with `GameTextBox_SetString16`, and the new glyphs need `UiManager_QueueGlyphs` as the ghost name tags do (TESTING.md, "Text box glyphs"; `ap_nametag.cpp`). Names fit the 29-character rows; nothing longer is needed.
- **Fallback layouts:** `Gm_System` itself (4 rows of 10 characters: too short), or a `Gm_Jukebox.bflyt` the patcher clones from `Gm_MemoryList` (only if the standalone build misbehaves).

### Input while the page is open

- The page's update reads the game's controller (Framework `+0x518`), as the header does: Up/Down move the cursor (repeating while held), and A plays the track under the cursor.
- The header keeps L, R, B, Plus and Minus. The page ignores them, and the header's own popup checks (UI ids 0x1a, 0x1f, 0x20, 0x26) still block it.
- X, the "memories" button on the map pages, stays free for later (e.g. "back to the game's music").

### Track names

- **The list:** row 0 is "Game music" (the override off), then every track in `music.TRACKS`.
- **Names:** the game has no names for its BGM, so the module holds a table of id and display name in `ap_music.cpp`, written for players (e.g. `BGM_DUNGEON_LV1_TAIL` is "Tail Cave", `BGM_MEVE` "Mabe Village"). `test_music` parses that table and checks that it lists exactly `music.TRACKS`, as it already checks the patcher's `BGM_TRACKS`.
- **Title:** `UI:SubScreenTitleJukebox` comes from the module's MSBT label hook (`ap_text.cpp`), with the text "Jukebox" in every language. It fits the title (12 characters), the L label (9) and the R label (8).
- **Hook on `Navi_SetTitles`** (trampoline), run after the original. It writes the three labels when the Jukebox is involved: on the Jukebox (Jukebox, System, Map), on System (Next = Jukebox) and on the map (Back = Jukebox). It calls `MessageText_SetLabel` on the Navi's texts at `+0x2d0`, `+0x2e8` and `+0x300`.
- **Button guide:** `Navi_SetGuide` keeps System's guide for index 2; with the flag set, a hook sets the Memories list's guide or no guide at all.

### Using the override

- `AP::Music` gets `Choose(track)`, which does what `HandleLine` does and also sends `MUSIC <track>` (or `MUSIC off`) to the client. The client stores it as if `/music` had set it, so it is sent again on the next game connection.
- The protocol gains one game-to-client line: DESIGN.md's table and `las_client.py`, with a world test for the client side.
- `Music::Tick` runs only while the game ticks levels, not while the menu is open. Two ways to apply a choice:
  - **On close:** the choice applies when the menu closes (the next Tick). This is safe and needs nothing new.
  - **At once, as a preview:** the page calls the apply step itself (`PlayZoneBgm(true)` and the stop of the previous track), so the player hears the track in the menu.

  Open question 1 decides which.
- The pane shows the chosen track with a mark and the playing one in `T_Panel_00`.

### Close and level loads

- **Close (B, Plus on Items, Minus on the map):** the game closes and removes the page; the module clears its flag and frees the list wrapper with the page. The track keeps playing.
- **Level loads:** the menu cannot be open during one. The override already restarts the chosen track after each level load.
- **Reopening:** the menu opens on Items or the map as always. The Jukebox is never the opening page.

## Risks

- **Biggest:** turning a Gm_System object into the jukebox depends on Gm_System's update being safe to stop after Open. It also depends on its fields (`+0xe0`, the machine at `+0xf0`) behaving as read, and on a 2-to-2 change (remove and rebuild id 10 in one go) working. `UiManager_Request` returns an existing request for the same id: if the old request is still queued, the new page could be the old, removed one. The first implementation step tests exactly this (see "Order of work").
- `LayoutWrapper_AttachTo` with a standalone part layout (`Gm_MemoryList`) under a page is untested. A part layout may need its own root size or animation setup.
- Glyphs: the list's text boxes are outside a scene layout, so missing glyphs draw nothing until queued (known from the name tags).
- The map pages call `Navi_SetTitles` themselves (index 0 leaving Memories, 4 entering it). The hook acts on index 0 and 2 only, so Memories keeps its title, and leaving Memories gets Back = Jukebox like any map title.
- The audible preview: `PlayZoneBgm` from the pause menu has not been tried. The sandbox is muted (SDL dummy audio), so only the BGM manager's state can be checked there; the sound needs a check on real audio by the user.
- **Event songs that replace the zone's music.** The override swaps only the zone music's calls, and two events play a zone-like track with PlayBGM and never call PlayZoneBGM after it:
  - the washed-up sword (`SinkingSword.bfevfl`): `StopAllBGM`, the item-get, then `BGM_FIELD_NORMAL_INTRO`;
  - the new game's wake-up (`PlayerStart.bfevfl`): `BGM_HOUSE_FIRST`.

  After either, the event's track plays instead of the chosen one until the next level change (seen in the sandbox, test `jukebox_sword`). Every other event PlayBGM in the RomFS is a song the event means to play (bosses, the owl, the telephone, Marin's songs, fanfares). The fix is in `ap_music.cpp`, not the pane: swap these two labels in the event action's call (return address `0x805bc4`) as the zone calls are.
- The CloseOldUi state is entered inline (the update writes the state bytes itself), so the post-hook must check the state byte rather than rely on `Navi_ChangeState`.

## Open questions

1. Should a choice apply at once (an audible preview in the menu) or when the menu closes?
2. Should the choice persist past the client session (a host setting or a save flag), or stay per session as `/music` is?
3. What should the track names be? The table needs names a player recognises; some tracks have no obvious place (`BGM_DANPEI`, `BGM_CHICKEN_HURT`).
4. Should there be a Jukebox in the Chamber Dungeon, whose third page is id 0xb?
5. Should X on the Jukebox do something (e.g. "Game music")?
6. Should the chosen track also replace the event songs that stand in for the zone's music (the washed-up sword's `BGM_FIELD_NORMAL_INTRO`, the wake-up's `BGM_HOUSE_FIRST`)? `jukebox_sword` expects it to.

## Order of work and size

| Part | Work | Size |
|---|---|---|
| Spike | A test-build `DBG pane <0-3>` that drives the hooks; a 2-to-2 change that rebuilds System; check that the page is new (address) and the menu survives 20 switches | 1–2 days |
| Navi hooks | WaitKey, CloseOldUi, Close, flag; L/R through all four pages | 2 days |
| Header | `Navi_SetTitles` hook, the label via the MSBT hook, the guide | 1 day |
| Page | Gm_System setup/update/destructor hooks: hide its content, build and attach `Gm_MemoryList` | 2–3 days |
| List | Rows, scrolling, cursor, scroll bar, glyph queue, the track mark | 2–3 days |
| Names | The name table, the `test_music` check, the user's review | 1 day |
| Override | `Music::Choose`, the `MUSIC` line to the client, the client side and its test, preview or on-close | 1 day |
| Test and docs | Scripted test, TESTING.md and DESIGN.md | 1–2 days |

In all: about two weeks. The spike settles the biggest risk before any UI work.

### Alternative: a sub-screen of System, as Memories is of the map

The Jukebox could be a choice on System (its hidden fourth row, `L_SystemChoice_03`) that opens a sub-screen:
- **What it is:** a UI of the module's own over System, with the header retitled to "Jukebox" the way the map retitles it to "Memories".
- **What it saves:** it needs none of the Navi switching hooks and no 2-to-2 rebuild.
- **What it costs:** the Jukebox is then not on L/R. That is the trade-off against the chosen design.

### Fallback

If the page turns out too risky (the spike fails or the menu crashes), the fallback is an overlay opened by holding a button, as the warp menu is opened by holding Minus. It is a `LayoutWrapper_Build` layout (the same `Gm_MemoryList`) drawn above the HUD, with the module's own input. It needs no Navi or page hooks and is about a week.

## Scripted test design

The test `jukebox_pane` runs after `boot_file1`, with the client connected (no server). It is state-gated and fail-fast and uses no screenshots for checks. Test-only code stays under `LASAP_TEST`.

- **Test build additions:**
  - The machine `navi`: Navi's ChangeState `0xee7dc0`, 6 states. Its names are at machine `+0x178 + state*8`, not at `+0x58 + state*0x30`, so `kSmHooks` needs a name-table offset per hook.
  - STATE `pane=<0-3|->`, the page the player sees (3 the Jukebox, `-` with the menu closed).
  - STATE `jbox=<cursor>/<top>`, the list's cursor and its first visible row.
  - `DBG jukebox`: `JUKEBOX` with the flag, the page's address, the rows' texts (to check the names and the glyph queue) and the three header labels.
- **Steps** (each waits on a state, never a delay):
  1. Warp into Tarin's house and walk out (as `jukebox`), so Mabe's track plays (`music=-`).
  2. Tap Plus when `free`; until `navi:WaitKey` and `pane=1`. Tap R; until `pane=2` and `navi:WaitKey`. Tap R; until `pane=3` and `navi:WaitKey`. `DBG jukebox` shows the labels Jukebox / System / Map and row 0 "Game music".
  3. `tapuntil` Down until `jbox=` is on the row of `BGM_FAIRY`. Tap A; await `music=BGM_FAIRY`. Check that the client logged `Music: BGM_FAIRY` (the line reached it). With the preview, also `bgm=BGM_FAIRY:1`.
  4. Tap R; until `pane=0` (the map). Tap L; until `pane=3`. Tap L; until `pane=2` and `choice:KeyWait` (System is whole again).
  5. Tap R to the Jukebox again. Tap B; until `paused=0` and `free`. Check `music=BGM_FAIRY` and `bgm=BGM_FAIRY:1`. Walk through Tarin's door; the track still plays.
  6. Open with Minus; until `pane=0`. Tap L; until `pane=3`. Pick row 0; await `music=-` and `bgm` equal to `zonebgm`. Close with B.
  7. Check that the module logged no page leak: `DBG jukebox` shows no list wrapper with the menu closed.
- **Guards:** `dead`. A guard on `navi` in Close while `pane=3` before step 5 catches a B that closes too early.
- **World tests:** the name table matches `music.TRACKS`, and the client takes `MUSIC` from the game and sends it again on the next connection.
- **Override tests the pane relies on:** `jukebox` (room and level changes, off, unknown names) and `jukebox_sword` (a new game: the washed-up sword's event, which stops all music and plays its own track).
