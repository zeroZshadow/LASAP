# Link's Awakening (Switch) Setup Guide

## Required software

- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.7 or newer.
- The Legend of Zelda: Link's Awakening (2019), game version 1.0.0, on a Switch emulator. Ryujinx and Eden are
  tested; other yuzu-based emulators should work the same way. A Switch with Atmosphère is untested.
- The game's update turned off, if you installed it: in Ryujinx, right-click the game, Manage Title Updates, and
  choose No Update; in Eden, right-click the game, Properties, Add-Ons, and untick Update.
- A RomFS dump of the game, made with the update turned off. In Ryujinx: right-click the game, then Extract Data,
  RomFS.
- Nothing else: the world carries the patcher and runs it inside the client.

## Installing the world

Double-click `las.apworld`, or copy it into Archipelago's `custom_worlds` folder.

## Joining a multiworld game

1. Get your `.aplas` patch file: on the website it is on the room page, from a local generation it is in the zip.
2. Open it: double-click it, drag it onto the Archipelago Launcher, or use the Launcher's "Open Patch". The Links
   Awakening Switch Client opens on its Patch tab.
3. The first time, choose your emulator's program with Browse: `Ryujinx.exe`, or `eden.exe` (or another yuzu-based
   emulator). The client finds the emulator's SD card and mod folder, and the game in the emulator's game list.
4. Choose the RomFS dump, also once. Each setting shows what to do when it is missing:
   - Ryujinx: right-click Link's Awakening, Extract Data, RomFS, and pick a folder. Choose that folder.
   - Yuzu-based emulators: right-click Link's Awakening, Dump RomFS. The client finds the dump by itself.
5. Press **Play**. The client patches the game, installs the mod and starts the emulator with the game. The Patch tab
   says when the mod is installed.
   It connects to the server the patch names (patches from the website name their room) as your slot; otherwise,
   enter the address in the top bar.
6. Start a new save file. Items arrive once the save is loaded. Once the client is connected to both the game and the
   server, a small popup at the bottom right says "Archipelago connected as <slot>". The game also says when it loses
   the client.

The client remembers its settings. Opening the next seed's patch file installs it and starts the game without
further questions. Play also reinstalls the mod after an update of the world. "Reinstall mod" installs the current
seed again without starting the game.

"Open .aplas files with a double-click" at the bottom of the Patch tab sets Windows up to open `.aplas` files with
the Archipelago Launcher. It is needed once, as Archipelago's installer only does this for the games it ships with.

### Game settings

The Patch tab's game settings change how your own game looks and plays, for every seed: 360° movement, walking and
swimming 20% faster (on unless you turn it off), no blur at the screen's edges, skipping the song cutscenes, a
hookshot that grabs any surface, the damage Link takes (None, Normal or OHKO), the music (Vanilla, Shuffled or
Removed), shuffled dialogue, shuffled room lighting, no Guardian Acorns or Pieces of Power, text that shows a page at
once instead of letter by letter, no beep at low health, and a fishing line that never breaks, with Ol' Baron biting
more often (on unless you turn it off). After a change, Play reinstalls the mod; restart the
game to see it. They change nothing for the other players.

To start with more hearts, add Heart Containers to `start_inventory_from_pool` in your options file, e.g.
`Heart Container: 2` for five hearts.

### Advanced settings

The Patch tab's advanced settings are for setups the client cannot find by itself:

| Setting | When to set it |
|---|---|
| SD card folder | An emulator the client does not know, a Ryujinx data folder used with `--root-data-dir` (choose its `sdcard` folder; the client then starts Ryujinx with that data folder), or a Switch's SD card |
| Mod folder | Mods go somewhere other than the emulator's default: `<SD card>/atmosphere/contents/01006BB00C6F0000` for Ryujinx and a Switch, `<data folder>/load/01006BB00C6F0000/Archipelago` for yuzu-based emulators |
| Patcher command | Leave it empty: the client uses the patcher the world carries. Set it only to run another patcher program (see the repository's README) |
| Game address | The Switch's IP address when the game runs on a Switch; `127.0.0.1` for an emulator on this computer |

The settings are saved in `host.yaml` under `las_options`.

## In-game hints

Owl statues and the library's books tell where your items are (option In-Game Hints): each one names a location, in
your world or another player's, that holds one of your progression items. Dungeon owl statues need that dungeon's
stone beak. With the default setting `server`, a hint you read also shows in the server's hint list, as `!hint` would,
without costing hint points. Hints read while the client is not connected reach the server once it is.

## Troubleshooting

- **The client says the game runs another seed.** The installed mod belongs to another slot or seed. Open this slot's
  patch file, press Play, then restart the game.
- **Another Link's Awakening mod is installed.** The emulator would load it together with this one. Delete the folder
  the Patch tab names.
- **Patching failed.** The Patch tab says why, and the Archipelago tab shows the patcher's output. "The RomFS dump is
  not of game version 1.0.0": turn the game's update off (Required software) and dump the RomFS again.
- **The emulator changed nothing.** Mods load when the game starts: close the game and start it again.
- **The game shows "Archipelago client not connected".** Start the client; the game connects when it runs. The
  Patch tab shows the game connection.
- **The game shows "Archipelago server not connected".** The client runs but is not in your slot: enter the room's
  address at the top of the client and press Connect. Until then the map shows every check as reachable ("Offline"
  on the map).
- **The client never connects to the game.** The game listens on TCP port 43384: run one modded game at a time, and
  turn the emulator's airplane mode off if it has one. Check the game address, and on a Switch, that the console and
  the computer are on the same network.
