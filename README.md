# Link's Awakening (Switch) for Archipelago

[Archipelago](https://archipelago.gg) multiworld support for The Legend of Zelda: Link's Awakening, the 2019 Switch release (game version 1.0.0). The items Link finds on Koholint Island are shuffled across the multiworld, with the logic of Owen-Splat's [LAS Randomizer](https://github.com/Owen-Splat/LAS-Randomizer), so the game can always be finished.

It is built on LAS Randomizer, exlaunch, Archipelago and more: see [CREDITS.md](CREDITS.md).

## Features

- Checks reach the server as you make them, and items from other players arrive with the game's own item-get. Another world's item shows its own model when the game has a matching one, otherwise the Archipelago model, and its text says whose it is.
- An in-game map tracker: the pause map marks every spot with checks by whether your items reach them.
- In-game hints from owl statues and the library's books.
- Shuffled dungeon entrances, enemy randomization, traps and DeathLink.
- Ghosts: the other Link's Awakening players of your multiworld show as see-through Links where you are.
- Hold Minus to warp home to Mabe Village.
- PopTracker and Universal Tracker support.

## Getting started

1. Install [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.7 or newer.
2. Download `las.apworld` from the [latest release](https://github.com/zeroZshadow/LASAP/releases/latest) and double-click it.
3. Follow the [setup guide](apworld/las/docs/setup_en.md). You need the game at version 1.0.0 on Ryujinx or Eden and your own RomFS dump of it; the client patches the game and installs the mod for you.

The [game page](apworld/las/docs/en_Links%20Awakening%20Switch.md) explains the goal, the options and what to expect in play. Only whoever generates the seed and the Link's Awakening players need the world; other players need nothing new.

Ghosts use a public relay by default. To host your own, see [docs/ghost-relay.md](docs/ghost-relay.md).

## Status

Playable on Ryujinx and Eden on Windows. Linux, macOS and a Switch are untested, and a few rarely reached checks have not been played yet: see the setup guide's [known issues](apworld/las/docs/setup_en.md#known-issues).

## Development

| Part | Where | What it does |
|---|---|---|
| Archipelago world | `apworld/las` | Items, locations, LASR's logic as AP rules, options; writes an `.aplas` patch per slot |
| Patcher | `patcher/las_ap_patch.py` | Turns an `.aplas` plus your RomFS dump into an Atmosphère/emulator mod (LASR's mod generator) |
| Game module | `las-ap-exl` | exlaunch code in the game: reports checks, applies received items, talks TCP on port 43384 |
| Client | `apworld/las/client.py` | Patches and installs the mod from an `.aplas`, connects the AP server and the game |

[DESIGN.md](DESIGN.md) has the contracts between the parts, [TESTING.md](TESTING.md) covers the test harness and [TODO.md](TODO.md) lists open work.

The release's `las.apworld` is all a player installs, on any system Archipelago runs on: it carries the patcher and the game module, and the client runs the patcher in its own process.

### Building a release

Publishing a GitHub release runs the Release workflow (`.github/workflows/release.yml`): it builds the module and the release as below, runs the world's tests and attaches the files to the release. The release's tag must be the world's version (`world_version` in `apworld/las/archipelago.json`, a leading `v` allowed). Publishing also builds the ghost relay's container image (`docs/ghost-relay.md`). Run the Release workflow by hand from the Actions tab to build without releasing; the files are then the run's `release` artifact.

To build locally:

1. Set up `.venv-lasr`, `.venv-ap` and `vendor/Archipelago` (TESTING.md) and build the module with `bash scripts/build_exl.sh` (Docker; it writes `patcher/exl/`, the same for every seed).
2. Build:
   ```bash
   .venv-lasr/Scripts/python.exe scripts/build_release.py
   ```
   It writes `build/release/LASAP-<world version>/` with `las.apworld`, the options template and the setup guide (`Setup guide.md`, for players of a world that is not on the website).
   With `.venv-relay` (PyInstaller) it also writes `LASGhostRelay.exe`, the ghost relay as one Windows program (`scripts/build_relay.py`, from `scripts/relay_exe.py`):
   ```bash
   uv venv .venv-relay --python 3.12
   ```
   ```bash
   uv pip install pyinstaller --python .venv-relay
   ```

### Developing

- **The patcher venv.** The patcher and its tools run in a Python 3.12 venv; `requirements-assets.txt` adds the packages for rebuilding the assets to the patcher's own `requirements.txt`. With `uv`:
  ```bash
  uv venv .venv-lasr --python 3.12
  ```
  ```bash
  uv pip install -r patcher/requirements-assets.txt --python .venv-lasr
  ```
- **The world from source.** With the `worlds/las` junction (TESTING.md), the client runs this repository's `patcher/` in its own process, which needs the patcher's libraries in Archipelago's environment:
  ```bash
  uv pip install -r patcher/requirements.txt --python .venv-ap
  ```
  Alternatively, set the Patch tab's patcher command (advanced settings) to `<repo>/.venv-lasr/Scripts/python.exe <repo>/patcher/las_ap_patch.py`: the client then runs that as a separate program.
- **The world alone.** `.venv-ap/Scripts/python.exe scripts/ap.py Launcher.py "Build APWorlds" -- "Links Awakening Switch" --skip_open_folder` writes `las.apworld` without the patcher to `vendor/Archipelago/build/apworlds`.
- **The patcher alone.** `.venv-lasr/Scripts/python.exe patcher/las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <out dir>` writes the SD card layout (`atmosphere/contents/01006BB00C6F0000`, `config/lasr-exl/config.ini`) to `<out dir>`.

## Licenses

- This project's own code (`apworld/las` but `data/`, `patcher/*.py`, `scripts`, `docker`): GPL-2.0-or-later, see [LICENSE](LICENSE).
- `las-ap-exl`: GPL-2.0, from exlaunch and Owen-Splat's fork; the socket code follows open-dread-rando-exlaunch.
- `patcher/RandomizerCore` and `apworld/las/data`: MIT, from LASR (see the LICENSE files in those folders). The bntx editor (`RandomizerCore/Tools/bntx_editor`) is GPLv3; only the asset build (`scripts/build_assets.py`) uses it, and the release leaves it out.

Link's Awakening is © Nintendo. This project is not affiliated with Nintendo and contains no game files.
