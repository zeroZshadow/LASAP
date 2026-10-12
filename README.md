# Link's Awakening (Switch) for Archipelago

Archipelago multiworld support for The Legend of Zelda: Link's Awakening (2019, game version 1.0.0). It is built on the [LAS Randomizer](https://github.com/Owen-Splat/LAS-Randomizer) by Owen-Splat.

| Part | Where | What it does |
|---|---|---|
| Archipelago world | `apworld/las` | Items, locations, LASR's logic as AP rules, options; writes an `.aplas` patch per slot |
| Patcher | `patcher/las_ap_patch.py` | Turns an `.aplas` plus your RomFS dump into an Atmosphère/emulator mod (LASR's mod generator) |
| Game module | `las-ap-exl` | exlaunch code in the game: reports checks, applies received items, talks TCP on port 43384 |
| Client | `apworld/las/client.py` | Connects the AP server and the game |

DESIGN.md has the contracts between the parts, TESTING.md covers the test harness, and FEASIBILITY.md has the original assessment.

## Playing

1. **Install the world.** Put the world in Archipelago: build `las.apworld` with the Launcher's "Build APWorlds", or copy `apworld/las` into `custom_worlds`. Then generate as usual with `game: Links Awakening Switch`.
2. **Build the game module** once: `bash scripts/build_exl.sh` (needs Docker).
3. **Patch your game.** Use your own RomFS dump of version 1.0.0:
   ```bash
   .venv-lasr/Scripts/python.exe patcher/las_ap_patch.py AP_<seed>_P1_<slot>.aplas --romfs <romfs dump> --out <out dir>
   ```
4. **Install the mod.** Copy `<out dir>/atmosphere` and `<out dir>/config` to the SD card root, or to Ryujinx's `sdcard` folder.
5. **Start a new save** and run the client:
   ```bash
   .venv-ap/Scripts/python.exe scripts/las_client.py --connect <server:port> --name <slot>
   ```
   - On a Switch, add `--game-host <switch ip>`.
   - Every randomized location shows "You found an Archipelago item!". Your actual items arrive through the client, including your own.

## Status

The core loop works on Ryujinx: checks, received items with the game's own item-get display, resync, goal. TESTING.md lists what is verified and what is not.

**Known gaps:**
- In-game text doesn't name the item's sender or, for a found placeholder, the item and its owner. Shops label the placeholder with its model's name (Seashell Sensor).
- A few received items show their vanilla text, which refers to the original source (the Boomerang's trade, the Mad Batter's capacity upgrades).
- Dungeon entrance shuffle isn't supported.
- DeathLink is untested. Not tested on hardware.

## Licenses

- `las-ap-exl`: GPL-2.0, from exlaunch and Owen-Splat's fork; the socket code follows open-dread-rando-exlaunch.
- `patcher/RandomizerCore` and `apworld/las/data`: MIT, from LASR (see the LICENSE files in those folders). The bntx editor is GPLv3.
