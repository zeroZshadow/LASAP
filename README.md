# Link's Awakening (Switch) for Archipelago

[Archipelago](https://archipelago.gg) multiworld support for The Legend of Zelda: Link's Awakening, the 2019 Switch release (game version 1.0.0). Items are shuffled across the multiworld using the logic of Owen-Splat's [LAS Randomizer](https://github.com/Owen-Splat/LAS-Randomizer). See [CREDITS.md](CREDITS.md) for the projects it builds on.

## Features

- Live checks and items. Another world's item shows a matching model or the Archipelago logo and names its owner.
- Two goals: the instruments or a seashell hunt.
- A map tracker in the pause menu, with check lists and server hints.
- Hints from owl statues and library books.
- Shuffled dungeon entrances, random enemies, traps and DeathLink.
- Ghosts of the other Link's Awakening players in your multiworld.
- A Jukebox page, optional item popups, and a warp home on Minus.
- PopTracker and Universal Tracker support.

## Getting started

1. Install [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer.
2. Download `las.apworld` from the [latest release](https://github.com/zeroZshadow/LASAP/releases/latest) and double-click it.
3. Follow the [setup guide](apworld/las/docs/setup_en.md). You need the game at version 1.0.0 on Ryujinx or Eden and your own RomFS dump. The client patches the game and installs the mod.

Only the person generating the seed and the Link's Awakening players need the world. The [game page](apworld/las/docs/en_Links%20Awakening%20Switch.md) covers the goal, options and play. To host your own ghost relay, see [docs/ghost-relay.md](docs/ghost-relay.md).

## Status

Playable on Ryujinx and Eden on Windows. Linux, macOS and a real Switch are untested. See the [known issues](apworld/las/docs/setup_en.md#known-issues).

## Development

| Part | Where | What it does |
|---|---|---|
| Archipelago world | `apworld/las` | Items, locations, logic and options; writes an `.aplas` patch per slot |
| Patcher | `patcher/las_ap_patch.py` | Turns an `.aplas` and a RomFS dump into a mod. `patcher/modbuild` builds its assets |
| Game module | `las-ap-exl` | exlaunch code in the game: reports checks, applies items, listens on TCP 43384 |
| Client | `apworld/las/client.py` | Installs the mod and connects the AP server and the game |
| Ghost relay | `apworld/las/ghost_relay.py` | Passes ghost positions between players |

[DESIGN.md](DESIGN.md) has the contracts between the parts, [TESTING.md](TESTING.md) the test harness and venv setup, and [TODO.md](TODO.md) the open work.

### Building a release

Publishing a GitHub release runs `.github/workflows/release.yml`, which builds and tests the world and attaches the files. The tag must match `world_version` in `apworld/las/archipelago.json`, with or without a leading `v`. Publishing also builds the relay's container image. Running the workflow by hand keeps the files as a `release` artifact instead.

To build locally, set up the venvs and `vendor/Archipelago` ([TESTING.md](TESTING.md)), then:

```bash
bash scripts/build_exl.sh
```
```bash
.venv-lasr/Scripts/python.exe scripts/build_release.py
```

This writes `build/release/LASAP-<world version>/`. With a `.venv-relay` holding PyInstaller (`uv venv .venv-relay --python 3.12`, then `uv pip install pyinstaller --python .venv-relay`), it also builds `LASGhostRelay.exe`.

### Developing

Patcher venv, Python 3.12:

```bash
uv venv .venv-lasr --python 3.12
```
```bash
uv pip install -r patcher/requirements-assets.txt --python .venv-lasr
```

The world from source, through the `worlds/las` junction, needs the patcher's libraries in Archipelago's environment:

```bash
uv pip install -r patcher/requirements.txt --python .venv-ap
```

Build the world alone, without the patcher, to `vendor/Archipelago/build/apworlds`:

```bash
.venv-ap/Scripts/python.exe scripts/ap.py Launcher.py "Build APWorlds" -- "Links Awakening Switch" --skip_open_folder
```

Run the patcher alone:

```bash
.venv-lasr/Scripts/python.exe patcher/las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <out dir>
```

## Licenses

- Own code (`apworld/las` except `data/`, `patcher/*.py`, `patcher/modbuild/*.py`, `scripts`, `docker`): GPL-2.0-or-later, see [LICENSE](LICENSE).
- `las-ap-exl`: GPL-2.0, from exlaunch and Owen-Splat's fork. The socket code follows open-dread-rando-exlaunch.
- `patcher/modbuild/RandomizerCore`, `patcher/data` and `apworld/las/data`: MIT, from LASR (see `patcher/LICENSE-LASR.txt` and the LICENSE files in those folders). The bntx editor (`RandomizerCore/Tools/bntx_editor`) is GPLv3 and is used only by the asset build, which the release leaves out.

Link's Awakening is © Nintendo. This project is not affiliated with Nintendo and contains no game files.
