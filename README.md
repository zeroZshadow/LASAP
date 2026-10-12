# Link's Awakening (Switch) for Archipelago

Archipelago multiworld support for The Legend of Zelda: Link's Awakening (2019, game version 1.0.0). It is built on the [LAS Randomizer](https://github.com/Owen-Splat/LAS-Randomizer) by Owen-Splat.

| Part | Where | What it does |
|---|---|---|
| Archipelago world | `apworld/las` | Items, locations, LASR's logic as AP rules, options; writes an `.aplas` patch per slot |
| Patcher | `patcher/las_ap_patch.py` | Turns an `.aplas` plus your RomFS dump into an Atmosphère/emulator mod (LASR's mod generator) |
| Game module | `las-ap-exl` | exlaunch code in the game: reports checks, applies received items, talks TCP on port 43384 |
| Client | `apworld/las/client.py` | Patches and installs the mod from an `.aplas`, connects the AP server and the game |

DESIGN.md has the contracts between the parts, TESTING.md covers the test harness, TODO.md lists open work, and FEASIBILITY.md has the original assessment.

## Playing

The player's guide is `apworld/las/docs/setup_en.md`: install the world, open the `.aplas`, choose the emulator and the RomFS dump in the client's Patch tab, and press Install and play. The patcher runs from this repository, so it needs a one-time setup.

### One-time setup

1. **Install the world.** Copy `las.apworld` into Archipelago's `custom_worlds` folder. To build it, use the dev setup in TESTING.md (`vendor/Archipelago` with the `worlds/las` junction):
   ```bash
   .venv-ap/Scripts/python.exe scripts/ap.py Launcher.py "Build APWorlds" -- "Links Awakening Switch" --skip_open_folder
   ```
   The file lands in `vendor/Archipelago/build/apworlds`. The world is needed only by whoever generates the seed and by the LAS player for the client; other players need nothing new.
2. **Dump the RomFS.** In Ryujinx, right-click the game, then Extract Data → RomFS.
3. **Set up the patcher.** It runs in a Python 3.12 venv with `patcher/requirements.txt`. With `uv`:
   ```bash
   uv venv .venv-lasr --python 3.12
   ```
   ```bash
   uv pip install -r patcher/requirements.txt --python .venv-lasr
   ```
4. **Build the game module** with `bash scripts/build_exl.sh`. It needs Docker and writes `patcher/exl/`. The module is the same for every seed.
5. **Point the client at the patcher.** In the client's Patch tab, under advanced settings, set the patcher command to `<repo>/.venv-lasr/Scripts/python.exe <repo>/patcher/las_ap_patch.py` (or `las_options.patcher_command` in `host.yaml`).

### Each seed

1. **Options.** In the Launcher, "Generate Template Options" writes `Links Awakening Switch.yaml` to `Players/Templates`. Its defaults are LASR's default settings:
   - Maps, compasses and stone beaks are in the starting inventory.
   - Small keys stay in their own dungeon.
   - Instruments can be anywhere.
   - Traps are set to many.
   - You start with the sword, shield, ocarina and Manbo's Mambo.

   Set your `name`, put the file in `Players` with everyone else's, and generate. Host the result as usual, on archipelago.gg or with the Launcher's "Host".
2. **Open the patch.** The output zip has a patch file per LAS slot, `AP_<seed>_P<n>_<slot>.aplas`. Open it with the Launcher (double-click, drag onto the Launcher, or "Open Patch"). The Links Awakening Switch Client opens on its Patch tab; the first time, choose the emulator and the RomFS dump there. Install and play patches and installs the mod unless that seed is installed already, starts the emulator with the game, and the client connects to the server named in the patch.
3. **Play.**
   - Start a new save.
   - Connect the client to the server if the patch named none. The client finds the game at the game address, `127.0.0.1` by default; emulators need no settings for that.
   - Every randomized location holds an Archipelago item. It shows the item's model when the item is a LAS item or its name matches one (`apworld/las/data/item_models.yml`), otherwise the Archipelago model, and its text names the item and whose it is. Your items, including the ones you find yourself, arrive through the client. Checks made while the client is offline are sent when it reconnects.

The patcher also runs on its own: `.venv-lasr/Scripts/python.exe patcher/las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <out dir>` writes the SD card layout (`atmosphere/contents/01006BB00C6F0000`, `config/lasr-exl/config.ini`) to `<out dir>`.

## Status

The core loop works on Ryujinx: checks, received items with the game's own item-get display, resync, goal. TESTING.md lists what is verified and what is not.

**Known gaps:**
- Dungeon entrance shuffle isn't supported.
- DeathLink is untested. Not tested on hardware.

## Licenses

- `las-ap-exl`: GPL-2.0, from exlaunch and Owen-Splat's fork; the socket code follows open-dread-rando-exlaunch.
- `patcher/RandomizerCore` and `apworld/las/data`: MIT, from LASR (see the LICENSE files in those folders). The bntx editor is GPLv3.
