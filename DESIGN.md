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

The files the mod changes the same way for every seed are assets (`patcher/ap_assets.py`): `StartUp.arc` (LASR's title screen logo and the notice look, `ap_ui.py`), `ItemArchipelago.bfres` (`ap_model.py`), `Game.arc` (the map tracker's icons, `ap_map_icons.py`; installed only with the map tracker) and LASR's chest textures (`ObjTreasureBox{Junk,Key,LifeUpgrade}.bfres`, for "Texture + Size"). Building them needs native packages (oead's SARC, quicktex, Pillow). `scripts/build_assets.py` builds them from a RomFS dump and stores each in `patcher/assets` as a bsdiff4 patch against the RomFS file it comes from, with both files' SHA-256 in `assets.json`. The patcher applies the patches to the player's dump and checks the results; a dump of another game version fails with a message naming the file. After a change to `ap_ui.py`, `ap_model.py`, `ap_map_icons.py` or LASR's title screen or chest textures, rebuild and commit the assets.

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
- The mod is current when the installed `config.ini`'s `[archipelago] seed_hash` matches the patch, both mod folders exist, and, for the built-in patcher of a release world, `config/lasr-exl/patcher_build.txt` names its `build.txt`; then Play skips patching (`mod_state`). Reinstall mod patches regardless. Other Link's Awakening mod folders the emulator would also load are reported.
- Opened with a patch file and a known emulator, the client installs, starts the emulator with the game (unless it runs), and connects to the patch's `server` as its `player_name`.
- `file_types.py` registers `.aplas` for the current user (`HKCU\Software\Classes`), opening with the Archipelago Launcher, which hands the file to the client. It runs only from the Patch tab's button.

## Item model: remote items

- Every randomized location in the patched game gives the **AP placeholder item**, with `index` = the location's **local id** (AP location id − `LOCATION_BASE`).
- The module's `Inventory::AddItemID` hook sees the placeholder item id, reports a check, and adds nothing to the inventory.
- Every real item, including the player's own, arrives from the server and is granted by the module. The client uses `items_handling = 0b011`: starting inventory is granted by the patch (LASR's starting gear), so the server does not send it again.
- A location the options do not randomize gives its default item (`lasr_settings.unrandomized_locations`). It is a check only when the logic can need that item (`lasr_settings.default_item_matters`: an item the rules ask for; seashells only while a Seashell Mansion reward is randomized; never the Mansion's unrandomized rewards, whose level-2 sword the rules always have alternatives for). Such a location is an AP location holding its default item, locked (`LASWorld.locked`): it holds the placeholder, reports a check, can be hinted, and the item arrives from the server. Any other (`LASWorld.plain`) is no AP location, the patcher leaves it alone and the logic leaves its item out, so the player never needs it.
- Seashells are progression only while a Mansion reward is randomized (`LASWorld.classification`), and only then do their chests look important ("Seashells Important").
- The Trendy Game's prizes and the companions cannot hold the placeholder (LASR does not place items in the crane game; LASR's followers are no item locations). They are never randomized and keep their item in the game (`lasr_settings.kept_locations`); the patch's `placements` give them that item, and the rule above decides which are checks. The companions are the locations `moblin-cave` (Bow-Wow) and `rooster-statue` (the rooster), holding the items Bow-Wow and Flying Rooster, which the rules ask for. The patcher writes `config/lasr-exl/ap_vanilla.txt`: per Trendy Game check the item's key, its index (-1 for none) and the local id; per companion `F`, the flag its joining sets and the local id (`BowWowJoin`; `RoosterFoundFlag`, which the patcher sets after the rooster's `Join` in `RoosterBones.bfevfl`). The module's `Inventory::AddItemID` hook reports a Trendy Game check when the game adds that item with that index (any index for -1), except while the module grants a received item; the module reports a companion once its flag is set. The server then sends the item back; the module does not grant it again when the location was collected here. Their heart pieces and seashells count as vanilla sources in `ap_indices.txt`.
- The shop slots LASR does not shuffle (`lasr_settings.fixed_locations`) keep their vanilla content and are AP event locations (no id) with the vanilla item locked, if it is progression.
- Each location shows a model for the item it holds. The world picks it (`models.py`):
  - LAS items show their own model.
  - Other games' items are matched by name against `data/item_models.yml` (e.g. any shield shows the shield).
  - Traps and unmatched names show the Archipelago model.
- The patch file's `location_models` carries these picks.
  - The patcher gives each such location a variant of the placeholder, `ArchipelagoItem_<item key>`. It shows the item's model on the ground and in shops.
  - Each variant gets an `Items.gsheet` row with its own item ID, because the item-get picks its model by ID. IDs come from 72–99, 103, 107, 109, 118, 119, 132–199 and 203–255, which neither the game nor LASR uses. That is more IDs than LASR has models; should they run out, the most used models keep theirs and the rest fall back to the placeholder's ID 71.
  - Item IDs are a byte. The item-get finds the row by searching `Items.gsheet` for the ID, and handles every one of these IDs the same way (the default case of its switch on the ID).
  - The module counts all these IDs as the placeholder (`AP::IsPlaceholderItem`).
  - A room with a chest whose item key has 32 characters or more crashes the game as it loads (`std::length_error`). Variant keys longer than 31 characters are cut short and end in a hash of the item key (`variant_key`).
- The Archipelago model is six balls in the logo's colors. The patcher builds it from the player's Seashell Sensor model (`patcher/ap_model.py`) and writes it as `ItemArchipelago.bfres`, with its own Npc entry, `ItemArchipelago`.
- Indexed items (heart pieces, seashells, golden leaves, heart containers) are counted by inventory bits: collecting one sets the bit of its source (its vanilla index, `index` in `logic.yml`), and the count is the number of bits set. The patcher writes `config/lasr-exl/ap_indices.txt`: per source its kind (`S`, `H`, `L`, `C`), its bit and the local id of its location when shuffled, else -1. A received item takes the lowest bit that is free and that no vanilla source of the seed sets, so a vanilla source collected later still counts. Received heart pieces stay below index 32 and seashells below 50, the game's totals.
- The Memories map marks heart piece and seashell bit i at the vanilla place of source i (rows of `UiMemoryHearts.gsheet` and `UiMemoryShells.gsheet`). Received items hold the bits of shuffled sources, so the module (`ap_memory.cpp`) marks source i once it is done instead: a vanilla source by its bit, a shuffled one by its location's check.
- Item actors hide themselves while the inventory bit of their index (`parameters[0]`) is set: `ItemHeartPiece` checks the heart piece bits, `ItemSlimeKey` (LASR's static seashells) the seashell bits. The index must be unique within a level. LASR gives every heart piece and seashell actor a flag, shuffled or not, and the patcher gives each of these actors its own index above the received range (LASR's converted misc items keep heart piece 51–54), so their visibility depends only on LASR's flag.

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
  "dungeon_entrances": { "<dungeon>": "<dungeon>" },
  "hints": { "<hint source>": { "text": "<hint text>", "player": "<slot of the location (int)>", "location": "<AP location id (int)>" } }
}
```

- `settings` uses LASR's `SettingsManager.fetch()["Settings"]` keys (`lasr_settings.lasr_settings`), as the options set them. "Bad Pets" is always off: it is not supported.
- `placements` covers every LASR item location: AP locations are `"ap-item"`, the kept and fixed ones carry their vanilla item. Owl statues that give no gift are left out, as in LASR.
- `location_names` covers the AP locations. The patcher uses it for the shop's price tags; in-game text gets the same names at runtime from the client.
- `location_models` leaves out locations that show the Archipelago model. It is optional: older patches show the Archipelago model everywhere.
- `location_chests` gives each AP location's chest the size and texture of LASR's "Chest Types" setting (option `chest_appearance`): for this slot's items, the LASR item's type and `chest-type` with the adjustments LASR's shuffler makes; for other players' items, `important` for progression, `good` for useful, and `junk` with the junk texture for filler and traps (`lasr_settings.chest_look`). It is optional.
- `dungeon_entrances` is an identity map; entrance shuffle is not supported.
- `hints` gives each in-game hint source its text (In-game hints). `player` and `location` name the hinted location when reading the hint makes a server hint. It is optional and empty with the option off.

## slot_data

`{"seed_hash": str, "goal": str, "death_link": bool, "location_count": int, "settings": {...}, "logic": {...}, "options": {...}, "starting_instruments": [...]}`

`settings` holds the seed's LASR settings that decide which checks exist and what reaches them (`TRACKER_SETTINGS` in `lasr_settings.py`), under LASR's names and values (`"Preset": "Basic"`, `"Owl Gifts": "None"`, `"Seashell Mansion": 15`, ...), as the options set them. Trackers such as PopTracker packs read them on connect.

`logic` is what the client needs to evaluate the world's rules for the map tracker (`tracker.py`): `preset` (the logic option), `tokens` (the settings tokens the rules test), `vanilla` (fixed locations that keep a progression item, whose items count as found once they are reachable), `locked` (AP locations holding their own progression item, whose items count as found once they are reachable, until the location is checked and the received item counts instead) and `starting` (the slot's starting items: the patch grants them, so the server does not send them).

`options` (the world's own option values, `SLOT_DATA_OPTIONS`) and `starting_instruments` (the LASR instruments the slot starts with) let Universal Tracker regenerate the slot without a YAML: the world sets `ut_can_gen_without_yaml`, `interpret_slot_data` returns the slot data, and `generate_early` takes the options and starting instruments from `multiworld.re_gen_passthrough` instead of the YAML and the seed's random. Universal Tracker asks the server for the starting inventory (items_handling `0b111`), which the LAS client leaves out. `test_universal_tracker.py` checks that the regenerated slot has the same regions, locations, locked items and reachability. Universal Tracker sweeps only event locations, so it counts a locked location's item once the server sends it (after the check); the LAS client counts it once the location is reachable.

## Data storage

The client keeps the level the game shows (`Field`, `Lv01TailCave`, ...) in `las_level_<team>_<slot>`, from the game's `LOG level` lines. Trackers watch it to show the current dungeon's map.

## Game ↔ client protocol (TCP 43384, game listens)

ASCII lines terminated by `\n`, fields separated by single spaces. The game accepts one client at a time. The client sends `PING` every 2 s, and the game drops a client that has been silent for 10 s.

- The game listens and the client connects, so a Switch needs no address of the PC. The network thread waits for a client with `Poll` in 100 ms steps rather than a blocking `Accept`, which would keep emulators from closing the game.
- The client drops a connection that gets no `HELLO` within 5 s. While it cannot reach the game, it looks for several programs listening on the port (two emulators running the modded game) and reports them in the Patch tab, which also shows the game and server connections.
- Opened with a patch file, or given one in the Patch tab, the client logs in as the patch's slot.

**Client → game**

| Line | Meaning |
|---|---|
| `PING` | Keep-alive; the game answers `PONG` |
| `ITEM <n> <item-key> <count> <dungeon> <location> <item name>\|<sender>` | The n-th received item (1-based AP index). `item-key` is the game's item symbol (`SwordLv1`, `SmallKey`, …). `dungeon` is the dungeon level for dungeon items, else `-1`. `location` is the local id the item came from when it is this slot's own, else `-1`. `sender` is empty for own items and items from the server. The game applies it only if `n` = stored count + 1, then answers `RECV <count>` |
| `NAME <local id> <item name>\|<owner>` | What a location holds, from `LocationScouts`; `owner` is empty for this slot's items. Sent for every location on connect |
| `NOTICE <text>` | A toast (see In-game text); `\|` starts a new line. The client sends "Archipelago connected as <slot>" (or a wrong-seed warning) once the game and the slot are both connected, and "Archipelago server disconnected" when the server goes away |
| `SERVER 1` / `SERVER 0` | Whether the client is logged into the slot of the game's seed: sent when the game connects, when the slot connects and when the server goes away |
| `SYNC` | Asks the game to send `RECV <count>` and `CHECKS ...` |
| `CHECKED <id> <id> ...` | Locations the server has as checked, for the map tracker: all on connect (possibly several lines), then new ones from `RoomUpdate`. Kept in memory only |
| `INLOGIC CLEAR` / `INLOGIC <id> <id> ...` | Locations in logic for the items received, for the map tracker: `CLEAR`, then all of them (possibly several lines), each time the set changes. Kept in memory only |
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
| `HINT <slot> <location id>` | An in-game hint was read (its flag is set): once per hint and connection, and all read hints again on `SYNC` |
| `GOAL` | Goal completed: the `ShadowClear` flag was set (also resent on `SYNC`) |
| `DIED` | DeathLink: the player's health reached zero |
| `LOG <text>` | Debug text; `LOG level <name>` on every level change and when a client connects |

Lines that need a loaded save (`ITEM`, `SYNC`, `DEATH`, most `DBG`) wait until the player has been in the current level for 1.5 s; other lines are handled immediately.

**Items with special handling in the module**
- Progressive sword, shield and bracelet arrive as their Lv1 key; the module upgrades when LASR's `*FoundFlag` is set.
- Seashells, heart pieces, heart containers and golden leaves take the lowest in-game index that is unused and that no vanilla source sets (Indexed items, above).
- Traps (`*Trap`, `$ENEMY`) wait like item displays (below). Unknown keys are logged and counted as applied.

**In-game text**
- A found placeholder says what it is: "You found <item> for <player>!", or "You found your <item>!" for this slot's own items. The patcher writes the names to `config/lasr-exl/ap_names.txt` (`NAME` lines without the command), which the module reads at boot, so this works before the client connects; `NAME` lines replace them. A location without a name says "You found an Archipelago item!".
- **Toasts.** Notices that are not items show in the autosave popup (the "Now saving." text with a spinning icon at the bottom right), which does not stop the game:
  - The module shows `NOTICE` lines from the client and "Archipelago client disconnected" when the client goes away. While the client or the server (`SERVER`) is not connected, it shows "Archipelago client not connected" or "Archipelago server not connected" once the player has been 20 s in a level, then every 5 minutes; after a disconnect notice, the first reminder comes 5 minutes later.
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

**Map tracker** (option `map_tracker`, settings key `Map Tracker`; `patcher/ap_map.py`, `ap_map.cpp`)
- The pause map shows the slot's unchecked locations, and the whole overworld and every dungeon room are revealed. Off for a patch without the settings key.
- **Positions.** `patcher/map_positions.json`, written by `scripts/map_positions.py`, gives each location a place: overworld locations world coordinates (x, z), dungeon locations a room and level coordinates. Sources, in order: the location's item actor in an overworld room (LASR's `*_ROOMS` tables); for a location inside a building or cave (a room table, or `DOOR_LEVELS` for NPCs, shops and games), the overworld door into it (`AreaLevelOpen`), or the hole that drops into it (`AreaHole`, as into Mabe's well); for an NPC or statue outside (`ACTOR_PREFIXES`), its actor; otherwise the pin of the LASR PopTracker pack (Br00ty/lasr_brooty), converted by an affine fit through the locations with an actor. Where there are several doors or actors, the one nearest the pack's pin counts. Dungeon locations use the item actor or owl statue in their room, else the room's upper left. Overworld positions carry the height (y) of their actor or door, or of the overworld actor nearest to them (not counting each room's static mesh, `MapStatic`, at height 0): the map draws higher places further north, as the game's own icon rows do with their `mPosY`. Each entry also has the AP location name.
- **Overworld icons.** The patcher groups the slot's overworld locations that lie within 3 units of each other and adds four `UiFieldMapIcons.gsheet` rows per group, one per look, each with `APMap<mark><group>` as its show flag and its name label: in logic (no mark), partly in logic (`P`), out of logic (`X`) and collected (`C`). Each dungeon also gets a group with all of its checks, 2.5 units east of the game's marker of its entrance (`DUNGEON_MARKERS`). Only the slot's shuffled locations (the patch's `location_ids`) get icons, so options that leave checks vanilla leave them off the map. The map builds its icons each time it opens and shows a row while `EventFlags::CheckFlag` of its show flag is true. The module's `CheckFlag` hook answers these flags: the group's look is set. A location counts as checked in the checks file or by `CHECKED`, and in logic by `INLOGIC`; before the client sends `INLOGIC`, every location counts as in logic. Hovering an icon shows `Place:APMap<mark><group>`; the patcher adds those entries, and the module's text hook replaces them with the group's unchecked locations on one line (the hover box has one), named without their region, in-logic ones first after `<n>/<unchecked> in logic: ` when only some are, as many as fit, then `+<rest>`. A collected icon names the group's locations.
- **Icons.** `ap_map_icons.py` adds the looks to `Game.arc`: dots in PopTracker's colours (green, yellow, red, grey with a tick). The map's marker (`Gm_MapMarker`) shows the frame of its `SetMark` animation given by the row's `mType`; that animation's texture pattern (FLTP) picks a marker texture per frame, and its pane animation (FLPA, size targets 8 and 9) the marker's size. It adds a texture per look to `timg/__Combined.bntx` and eight frames after the game's seven: per look one at the size of a group, then one at the smaller size of a single check (`frame()`). New texture names need the BNTX name dictionary (a Patricia trie, `_DIC`) rebuilt: `build_dictionary()` builds it as the game's files are built, which the asset build checks on the original dictionary first.
- **Logic.** The client evaluates the world's rules (`LogicCompiler`, `logic.py`) for the items received with `tracker.LogicTracker`, from the slot data's `logic`: a state holding item counts and region stand-ins, with regions, vanilla progression items and followers added until nothing changes, as Archipelago's sweep does. `test_tracker.py` checks it against Archipelago's reachability.
- **Overworld reveal.** The map shows an area once its flag `UI_FieldMapTraverse_<area>` is set, and plays the reveal once `UI_FieldMapOpen_<area>` is not; the `CheckFlag` hook answers both as set. `LocationName.gsheet` gates the area name banners on the same flags.
- **Dungeons.** The dungeon map draws the rooms whose bit (column + 8·row) is set in the dungeon's visited-room mask and, with the compass, the chests, open or closed by their opened flag. While Link is in a dungeon the module fills the mask (`DungeonItems[dungeon] + 8`), so every room counts as visited in the save. An inline hook on the map's read of the dungeon's flags byte (bit 0 compass, bit 1 map) sets both for the map screen only; the sidebar still shows the items Link has.
- **Out-of-logic toggle.** The map's X button, which opens the memories, hides and shows the out-of-logic icons instead, on both map pages, and its caption (`UI:MapCaptionMemory`) reads "Toggle out of logic" after the button's glyph tag. While the server is not connected it reads "Offline: toggle out of logic", or "Offline: logic unknown" before the client has sent any `INLOGIC` (every location then shows in logic). The overworld page reads the button inline (`w21` in its KeyWait state), the dungeon page through a check function; the module clears the press. The icon setup creates a marker only for rows whose flag is set and adds it to the page's markers (a vector of shared pointers): out-of-logic rows keep their flag, and a hook after that step hides their marker while the toggle is off. Toggling sets the visible bit of the out-of-logic markers on the open page. The toggle is kept in memory only.
- **Dungeon icons.** Every room with locations that are not chests (key drops, instruments, bosses, owl statues, the Color Dungeon's rupees and fairies) gets a `UiDungeonIcon.gsheet` row with the chest icon (`mActorID` 0xF7). The game makes these icons from the rows like actors whose switches are unset, so the module hooks the icon update and shows the icon open once all of the room's locations are checked. The same hook shows a dungeon chest open once its location is checked on the server.
- **ap_map.txt.** The patcher writes `config/lasr-exl/ap_map.txt` next to `config.ini`: `O <group> <local id>...` per overworld icon, `D <group> <x> <z> <local id>...` per dungeon icon (the module finds an icon by its position), `C <level> <x> <z> <local id>` per dungeon chest of the slot (found by the placeholder and local id in the room's actors) and `N <local id> <short name>`. The module turns the tracker on when the file exists. An install without the option removes it (`install.OPTIONAL_CONFIG`).

**In-game hints** (option `in_game_hints`; `hints.py`, `patcher/ap_hints.py`)
- The hint sources are the 9 overworld owl statues, the 24 dungeon owl statues (by the LASR name of their location) and six of the library's books (`book-<color><n>`, the book actor's name); the map, the egg book and the Color Dungeon's book keep their text. Dungeon owl statues need their dungeon's stone beak, as in the game.
- The world picks the hints once the multiworld is filled (`choose_hints`, with the world's random): each source names a different location, in any world, holding one of this slot's progression items, then useful items, then a joke line. It leaves out the locations whose item is known anyway: unrandomized, kept and locked locations of this slot, items in `start_hints` and locations in `start_location_hints`, seashells, dungeon items in their own dungeon, and an owl statue's own gift for that statue. The text says "Your <item> is at <location>." or "Your <item> is in <player>'s world at <location>.". The spoiler lists the hints.
- The patcher gives each source its own message, `Hint:APHint<n>` for owl statues and `System:APHint<n>` for books (n: the source's place in `ap_hints.SOURCES`), wrapped to 42 characters and 3 lines per box, with the window style of the text it replaces. Owl statues that share an entry point (Tail Cave 04B and 05F, the three Color Dungeon statues) get entries of their own, as LASR's Owl Gifts give them, with their own copy of the statue's events. With Owl Gifts on, the gift follows the hint.
- With `server`, a hint sets the event flag `APHint<n>` after its text (added to `GlobalFlags.gsheet`), and the patcher writes `config/lasr-exl/ap_hints.txt`: per hint `<n> <flag> <slot> <location id>` (`install.OPTIONAL_CONFIG`). The module polls the flags, which the save keeps, and sends `HINT` lines; the client sends `CreateHints` with priority status for each new one, or on servers before 0.6.2 `LocationScouts` with `create_as_hint` 2 for this slot's own locations. It sends them again after each server connection; the server skips hints it has.

**Persistence**
- **Received-item count:** event flags `APRecv0`..`APRecv15`, read as a 16-bit counter. The patcher adds them to `GlobalFlags.gsheet`.
- **Collected locations:** a bitset file `sd:/lasap/<seed_hash>.chk`, written on each check, so checks made while disconnected are reported later.
- **Read hints:** event flags `APHint<n>` (In-game hints).
