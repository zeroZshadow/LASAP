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
| `reset_saves.sh` | Restores sandbox saves from the user's saves and clears `sdcard/lasap` |
| `ap.py <Generate.py\|MultiServer.py> ...` | Runs an AP entry point from `vendor/Archipelago` in `.venv-ap` |
| `las_client.py [--nogui --connect host:port --name Slot]` | Runs the LAS client without the Launcher |
| `make_test_patch.py <out.aplas>` | Writes a synthetic patch (all locations AP items) for patcher tests |
| `verify_patch.py <patch> <out dir>` | Checks that every shuffled chest holds the placeholder with its local id |
| `test_client.py` / `drive.py <script>` | Talk to the module directly (only when no AP client is connected) |

## Observing and driving the game

- **Guest log.** Module output (`[AP] ...`, `[exlaunch|...]`) goes to the emulator log via `svcOutputDebugString`.
- **Screenshots.** `emu.py shot` uses `PrintWindow`; it works while the window is covered or unfocused.
- **Debug commands** (`DBG ...` on the game socket; `/dbg ...` in the LAS client):

  | Command | Effect |
  |---|---|
  | `state` | Prints level, position, health, rupees and key inventory bits, plus the received counter |
  | `flag <name>` / `setflag <name> 0\|1` | Reads or sets an event flag |
  | `input <buttons hex> <ms> [lx ly]` | Holds buttons and the left stick for a duration |
  | `grant <key> <count> <dungeon>` | Same path as `ITEM`, without touching the counter |
  | `give <id> <count> <index>` | Raw `Inventory::AddItemID` |
  | `collect <local id>` | Simulates a placeholder pickup |
  | `setrecv <n>` / `clearchecks` | Resets the counter or the checks file |
  | `pos <x> <y> <z>` | Teleports within the current level. Works while Link stands on the ground, not while he falls |
  | `warp <level> <x> <y> <z>` | **Not working:** loads the level but leaves Link at the origin; an autosave there strands the file |

  Button bits follow `nn::hid::NpadButton`: A `1`, B `2`, L+R `c0`, Plus `400`, D-pad left/up/right/down `1000`/`2000`/`4000`/`8000`. The stick range is ±32767.
- **Inputs.** Hold at least 400 ms. Shorter presses are dropped in dialogue.
- **Scripted AP session.** Run the client and server with stdin fed from a file (`tail -f cmds.txt | ...`) so `/dbg`, `/las` and server `/send` commands can be appended while they run.
- **Boot sequences.** `scripts/tests/*.txt` are `drive.py` scripts: boot to file 2, or create a new game in file 2.
- **Overworld coordinates.** Screen `Field_<row><col>` (col A=0) is around x = 15·col + 7.5, z = 12·(row − 1) + 6.75. Mabe at Marin's door is (38.25, 10.5, 128.25); in front of the shop is (51.75, 10.5, 117), walk up to enter.

## Verified (2026-10-12, Ryubing Canary 1.3.340, game 1.0.0)

- **Boot.** The module boots with LASR's randomizer hooks; the socket listens on 43384.
- **Generation and patching.** Generate → patch → deploy → new game works end to end.
- **First check.** Tarin's gift (the first check) reaches the server as a location check.
- **Shop.** Buying the placeholder in the Mabe shop charges the price, plays the item get and reports the slot's location (`shop-slot3-1st`).
- **Received items.** Items from the server are applied in-game: progressive sword/shield/bracelet, feather, seashells, rupees, small key, tunic, heart container, golden leaf and a trap. A burst of 240 items also applied.
- **Persistence.** The received count survives a restart without double grants. On a fresh save, everything is re-delivered.
- **Goal.** `ShadowClear` reports the goal to the server.
- **Chests.** All 110 shuffled chests hold the placeholder with the right index (static check).

## Not yet verified in-game

- Opening a chest, heart pieces, seashells, dungeon items, owl statues, Seashell Mansion.
- DeathLink in both directions.
- Real hardware.

## Building the exlaunch module

The module is `las-ap-exl` (from Owen-Splat/exlaunch, GPL-2.0). Run `bash scripts/build_exl.sh`.

## Why Ryubing, not Eden

Eden (yuzu fork) offers no decisive advantage (assessed 2026-10-12):
- Randovania's Dread exlaunch socket mod officially supports only Ryujinx.
- Eden defaults to `airplane_mode=true`, which breaks TCP sockets until it is changed.
- Eden's TAS player runs only in the Qt frontend.
