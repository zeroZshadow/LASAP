# Link's Awakening (Switch) Setup Guide

## Required software

- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer.
- The Legend of Zelda: Link's Awakening (2019), game version 1.0.0 with no update applied, on a Switch emulator.
  Ryujinx and Eden are tested; other yuzu-based emulators should work the same way. A Switch with Atmosphère is
  untested.
- A RomFS dump of the game. In Ryujinx: right-click the game, then Extract Data, RomFS.
- Nothing else: the world carries the patcher and runs it inside the client.

## Installing the world

Double-click `las.apworld`, or copy it into Archipelago's `custom_worlds` folder.

## Joining a multiworld game

1. Get your `.aplas` patch file: on the website it is on the room page, from a local generation it is in the zip.
2. Open it: double-click it, drag it onto the Archipelago Launcher, or use the Launcher's "Open Patch". The Links
   Awakening Switch Client opens on its Patch tab.
3. The first time, choose your emulator's program with Browse: `Ryujinx.exe`, or `eden.exe` (or another yuzu-based
   emulator). The client finds the emulator's SD card and mod folder, and the game in the emulator's game list. When
   the emulator is running, the client finds it by itself.
4. Choose the RomFS dump, also once. Each setting shows what to do when it is missing:
   - Ryujinx: right-click Link's Awakening, Extract Data, RomFS, and pick a folder. Choose that folder.
   - Yuzu-based emulators: right-click Link's Awakening, Dump RomFS. The client finds the dump by itself.
5. Press **Install and play**. The client patches the game, installs the mod and starts the emulator with the game.
   It connects to the server the patch names (patches from the website name their room) as your slot; otherwise,
   enter the address in the top bar.
6. Start a new save file. Items arrive once the save is loaded. Once the client is connected to both the game and the
   server, a small popup at the bottom right says "Archipelago connected as <slot>". The game also says when it loses
   the client.

The client remembers its settings. Opening the next seed's patch file installs it and starts the game without
further questions. "Patch again" reinstalls the current seed.

"Open .aplas files with a double-click" at the bottom of the Patch tab sets Windows up to open `.aplas` files with
the Archipelago Launcher. It is needed once, as Archipelago's installer only does this for the games it ships with.

### Advanced settings

The Patch tab's advanced settings are for setups the client cannot find by itself:

| Setting | When to set it |
|---|---|
| SD card folder | An emulator the client does not know, a Ryujinx data folder used with `--root-data-dir` (choose its `sdcard` folder; the client then starts Ryujinx with that data folder), or a Switch's SD card |
| Mod folder | Mods go somewhere other than the emulator's default: `<SD card>/atmosphere/contents/01006BB00C6F0000` for Ryujinx and a Switch, `<data folder>/load/01006BB00C6F0000/Archipelago` for yuzu-based emulators |
| Patcher command | Leave it empty: the client uses the patcher the world carries. Set it only to run another patcher program (see the repository's README) |
| Game address | The Switch's IP address when the game runs on a Switch; `127.0.0.1` for an emulator on this computer |

The settings are saved in `host.yaml` under `las_options`.

## Troubleshooting

- **The client says the game runs another seed.** The installed mod belongs to another slot or seed. Open this slot's
  patch file, press Install and play, then restart the game.
- **Another Link's Awakening mod is installed.** The emulator would load it together with this one. Delete the folder
  the Patch tab names.
- **Patching failed.** The Archipelago tab shows the patcher's output. The RomFS dump must be of version 1.0.0.
- **The emulator changed nothing.** Mods load when the game starts: close the game and start it again.
- **The game shows "Archipelago client not connected".** Start the client; the game connects when it runs. The
  Patch tab shows the game connection.
- **The Patch tab says the modded game runs in two programs.** Close all emulators but one; the client may otherwise
  talk to the wrong game.
- **The client never connects to the game.** The game listens on TCP port 43384. Check `game_host`, and on a Switch,
  that the console and the computer are on the same network.
