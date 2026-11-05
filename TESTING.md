# Test harness

How to build, run and observe the LAS mod without manual play. Scripts live in `scripts/`.

## Requirements

- Windows session **unlocked**. On a locked desktop the emulator stops presenting frames and the game loop stalls. Monitors off is fine.
- Docker Desktop with the image `devkitpro/devkita64`, pinned by digest in `build_exl.sh` (`IMAGE`).
- Python venvs created with `uv` (Python 3.12):
  - `.venv-ap`: `vendor/Archipelago/requirements.txt`, pytest, the patcher's `requirements.txt`, evfl and PyLibMS.
  - `.venv-lasr`: the patcher's `requirements.txt` and `patcher/requirements-assets.txt`.
  - `.venv-relay`: PyInstaller, for `LASGhostRelay.exe` (README, Building a release).
- `vendor/Archipelago`: an Archipelago checkout, with a junction `vendor/Archipelago/worlds/las` → `apworld/las`.
  - Universal Tracker: `tracker.apworld` (FarisTheAncient/Archipelago releases) in `custom_worlds`, with an empty `Players` folder in `vendor/Archipelago`. Headless, `launch("--nogui", "--list", "--name", <slot>, "--connect", <host:port>)` from `worlds.tracker.TrackerClient` logs the in-logic locations at error level. `sys.argv` must contain `--nogui` before the import.
- `scripts/local_paths.json` (gitignored): this machine's paths, with the keys of `scripts/local_paths.example.json`: `romfs` (the RomFS dump), `game` (the game file), `ryujinx` (the emulator), `ryujinx_logs`, `sandbox`, `re` (`main.bin` and the Ghidra project), `poptracker`, `lasr_brooty` (the PopTracker pack). `LASAP_ROMFS` comes before `romfs` and `LASAP_RE` before `re`. A `--romfs` option defaults to `romfs`. World tests that read the RomFS (`test_map_positions.py`) skip without it. `worktree.sh setup` copies the main checkout's file into a worktree.
- PopTracker for `poptracker_test.py`: a copy of the user's in the `app` folder of the `poptracker` folder, without its `packs`, in portable mode (`portable.txt`: everything stays in its `portable-config` folder, never `%APPDATA%\PopTracker`), with `log` on and the update check and EmoTracker question answered in `portable-config\PopTracker\PopTracker.json`.

## Emulator: Ryubing (Ryujinx fork)

- **Binary:** `ryujinx` in `scripts/local_paths.json` (Ryubing Canary).
- **Game:** `game`, the 1.0.0 `.nsp`. The RomFS dump is `romfs`.
- **Sandbox data folder:** `ryujinx` in the sandbox folder (`sandbox`), a copy of the user's Ryujinx data (keys, firmware, saves). Tests never use `%APPDATA%\Ryujinx`. It contains `prod.keys`: never copy it into this repo. `emu.py` silences it with SDL's dummy audio driver.
- **Translation cache:** `emu.py` starts the emulator with `--disable-ptc`. `LASAP_PTC=1` keeps the cache on, as players have it (DESIGN.md, "Code the module writes"). It lives per build in the data folder's `games/01006bb00c6f0000/cache/cpu/0` (`1.0.0-<hash>.cache` and `.info`), with a backup in `cpu/1`. A run from an empty cache needs the build's files removed from both.
- **Logs:** the emulator writes logs to its own `Logs` folder (`ryujinx_logs`) even with `--root-data-dir`.
- **Closing:** headless Ryubing (`--no-gui`) ignores a window close. `emu.py stop` ends it by PID.
- **Playing in the sandbox:** with `LASAP_INPUT=config`, `emu.py start` takes player 1's input from the data folder's `Config.json`. `ghost_relay.py --echo <dx>` sends each player its own frames back as a player `Echo`, `dx` units along x.
- **Sharing the sandbox.** There is one sandbox (data folder, `emu.pid`, port 43385, the e2e folder), so sessions take turns. Before deploying, resetting saves or starting the emulator, take it with `emu.py lock <owner>`, run those steps with `LASAP_SANDBOX_OWNER=<owner>`, and `emu.py unlock <owner>` when the test run is over. `emu.py start`/`stop`, `deploy_mod.sh` and `reset_saves.sh` refuse while another owner holds the lock. The test suite ("Test suite") runs on copies of the sandbox and needs the lock free or held by `LASAP_SANDBOX_OWNER`.

| Script | Does |
|---|---|
| `emu.py start\|stop\|shot <png>\|log [regex]\|wait <regex> [s]\|lock <owner>\|unlock <owner>\|check` | Drives the headless game on the sandbox (Vulkan, `LASAP_GFX=OpenGl` overrides): window capture, newest log, the sandbox lock. `LASAP_INSTANCE=<n>` works on suite instance n ("Test suite"), `e2e.py` too. `LASAP_AUDIO=1` starts it with sound. Its window opens unfocused, behind others |
| `build_exl.sh` | Builds `las-ap-exl` in Docker: the release module to `patcher/exl/`, the test build to `patcher/exl-test/` ("Building the exlaunch module"). Checks that only the test build has the debug port. `--release` builds the release module only |
| `deploy_mod.sh <out dir>` | Installs a patcher output (SD layout) into the sandbox with the test build's `subsdk9`, and the sandbox game port (`LASAP_GAME_PORT`, default 43385) in `config.ini`. Warns when the output is older than the patcher's last commit: patch its `.aplas` again |
| `reset_saves.sh [newgame]` | Restores sandbox saves from the user's saves, or from the new-game snapshot, and clears `sdcard/lasap` |
| `ap.py <Generate.py\|MultiServer.py\|Launcher.py> ...` | Runs an AP entry point from `vendor/Archipelago` in `.venv-ap`. `LASAP_AP` points it at another checkout |
| `las_client.py [--nogui --connect host:port --name Slot]` | Runs the LAS client without the Launcher. Ghosts stay off unless `--ghost-relay` names a relay, so tests never join the public one |
| `ghost_peer.py --relay host:port --secret <ghost secret> [--slot Peer] [--level Field] [--at x,y,z] [--radius r] [--every n] [--seconds s] [--no-stdin]` | A second ghost player for tests (exits after `--seconds`, default 60). Circles a point at 20 Hz, logs `PLAYER`, `FRAME` (first and every 20th per player, or every n-th) and `OFF`. Stdin takes these, explained in its docstring: `goto <level> <x> <y> <z>`, `radius <r>`, `shapes <hex>`, `tunic <n>`, `face <eye> <mouth> <ear>`, `upper <anim id>`, `nowhere`, `burst <every ms> <hold ms>`, `link <jitter ms> <loss %> <stall ms>`, `record <file> [<slot>]`, `record stop`, `play <file>`. Run with stdin from `peer_cmds.txt` (`tail -n 0 -f`), output to `peer.log` |
| `ghost_crowd.py --relay host:port --secret <ghost secret> --crowd "<count> <level> <x0>,<z0>,<x1>,<z1> <y>" ... [--seconds s] [--report s] [--seed n] [--one-address] [--names a,b,...] [--shapes <hex>]` | A crowd of ghost players for load tests, each from its own loopback address (the relay takes 16 per address), running between random points of its rectangle. Prints `READY <n>`, then `STATS` (frames sent and received, players in each one's area) |
| `ghost_puppet.py install\|uninstall [--romfs dir] [--contents dir]` | Puts the ghosts' model (`PlayerGhost.bfres`) and the `APGhost` rows (`Npc.gsheet`, `Facial.gsheet`) into the sandbox mod, for a mod deployed from an older patcher output. `uninstall` takes them out. In `.venv-lasr` |
| `burst.py <out dir> <seconds> [--at s] [--dbg "<command>"] [--peer "<command>"] [--crop x0,y0,x1,y1] [--fps n] [--video file.mp4]` | Captures the emulator's window faster than the game's 60 frames a second, as `<index>_<ms>.png`, and sends a `DBG` or peer command `--at` seconds in. `--video` writes H.264 through ffmpeg instead: use it for a few seconds of play, because a long PNG capture takes minutes. In `.venv-lasr` |
| `deathlink_peer.py --connect host:port --name Slot` | A second DeathLink player for tests, a text-only client. `/die [cause]` sends a death, received ones log as `DeathLink: <cause>`. Run with stdin from `peer_cmds.txt`, output to `peer.log` in the e2e folder |
| `model_bounds.py <file.bfres>[:<model>] ... [--ref <file.bfres>[:<model>]] [--shapes]` | Bind-pose bounds of BFRES models. With `--ref`, the scale that gives each model the reference's radius. The source of `g_HeldModelFixes` (DESIGN.md, Showing received items) |
| `drop_bounds.py [--romfs dir]` | Writes the drop actor's model bounds into the module (`ap/drop_bounds.hpp`, DESIGN.md, Drop actor, Model). Run it after an `items.yml` item's model or the Archipelago model changes, then build the module. In `.venv-lasr` |
| `check_gsheet.py [--romfs dir] [files]` | Compares the pure-Python datasheet code with oead on every RomFS datasheet and the given files |
| `build_assets.py [--romfs dir] [--check]` | Rebuilds `patcher/assets` (DESIGN.md, Assets) from a RomFS dump. `--check` compares with the committed assets instead of writing them. In `.venv-lasr` |
| `build_release.py` | Builds the release world with the patcher inside (README, Building a release), in `.venv-lasr` |
| `build_relay.py [<out dir>]` | Builds `LASGhostRelay.exe` from `relay_exe.py`, in `.venv-relay`. `build_release.py` runs it when that venv exists |
| `track_lengths.py` | Writes each Jukebox track's play-through length into `music_tracks.hpp` (DESIGN.md, Jukebox page) |
| `local_paths.py <key>` | Prints a key of `scripts/local_paths.json`, for shell scripts |
| `make_test_patch.py <out.aplas> [--seed-hash <hash>] [--slot <name>] [--settings JSON] [--dungeons JSON] [--models loc=item,...] [--names loc=item[\|player],...] [--starting item,...]` | Writes a synthetic patch with the locations the world gives every seed, for patcher tests and the asset build. The seed hash defaults to a hash of the patch, so a mod's `seed_hash` names its patch. Its docstring explains the options |
| `verify_patch.py <patch> <out dir>` | Static checks of a patcher output against its patch, listed in its docstring: chests, `models`, map icons, the egg's event, `config.ini`, `shop`, `hints`, `start`, `jumps`, `markers` |
| `test_client.py` / `drive.py <script>` | Talk to the module directly on the sandbox port (only when no AP client is connected) |
| `suite.py [-j <instances>] [--no-world] [<group> ...]` | Runs the test suite on several emulators at once, with the world's tests beside it ("Test suite") |
| `e2e.py send\|srv\|dbg\|cli\|state\|goto\|wait\|trace\|tail\|run` | Drives the game through the debug port and a scripted AP session (below). Its docstring lists the commands. `dbg` prints only the first reply line that matches `e2e.py`'s `REPLY`, so `PUPPET`, `STATS` and other such replies need a script's `expect` |
| `reload_exl.sh` | Restarts the sandbox game with the freshly built test module and boots to file 1 |
| `leb_actors.py <room> [regex] [--near x z [tiles]] [--romfs dir]` | Lists a room's actors with positions, in `.venv-lasr`. Point `--romfs` at the deployed mod for patched rooms. `--near` lists those within that many tiles (default 2) of a point, nearest first, with their offset in tiles |
| `map_positions.py [<pack>] [--romfs dir]` | Writes `patcher/map_positions.json` (DESIGN.md, Map tracker) from the RomFS rooms and the PopTracker pack's pins, in `.venv-ap`. Prints the pin fit's error |
| `poptracker_map.py <pack>` | Writes the Archipelago id tables (`scripts/autotracking/*_mapping.lua`) of the LASR PopTracker pack (Br00ty/lasr_brooty) and checks them against the pack, in `.venv-lasr` |
| `poptracker_test.py <pack> [--variant var_itemsonly] [manual\|full\|marks\|lean\|shells ...]` | Tests the pack's auto-tracking in the sandbox PopTracker against a local MultiServer (port 38297), with the newest seed in the PopTracker folder's `out` (slots Full, Lean and Shells from its `players` folder, Shells' `seashells_required` a `random-range`). The test hook `poptracker_test.lua` goes into PopTracker's `user-override` folder, never into the pack. Screenshots go to its `shots`. In `.venv-ap` |
| `re/scan.py str\|strs\|xref\|xstr\|callers\|dis\|func\|ptr\|sms\|actors` | Static queries on a memory dump of the game (see "Reverse engineering") |
| `re/decomp.sh <out.c> <hex offset>[:<hex word>]...` | Decompiles functions of the dump with Ghidra headless, named from `re/las.sym` (see "Reverse engineering") |

## Working in a worktree

Sessions that change code work in their own git worktree and branch. The venvs exist only in the main checkout and are used from there by path (`<main>/.venv-ap/Scripts/python.exe`, `<main>/.venv-lasr/...`). `scripts/worktree.sh` (run in the worktree) sets up the rest:

- `worktree.sh setup` adds an Archipelago worktree at `vendor/Archipelago` with `worlds/las` pointing at this worktree's world, and copies the main checkout's built modules with their build times. Run `build_exl.sh` in the worktree when `build_release.py` or `suite.py` shows a module older than the source. `ap.py`, `las_client.py`, `build_exl.sh` and `reload_exl.sh` work from the worktree.
- `worktree.sh test [pytest args]` runs the world's tests on this worktree's world.
- `worktree.sh land <subject> [<line> [<line>]]` puts this branch on `master` as one commit with that message, then removes the Archipelago worktree. When `master` has moved on it rebases the branch and stops: rerun the tests, land again (on conflicts: resolve, `git rebase --continue`, test, land). It refuses when the worktree has uncommitted changes, when the main checkout is not on `master`, or when the main checkout has uncommitted edits in files the branch changes (land after it has committed).
- `worktree.sh teardown` removes the Archipelago worktree without landing.

Take the sandbox lock (Emulator, "Sharing the sandbox"). A local MultiServer outside a sandbox run uses a port of its own, not the default 38281.

## Test suite

`scripts/suite.py` (in `.venv-ap`) runs the sandbox tests of `scripts/tests/suite.txt` on several emulator instances (`-j`, default 4), and the world's tests beside them (`--no-world` skips them). Names of groups run only those. It needs the lock free or held by `LASAP_SANDBOX_OWNER`, the suite's mod installed in the sandbox (`deploy_mod.sh`) and this checkout's test module built (`build_exl.sh`). It refuses a test module older than the module's source. The suite's mod comes from this checkout's patcher, with the `make_test_patch.py` patch of `suite.txt`'s `mod:` options: `suite.py` makes that patch (`suite/suite.aplas` in the sandbox folder), refuses to run when the installed `config.ini`'s `seed_hash` differs and prints the commands that patch and install it.

- **Groups.** A line of `suite.txt` is a group: `<group> [<section>.<key>=<value> ...] [<seed sections>.txt ...] [client|ghost]: <script> ...`. Its scripts run in order in one fresh emulator and session, each leaving the game as the next expects, from the new-game snapshot's saves with the group's `config.ini` options. Each of its files in `scripts/tests/seed` (`shuffles.txt`) replaces the seed file's sections it holds. `client` starts the LAS client fed from the instance's `cmds.txt`. `ghost` also starts a ghost relay and `ghost_peer.py --secret test` fed from `peer_cmds.txt`, and joins the client to the relay.
- **Instances.** Instance n (1 and up) lives in the sandbox folder's `suite\<n>`: `ryujinx` (a copy of the sandbox's data folder, made on the first run; delete it to copy again), `emu.pid` and `e2e`. Its game port is 43385 + 10·n (the debug port 100 above, the relay 6 above). Each run copies in the sandbox's `Config.json`, mod and the checkout's `subsdk9`.
- **Output.** A line per group: PASS or FAIL, the instance, each script's seconds, a failure's log end. Logs go to `suite\logs\<group>.<script>.log` and `world.log`. `suite\times.json` keeps each group's last time, and the longest groups run first.
- **Timing** (2026-10-23, 715 s of group time): 188 s with 4 instances, 168 s with 5, the world's tests (115 s) beside them.
- **Game speed.** The emulators run at the Switch's rate. With Ryubing's `--vsync-mode Unbounded`, scripts that poll STATE from Python miss short states and the timing checks lose their meaning.

## Observing and driving the game

- **Guest log.** Module output (`[AP] ...`, `[exlaunch|...]`) goes to the emulator log via `svcOutputDebugString`.
- **Screenshots.** `emu.py shot` works while the window is covered or unfocused.
- **Debug commands** exist only in the test build ("Building the exlaunch module"): `DBG ...` on the debug port (the game port + 100, 43485 in the sandbox) or the game socket, `/dbg ...` in the LAS client (`/dbg`, `/game`, `/patch`, `/play`, `/start`, `/setting` and `/setup` exist in a client run from source, not in the release world). The debug port answers fastest.

  | Command | Effect |
  |---|---|
  | `state` | Level, screen (`intro`, `title`, `menu`, `loading`, `level`, `other`, `none`: FRAMEWORK.md, Scenes), position, health, rupees, key bits, `dkeys` (per `LevelIndex` 0–9, `<keys>.<bits>`, bits 1 compass, 2 map, 4 stone beak, 8 nightmare key), `dlevel` (255 outside dungeons), received counter, event state, `paused`, `carry` (held over Link's head), `showq=<n>` (item-get queue, `+1` while one shows), `shown` (the last display's item, -1 until it shows), `popup`, `toast` |
  | `flag <name>` / `setflag <name> 0\|1` | Reads or sets an event flag |
  | `input <buttons hex> <ms> [lx ly]` | Holds buttons and the left stick for a duration |
  | `grant <key> <count> <dungeon> [show 0\|1]` | Same path as `ITEM`, without touching the counter |
  | `map [0\|1\|2]` | Map tracker state: on, overworld groups unchecked/total, groups per look, client sent logic, flag reads, room icons updated, dungeon dots made, chest icons hidden, out-of-logic toggle, `rings=<shown>/<made>[,<group>...]`, `skipped=<n>`, `hover=<group>/<locations>/<look>` (`in`, `partly`, `out`, `collected`, `-1` none). On the open overworld page `made=` and `drawn=<in>,<partly>,<out>,<collected>`. Last, `\| name=` (the name text the cursor last took). `map 0` turns the tracker off, `map 2` hides the overworld icons until `map 1` |
  | `mapdot [cursor\|near\|press <n>]` | `MAPDOT` (DESIGN.md, Map tracker, Dungeon dots): dots made, `page`, the page's dots and those shown, rooms shown, chest icons hidden, floor, cursor, `hover=<index>/<dot>/<locations>`, `at=`, `looks=` per dot (`i` in logic, `p` partly, `o` out, `c` done, capital while hidden), `rings=<shown>/<made>`, last `text=` (hover text). `cursor <n>` puts the cursor on dot n (`near`: 8 units right of it) and runs the hover. `press <n>` takes A on dot n |
  | `flood <lines> <bytes>` | Sends `LOG` lines to the AP client, to fill the socket of a client that reads nothing (`stall`) |
  | `heap` | `OK heap` with `arena` (bytes taken, never given back), `used`, `free`, `size` (`HeapSize` in `setting.hpp`) |
  | `maplabel [group]` | `MAPLABEL` with an overworld icon group's hover text (`O` lines in the seed file's `map`), by default the hovered one |
  | `mapmarker <label>` | `MAPMARKER <label>` with each `UiFieldMapIcons` row of that label as the game holds it: `at=<x>,<z> flag=<show flag>` |
  | `maplist [open <group>]` | `MAPLIST` (DESIGN.md, Map tracker, Check list): lists opened, rows toggled, `open_us`, `tick_us=<mean>/<most>`, `ticks`, `open=0` or `open=1` (`closing` while fading), group, `at` (cursor's location), `top` (first row's), `looks=` per location (`i` in logic, `o` out, `c` checked, `s` skipped), `tags=` (`s` skipped, `h` a hint's item, `-` none), fade, row text boxes and those drawn, `hint=` (cursor row's item, `-` none), `count=`, the name under the cursor, last `title=`. `open <group>` opens that list on the open overworld map's next frame. STATE has `mlist=<group>/<cursor>/<first row>` (`/closing` while fading) or `mlist=-` |
  | `maphint` | `MAPHINT` (DESIGN.md, Map tracker, Hints): the page (`field`, `dungeon`, `-`), whether it shows, last its items |
  | `name` | `PLAYERNAME <name>`, the name the game holds for the save (DESIGN.md, Save name) |
  | `text [label]` | Known location names, hooked messages, the last found location. With a message label (`SBMountTamaranch`), the text last fetched for it without tags: `TEXT <label> said=<text>` or `unseen` |
  | `music [<track>\|off]` | First chooses the track if given. `MUSIC` with the jukebox's STATE fields, `MUSICLOG open=<last stream file>` and the latest BGM requests: `play <name>[-><chosen track>] +<return address> <the three bools> new\|-` (`new`: an instance started), `stop <name> +<return address> <fade frames>`, and the Jukebox's steps (`hold`, `held stop`, `take over`, `yield`, `resume`) |
  | `jukebox` | `JUKEBOX` with the Jukebox page's state |
  | `itemget <key or item id> [message entry]` | Plays the item-get display without granting the item, only for looking at models. It skips the checks that hold received items back, so it can break the running flow. Tests of received items use `grant` or `ITEM` lines |
  | `enemies [reset\|kill\|at [prefix]]` | The enemy randomizer since the last reset: `seen`, changed, resized, the latest as `<vanilla id>><id>@<scale %>` (hex). `kill` removes the loaded rooms' enemies with no drop or event, `OK enemies killed=<n>` (ones without a LEB key stay). They stay away while their zone is among the last five visited. `at` lists actors by name prefix (default `Enemy`): `OK enemies at <name>@<x>,<y>,<z> ...` |
  | `hit <actor table name> [damage comp]` | A hit past the actor's own checks on the first of that name in the loaded rooms (ACTORS.md, Enemy damage), which ends a boss's phase or its life. It takes the comp named (`comps` lists them, e.g. `DamageColCompTail`), else the one whose defence scales an attack type most. `OK hit <name> life=<before> type=<attack type> part=<comp>`. `ERR hit event` (Link in an event), `ERR hit dead` (after the killing blow), `ERR hit no <name>` |
  | `comps <actor table name>` | Each actor of that name in the loaded rooms: its components as `<name>:<vtable offset>`, actors apart by ` \|` |
  | `slimeeye [fall\|split]` | `OK slimeeye state=<controller> body=<body, - without one>` (ACTORS.md, Slime Eye). `fall` first does what a boots bonk does (from the controller's wait or Zol rain, else `ERR slimeeye state <n>`), `split` what a boots dash into the body does. `ERR slimeeye no controller` outside its room |
  | `rapids end` | Ends a rapids ride or race under way (`RapidflowGame_Finish`, scripts/re/FRAMEWORK.md, Audio): `OK rapids end` |
  | `zols` | The last Zol Trap's source (`from=chest`, `link`, `-` none), `spawned` (Zol Traps so far) and its live Zols as `<actor id>@<x>,<y>,<z>/<zone>` |
  | `actor <LEB key, hex>` | `OK actor <key> 1 id=<actor id, hex> x=<x> z=<z> scale=<scale> rot=<rotation>` while that actor is live, with its spawn place, else `OK actor <key> 0` |
  | `shuffles` | `SHUFFLES` with the swaps in the seed file's `shuffles` (`tracks`, `environments`, `messages`), `environment=<asked>-><found>` (the last swap), `message=<asked>[-><donor>]` (the last lookup) |
  | `drop` / `drop launch <LEB key, hex>` / `drop at <x> <y> <z>` | The drop actor whose `Fall` ran last (`ap_drop.cpp`), `-` when it is gone. `launch` first makes that key's drop the one shown and turns its switch 0 on, `at` moves it there at rest. `<x>,<y>,<z> active=<0\|1> zone=<zone> flags=<entity flags, hex> zs=<+0xa2, hex> room=<LEB path> chest=<its chest's opened flag, -1 no chest> water=<surface, - out of water> v=<x>,<y>,<z> spin=<rad/s> ground=<0\|1> spindamp=<d>` (the last two: the Bouncy Items ball's) |
  | `popmodel <key or item id>` | Shows the received-item popup's model alone (DESIGN.md, Showing received items) |
  | `popsway <on or off>` | Whether the popup's model turns from side to side |
  | `popdraw <on or off>` / `popdraw stats` | Whether the popup's model draws in front of the world. `stats`: `OK popdraw <pass>:<count> ...`, its opaque draw items since the last call |
  | `popframes <count>` / `popframes dump` | Records the popup's next poses: time, frame counter (`+0x4d4`), Link's model's and the popup's world rotation. `dump` sends `POPFRAME` lines |
  | `heldpose` | World rotations of Link's model, the item-get's NPC model and its parent node, the model's local rotation, `item=<scale> at= rot=` (local transform), `popup=` (scale, 0 without one), the camera's view rotation |
  | `pickup <key or item id>` | Plays an item as a find with Instant Pickups (DESIGN.md, Instant pickups) without granting it |
  | `heldangle <model> <pitch> <yaw> <roll>` | Sets an NPC model's item-get angle until the game restarts, for trying angles with `itemget` |
  | `give <id> <count> <index>` | Raw `Inventory::AddItemID` |
  | `collect <local id>` | Simulates a placeholder pickup |
  | `setrecv <n>` / `clearchecks` | Resets the counter or the checks file |
  | `pos <x> <y> <z>` | Teleports within the current level and sets the respawn point there, also while Link swims. Ignored in some states (wading against a wall, events) |
  | `warp <level> <x> <y> <z>` | Reloads a level at a position. Only from another level: within the current level it leaves Link at the origin. Some targets do too (`GhostHouse`, over a pit): warp to Mabe and use `e2e.py goto`, or warp outside the door and walk in. `<level>` is the level name (`GhostHouse`, `Field`): a room name (`GhostHouse_01A`) crashes the game. It starts no music |
  | `peek <p\|f\|i\|l\|b\|m\|h> <hex off> <hex len>` | Dumps at most 0x2000 bytes as `PEEK <base> <off> <hex>`, going on in `PEEK+` lines. Bases: `p` player, `f` framework, `i` inventory, `l` level flags (bit `index` of switch type 0), `b` BGM manager, `m` its first playing instance, `h` that instance's sound |
  | `uis` | `UIS` with the UI manager's UIs (id, `a` while active, vtable), the scene's vtable and `nosave` (1 while no save counts as loaded) |
  | `poke <hex off> <hex bytes>` | Writes bytes into the inventory, e.g. `poke 88 ffffffffffff0300` sets seashells 0–49 |
  | `dumpmain` | Writes the loaded main module to `sd:/lasap/main.bin` |
  | `press <buttons> [down] [up] [lx ly] [when <condition> [ms]]` | Holds buttons (and the stick) for `down` frames, then none for `up` (default 2/2), from the first frame the condition holds. `DONE press <buttons> f=<frame>` or `FAIL press` |
  | `await <condition> [ms]` / `holds <condition>` | `HELD <condition> f=<frame>` once it holds, or `FAIL await`. `holds`: whether it holds now |
  | `advance [buttons] [ms] [fast]` / `advance off` | One press (B by default) at each `textready`, until Link is free (`DONE advance <presses> retries=<n>`) or a choice opens (`CHOICE advance`). `fast` holds the button while a page prints. Fails with `no text` when Link stays free with no message for 20 frames |
  | `choose <option from 0> [ms]` | At a choice: Down/Up to the option, then A once the choice takes buttons (`choiceready`) |
  | `tapuntil <buttons> <until> [when <condition>] [ms]` | Taps whenever `when` holds until `until` holds, again only after a tap changed nothing for 20 frames |
  | `interact [buttons] [ms]` | Once Link is free, one press (A by default) that must start an event, a message or a lift. `FAIL interact nothing` when he is still free and empty-handed 20 frames later |
  | `hold <buttons> <lx> <ly> until <condition> [ms]` | Holds buttons and the stick every frame until the condition holds |
  | `release` | Drops every pending press, hold and op |
  | `guard <condition>` / `guard off <condition>` / `guard reset` | A state a test must never reach: when one starts to hold, every pending op fails with `alert <condition>` and every debug client gets `ALERT <condition> <fields>`. `dead` is always guarded. `hurt` holds once health drops below its value at the guard. `e2e.py` resets the guards when it connects |
  | `ghostnet` | Whether the client asked for positions, `POS` sent, `GHOST` received, the last `POS`, each slot in use, `handle_us` (time on GHOST lines), `names` (the client's `GHOSTNAMES`). Since the last call, Link's animation starts (`<name>@<frame>`) and `GHOST` lines carrying one (`rx:<name>@<frame>`) |
  | `trace on\|off` | The trace ("Game state") |
  | `frame` | `FRAME <frame> counter=<the framework's frame counter> passes=<main loop passes> ms=<now>` |
  | `savepopup` | Shows the game's own autosave popup |
  | `probe <what> ...` | Engine probes for research (`norow`, `poison`, `force`, `hits`, `pool`, `bodies`, ...): `ProbeCommand` in `test_probes.cpp` lists them |
  | `ui` | Lists the UI state machines seen so far with their states |
  | `mem <hex address> <hex length>` | Raw bytes at an address (up to 0x1000), when readable |
  | `memq <hex address \| +module offset>` | `MEMQ` with the memory region there: start, size, type, permissions |
  | `puppet spawn <actor id hex> [dx dz]` / `puppet drop` | Creates an actor beside Link through the actor factory, not driven until `at` or `follow`. With `ghost_puppet.py install`, actor `179` (`NpcGhost`) is a ghost. `drop` removes the test puppet's actor |
  | `puppet at <x> <y> <z>` / `puppet follow <frames>` / `puppet` | Holds the test puppet at a position, or on Link's path that many frames late. `PUPPET` has the dithered draws, `remade` (actors remade for staying inactive) and per puppet (ghost slot or `test`): actor, `hidden`, position, model, `flags` (`+0x40`, active: bits 0–2 and 4), `zone` (`+0xb0`, -1 for every puppet), `anim`, `shapes`, `tunic=<tunic>@<slot>`, `face=<eye>,<mouth>,<ear>@<slots>` (`own`: its own face), `upper`, `ready`, `visfix` (frames visibility was rewritten), `arriving` (hidden at an arrival), `buffer` (ms behind), `speed=<mean>~<deviation>` over 60 frames |
  | `puppet` (crowd and cost fields) | `PUPPET` also has `fps`, `lod=<whole>/<drawn>`, `tick_us` (mean/most over 60 frames), `draws/frame` (ghost draws per command buffer since the last call), `changes` (crowd detail changes in all), `inst` (records per instanced group, re/MODELS.md, Instanced models) |
  | `puppet steer <hz>` / `puppet buffer <ms> [<most ms>]` / `puppet stats [reset]` / `puppet blend <s>` | `steer`: with `follow`, replays Link's delayed path like ghost lines (DESIGN.md, Ghosts), 0 writes it as it is. `buffer`: least and most ms behind the newest frame (default 100 and 400, equal: fixed). `stats`: `STATS` per ghost slot since `reset`: `frames`, per mille `held` (at the newest line while the sender moved), `past` (run past it), `catchup` (correcting by 0.3 units or more), `snaps` (corrections over 2 units), `speed=<mean>~<deviation>`, `buffer=<mean>/<most>`. `blend`: the sender's animation blend time (default 0.0667) |
  | `puppet anim <name>` / `puppet animlog` | Plays a test puppet animation by name. `ANIMLOG` per puppet since the last call: `<name>@<frame>` (`<name>/0@<frame>` unblended), `upper:<name>@<frame>`, `vis@<frame>`/`hide@<frame>` (visibility written), `model@<frame>` (model loaded). Frames are Framework `+0x4d4` |
  | `puppet framelog` / `puppet layered on\|off` / `puppet tags` | `FRAMELOG <frame> <paused> <ghost draws> <instanced draws> <records> <ready>` for the latest ticked frames (paused: Framework `+0x510`), then `FRAMELOG end`. `layered`: new ghost models get the layered animation player. `TAGS` with each name tag's text extent |
  | `puppet tags on\|off` / `puppet tagstyle <scale> <text alpha> <window alpha> <raise> <margin x> <margin y>` | Shows or hides the ghosts' name tags, sets their look |
  | `puppet lod auto` / `puppet lod <whole> <drawn>` / `puppet crowdshapes <hex>` / `puppet keep <units>` | Crowd limits follow the frame rate, or are fixed (nearest ghosts whole, up to `<drawn>` with the crowd shapes). The crowd shapes' mask. The ranking's margin for a ghost's present detail |
  | `puppet trace <slot>` / `puppet tracedump` / `puppet vsyncwait on\|off` | Records a ghost slot's next replay frames. `TRACE <now ms> <replay time> <line's time> <line>/<lines> <mode> <x> <z> <past> <held> <lines>` (mode `i` between lines, `e` past the newest, `h` holding it). `vsyncwait off`: frames end without waiting for vsync (re/FRAMEWORK.md, Frame pacing) |
  | `puppet draw on\|off` / `puppet dither on\|off` / `puppet shapes <hex>` / `puppet skip <hex>` | Finding the ghosts' cost: hide the models with all else running, draw without the dither state, draw only those shapes, drop the draws on those command buffers |
  | `watch <base> <hex off> <hex len>` / `watch off` | Traces changes of a memory range. Bases: `f` framework, `p` player, `i` inventory, `c` controller, `m<id>`/`o<id>` (or `m:<label>`/`o:<label>`) a UI machine or its owner, `g<hex>` a module offset, `x<hex>` an address |

  A reply goes to the debug client that sent the command. Lines for the AP client that a command causes (`CHECK`, `CHECKS`, `HINT`, `GOAL`, `SIGN`, `DUNGEON`, `DIED`, `DEATHDROPPED`, `RECV`, `LOG`) also go to the AP client, so the server sees the check a `give` or `collect` makes.

  Button bits follow `nn::hid::NpadButton`: A `1`, B `2`, X `4`, L+R `c0`, Plus `400`, Minus `800`, D-pad left/up/right/down `1000`/`2000`/`4000`/`8000`. The stick range is ±32767.
- **Inputs.** Press when the game's state takes the press ("Game state"), not after a delay: a dropped press leaves no trace.
- **Buttons in tests.** B is the sword (holding it charges a spin). Dismiss text with B next to signs and NPCs: A also starts a new "Check" or "Talk". Git Bash rewrites arguments that start with `/`: set `MSYS_NO_PATHCONV=1` for `e2e.py cli /las`.
- **Sandbox port.** The sandbox game listens on 43385, beside the user's own modded game on 43384. Start the scripted client with `--game-port 43385`. A sandbox install done by the client (Patch tab) writes no port, so that game uses 43384.
- **Scripted AP session.** Run the client and server with stdin fed from a file (`tail -n0 -f cmds.txt | ...`) so `/dbg`, `/las`, `/game` and server `/send` commands can be appended while they run. `e2e.py` writes to these files in the folder `LASAP_E2E` names (default the sandbox folder's `e2e`). `/game <line>` sends a raw protocol line. For texts naming another player, generate a two-slot seed (the second a small game such as ChecksFinder) and send its items with `/send` or `/release`. Start MultiServer with `--release_mode goal --collect_mode goal` to keep a goal from releasing every location. A run that starts over needs this order: the game stopped and the saves reset (the client reads the collected checks from the SD card), every client stopped (a running client sends its checks to a new server), the server's `.apsave` deleted, then the server and client started.
- **Script ops.** `e2e.py run <script> [trace file]` scripts drive the game on the debug port and check it between steps. A failed step or an `ALERT` stops the script with the STATE line. The run ends with `SCRIPT OK in <s> s`. Conditions are the module's ("Game state").
  - `${NAME}` in a line is the environment variable `NAME`. The script stops when it is not set.
  - `connect [s]`: wait for the debug port. `expect HELLO [s]`: wait until the AP client is connected, also from before the script started. `expect <regex> [s]`: a debug reply or client log line since the last send. It reads the debug port only briefly before it turns to the client log. `poll <regex> <s> <DBG line>`: send the DBG line until a reply matches, for state that changes later.
  - `send <line>`: a `DBG` line goes to the debug port, anything else to the client's stdin. `logged <regex> [s] [client|server|peer] [count]`: wait for log lines since the script started. `logcount <regex> <n> [client|server|peer]`: stop unless exactly n such lines match. `peer <command>`: a command for the peer (`deathlink_peer.py` or `ghost_peer.py`). `game <line>`: a protocol line to the game on the debug port. `stall`: connects to the game port as a client that reads nothing, until the script ends (no LAS client may be connected). `srv <server command>`: a MultiServer console command. Arguments are split on spaces, so patterns use `\s`. `wait <s>`: sleep, only for checking that something stays as it is.
  - `until <condition> [s]`: wait until a condition holds. `tap <buttons> [down] [up] [lx ly] [when <condition> [ms]]` (`DBG press`), `tapuntil <buttons> <until> [when <condition>] [s]`, `hold <buttons> <lx> <ly> until <condition> [s]`, `interact [buttons] [s]`, `choose <option> [s]`, `guard <condition>`, `guard off <condition>`: the module ops.
  - `face up|down|left|right`: hold the stick until Link faces that way (he may take a step).
  - `select <slot>`: in the file menu, move the cursor to a slot (0 = file 1).
  - `dismiss [s]`: close the open text with B, holding B while pages print. Fails at a choice and with no text. `advance [buttons] [s] [fast]`: the same, stopping at a choice.
  - `free [s]`: wait until the player can move, no item-get shows or waits and no message holds him. `loaded [s]`: the same in a level entered after the last `send` or input op. `inlevel [s]`: such a level has loaded. `level <name>`: stop unless the player is in that level.
  - `state`, `shot <png>`: print the STATE line, capture the window (a relative path is in the sandbox folder). `check <regex>`: stop unless the STATE line matches. `await <s> <regex>`: wait until it matches.
  - `warp <level> <x> <y> <z>`: reload a level there (through another level when it is the current one) and wait for a free player. It fails for a Link who lands swimming (event state 30): send `DBG warp` there. `goto <x> <y> <z>`: teleport within the loaded room and wait until Link stands there. `at <x> <z> [tol]`: stop unless the player stands there.
  - `gotodrop [s]`: wait until the drop actor whose `Fall` ran last (DBG `drop`) is active and still, then put Link against it, which picks it up. `droprest [s]`: wait until it is at rest or gone, or taken as it lands on Link, printing each new position.
  - `screen <name>[|<name>] [s]`, `press <buttons> <ms> <screen> [tries]`, `mash <buttons> <count> [hold ms] [gap s]`: timed presses through `input`, for scripts that have no state to wait for.
- **Test scripts.** `scripts/tests/*.txt`. Each script's opening comment says what it needs and checks. `suite.txt` groups the scripts that run on the suite's mod ("Test suite"). One that needs its own mod or seed (`map_shuffled`, `dungeon_doors`, the DeathLink scripts, `hints`, `instant_multiworld`, ...) runs with `e2e.py run`. The relay's own behaviour is in the world's tests (`test_ghosts.py`).
  - `newgame_file2` boots to a new game in file 2 (after `reset_saves.sh newgame`) and ends free in Tarin's house. `boot_file1` loads file 1, a played file.
  - With Bouncy Items (`options.bouncy_items=True`), chests and dig spots launch a ball and play no item-get, and where it ends is random. Scripts that wait for an item-get or popup stop there: `beach_chest`, `zol_trap`, `instant_pickup`, `instant_song`, `instant_multiworld`. `dig_slime_key`'s walk north may miss the ball. `drop_bush` fails when its ball falls out of the world. `bouncy_chest` fails about one run in four (2026-10-29), by where its second ball ends, and when `gotodrop` puts Link on top of it (2026-11-04). `owl_drop` fails when its ball lands on Link (2026-11-04).
  - `jukebox` and `jukebox_sword` assume that the field plays its zone's track (`zonebgm`). Before the sword it plays `BGM_FIELD_FIRST`, so on an early save those checks fail.
  - The sandbox folder's `docs\item_sheet.png` (outside the repo) shows every item's item-get, labelled with its Items.gsheet key (captured 2026-10-17). The recovery heart has no item-get display.
- **New-game snapshot.** The sandbox folder's `save-newgame` is the sandbox save folder right after file 2 was created (file select, file 2, Normal, OK on the name entry). File 1 and the autosave are a copy of the user's played file, file 3 is empty. `reset_saves.sh newgame` restores it. The file-select cursor starts on the last file used (file 2 in the snapshot) and stops at file 1 and the autosave instead of wrapping.
- **Overworld coordinates.** Screen `Field_<row><col>` (col A=0) is around x = 15·col + 7.5, z = 12·(row − 1) + 6.75. Mabe at Marin's door is (38.25, 10.5, 128.25). In front of the shop is (51.75, 10.5, 117), walk up to enter.
- **Warps and pits.** Warp into a dungeon or house from `Field`: after a warp out of `TownWell` (Link standing in water) the screen can stay black until the emulator restarts. `pos` also sets the respawn point, so a `pos` over a pit makes Link fall again on every respawn until he dies. Reset with a warp to another level and back.
- **Health.** `poke 84 1c00` sets health to 7 hearts (quarter hearts, u16).
- **Tarin's gift.** The detain scene on leaving the house only stops Link. Talk to Tarin (A at about (9.4, 0, 6.0) in his house) to get the gift.
- **Finding a location.** LASR's `Randomizers/*.py` map locations to rooms (`CHEST_ROOMS`, `HEART_ROOMS`, ...). Teleport one tile (1.5) south of a chest and press A facing up. A `pos` into a room that is not loaded lands on no floor, `warp` loads the target's room. A bush (`ObjGrass`), rock or pot is solid: Link teleported onto one stands on top of it and cannot reach what is below. Pick a tile free of them from `leb_actors.py <room> --near <x> <z>` (tall grass, `ObjLawn`, is walkable). When every tile next to the object holds one, teleport to the nearest free tile and cut or lift one first. `warp` and `goto` stop the script when Link lands more than 1 above the target or away from it. A test that needs no enemies sends `DBG enemies kill` once it has arrived.

## Game state

- **STATE** has `ghosts` (slots in use), `music` (the jukebox's track, `-` none), `zonebgm` (the zone's track), `bgm` (tracks playing, `name:kind`, `-` none), `bgmvol` (their volume targets, percent), `layers` (the Ballad's instruments in hex, bit i for dungeon i + 1, `-` off), `muted` (1 while the Jukebox mutes), `pane` (pause menu page, 3 the Jukebox), `navi` (header state), `jbox` (the Jukebox's `<cursor>/<first row>`; these three `-` with the menu closed), `mlist` (`DBG maplist`), `puppets=<drawn>/<with an actor>`, `gear=<shapes>/<tunic>/<eye>,<mouth>,<ear>/<upper>` (slot 0's puppet, shapes in hex, -1 none yet, `-` not drawn), `tags` (per slot: `-` none, `h` hidden, `g` glyphs drawing, `d` drawn), then the frame fields. `ms_in_level` is the time since the level loaded, `player` 1 while Link's actor exists. It ends with `frame` (main loop passes since boot, menus included, also counting every 16 ms before the loop runs and every 100 ms during a pass), `msg` (message window state), `choice` (its cursor, 0 = first), `face` (`up`, `down`, `left`, `right`), `yaw` (degrees, 0 = down), `anim` (Link's animation, its `PlayerAnimation.gsheet` file name), `file` (file menu cursor), `entry` (name entry cursor: 0 the name, 1 OK), `read` (frames the last page spent in `Reading`).
- **Fields** conditions test: `screen`, `level`, `event` (Link's state number, PLAYER.md's state table: 0 when he stands, 9 a dash, 22 demoEvent; not whether an event runs), `paused`, `free`, `lock` (the game's controller ignores input), `pad` (the controller's buttons), `show` (an item-get waits or shows), `text` (message window state with its wait counter, `KeyWaiting,6/6`), `choice`, `client`, `server`, `file`, `face`, `carry`, `health`, `entry`, `anim`, `zone` (Link's zone, actor `+0xb0`: his camera room, -1 none; ACTORS.md).
- **Conditions** (`await`, `press ... when`, script ops): `<field>=<value>`, `<machine>:<state>` (`title:WaitLR`, `files:DataSelectKeyWait`, `msg:KeyWaiting`), `free` (Link can move, no item-get pending, no message holds him; one in `EventWaiting` does not), `unlocked`, `textready` (the open message takes a button now), `choiceready` (a choice takes buttons now), `dead` (in a level with no health), `navi=<state>` (pause menu header: 1 Open, 2 WaitKey, 3 Selected, 4 CloseOldUi, 5 Close, `-` closed). `!` negates, `+` joins (`choice=1+choiceready`).
- **UI state machines.** `title`, `files` (file menu), `msg` (message window; a message's choice is `msg:Selecting` with STATE's `choice`), `choice` (system choice dialog), `file` and `name` (file and name screens), `map` and `dmap`, `lure` (fishing rod and lure) and `fish` (each pond fish) are learned from their state changes and named by their own state tables. `DBG ui` lists them.
- **Trace.** `DBG trace on` (or `e2e.py run <script> <trace file>`) sends `T <ms> <frame> <what>` on every change of a field, a UI machine's state, the buttons each frame's input polls carried (`hid phys=<buttons> inj=<buttons>`), every press and op step, and watched memory.
- **What takes a press** (2026-10-14, Ryubing, game 1.0.0, sandbox):
  - The intro takes a tap about 20 frames after the title's machine appears.
  - The title takes L+R on the first frame of `WaitLR` with the controller unlocked. A 2-frame tap is enough.
  - The file menu takes A on the first frames of `DataSelectKeyWait` and of `DataSelectConfirm`, Up/Down one slot per tap.
  - The message window takes a button in `KeyWaiting` 6 frames after it enters the state (10 in `PreSelecting`). A choice (`Selecting`) takes buttons once its layout animation ends. An A on the sixth frame of `Selecting` was dropped.
  - Holding B while a page prints shows it whole. With Instant Text a page needs 1–3 frames of `Reading`.
- **Boot timeline** (2026-10-14, `newgame_file2` with Instant Text): the file loads in 2.4 s, and Link is free 18.2 s after the intro shows.

## Reverse engineering

- `DBG dumpmain` writes the decrypted, relocated main module from memory (runtime base 0x8504000 on Ryubing). Keep it in the `re` folder (`re` in `scripts/local_paths.json`), outside the repo.
- `re/scan.py` (`LASAP_DUMP` names another dump, `LASAP_BASE` another runtime base) uses module offsets (the ones `InstallAtOffset` takes). `xstr <action name>` leads to an event action's handler.
- Ghidra (scoop `ghidra`): `re/decomp.sh` imports `main.bin` raw (AARCH64 little endian, base 0x8504000, no auto-analysis) into a project next to it (`LASAP_RE`, default `re`), names it from `re/las.sym` and decompiles the functions at the given offsets. An offset given as `<offset>:<hex word>` puts that instruction there first, because in the dump a hooked function starts with the hook's branch (`0x8d08e0:d10143ff` for `Inventory_AddItemID`).
- `re/las.sym` names what is found so far, each finding adds its symbols: `<name> <absolute address> f|l [note]`, f a function, l a label. In the Ghidra GUI, `re/ghidra/ImportSymbols.java` (script directory `scripts/re/ghidra`) imports it into a program imported at base 0x8504000.
- Docs on structures and fields: `re/UI.md` (UI framework, state machines, map pages; `re/bflyt_tree.py <layout>` prints a layout's pane tree), `re/ACTORS.md` (actors, rooms, what persists), `re/ITEMS.md` (item ids, chamber stones, saved data), `re/MODELS.md` (models, animation), `re/PLAYER.md` (Link), `re/FRAMEWORK.md` (framework, camera, audio), `re/CRANE.md` (Trendy Game), `re/EVENTS.md` (event manager, flowchart binding). The module's `game/` headers type these objects.
- `DBG event` prints the event manager's queue and signals, and for the running event its entry, flowchart, flow, actor map (each name with `c` a chara or `s` a system actor, `a`/`q` its lookups) and the first flowchart's bindings (`<actor>:<c or -><bound actions>/<actions>`).
- `DBG flow <flowchart> <entry>` requests that entry of `event/<flowchart>.bfevfl` with Link and the system actors (`AP::Bridge::RequestEvent`). The flowchart's other actors are not in the event.

## Measurements

On the sandbox (Ryubing, game 1.0.0) with the test module.

- **Item-get timing** (2026-10-17, a received seashell and the beach chest, B held from the page's `Reading`). The text opens 1.15 s after the item-get's start and Link is free 0.63 s after it closes: 2.80–2.85 s in all, the fanfare playing on. The next queued item-get starts 0.22 s after the end. The game's own item-get takes 5.27 s.
- **Check list cost** (2026-10-19, `DBG maplist`). Opening a list takes 1.8–2.3 ms, each frame with Tail Cave's 15 rows 120–125 µs, at most 390 µs while scrolling.
- **Frames in menus** (2026-10-19). STATE's `frame` rises 60 a second in a level and with the pause menu's map open, where the framework's counter stands still.
- **Ghost crowd** (2026-10-15, 30 ghost slots, a local relay, `ghost_crowd.py` players on screen in Field_10C). The draw calls limit the frame rate: each shape a ghost draws is one draw in each of two command buffers, and 60 fps holds up to about 120 a buffer. All whole without instancing: 20 ghosts 59.5–60 fps, 30 ghosts 31. All 30 hidden, with 270 GHOST lines/s running (the lines 0.04 ms a frame): 60 fps. Instanced with the release's defaults: all 30 whole 60.0 fps (worst second 58.8).
- **Ghost crowd motion and frame pacing** (2026-10-15, 30 `ghost_crowd.py` players, no instancing, lines arriving in lumps after 83–100 ms gaps). The ghosts' per-frame speed spread (deviation over mean) with the replay buffer at 33 ms 1.06, 66 ms 0.31, 100 ms 0.24. All 30 whole: 30.1 fps with the end-of-frame vsync wait (re/FRAMEWORK.md, Frame pacing), 55.2 without.
- **Ghost network** (`ghost_crowd.py` against a local relay). 30 players in one area (2026-10-15): each sends about 9 frames/s, and the relay makes 591 socket writes/s, 261 KiB/s, 2% of a core. 60 players: 587 KiB/s, 7% of a core. Positions as changes with keyframes (2026-10-22): each player gets 3.3–4.6 KiB/s of frames.
- **Ghost replay on a bad link** (2026-10-27, `ghost_link`: one peer running at about 6 units a second, 30 s per row). Per mille of frames held at the newest line / run on past it / catching up, jumps, speed spread (deviation over mean), buffer mean/most (ms):

  | Link | Fixed 100 ms | Adaptive |
  |---|---|---|
  | Wi-Fi, 60 ms of jitter | 101 / 350 / 205, 2 jumps, 0.80 | 14 / 56 / 22, 0 jumps, 0.35, 214/289 |
  | Loss, 20 ms of jitter, 2% 300 ms late | 60 / 158 / 62, 0 jumps, 0.49 | 25 / 24 / 36, 0 jumps, 0.36, 211/291 |

## Not yet verified in-game

- Dampé's rewards.
- Owl statues as locations: no test confirms that a gift's check reaches the server.
- The Slime Eye's fight played with the boots (`slime_eye_drop` uses `DBG slimeeye`).
- Real hardware.
- Stealing from the shop with Instant Pickups on Everything: no test times the shopkeeper looking away.
- `APSignRead` through a save and a restart of the game (other added flags, `APRecv<n>`, last in the save the same way).
- Hidden counts and unfound dungeon doors in Universal Tracker's window.
- Easy Fishing in play: the line holding against a hooked fish.
- The jukebox by ear, the Ballad's tracks and loop seam included, and event songs playing over a chosen track in game.
- Player settings without a visible or textual effect in the sandbox: Music, Nice Hookshot, Blur Removal, Movement Speed while swimming. Damage None against fall and drown damage.
- Mouse clicks on the Patch tab's look and sound settings (`/setting` changes them).
- The stairs after the Nightmare into `Ending`, the companions' events and a prize won with the Trendy Game's crane (tests stand in for these).
- The Fairy Queen's warps with Shuffled Dungeons: played in the game only for the Color Dungeon's exit.
- PopTracker: the "?" as drawn, and other PopTracker versions.
- The ghost relay through a real Cloudflare Tunnel (`wss://`), and the image workflow on GitHub.
- No 30 FPS Drops on the yuzu-based emulators.

## Building the exlaunch module

The module is `las-ap-exl` (from Owen-Splat/exlaunch, GPL-2.0). Run `bash scripts/build_exl.sh`, which builds two modules:

- The release module (`patcher/exl/`), which the patcher installs for players. It has no debug commands, debug port, trace or input injection.
- The test build (`patcher/exl-test/`, `make BUILD=build-test DEFINES=-DLASAP_TEST`), which `deploy_mod.sh` and `reload_exl.sh` install in the sandbox. Everything tests need beyond the game lives under `#ifdef LASAP_TEST`: the test build's own code in the module's `test/` folder, and small probes in the features' files.

## Why Ryubing, not Eden

Eden (yuzu fork) offers no decisive advantage (assessed 2026-10-12):
- Randovania's Dread exlaunch socket mod officially supports only Ryujinx.
- Eden's airplane mode (`airplane_mode`) breaks TCP sockets while it is on.
- Eden's TAS player runs only in the Qt frontend.
