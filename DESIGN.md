# LAS Archipelago: design

How the pieces fit and the contracts between them. FEASIBILITY.md covers the background; TESTING.md covers the harness.

## Components

| Dir | What | Language / runtime |
|---|---|---|
| `apworld/las/` | Archipelago world (`Links Awakening Switch`) plus PC client | Python, AP's venv (`.venv-ap`, Py 3.12) |
| `patcher/` | Builds the romfs/exefs mod from an AP patch file; derived from LASR (MIT) | Python: pure Python plus `bsdiff4` for patching (`requirements.txt`); oead, quicktex and Pillow only to build the assets (`requirements-assets.txt`) |
| `las-ap-exl/` | exlaunch module (subsdk9) derived from Owen-Splat/exlaunch (GPL-2.0) | C++, Docker `devkitpro/devkita64` |

The module runs LASR's hooks and its code patches (`patches.hpp`), except two of LASR's fixes (`lasrOnlyFixes`): the `CompareInt` event rewrite, which only LASR's own keysanity chest flows use, and item actors spawning by actor switch, which would undo the inventory-bit hiding of heart piece and seashell actors.

Flow:
1. AP generates a multiworld. The world writes `AP_<seed>_P<n>_<slot>.aplas` (the patch file).
2. The player opens the patch file with the Launcher, which starts the client with it. The client runs the patcher on the patch file plus the user's romfs dump, and installs its output (see Client install).
3. The game runs with the mod. The module listens on TCP port **43384**, or on the port `config.ini` sets (`[archipelago] port`); the client's `--game-port` must match. Players keep the default; the test sandbox uses its own port so it runs beside a modded game.
4. The client connects to the AP server (websocket) and to the game (TCP), and relays checks and items.

## Assets

The files the mod changes the same way for every seed are assets (`patcher/ap_assets.py`): `StartUp.arc` (LASR's title screen logo and the notice look, `ap_ui.py`), `ItemArchipelago.bfres` (`ap_model.py`) and LASR's chest textures (`ObjTreasureBox{Junk,Key,LifeUpgrade}.bfres`, for "Texture + Size"). Building them needs native packages (oead's SARC, quicktex, Pillow). `scripts/build_assets.py` builds them from a RomFS dump and stores each in `patcher/assets` as a bsdiff4 patch against the RomFS file it comes from, with both files' SHA-256 in `assets.json`. The patcher applies the patches to the player's dump and checks the results; a dump of another game version fails with a message naming the file. After a change to `ap_ui.py`, `ap_model.py` or LASR's title screen or chest textures, rebuild and commit the assets.

Datasheets (`.gsheet`) are read and written by `RandomizerCore/Tools/gsheet.py`, a pure-Python version of oead's gsheet module that LASR's `oead_tools` and `flag_manager` use. It writes what oead writes, byte for byte, and copies values stored into structs and arrays as oead does. `scripts/check_gsheet.py` compares the two on every RomFS datasheet and on any given files.

## Release

`scripts/build_release.py` (in `.venv-lasr`, after `build_exl.sh`) writes `build/release/LASAP-<world version>/`: `las.apworld` and the options template `Links Awakening Switch.yaml`.
- The world carries the patcher as `las/patcher/`: its Python files, LASR's code and data (and the resources it reads), the assets, the game module, and in `lib/` the pure-Python libraries it needs (evfl, PyLibMS, appdirs, with their licenses). `build.txt` holds a hash of all of it. The world's own build ("Build APWorlds") is the same without it.
- The patcher runs in Archipelago's Python: pure Python (Assets) plus PyYAML and bsdiff4, which Archipelago ships. LASR imports PySide6 only for its GUI thread and signals; `patcher/lasr_qt.py` stands in for it. `las_ap_patch.py` sets LASR's paths (`randomizer_paths`) to its own folder.

## Client install

- The client's Patch tab (`client_gui.py`) edits the settings in `host.yaml` under `las_options` (`host_settings.py`): `emulator`, `game_file`, `romfs_folder`, `sd_card_folder`, `mod_folder`, `patcher_command`, `game_host`. `checklist.py` turns them into one status line per setting and decides whether everything needed to install is valid.
- `emulators.py` derives the rest from the emulator's executable:
  - Ryujinx: data folder `portable` next to the executable, else the per-user `Ryujinx` folder. SD card `<data>/sdcard`, mods `<SD card>/atmosphere/contents/01006BB00C6F0000`, game folders from `Config.json`.
  - Yuzu and its forks (Eden, Citron, Sudachi, Suyu, Torzu): data folder `user` next to the executable, else the per-user folder named after the emulator. `config/qt-config.ini` gives the SD card (`sdmc_directory`), mods (`load_directory`, plus `01006BB00C6F0000/Archipelago`; their SD card's `atmosphere/contents` loads only RomFS mods), RomFS dumps (`dump_directory`), game folders and recent files.
  - The game is the `.nsp` or `.xci` whose name holds the title id, or whose PFS0 file list holds the base game's ticket.
  - An SD card folder in the settings overrides the emulator's; for Ryujinx, its parent is the data folder, passed as `--root-data-dir` when it is not the default.
  - When no emulator is set, the client takes a running one (Windows, with Ryujinx's `--root-data-dir`), and opens the Patch tab instead of installing.
- Install (`install.py`): with `patcher_command` empty, the client runs the patcher in its own process, on a worker thread: the release world's `las/patcher`, unpacked once per `build.txt` into Archipelago's cache folder (`las/patcher/<build>`), or the repository's `patcher/` when the world runs from source. It imports `las_ap_patch` fresh for each run and calls `main([<patch>, "--romfs", <dump>, "--out", <temp dir>])`, then drops the patcher's modules again, as the patcher keeps state in module globals. The patcher thread's printed lines go to the client's log. With `patcher_command` set, the client runs that command with the same arguments as a subprocess and logs its output. It then replaces `romfs` and `exefs` in the mod folder and copies `config/lasr-exl/config.ini` to the SD card, config last.
- The mod is current when the installed `config.ini`'s `[archipelago] seed_hash` matches the patch and both mod folders exist; then Install and play skips patching. Other Link's Awakening mod folders the emulator would also load are reported.
- Opened with a patch file and a known emulator, the client installs, starts the emulator with the game (unless it runs), and connects to the patch's `server` as its `player_name`.
- `file_types.py` registers `.aplas` for the current user (`HKCU\Software\Classes`), opening with the Archipelago Launcher, which hands the file to the client. It runs only from the Patch tab's button.

## Item model: remote items

- Every randomized location in the patched game gives the **AP placeholder item**, with `index` = the location's **local id** (AP location id − `LOCATION_BASE`).
- The module's `Inventory::AddItemID` hook sees the placeholder item id, reports a check, and adds nothing to the inventory.
- Every real item, including the player's own, arrives from the server and is granted by the module. The client uses `items_handling = 0b011`: starting inventory is granted by the patch (LASR's starting gear), so the server does not send it again.
- Locations whose category is not shuffled keep their vanilla content. They are AP event locations (no id) with the vanilla item locked, and the patcher leaves them alone.
- Each location shows a model for the item it holds. The world picks it (`models.py`):
  - LAS items show their own model.
  - Other games' items are matched by name against `data/item_models.yml` (e.g. any shield shows the shield).
  - Traps and unmatched names show the Archipelago model.
- The patch file's `location_models` carries these picks.
  - The patcher gives each such location a variant of the placeholder, `ArchipelagoItem_<item key>`. It shows the item's model on the ground and in shops.
  - Each variant gets an `Items.gsheet` row with its own item ID, because the item-get picks its model by ID. IDs come from 72–99, 103, 107, 109, 118, 119, 132–199 and 203–255, which neither the game nor LASR uses. That is more IDs than LASR has models; should they run out, the most used models keep theirs and the rest fall back to the placeholder's ID 71.
  - Item IDs are a byte. The item-get finds the row by searching `Items.gsheet` for the ID, and handles every one of these IDs the same way (the default case of its switch on the ID).
  - The module counts all these IDs as the placeholder (`AP::IsPlaceholderItem`).
  - The Chamber Stone gets no variant (`NO_VARIANT_ITEM_KEYS`): a room with a chest holding one crashes the game as it loads. Those locations show the Archipelago model.
- The Archipelago model is six balls in the logo's colors. The patcher builds it from the player's Seashell Sensor model (`patcher/ap_model.py`) and writes it as `ItemArchipelago.bfres`, with its own Npc entry, `ItemArchipelago`.
- Indexed items (heart pieces, seashells, golden leaves, heart containers) are granted at the lowest free index of their inventory bitfield. Received heart pieces stay below index 32 and seashells below 50, the game's totals.
- Item actors hide themselves while the inventory bit of their index (`parameters[0]`) is set: `ItemHeartPiece` checks the heart piece bits, `ItemSlimeKey` (LASR's static seashells) the seashell bits. The index must be unique within a level. The patcher gives every randomized heart piece and seashell actor its own index above the received range (LASR's converted misc items keep heart piece 51–54), so their visibility depends only on LASR's flag.

## IDs

- `ITEM_BASE = 0x4C410000` and `LOCATION_BASE = 0x4C420000`. The AP id is the base plus a stable offset.
- **Stable offsets.** They live in `apworld/las/data/ids.json` (`{"items": {lasr_name: offset}, "locations": {lasr_location: offset}}`).
  - The file is generated once from LASR's data in file order and committed.
  - New names are appended; existing offsets never change.
- **Local id.** A location's local id is its offset, which is the `index` stored in-game. It must stay below 65536.

## Names

- **AP display names** are human-readable, e.g. `Progressive Sword`, `Small Key (Tail Cave)`, `Tail Cave - West Hallway Chest`.
- **LASR names** (`sword`, `key-D1`, `D1-west-hallway`) are used in the patch file and in `ids.json`.
- The world keeps the mapping between the two.

## Patch file (`.aplas`)

An Archipelago player container (`container.py`, `APPlayerContainer`): a zip with `archipelago.json` (game, player, player name, server; WebHost fills in the room's server) and `patch.json`. The patcher also accepts a bare `patch.json`. `patch.json` is UTF-8 JSON:

```json
{
  "format": 1,
  "seed_name": "<AP seed name>",
  "player": 1,
  "slot_name": "Link",
  "seed_hash": "<8 hex chars; also in slot_data; the game reports it in HELLO>",
  "settings": { "<LASR setting key>": "<value>" },
  "starting_items": ["<lasr item name>"],
  "placements": { "<lasr location name>": "<lasr item name> | \"ap-item\"" },
  "location_ids": { "<lasr location name>": "<local id (int)>" },
  "location_names": { "<lasr location name>": ["<AP item name>", "<owner's name, empty for this slot>"] },
  "location_models": { "<lasr location name>": "<lasr item name whose model it shows>" },
  "location_chests": { "<lasr location name>": ["<LASR item type: chest size>", "<chest texture or null>"] },
  "dungeon_entrances": { "<dungeon>": "<dungeon>" }
}
```

- `settings` uses LASR's `SettingsManager.fetch()["Settings"]` keys.
- `placements` covers every LASR item location: shuffled ones are `"ap-item"`, vanilla ones carry their vanilla item. Owl statues that give no gift are left out, as in LASR.
- `location_names` covers the AP locations. The patcher uses it for the shop's price tags; in-game text gets the same names at runtime from the client.
- `location_models` leaves out locations that show the Archipelago model. It is optional: older patches show the Archipelago model everywhere.
- `location_chests` gives each AP location's chest the size and texture of LASR's "Chest Types" setting (option `chest_appearance`): for this slot's items, the LASR item's type and `chest-type` with the adjustments LASR's shuffler makes; for other players' items, `important` for progression, `good` for useful, and `junk` with the junk texture for filler and traps (`lasr_settings.chest_look`). It is optional.
- `dungeon_entrances` is an identity map; entrance shuffle is not supported.

## slot_data

`{"seed_hash": str, "goal": str, "death_link": bool, "location_count": int}`

## Game ↔ client protocol (TCP 43384, game listens)

ASCII lines terminated by `\n`, fields separated by single spaces. The game accepts one client at a time. The client sends `PING` every 2 s, and the game drops a client that has been silent for 10 s.

- The game listens and the client connects, so a Switch needs no address of the PC. The network thread waits for a client with `Poll` in 100 ms steps rather than a blocking `Accept`, which would keep emulators from closing the game.
- The client drops a connection that gets no `HELLO` within 5 s. While it cannot reach the game, it looks for several programs listening on the port (two emulators running the modded game) and reports them in the Patch tab, which also shows the game connection.
- Opened with a patch file, or given one in the Patch tab, the client logs in as the patch's slot.

**Client → game**

| Line | Meaning |
|---|---|
| `PING` | Keep-alive; the game answers `PONG` |
| `ITEM <n> <item-key> <count> <dungeon> <location> <item name>\|<sender>` | The n-th received item (1-based AP index). `item-key` is the game's item symbol (`SwordLv1`, `SmallKey`, …). `dungeon` is the dungeon level for dungeon items, else `-1`. `location` is the local id the item came from when it is this slot's own, else `-1`. `sender` is empty for own items and items from the server. The game applies it only if `n` = stored count + 1, then answers `RECV <count>` |
| `NAME <local id> <item name>\|<owner>` | What a location holds, from `LocationScouts`; `owner` is empty for this slot's items. Sent for every location on connect |
| `NOTICE <text>` | A toast (see In-game text); `\|` starts a new line. The client sends "Archipelago connected as <slot>" (or a wrong-seed warning) once the game and the slot are both connected, and "Archipelago server disconnected" when the server goes away |
| `SYNC` | Asks the game to send `RECV <count>` and `CHECKS ...` |
| `DEATH` | DeathLink: sets the player's health to zero |
| `MSG <text>` | Logged by the module |
| `DBG <cmd> [args]` | Test-only debug commands (see the module source) |

**Game → client**

| Line | Meaning |
|---|---|
| `HELLO <proto=2> <seed_hash>` | Sent on connect |
| `PONG` | Answer to `PING` |
| `RECV <count>` | Number of items applied, stored in the save as a flag-bit counter |
| `CHECK <local_id>` | A location was collected |
| `CHECKS <id> <id> ...` | Every collected location, sent on connect and on `SYNC` (possibly several lines) |
| `GOAL` | Goal completed: the `ShadowClear` flag was set (also resent on `SYNC`) |
| `DIED` | DeathLink: the player's health reached zero |
| `LOG <text>` | Debug text; `LOG level <name>` on every level change |

Lines that need a loaded save (`ITEM`, `SYNC`, `DEATH`, most `DBG`) wait until the player has been in the current level for 1.5 s; other lines are handled immediately.

**Items with special handling in the module**
- Progressive sword, shield and bracelet arrive as their Lv1 key; the module upgrades when LASR's `*FoundFlag` is set.
- Seashells, heart pieces, heart containers and golden leaves take the lowest unused in-game index.
- Traps (`*Trap`, `$ENEMY`) wait like item displays (below). Unknown keys are logged and counted as applied.

**In-game text**
- A found placeholder says what it is: "You found <item> for <player>!", or "You found your <item>!" for this slot's own items. The patcher writes the names to `config/lasr-exl/ap_names.txt` (`NAME` lines without the command), which the module reads at boot, so this works before the client connects; `NAME` lines replace them. A location without a name says "You found an Archipelago item!".
- **Toasts.** Notices that are not items show in the autosave popup (the "Now saving." text with a spinning icon at the bottom right), which does not stop the game:
  - The module shows `NOTICE` lines from the client, "Archipelago client disconnected" when the client goes away, and, once per boot, "Archipelago client not connected" after 20 s in a level without a client.
  - It calls the game's show function for the popup (autosave mode) and hooks the popup's state changes: when its auto state starts, the module sets the popup's display time and puts the toast into its text pane (the message `UI:SystemSaving`, which the text hook hands out). When the popup is hidden again, the pane gets the game's own text back. A toast waits while the popup is busy, as during a real save; a save during a toast shows the toast's text.
  - The patcher widens the popup's text pane and its text buffer, and replaces the egg icon with the Archipelago logo, white on transparent like the egg (`ap_ui.py`, `StartUp.arc`). The loading screen's spinner shares that texture.
- A received item from another player says "You got <item> from <player>!". Own items found here are not shown again; other own items and server items show the item's native text, except items whose native text names their vanilla source (the Boomerang, the capacity upgrades), which say "You got <item>!".
- The patcher adds the messages `Place:ArchipelagoItem` and `System:APReceived` (with the native item-text window style) and the Item event entry point `APReceived`. The module hooks the game's MSBT label lookup and text getter and hands out the text for these two messages (`ap_text.cpp`).
- The module learns the location from the placeholder's `AddItemID`, so placeholder flows add the item before playing the item-get (LASR's item-get chains and a TreasureBox branch); other items keep LASR's order. That location names the found text until the player is free again.
- The Seashell Mansion's presents add their item after the item-get. The module reads the location from `ShellMansionMaster.PopBox` when the present appears.

**Showing received items**
- The module grants an item the moment its `ITEM` line is handled, and queues a display of it.
- A display is the game's own item-get (Link holds the item up, the item's text shows), played as an event: the patcher adds the `Common.bfevfl` entry `APItemGet`, Link's item-get of the placeholder with the message entry `APReceived`. The module starts that entry the way the game starts its game over, and hooks the item-get code behind `Link.GenericItemGetSequenceByKey` to show the received item and its text instead. The swap is pending from the request until that item-get starts, apart from the event's state: the event takes the player before its item-get runs. As an event, the game locks Link's controls and returns the camera when it ends; an item-get outside an event leaves the camera zoomed in.
- A display starts once the player has been free for 0.6 s (no talk, event, cutscene, pause menu or level load) and holds nothing over his head: an item-get takes a held object out of the game, such as a shop item. Displays run one at a time.
- With three or more displays waiting, filler (rupees, ammo, drops) is granted without a display. The queue is not saved; a restart drops pending displays, not items.
- A trap plays LASR's effect of that trap instead of an item-get, as the `Common.bfevfl` event `AP<trap key>` the patcher builds with LASR's trap flow (animation, damage, autosave), and the autosave popup names the trap and its sender. The damage follows the damage mode; in the normal mode a trap leaves at least a quarter heart (the module raises health first if needed). The Zol Trap has no such flow (in a chest it spawns enemies) and costs one heart.

**Warp menu**
- Holding Minus for a second opens "Return to Tarin's house?" with Warp home and Continue, the `Common.bfevfl` event `APWarpMenu` (a `Dialog.Show` of `System:APWarpMenu`, then `GameControl.RequestLevelJump` to `MarinTarinHouse`, locator `MarinTarinHouse_01A`: inside the house at its door). The cursor starts on Warp home; B moves it to Continue, as on the game's yes/no prompts.
- The module hides Minus from the game while it is held; released sooner, a short press reaches the game, so a tap still opens and closes the map.
- The menu opens only when an item display could start (player free, holding nothing, no display running) and not in levels where a warp would break what runs there: the Wind Fish's Egg, the Chamber Dungeons and their editor (`Panel*`), the fishing pond, the trendy game, the ending and Marin's event levels.

**Ammo ghost** (option `ammo_ghost`, settings key `Ammo Ghost`; `patcher/ap_ghost.py`)
- A ghost stands in Tarin's house once Link has found bombs or the bow, and tops bombs and arrows (of those he has) up to 15 each time Link talks to him. The option is on by default; a patch without the settings key gets him too.
- He is an `NpcGhost` actor added to `MarinTarinHouse_01A`. The follower ghost that used `NpcGhost` never spawns in LASR (the ghost's reward is at his grave), so the patcher rewrites the entry: Tarin's talk settings, a talk trigger on the `Ghost.bfevfl` entry `APAmmoGhost`, and layout conditions on the flags `BombsFoundFlag` (LASR's getting flag for bombs) and `APBowFound` (the bow's getting flag, which the patcher adds). Every added item sets its getting flag. He has no collision: Link walks through him.
- The event shows `Scenario:APAmmoGhost`, then adds `Bomb` and `Arrow` with count 15 and index `AP::AMMO_REFILL_INDEX`, each behind a check of its flag. With that index the module's `AddItemID` hook adds only what is missing to reach the count.

**Persistence**
- **Received-item count:** event flags `APRecv0`..`APRecv15`, read as a 16-bit counter. The patcher adds them to `GlobalFlags.gsheet`.
- **Collected locations:** a bitset file `sd:/lasap/<seed_hash>.chk`, written on each check, so checks made while disconnected are reported later.
