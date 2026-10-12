# Test harness

How to build, run and observe the LAS mod without manual play. Scripts live in `scripts/`.

## Requirements

- Windows session **unlocked**. On a locked desktop the emulator stops presenting frames and the game loop stalls. Turning the monitors off is fine.
- Docker Desktop with the image `devkitpro/devkita64:latest`.
- Python venvs created with `uv` (Python 3.12):
  - `.venv-ap`: Archipelago requirements (`vendor/Archipelago/requirements.txt`) plus pytest.
  - `.venv-lasr`: LASR requirements (oead, evfl, PyLibMS, PySide6, ...).
- `vendor/Archipelago`: Archipelago 0.6.8, with a junction `vendor/Archipelago/worlds/las` → `apworld/las`.

## Emulator: Ryubing (Ryujinx fork)

- **Binary:** `D:\Games\Ryujinx\Ryujinx.exe` (Ryubing Canary).
- **Game:** `D:\Games\Switch\The Legend of Zelda Links Awakening [01006BB00C6F0000][v0].nsp` (1.0.0). The RomFS dump is at `D:\Games\Switch\LASRomFS`.
- **Sandbox data folder:** `D:\DEV\LASAP-sandbox\ryujinx`, a copy of the user's Ryujinx data (keys, firmware, saves). Tests never use `%APPDATA%\Ryujinx`. It contains `prod.keys`; never copy it into this repo.
- **Logs:** the emulator writes logs to `D:\Games\Ryujinx\Logs` even with `--root-data-dir`.

| Script | Does |
|---|---|
| `emu.py start\|stop\|shot <png>\|log [regex]\|wait <regex> [s]` | Boots the game headless on the sandbox (Vulkan; override with `LASAP_GFX=OpenGl`), stops it by PID, captures the window, reads the newest log |
| `build_exl.sh` | Builds `las-ap-exl` in Docker and copies `subsdk9`/`main.npdm` to `patcher/exl/` |
| `deploy_mod.sh <out dir>` | Installs a patcher output (SD layout) into the sandbox |
| `reset_saves.sh [newgame]` | Restores sandbox saves from the user's saves, or from the new-game snapshot, and clears `sdcard/lasap` |
| `ap.py <Generate.py\|MultiServer.py\|Launcher.py> ...` | Runs an AP entry point from `vendor/Archipelago` in `.venv-ap`; `LASAP_AP` points it at another checkout, such as a clean one with only `las.apworld` in `custom_worlds` |
| `las_client.py [--nogui --connect host:port --name Slot]` | Runs the LAS client without the Launcher |
| `make_test_patch.py <out.aplas>` | Writes a synthetic patch (all locations AP items) for patcher tests |
| `verify_patch.py <patch> <out dir>` | Checks that every shuffled chest holds the placeholder (its model variant) with its local id |
| `test_client.py` / `drive.py <script>` | Talk to the module directly (only when no AP client is connected) |
| `e2e.py send\|srv\|dbg\|cli\|state\|goto\|wait\|tail\|run` | Drives a scripted AP session (below): server `/send`, client `/dbg`, the game's STATE line, hop teleports, `drive.py` scripts through the client |
| `reload_exl.sh` | Restarts the sandbox game with the freshly built module and boots to file 2 |
| `leb_actors.py <room> [regex] [--romfs dir]` | Lists a room's actors with positions (run in `.venv-lasr`; point `--romfs` at the deployed mod for patched rooms) |
| `re/scan.py str\|xref\|xstr\|callers\|dis\|func\|ptr` | Static queries on a memory dump of the game (see "Reverse engineering") |

## Observing and driving the game

- **Guest log.** Module output (`[AP] ...`, `[exlaunch|...]`) goes to the emulator log via `svcOutputDebugString`.
- **Screenshots.** `emu.py shot` uses `PrintWindow`; it works while the window is covered or unfocused.
- **Debug commands** (`DBG ...` on the game socket; `/dbg ...` in the LAS client):

  | Command | Effect |
  |---|---|
  | `state` | Prints level, screen (`intro`, `title`, `menu`, `loading`, `level` or the raw ID), position, health, rupees, key inventory bits, the received counter, the player's event state, the pause flag and the item-get display queue (`+1` while one shows) |
  | `flag <name>` / `setflag <name> 0\|1` | Reads or sets an event flag |
  | `input <buttons hex> <ms> [lx ly]` | Holds buttons and the left stick for a duration |
  | `grant <key> <count> <dungeon> [show 0\|1]` | Same path as `ITEM`, without touching the counter |
  | `text` | Prints the in-game text state: known location names, hooked messages, the last found location |
  | `itemget <key or item id> [message entry]` | Starts the native item-get display directly, bypassing the queue. Model variants by ID (from the patched `Items.gsheet`) |
  | `give <id> <count> <index>` | Raw `Inventory::AddItemID` |
  | `collect <local id>` | Simulates a placeholder pickup |
  | `setrecv <n>` / `clearchecks` | Resets the counter or the checks file |
  | `pos <x> <y> <z>` | Teleports within the current level and sets the respawn point there. Ignored while Link is in some states (wading against a wall, events) |
  | `warp <level> <x> <y> <z>` | Reloads a level at a position (the game's restart path). Verified for `Field`, dungeons (`Lv01TailCave`) and interiors (`TownWell`). Only from another level: a warp within the current level does not reload it and leaves Link at the origin. Some targets spawn Link at the origin too; warp to Mabe and use `e2e.py goto` from there |
  | `peek <p\|f\|i\|a\|c> <hex off> <hex len>` | Dumps bytes of the player, framework, inventory, or the last item-get action and its control block |
  | `poke <hex off> <hex bytes>` | Writes bytes into the inventory, e.g. `poke 88 ffffffffffff0300` sets seashells 0–49 |
  | `dumpmain` | Writes the loaded main module to `sd:/lasap/main.bin` |

  Button bits follow `nn::hid::NpadButton`: A `1`, B `2`, L+R `c0`, Plus `400`, D-pad left/up/right/down `1000`/`2000`/`4000`/`8000`. The stick range is ±32767.
- **Inputs.** Hold at least 400 ms in dialogue; shorter presses are dropped. The title needs L+R held 500 ms and takes it once it has faded in, about 2.5 s after the intro is skipped. Menu inputs during a fade are dropped.
- **Buttons in tests.** B is the sword (holding it charges a spin). Dismiss text with B next to signs and NPCs: A also starts a new "Check" or "Talk". Git Bash rewrites arguments that start with `/`; set `MSYS_NO_PATHCONV=1` for `e2e.py cli /las`.
- **Scripted AP session.** Run the client and server with stdin fed from a file (`tail -n0 -f cmds.txt | ...`) so `/dbg`, `/las`, `/game` and server `/send` commands can be appended while they run. `e2e.py` writes to these files in the folder `LASAP_E2E` names (default `D:\DEV\LASAP-sandbox\e2e`). `/game <line>` sends a raw protocol line, e.g. a hand-made `ITEM` or `NAME`. For texts naming another player, generate a two-slot seed (a second slot with a small game such as ChecksFinder) and send its items with `/send` or `/release`. Start MultiServer with `--release_mode goal --collect_mode goal` to keep a goal from releasing every location.
- **Script ops.** `e2e.py run` scripts (`scripts/tests/*.txt`) send lines and check the game between steps, so a failed step stops the script within seconds:
  - `send`, `wait`, `mash`, `expect <regex|HELLO> [s]`: the `drive.py` basics.
  - `state`, `shot <png>`: print the STATE line, capture the window.
  - `screen <name> [s]`: wait for a screen. `press <buttons> <ms> <screen> [tries]`: press until that screen shows.
  - `free [s]`: wait until the player can move and no item-get shows or waits. `loaded [s]`: the same in a level entered after the last `send`. `inlevel [s]`: a level entered after the last `send` has loaded.
  - `dismiss [s]`: press B until the player is free.
  - `warp <level> <x> <y> <z>`: reload a level there (through another level when it is the current one) and wait. `goto <x> <y> <z>`: hop there within the level. `at <x> <z> [tol]`: stop unless the player stands there.
- **Test scripts.**
  - `boot_file2` and `newgame_file2` boot to file 2. The new-game one ends free in Tarin's house, with Marin's lines closed by B.
  - `beach_chest` (lifts the rock in front of the chest), `island_bush` (cuts a bush), `diamond_island` (walks into a free-standing item) and `itemget_models` (the AP model next to a rupee) each check a kind of location and save screenshots in the sandbox.
- **New-game snapshot.** `D:\DEV\LASAP-sandbox\save-newgame` is the sandbox save folder right after file 2 was created (file select, file 2, Normal, OK on the name entry) and before any play. `reset_saves.sh newgame` restores it, so new-game tests skip the name entry. The file-select cursor starts on the last file used.
- **Overworld coordinates.** Screen `Field_<row><col>` (col A=0) is around x = 15·col + 7.5, z = 12·(row − 1) + 6.75. Mabe at Marin's door is (38.25, 10.5, 128.25); in front of the shop is (51.75, 10.5, 117), walk up to enter.
- **Warps and pits.** Warp into a dungeon or house from `Field`: after a warp out of `TownWell` (Link standing in water) the screen can stay black until the emulator restarts. `pos` also sets the respawn point, so a `pos` over a pit makes Link fall again on every respawn until he dies; reset with a warp to another level and back.
- **Health.** `poke 84 1c00` sets health to 7 hearts (quarter hearts, u16).
- **Tarin's gift.** The detain scene on leaving the house only stops Link; talk to Tarin (A at about (9.4, 0, 6.0) in his house) to get the gift.
- **Finding a location.** LASR's `Randomizers/*.py` map locations to rooms (`CHEST_ROOMS`, `HEART_ROOMS`, ...); `leb_actors.py` prints the actors there. Teleport one tile (1.5) south of a chest and press A facing up. A `pos` into a room that is not loaded lands on no floor; `e2e.py goto` hops through the rooms in between, and `warp Field ...` reloads the overworld at the target.

## Reverse engineering

- `DBG dumpmain` writes the decrypted, relocated main module from memory (runtime base 0x8504000 on Ryubing). Keep it in `D:\DEV\LASAP-sandbox\re`, outside the repo.
- `re/scan.py` finds strings, the code that loads a string's address, and callers of a function, using module offsets (the ones `InstallAtOffset` takes). Event actions are dispatched by name, so `xstr <action name>` leads to the action's handler.
- `DBG peek` snapshots taken in different game states, compared byte by byte, found the player's event state and the pause flag.

**Offsets used by the module (game 1.0.0)**

| Offset | What |
|---|---|
| `0xd795d0` | Item-get sequence: `(shared_ptr* out, Player*, int itemId, bool keepCarry, StringView* messageEntry)`, the body of `Link.GenericItemGetSequenceByKey` |
| `0x14311f0` | libc++ `__shared_weak_count::__release_weak` |
| Player `+0x4ed` | u8, 0 while the player is free; 22 during talk, events and cutscenes |
| Framework `+0x510` | u8, 1 while the pause menu is open or a level loads |
| Item-get action `+0x38` | u32, 0x2a once the item-get text is closed |
| Level-load data `+0xdc` | Restart position, used by `warp` |
| `0x10aaf40` / `0x10aa5a0` | MSBT label lookup and text getter, hooked for the AP texts |
| `0xa35994` | In `ShellMansionMaster.PopBox`, the call that creates the present: `x3` = item key view, `w4` = item index |
| `0x148b10` | Collected check `(kind, count, index)` that item actors use to hide: kind 8 tests heart piece bit `index`, kind 9 seashell bit `index` |
| Framework `+0xb0` | u8 screen: 2 intro and title, 5 file menus, 0x0b level load, 0x17 level |
| Framework `+0x6d0` | Pointer, set only while the intro plays |

## Verified (2026-10-12, Ryubing Canary 1.3.340, game 1.0.0)

- **Boot.** The module boots with LASR's randomizer hooks; the socket listens on 43384.
- **Generation and patching.** Generate → patch → deploy → new game works end to end.
- **Packaged world.** `las.apworld` built with "Build APWorlds" generates in a clean Archipelago checkout (`custom_worlds`), and its Launcher starts "Links Awakening Switch Client". The patcher runs in a fresh venv made from `patcher/requirements.txt`.
- **Client install.** Opening an `.aplas` from the clean checkout's Launcher, or with the double-click command, opens the client on its Patch tab.
  - With a running Eden and no emulator set, it filled in Eden and the game from Eden's recent files, and installed nothing.
  - With Eden set, it installed into Eden's `load` folder, and the game in Eden reported the seed.
  - With Ryujinx and the sandbox SD card, it found the game in the sandbox's game folders, installed in about 6 s, and started Ryujinx with `--root-data-dir` and the game. Opening the same seed again skipped patching.
  - The clean checkout's `host.yaml` has the RomFS dump and patcher command; tests that set an emulator use the sandbox SD card or Eden (installed for testing).
- **Multi-game generation.** Eight seeds with two LAS slots on default options plus Hollow Knight, The Messenger, TUNIC and ChecksFinder generate and write their patch files.
- **Default options.** A new save on default options has the starting gear (sword, shield, ocarina, Manbo's Mambo); received progressive swords and shields upgrade it.
- **First check.** Tarin's gift (the first check) reaches the server as a location check.
- **Shop.** Buying the placeholder in the Mabe shop charges the price, plays the item get and reports the slot's location (`shop-slot3-1st`).
- **Chests.** Opening `beach-chest` gives the placeholder and the server logs the check.
- **Offline checks.** Checks are stored in the save's checks file and reach a server that had not seen them on the next connection.
- **Received items.** Items from the server are applied in-game: progressive sword/shield/bracelet, feather, seashells, rupees, small key, tunic, heart container, golden leaf and a trap. A burst of 240 items also applied.
- **Receiving in any state.** Items are applied at once in the pause menu, during signs, NPC talk, Marin's song cutscene and a door transition, and are held back on the title screen and file select. Items sent while the game was closed, or while the client was down, arrive on the next load or connection.
- **Item-get display.** Each received item plays the game's item-get (pose and text) once the player is free: after the pause menu closes, after a dialogue or cutscene ends, after a level has loaded. Displays run one at a time; Link cannot move during one; A closes the text without starting a talk. A long queue skips filler.
- **Marin's song.** The song gift is a check (`Mabe Village - Marin`).
- **Heart pieces.** The Mabe Well and Pothole North pieces appear on a save that has received heart pieces, and report their checks. A received piece still counts toward a heart.
- **Dug seashells.** The doghouse dig gives its item with all 50 seashell bits set, and reports its check.
- **Bush seashells.** Cutting the plant beside the Seashell Mansion drops its item, which names Bob's item and reports the check.
- **Seashell Mansion.** The 5, 15 and 30 shell presents name their items ("You found Map Width for Bob!") and report their checks.
- **Item names in text.** A received item from another slot says "You got Hookshot from Bob!". A dungeon chest holding this slot's item says "You found your Stone Beak (Tail Cave)!" and the item is not shown a second time when it comes back from the server. The shop's price tag names the item and its owner, and a heart piece holding another slot's item says "You found Map Bombs for Bob!".
- **Persistence.** The received count survives a restart without double grants. On a fresh save, everything is re-delivered.
- **Instruments.** The Full Moon Cello pedestal gives the placeholder, names its item and reports its check.
- **Release.** `/release` of another slot delivers its items in a burst, each saying "from <player>".
- **Goal.** `ShadowClear` reports the goal to the server.
- **Item models.** Chest item-gets, bush drops and free-standing items show the model of the item they hold: this slot's Piece of Heart and Wind Marimba, another slot's unmatched item as the Archipelago model (six balls in the logo's colors with a soft white rim light, centered in the item-get glow like a rupee).
- **Patch data.** `scripts/verify_patch.py` checks that every shuffled chest holds the placeholder with the right index, and that every randomized heart piece and seashell actor has its own index above the received range (static check).

## Not yet verified in-game

- Owl statues and tree seashells as locations.
- Model variants in shop shelves and Seashell Mansion presents.
- Dungeon floor small keys and boss drops. They use paths that are verified elsewhere: floor keys are small key actors with a `SmallKey.bfevfl` entry, like the bush and dug seashells; boss drops use `item_get_manager.get`, like the instruments.
- DeathLink (out of scope for now) and real hardware.

## Building the exlaunch module

The module is `las-ap-exl` (from Owen-Splat/exlaunch, GPL-2.0). Run `bash scripts/build_exl.sh`.

## Why Ryubing, not Eden

Eden (yuzu fork) offers no decisive advantage (assessed 2026-10-12):
- Randovania's Dread exlaunch socket mod officially supports only Ryujinx.
- Eden defaults to `airplane_mode=true`, which breaks TCP sockets until it is changed.
- Eden's TAS player runs only in the Qt frontend.
