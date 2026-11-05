# Link's Awakening (Switch) Setup Guide

## Required software

- [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer.
- The Legend of Zelda: Link's Awakening (2019) at game version 1.0.0, on Ryujinx, Eden or another yuzu-based emulator.
- The game's update turned off. In Ryujinx: right-click the game, Manage Title Updates, No Update. In Eden: right-click
  the game, Properties, Add-Ons, untick Update.
- A RomFS dump of the game, made with the update off.

## Installing the world

Double-click `las.apworld`, or copy it into Archipelago's `custom_worlds` folder.

## Joining a multiworld game

1. Get your `.aplas` patch file, from the website's room page or the local generation's zip.
2. Open it by double-clicking it, dragging it onto the Archipelago Launcher, or using the Launcher's "Open Patch". The
   client opens on its Patch tab.
3. The first time, the Patch tab walks you through four steps:
   1. **Patch file**: leave "Open .aplas patch files in this client when I double-click them" ticked.
   2. **Where do you play?**: an emulator on this computer, or a Nintendo Switch.
   3. **Your emulator**: choose its program. The client finds the rest.

      **Your Switch**: put its SD card in this computer and choose it, then enter the Switch's IP address (System
      Settings, Internet, Connection Status).
   4. **Game files**: the RomFS dump.
      - Ryujinx: right-click the game, Extract Data, RomFS, pick a folder, then choose that folder in the client.
      - Yuzu-based emulators: right-click the game, Dump RomFS. The client finds it.
      - A Switch: dump it with a tool such as nxdumptool and copy it to this computer.
4. Press **Play** (with a Switch: **Install to SD card**). The client patches the game, installs the mod and starts the
   emulator. With a Switch, put the SD card back and start the game.
5. If the client is not connected to the server, enter the room's address in the top bar.
6. Start a new save file. Items arrive once it is loaded, and a popup says "Archipelago connected as \<slot\>".

For the next seed, open its patch file and press Play. "Reinstall the mod without starting" installs the current seed
again.

### Look and sound

These Patch tab settings apply to your game only, for every seed. After a change, press Play and restart the game.

- No blur at the screen's edges
- Music: Vanilla, Shuffled or Removed
- Shuffle the dialogue
- Shuffle the rooms' lighting
- No low-health beep
- No drops to 30 fps: On emulators (default), Always or Never

### Advanced settings

These are for setups the client cannot find by itself. "In use" shows the current value, even when the setting is empty.
They are saved in `host.yaml` under `las_options`.

| Setting | When to set it |
|---|---|
| SD card folder | An unknown emulator, or a Ryujinx `--root-data-dir` folder (choose its `sdcard` folder) |
| Mod folder | Mods go somewhere other than `<SD card>/atmosphere/contents/01006BB00C6F0000` (Ryujinx, Switch) or `<data folder>/load/01006BB00C6F0000/Archipelago` (yuzu-based) |
| Patcher command | Only to run another patcher program |
| Game address | The Switch's IP address, or `127.0.0.1` for an emulator on this computer |

## Ghosts

Other Link's Awakening players in your multiworld show as see-through Links where you are. They can't touch anything.
Ghosts are on by default and use `wss://ghosts.zerozshadow.com`. If that relay is down, the game plays as usual.

The Patch tab's Ghosts settings have matching client commands:

| Setting | Command |
|---|---|
| Show ghosts | `/ghosts on\|off` |
| Show their slot names over their heads (also Up on the D-pad) | `/ghostnames on\|off` |
| Ghost relay: `host:port`, `password@host:port` or `wss://<hostname>` | `/ghostrelay <address>` or `/ghostrelay default` |

Each player sets their own relay. A generating host can also set `ghost_relay` under `las_options` in `host.yaml`, which
applies to players who have emptied the field.

### Running your own relay

On Windows, double-click `LASGhostRelay.exe` from the release. It asks for a password (Enter for none), listens on TCP
port 43390, asks your router to forward the port, and prints the address players enter. Close its window to stop it.

On other systems, run `python3 ghost_relay.py --forward`. `las.apworld` is a zip that holds `las/ghost_relay.py`.

Every player must be able to reach the relay. If the router does not forward the port, forward TCP 43390 by hand, run
the relay on a server, or use a VPN such as Tailscale. `docs/ghost-relay.md` in the repository covers the options,
proxies and services.

## Troubleshooting

- **The client says the game runs another seed.** Open this slot's patch file, press Play, then restart the game.
- **Another Link's Awakening mod is installed.** Delete the folder the Patch tab names.
- **Patching failed.** The Patch tab says why, and the Archipelago tab has the patcher's output. For "The RomFS dump is
  not of game version 1.0.0", turn the update off and dump the RomFS again.
- **The RomFS dump is incomplete.** Dump it again with the update off, and choose the folder holding `region_common` and
  the six `region` folders.
- **The emulator changed nothing.** Mods load when the game starts, so restart the game.
- **The game shows "Archipelago client not connected".** Start the client.
- **The game shows "Archipelago server not connected".** Enter the room's address at the top of the client and press
  Connect.
- **The client never connects to the game.** The game listens on TCP port 43384. Run one modded game at a time and turn
  off the emulator's airplane mode. On a Switch, check the game address and that both devices share a network.
- **Another client is connected to the game.** Close the other client window. The game takes a few seconds to notice.

## Known issues

- Tested on Windows emulators only. Linux, macOS and a Switch are untested.
- Dampé's rewards have not been played yet and may not reach the server.
- Easy Fishing (on by default), Nice Hookshot, Music and blur removal have not been confirmed in play.
- A received Heart Container or fourth Piece of Heart can leave the health display a heart short until your health fills
  into it or the level reloads.

## Reporting a problem

Open an [issue on GitHub](https://github.com/zeroZshadow/LASAP/issues), or tell the person who gave you the world.
Include:

- what happened and where
- your slot name and seed, from the top of the Patch tab
- the newest `LASClient` file in Archipelago's `logs` folder
- your emulator and version, and any look and sound settings you changed
- a screenshot or short video, if it shows in the game
- after a crash, the emulator's newest log (Ryujinx: File, Open Logs Folder)
