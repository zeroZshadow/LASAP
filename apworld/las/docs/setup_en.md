# Link's Awakening (Switch) Setup Guide

## Required software

- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer.
- The Legend of Zelda: Link's Awakening (2019), game version 1.0.0 with no update applied, on a Switch emulator.
  Ryujinx is tested. Yuzu-based emulators and a Switch with Atmosphère are untested.
- A RomFS dump of the game. In Ryujinx: right-click the game, then Extract Data, RomFS.
- The LAS patcher from the LASAP repository (`patcher/las_ap_patch.py` in its Python 3.12 venv, with the game module
  built into `patcher/exl`). The repository's README describes the setup.

## Installing the world

Double-click `las.apworld`, or copy it into Archipelago's `custom_worlds` folder.

## Configuring the client

The client reads its paths from the `las_options` section of `host.yaml` in the Archipelago folder. The section appears
the first time the client or a generation loads the world.

| Key | What it is |
|---|---|
| `romfs_folder` | The RomFS dump. The client asks for the folder when it is missing. |
| `sd_card_folder` | The SD card the game sees. The client writes `config/lasr-exl/config.ini` there, and the game keeps its collected checks in `lasap/`. The client asks for the folder when it is missing. |
| `mod_folder` | Where the mod's `romfs` and `exefs` folders go. Empty means `<sd_card_folder>/atmosphere/contents/01006BB00C6F0000`. |
| `patcher_command` | The command that runs the patcher, e.g. `"D:/LASAP/.venv-lasr/Scripts/python.exe D:/LASAP/patcher/las_ap_patch.py"`. |
| `game_start` | Optional command that starts the game after a patch file is opened, e.g. `"C:/Ryujinx/Ryujinx.exe D:/Games/LinksAwakening.nsp"`. |
| `game_host` | The address of the machine running the game: `127.0.0.1` for an emulator on this computer, the console's IP for a Switch. |

Folders per emulator:

| Emulator | `sd_card_folder` | `mod_folder` |
|---|---|---|
| Ryujinx | `sdcard` in the Ryujinx folder (File, Open Ryujinx Folder); the default for a standard install | empty |
| Yuzu-based | `sdmc` in the emulator's data folder | `<data folder>/load/01006BB00C6F0000/Archipelago` |
| Switch with Atmosphère | the SD card root | empty |

The client replaces the `romfs` and `exefs` folders in `mod_folder` on each new seed. Remove other Link's Awakening mods
from that folder first.

## Joining a multiworld game

1. Get your `.aplas` patch file. On the website it is on the room page; from a local generation it is in the output zip.
2. Open it with the Archipelago Launcher: double-click it, drag it onto the Launcher, or use "Open Patch". The
   Links Awakening Switch Client opens and:
   - patches the game and installs the mod, unless the mod for this seed is installed already;
   - starts the game when `game_start` is set;
   - connects to the server the patch names (patches from the website name their room) as your slot.
3. Start a new save file in the game. The client connects to the game while it runs; items arrive once the save is
   loaded.

Without a server in the patch file, enter the address in the client's top bar. `/patch` in the client patches and
installs again, `/patch <file>` does so with another patch file.

## Troubleshooting

- **The client says the game runs another seed.** The installed mod belongs to another slot or seed. Open this slot's
  patch file again, or use `/patch`, then restart the game.
- **Patching failed.** The client log shows the patcher's output. Check `patcher_command` and `romfs_folder`; the RomFS
  dump must be of version 1.0.0.
- **The client never connects to the game.** The game listens on TCP port 43384. Check `game_host`, and on a Switch,
  that the console and the computer are on the same network.
