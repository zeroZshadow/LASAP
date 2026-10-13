# Test harness

How to build, run and observe the LAS mod without manual play. Scripts live in `scripts/`.

## Requirements

- Windows session **unlocked**. On a locked desktop the emulator stops presenting frames and the game loop stalls. Turning the monitors off is fine.
- Docker Desktop with the image `devkitpro/devkita64:latest`.
- Python venvs created with `uv` (Python 3.12):
  - `.venv-ap`: Archipelago requirements (`vendor/Archipelago/requirements.txt`) plus pytest, and the patcher's `requirements.txt` for the world from source.
  - `.venv-lasr`: the patcher's requirements with the asset tools (`patcher/requirements-assets.txt`: oead, quicktex, Pillow on top of `requirements.txt`).
- `vendor/Archipelago`: Archipelago 0.6.8, with a junction `vendor/Archipelago/worlds/las` → `apworld/las`.
  - Universal Tracker: `tracker.apworld` (FarisTheAncient/Archipelago releases) in `custom_worlds`, with an empty `Players` folder in `vendor/Archipelago` (without it, UT's settings open a folder browser). Headless, `launch("--nogui", "--list", "--name", <slot>, "--connect", <host:port>)` from `worlds.tracker.TrackerClient` logs the in-logic locations at error level; `sys.argv` must contain `--nogui` before the import.

## Emulator: Ryubing (Ryujinx fork)

- **Binary:** `D:\Games\Ryujinx\Ryujinx.exe` (Ryubing Canary).
- **Game:** `D:\Games\Switch\The Legend of Zelda Links Awakening [01006BB00C6F0000][v0].nsp` (1.0.0). The RomFS dump is at `D:\Games\Switch\LASRomFS`.
- **Sandbox data folder:** `D:\DEV\LASAP-sandbox\ryujinx`, a copy of the user's Ryujinx data (keys, firmware, saves). Tests never use `%APPDATA%\Ryujinx`. It contains `prod.keys`; never copy it into this repo. `emu.py` keeps it silent with SDL's dummy audio driver, as `--audio-volume 0` does not mute.
- **Logs:** the emulator writes logs to `D:\Games\Ryujinx\Logs` even with `--root-data-dir`.
- **Sharing the sandbox.** There is one sandbox (data folder, `emu.pid`, port 43385, the e2e folder), so sessions working side by side take turns. Before deploying, resetting saves or starting the emulator, take it with `emu.py lock <owner>`; run those steps with `LASAP_SANDBOX_OWNER=<owner>`, and `emu.py unlock <owner>` when the test run is over. `emu.py start`/`stop`, `deploy_mod.sh` and `reset_saves.sh` refuse while another owner holds the lock. With the lock free they work as before.

| Script | Does |
|---|---|
| `emu.py start\|stop\|shot <png>\|log [regex]\|wait <regex> [s]\|lock <owner>\|unlock <owner>\|check` | Boots the game headless on the sandbox (Vulkan; override with `LASAP_GFX=OpenGl`), stops it by PID, captures the window, reads the newest log, takes and gives back the sandbox lock. Its window opens without focus and stays behind other windows, so the user can keep working or playing |
| `build_exl.sh` | Builds `las-ap-exl` in Docker and copies `subsdk9`/`main.npdm` to `patcher/exl/` |
| `deploy_mod.sh <out dir>` | Installs a patcher output (SD layout) into the sandbox, with the sandbox game port (`LASAP_GAME_PORT`, default 43385) in `config.ini` |
| `reset_saves.sh [newgame]` | Restores sandbox saves from the user's saves, or from the new-game snapshot, and clears `sdcard/lasap` |
| `ap.py <Generate.py\|MultiServer.py\|Launcher.py> ...` | Runs an AP entry point from `vendor/Archipelago` in `.venv-ap`; `LASAP_AP` points it at another checkout, such as a clean one with only `las.apworld` in `custom_worlds` |
| `las_client.py [--nogui --connect host:port --name Slot]` | Runs the LAS client without the Launcher |
| `check_gsheet.py [--romfs dir] [files]` | Compares the pure-Python datasheet code with oead on every RomFS datasheet and the given files (values and bytes) |
| `build_assets.py [--romfs dir]` | Rebuilds `patcher/assets` (DESIGN.md, Assets) from a RomFS dump, in `.venv-lasr` |
| `build_release.py` | Builds the release world with the patcher inside (README, Building a release), in `.venv-lasr` |
| `make_test_patch.py <out.aplas>` | Writes a synthetic patch (all locations AP items) for patcher tests |
| `verify_patch.py <patch> <out dir>` | Checks that every shuffled chest holds the placeholder (its model variant) with its local id, that `ap_indices.txt` lists each indexed source once, that `ap_vanilla.txt` lists every kept location with its item or companion flag, with the map tracker on that every location has an icon, and below eight required instruments that the egg's event checks single instruments |
| `test_client.py` / `drive.py <script>` | Talk to the module directly on the sandbox port (only when no AP client is connected) |
| `e2e.py send\|srv\|dbg\|cli\|state\|goto\|wait\|tail\|run` | Drives a scripted AP session (below): server `/send`, client `/dbg`, the game's STATE line, hop teleports, `drive.py` scripts through the client |
| `reload_exl.sh` | Restarts the sandbox game with the freshly built module and boots to file 2 |
| `leb_actors.py <room> [regex] [--romfs dir]` | Lists a room's actors with positions (run in `.venv-lasr`; point `--romfs` at the deployed mod for patched rooms) |
| `map_positions.py [<pack>]` | Writes `patcher/map_positions.json`, the map tracker's positions with heights (DESIGN.md, Map tracker), from the RomFS rooms and the PopTracker pack's pins, in `.venv-ap`; prints the pin fit's error |
| `poptracker_map.py <pack>` | Writes the Archipelago id tables (`scripts/autotracking/*_mapping.lua`) of the LASR PopTracker pack (Br00ty/lasr_brooty) and checks every code against the pack, in `.venv-lasr` |
| `re/scan.py str\|xref\|xstr\|callers\|dis\|func\|ptr` | Static queries on a memory dump of the game (see "Reverse engineering") |

## Working in a worktree

Sessions that change code work in their own git worktree and branch, so they never share a working tree or index. `vendor/`, the venvs and `patcher/exl/` are gitignored and exist only in the main checkout; `scripts/worktree.sh` (run in the worktree) covers the rest:

- `worktree.sh setup` adds an Archipelago worktree at `vendor/Archipelago` (from the main checkout's, at its commit) with `worlds/las` pointing at this worktree's world, and copies the built module into `patcher/exl/`. The venvs are used from the main checkout by path (`<main>/.venv-ap/Scripts/python.exe`, `<main>/.venv-lasr/...`); nothing links to them, so deleting a worktree cannot reach them. `ap.py`, `las_client.py`, `build_exl.sh` and `reload_exl.sh` work from the worktree.
- `worktree.sh test [pytest args]` runs the world's tests on this worktree's world.
- `worktree.sh land` fast-forwards `master` in the main checkout to this branch, then removes the Archipelago worktree. It refuses when the worktree has uncommitted changes, when `master` has moved on (bring the branch up to date with `master`, rerun the tests, land again), or when the main checkout has uncommitted edits in files the branch changes (another session is working there: land after it has committed). Its other uncommitted edits are left alone.
- `worktree.sh teardown` removes the Archipelago worktree without landing.

The sandbox is shared with every other session: take the lock (Emulator, "Sharing the sandbox"). A local MultiServer outside a sandbox run uses a port of its own, not the default 38281.

## Observing and driving the game

- **Guest log.** Module output (`[AP] ...`, `[exlaunch|...]`) goes to the emulator log via `svcOutputDebugString`.
- **Screenshots.** `emu.py shot` uses `PrintWindow`; it works while the window is covered or unfocused.
- **Debug commands** (`DBG ...` on the game socket; `/dbg ...` in the LAS client):

  | Command | Effect |
  |---|---|
  | `state` | Prints level, screen (`intro`, `title`, `menu`, `loading`, `level` or the raw ID), position, health, rupees, key inventory bits, the received counter, the player's event state, the pause flag, whether Link holds something over his head (`carry`) the item-get display queue (`+1` while one shows) and the item the last display showed (`shown`: its id once the item-get has swapped it in, -1 until then) |
  | `flag <name>` / `setflag <name> 0\|1` | Reads or sets an event flag |
  | `input <buttons hex> <ms> [lx ly]` | Holds buttons and the left stick for a duration |
  | `grant <key> <count> <dungeon> [show 0\|1]` | Same path as `ITEM`, without touching the counter |
  | `map [0\|1\|2]` | Prints the map tracker's state: on, overworld groups with unchecked locations/total, groups per look (in logic, partly, out of logic, collected), whether the client sent what is in logic, how often the dungeon map read its flags, icons updated, added icons matched and chests matched, and whether the out-of-logic icons are toggled off. `map 0` turns the tracker off and `map 2` hides the overworld icons (for screenshots) until `map 1` |
  | `text` | Prints the in-game text state: known location names, hooked messages, the last found location |
  | `itemget <key or item id> [message entry]` | Plays the item-get display (the item event) without granting the item or waiting in the queue. Only for looking at item models (variants by ID, from the patched `Items.gsheet`): it skips the checks that hold received items back (talk, held object, pending display), so it can break whatever flow is running. Tests of received items use `grant` or `ITEM` lines |
  | `give <id> <count> <index>` | Raw `Inventory::AddItemID` |
  | `collect <local id>` | Simulates a placeholder pickup |
  | `setrecv <n>` / `clearchecks` | Resets the counter or the checks file |
  | `pos <x> <y> <z>` | Teleports within the current level and sets the respawn point there. Ignored while Link is in some states (wading against a wall, events) |
  | `warp <level> <x> <y> <z>` | Reloads a level at a position (the game's restart path). Verified for `Field`, dungeons (`Lv01TailCave`) and interiors (`TownWell`). Only from another level: a warp within the current level does not reload it and leaves Link at the origin. Some targets spawn Link at the origin too (`GhostHouse`, over a pit); warp to Mabe and use `e2e.py goto` from there, or warp outside the door and walk in. `<level>` is the level name (`GhostHouse`), not the room (`GhostHouse_01A`); a room name crashes the game |
  | `peek <p\|f\|i> <hex off> <hex len>` | Dumps bytes of the player, framework or inventory |
  | `poke <hex off> <hex bytes>` | Writes bytes into the inventory, e.g. `poke 88 ffffffffffff0300` sets seashells 0–49 |
  | `dumpmain` | Writes the loaded main module to `sd:/lasap/main.bin` |

  Button bits follow `nn::hid::NpadButton`: A `1`, B `2`, L+R `c0`, Plus `400`, D-pad left/up/right/down `1000`/`2000`/`4000`/`8000`. The stick range is ±32767.
- **Inputs.** Hold at least 400 ms in dialogue; shorter presses are dropped. The title needs L+R held 500 ms and takes it once it has faded in, about 2.5 s after the intro is skipped. Menu inputs during a fade are dropped.
- **Buttons in tests.** B is the sword (holding it charges a spin). Dismiss text with B next to signs and NPCs: A also starts a new "Check" or "Talk". Git Bash rewrites arguments that start with `/`; set `MSYS_NO_PATHCONV=1` for `e2e.py cli /las`.
- **Sandbox port.** The sandbox game listens on 43385, so it runs beside the user's own modded game on 43384. Start the scripted client with `--game-port 43385`. A sandbox install done by the client (Patch tab) writes no port, so that game uses 43384.
- **Scripted AP session.** Run the client and server with stdin fed from a file (`tail -n0 -f cmds.txt | ...`) so `/dbg`, `/las`, `/game` and server `/send` commands can be appended while they run. `e2e.py` writes to these files in the folder `LASAP_E2E` names (default `D:\DEV\LASAP-sandbox\e2e`). `/game <line>` sends a raw protocol line, e.g. a hand-made `ITEM` or `NAME`. For texts naming another player, generate a two-slot seed (a second slot with a small game such as ChecksFinder) and send its items with `/send` or `/release`. Start MultiServer with `--release_mode goal --collect_mode goal` to keep a goal from releasing every location.
- **Script ops.** `e2e.py run` scripts (`scripts/tests/*.txt`) send lines and check the game between steps, so a failed step stops the script within seconds:
  - `send`, `wait`, `mash`, `expect <regex|HELLO> [s]`: the `drive.py` basics.
  - `state`, `shot <png>`: print the STATE line, capture the window.
  - `screen <name> [s]`: wait for a screen. `press <buttons> <ms> <screen> [tries]`: press until that screen shows; `<screen>` may list alternatives (`loading|level`). `level <name>`: stop unless the player is in that level.
  - `free [s]`: wait until the player can move and no item-get shows or waits. `loaded [s]`: the same in a level entered after the last `send`. `inlevel [s]`: a level entered after the last `send` has loaded.
  - `dismiss [s]`: press B until the player is free.
  - `check <regex>`: stop unless the STATE line matches. `await <s> <regex>`: wait until it matches.
  - `warp <level> <x> <y> <z>`: reload a level there (through another level when it is the current one) and wait. `goto <x> <y> <z>`: hop there within the level. `at <x> <z> [tol]`: stop unless the player stands there.
- **Test scripts.**
  - `newgame_file2` boots to a new game in file 2 (after `reset_saves.sh newgame`) and ends free in Tarin's house, with Marin's lines closed by B. `boot_file1` loads file 1, a played file.
  - Menu presses can be dropped, during the file menu's fade-in and at random. The boot scripts never depend on one press: they repeat A until the game loads, and move the cursor only upwards, where it stops at file 1. `newgame_file2` checks the level it ends in. Both passed 5 runs in a row from a fresh start (2026-10-12).
  - `beach_chest` (lifts the rock in front of the chest), `island_bush` (cuts a bush), `diamond_island` (walks into a free-standing item) `ghost_house_pot` (lifts the Ghost House pot and picks up its item) and `itemget_models` (the AP model next to a rupee) each check a kind of location and save screenshots in the sandbox.
  - `shop_carry` (an item arrives while Link holds a shop item), `itemget_during_talk` (while he talks to the shopkeeper) and `newgame_items` (while he wakes up in a new game; run like `newgame_file2`) check when received items show: each waits, then plays as an event. `traps` plays each trap's effect and checks its damage.
  - `warp_menu` checks that a Minus tap opens and closes the map, and that holding Minus opens the warp menu, where Continue stays and Warp home lands in Tarin's house. `warp_bowwow` warps home with BowWow following (it sets his companion bit and `BowWowJoin` in the sandbox save).
  - `map_tracker` (run after `newgame_file2`, on a mod patched with `Map Tracker` on) checks that every overworld icon shows in a new game, that X toggles the out-of-logic icons without leaving the map, and that opening the Tail Cave map runs the dungeon hooks; screenshots `map_overworld` and `map_dungeon` show the revealed maps.
  - `ammo_ghost` (run after `newgame_file2`, on a mod patched with `Ammo Ghost` on) checks that the ghost in Tarin's house is missing without bombs or the bow, then tops bombs and arrows up to 15 and never past it.
  - `egg_goal` (run after `newgame_file2`, on a mod patched with `Instruments Required` 3) plays the Ballad at the egg: with two instruments it stays shut, with a third walking north enters `Lv09WindFishsEgg`. It sets the ocarina with the Ballad on X in the item menu: down to the ocarina, X opens its songs on the last one, left to the Ballad, X.
- **New-game snapshot.** `D:\DEV\LASAP-sandbox\save-newgame` is the sandbox save folder right after file 2 was created (file select, file 2, Normal, OK on the name entry) and before any play. File 1 and the autosave are a copy of the user's played file. `reset_saves.sh newgame` restores it, so new-game tests skip the name entry. The file-select cursor starts on the last file used, file 2 in the snapshot, and stops at file 1 and the autosave instead of wrapping.
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
| `0xd795d0` | Item-get sequence: `(shared_ptr* out, Player*, int itemId, bool keepCarry, StringView* messageEntry)`, the body of `Link.GenericItemGetSequenceByKey`; hooked to show received items |
| `0x1cc6db8` / `0x1cb24d8` | Globals: resource manager and event manager pointers |
| `0xfbbd80` / `0x3dc20` / `0x3d9f0` | Load a resource `(out handle, manager, StringView* path)`, wait for it, take its flowchart `(out, handle*)` |
| `0x80c290` / `0x80db00` / `0x81f2e0` | Make an event for a flowchart entry `(out, flowchart*, StringView* entry)`, prepare it, request it from the event manager `(out, manager, event*)`; the game's game over does these steps for `event/Common.bfevfl` |
| `0x14311f0` | libc++ `__shared_weak_count::__release_weak` |
| Player `+0x4ed` | u8, 0 while the player is free; 22 during talk, events and cutscenes |
| Player `+0x19b0` | Pointer, set while Link holds something over his head (pot, rock, shop item) |
| Framework `+0x510` | u8, 1 while the pause menu is open or a level loads |
| Level-load data `+0xdc` | Restart position, used by `warp` |
| `0x10aaf40` / `0x10aa5a0` | MSBT label lookup and text getter, hooked for the AP texts and the map tracker's icon names |
| `0xdfa920` | `EventFlags::CheckFlag(flags, StringView* name)`; hooked for the map tracker's flags |
| `0xe76204` | Dungeon map init, after it reads the dungeon's flags byte into `w8` (bit 0 compass, bit 1 map) |
| `0xe95278` | Overworld map KeyWait state, after it reads X (`w21`, page `x19`): the memories button |
| `0xe87840` | Dungeon map's memories button check `bool(page*)` |
| `0xe917c8` | Overworld map icon setup, after a shown icon's marker joined the page's markers (`x19` page, `x26` marker): the markers are a vector of shared pointers at page `+0x710`; a marker's row at `+0x240` (show flag name view at `+0x20`), its layout from virtual `+0x48` (root pane `+0x18`, visible bit 0 of `+0x58`) |
| `0xa57e0` | Play a sound `(StringView* name, nullptr, volume, pitch)` |
| `0xe91c28` / `0xe91ee0` | Memories map marker setup, at `tst x8, x9` for heart piece and seashell row `x21`: `x8` = the inventory's bits, `x9` = `1 << x21` |
| `0xf38040` | Dungeon map icon update `(icon*, actor*)`: shown and open from the actor's switches (`+0x1e`/`+0x20`, `+0x22`/`+0x24`); position at `+0x4`/`+0xc`; the open and closed panes at icon `+0xa0`/`+0xb8` |
| `0x1cc11b0` / `0x1cc13c8` | `DungeonItems` (per dungeon a pointer to its entry: flags byte `+1`, visited-room mask `+8`) and the current dungeon index (0xff outside dungeons) |
| `0xa35994` | In `ShellMansionMaster.PopBox`, the call that creates the present: `x3` = item key view, `w4` = item index |
| `0x148b10` | Collected check `(kind, count, index)` that item actors use to hide: kind 8 tests heart piece bit `index`, kind 9 seashell bit `index` |
| Framework `+0xb0` | u8 screen: 2 intro and title, 5 file menus, 0x0b level load, 0x17 level |
| Framework `+0x6d0` | Pointer, set only while the intro plays |

## Verified (2026-10-12, Ryubing Canary 1.3.340, game 1.0.0)

- **Boot.** The module boots with LASR's randomizer hooks; the socket listens on 43384, or on the `config.ini` port: the sandbox on 43385 ran its boot tests while the user played on 43384.
- **Generation and patching.** Generate → patch → deploy → new game works end to end.
- **Packaged world.** `las.apworld` built with "Build APWorlds" generates in a clean Archipelago checkout (`custom_worlds`), and its Launcher starts "Links Awakening Switch Client". The patcher runs in a fresh venv made from `patcher/requirements.txt`.
- **Archipelago 0.6.7.** On a 0.6.7 checkout the world's tests pass and a seed generates (world from source); the release `las.apworld` loads, patches in the client byte for byte as on 0.6.8, and the client starts without its window (2026-10-12). The client window was not opened; the parts of `kvui` the Patch tab uses are the same in both versions.
- **Patcher in the client.** The release `las.apworld` in a clean Archipelago checkout, with no patcher command and none of the patcher's libraries installed in Archipelago's environment, unpacks its patcher once into the cache and installs, twice in one process, the same files byte for byte as the patcher with oead, quicktex and Pillow did before the assets and the pure-Python datasheets, for a synthetic seed and for one with "Texture + Size" chests (2026-10-12).
- **Notices.** With the client connected to the game only, the game knows all location names from `ap_names.txt`. Connecting the server shows "Archipelago connected as ZeroLAS" in the autosave popup, with the Archipelago logo and both lines of text; the game keeps running. The popup shows "Now saving." again afterwards (`DBG savepopup`).
- **Closing the emulator.** Windowed Ryujinx exits within a few seconds of closing its window, modded or vanilla. Headless Ryujinx (`--no-gui`) ignores a window close either way.
- **Client install.** Opening an `.aplas` from the clean checkout's Launcher, or with the double-click command, opens the client on its Patch tab.
  - With a running Eden and no emulator set, it filled in Eden and the game from Eden's recent files, and installed nothing.
  - With Eden set, it installed into Eden's `load` folder, and the game in Eden reported the seed.
  - With Ryujinx and the sandbox SD card, it found the game in the sandbox's game folders, installed in about 6 s, and started Ryujinx with `--root-data-dir` and the game. Opening the same seed again skipped patching.
  - Patch tab: Play and Reinstall mod are off while an install runs, and the tab ends with the green "Done" line (2026-10-13).
  - The clean checkout's `host.yaml` has the RomFS dump and patcher command; tests that set an emulator use the sandbox SD card or Eden (installed for testing).
- **Multi-game generation.** Eight seeds with two LAS slots on default options plus Hollow Knight, The Messenger, TUNIC and ChecksFinder generate and write their patch files.
- **Connection loss** (two-slot seed with ChecksFinder, items sent with the server's `/send_location`):
  - Client disconnected from the server, another player sends an item: it arrives in game after `/connect`.
  - Client disconnected from the server, a check is collected in game: the server gets it after `/connect`.
  - Client process stopped, a check is collected and another player sends an item: a new client reports the check and delivers the item.
  - The game shows "Archipelago server disconnected" and "Archipelago client disconnected" in the autosave popup, and "Archipelago connected" again on reconnect.
- **Kanalet iron ball soldier.** With all five golden leaves held he still appears; without LASR's patch for him he does not, and his check is lost.
- **Traps from another player.** The autosave popup shows "Squish Trap / from Finder".
- **Warp menu.** Holding Minus opens it in Mabe and in Tail Cave; Warp home lands inside Tarin's house at the door, Continue leaves Link where he is, and a Minus tap still toggles the map (`warp_menu`, 5 runs in a row). BowWow comes along into the house and follows Link out again (`warp_bowwow`).
- **Ammo ghost.** In a new game he is not in Tarin's house; with bombs he appears and fills an empty bag to 15, 10 bombs to 15, and leaves 20 alone; with the bow he tops arrows up too; his line waits for a button (`ammo_ghost`, 2026-10-13). Without the option the patcher leaves Tarin's house unchanged.
- **Trap effects.** Each received trap plays LASR's effect and damage (`traps`): Zap 6, Squish 4, Drown, Deathball and Quake 2 quarter hearts; with a quarter heart left, a Zap Trap leaves a quarter heart.
- **Default options.** A new save on default options has the starting gear (sword, shield, ocarina, Manbo's Mambo); received progressive swords and shields upgrade it.
- **First check.** Tarin's gift (the first check) reaches the server as a location check.
- **Shop.** Buying the placeholder in the Mabe shop charges the price, plays the item get and reports the slot's location (`shop-slot3-1st`).
- **Chests.** Opening `beach-chest` gives the placeholder and the server logs the check.
- **Offline checks.** Checks are stored in the save's checks file and reach a server that had not seen them on the next connection.
- **Received items.** Items from the server are applied in-game: progressive sword/shield/bracelet, feather, seashells, rupees, small key, tunic, heart container, golden leaf and a trap. A burst of 240 items also applied.
- **Receiving in any state.** Items are applied at once in the pause menu, during signs, NPC talk, Marin's song cutscene and a door transition, and are held back on the title screen and file select. Items sent while the game was closed, or while the client was down, arrive on the next load or connection.
- **Item-get display.** Each received item plays the game's item-get (pose and text) as an event once the player is free: after the pause menu closes, after a dialogue or cutscene ends, after a level has loaded, after Marin's lines in a new game, after a held shop item is put back (the item stays in the shop). Displays run one at a time; Link's controls are locked during one, and the camera returns to the room afterwards. A long queue skips filler.
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
- **Online multiworld** (with Super Metroid on archipelago.gg, 2026-10-12/13): a playthrough to the Wind Fish, which reported the goal; the server then released the remaining items. Items went both ways throughout. Checks played by kind: chests (overworld and D1–D4), digs, hidden items (bushes, the Ghost House pot, bonk trees), standing and sunken items, dungeon small key drops, the golden leaf enemies (crow, mad bomber, final guard) and the Kanalet kill room drop, Lanmola's drop, NPC gifts (Tarin, Marin, Manbo, the walrus, the ghost), shop slots and the 5 and 15 shell Seashell Mansion rewards. The 15 shell present showed this slot's Piece of Heart model, and a shop shelf showed another player's Varia Suit as the Red Tunic.
- **Item models.** Chest item-gets, bush drops and free-standing items show the model of the item they hold: this slot's Piece of Heart and Wind Marimba, another slot's unmatched item as the Archipelago model (six balls in the logo's colors with a soft white rim light, centered in the item-get glow like a rupee).
- **Map tracker** (two-slot seeds with ChecksFinder, 2026-10-13). In a new game the overworld map is revealed with an icon per spot with checks; hovering one shows the unchecked locations (`Shop Slot3 2nd, Shop Slot6` after the server's `/send_location` of Slot3 1st). An icon changes look when its location is collected and the map reopens, without a level change. In Tail Cave without map or compass the map shows every room, its chests and the added chest icons, and the sidebar shows no items; an added icon shows open once its location is checked in the game or on the server (`map_tracker`).
- **Map tracker looks** (2026-10-13). In a new game 3 groups are in logic, 1 partly and 89 out of logic; after `/send` of Power Bracelet, Roc's Feather, Pegasus Boots and a sword, 37, 7 and 49; after `/send_location` of Tarin, that group is collected. A Tail Cave chest checked with `/send_location` shows open on the dungeon map.
- **Map tracker icons** (2026-10-13). The dots show in their four looks, groups larger than single checks; collected spots show a grey tick. The top row sits on the Tal Tal ledges and cave entrances (heights in `mPosY`), and every dungeon has a dot beside its entrance. With the patched `Game.arc` the title screen, a new game, the item screen and the Tail Cave map work.
- **Received heart pieces and seashells** (2026-10-13, seed with Heart Pieces, Seashells and Golden Leaves off). Before the fix, a received heart piece and seashell took bit 0, which vanilla seashell #0 sets when collected: a raw add of seashell index 0 (`DBG give 22 1 0`) left the seashells at one, and the Memories map marked heart piece #0 and seashell #0 in Mabe Village. With `ap_indices.txt` they take bits 22 and 2, the lowest of shuffled sources; the raw add of index 0 then counts (`shells=5`). The Memories map marks seashell #0 only, and the Seashell Mansion's heart piece once its location is checked on the server. The overworld map draws none of these marks.
- **Unrandomized locations and companions** (2026-10-13, Seashell Mansion 0, Heart Pieces off, Trade Quest on; `companions`). The patch settings follow the options (Rapids, Blue Rupees, Heart Pieces off, Mansion 0); rapids, blue rupee and heart piece locations are no checks; seashell chests are plain ("Seashells Important" off); `ap_vanilla.txt` lists the Yoshi Doll and the two companion flags, and `RoosterBones.bfevfl` sets `RoosterFoundFlag` right after the rooster's `Join`. Setting `BowWowJoin` and `RoosterFoundFlag` sends Moblin Cave and Rooster Statue to the server. The companions' events themselves were not played.
- **Starting items and hole positions** (2026-10-13). With the Ocarina in the slot data's `starting`, the client sends Marin as in logic before any item arrives (`test_tracker`). `map_positions.py` puts Mabe Well at its hole (`AreaHoleLower`, height 9.35); the map shows its dot there, named Mabe Well. Four other checks that took height 0 from a `MapStatic` mesh moved to their ground height.
- **Trendy Game prizes as checks** (2026-10-13, `kept_items`). `verify_patch.py` finds the seven prizes in `ap_vanilla.txt`. Adding the second heart piece prize (`DBG give 58 1 25`) and the Yoshi Doll (`DBG give 29 1 -1`) as the game does sends Trendy Prize 2 and Trendy Prize 1 to the server; the heart piece that comes back is not granted again (`hp` keeps bit 25 only) and `TradeYoshiDollGet` is set. The crane itself was not played in the sandbox: a won prize sets `TradeYoshiDollGet`, which only the `AddItemID` hook sets for trade items, so the crane's prizes pass through that hook.
- **Unshuffled locations as checks** (2026-10-13, seed with Heart Pieces, Seashells and Golden Leaves off). `verify_patch.py` finds the placeholder at every heart piece and seashell actor, and only the Trendy Game's heart pieces and seashells as vanilla sources. Cutting the Pond Island bush (`island_bush`) shows "You found your Secret Seashell!" with the seashell's model; the server logs the check and sends the seashell back (`shells=1 recv=1`).
- **Connection reminders** (2026-10-13, scripted client, no server at first). With the client connected and no server, `DBG map` reports `server=0`, the toast "Archipelago server not connected" shows about 20 s into the level, and the map caption reads "Offline: logic unknown" over all-green dots. After `/connect`: "Archipelago connected", `server=1`, logic. With the server process killed: "Archipelago server disconnected", `server=0`, and the caption "Offline: toggle out of logic" over the last logic's dots.
- **Out-of-logic toggle** (2026-10-13). On the overworld map X hides the red dots and shows them again without leaving the map; closed while hidden, the map opens without them. Y (hide markers) leaves them hidden. X on the Tail Cave map opens no memories. The caption reads "Toggle out of logic" with the X glyph (`map_tracker`).
- **Chamber Stone variant.** A chest holding it (key cut to 31 characters) loads its room, and the item-get shows the stone (2026-10-13).
- **Instruments Required** (2026-10-14). With 1 required, each instrument alone (granted with the Ballad in a new game) opens the egg after the Ballad and Link enters `Lv09WindFishsEgg`; with none it stays shut. So `HasItem` itemType 45–52 are Full Moon Cello to Thunder Drum. The song shows only the instruments held. With 3 required, two instruments leave it shut and a third opens it (`egg_goal`). `verify_patch.py` finds no `HasAllInstruments` left below eight.
- **Patch data.** `scripts/verify_patch.py` checks that every shuffled chest holds the placeholder with the right index and a key of at most 31 characters, and that every randomized heart piece and seashell actor has its own index above the received range (static check).

## Not yet verified in-game

- Owl statues as locations.
- The dungeon bosses' and the Armos Knight's drops, and the Kanalet bombed guard. Boss drops use `item_get_manager.get`, like Lanmola's and the instruments.
- The ending with fewer than eight instruments. Statically: no event of the egg dungeon or the ending (`WindFish.bfevfl`) checks instruments, and `ending_part3` sets `ShadowClear` without conditions.
- DeathLink (out of scope for now) and real hardware.

## Building the exlaunch module

The module is `las-ap-exl` (from Owen-Splat/exlaunch, GPL-2.0). Run `bash scripts/build_exl.sh`.

## Why Ryubing, not Eden

Eden (yuzu fork) offers no decisive advantage (assessed 2026-10-12):
- Randovania's Dread exlaunch socket mod officially supports only Ryujinx.
- Eden defaults to `airplane_mode=true`, which breaks TCP sockets until it is changed.
- Eden's TAS player runs only in the Qt frontend.
