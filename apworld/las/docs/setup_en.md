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

### Look and sound

The Patch tab's "Look and sound" settings change how your own game looks and sounds, for every seed: no blur at the
screen's edges, the music (Vanilla, Shuffled or Removed), shuffled dialogue, shuffled room lighting and no beep at low
health. After a change, Play reinstalls the mod; restart the game to see it. They change nothing for the other
players.

What changes how the game plays is in your options file (group "Gameplay"), the same for everyone in a seed: the
damage Link takes, walking and swimming 20% faster, 360° movement, skipping the song cutscenes, text that shows a
page at once, a hookshot that grabs any surface, no Guardian Acorns or Pieces of Power, and easy fishing.

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

## Ghosts

The other Links Awakening Switch players of your multiworld show as see-through Links when they are where you are: in
the same house, cave or dungeon, or near you in the overworld. They show with their tunic and the items they hold.
They can't touch you or anything else. Ghosts travel through a ghost relay, not the Archipelago server, and only
players of the same generation see each other.

- The person who generates sets the relay before generating: `ghost_relay` (`host:port`) under `las_options` in
  `host.yaml`. Every patch file then carries it, and the players have nothing to set.
- Otherwise each player enters it in the Patch tab's "Ghost relay", which also replaces the patch file's.
- "Show ghosts" on the Patch tab, or `/ghosts off` in the client, turns them off. The Patch tab shows the relay status.

To run a relay: `python ghost_relay.py` (in the world's folder; `las.apworld` is a zip holding it) needs only Python
and listens on TCP port 43390 (`--port` changes it). Every player must be able to reach it: `--forward` has your
router forward the port while the relay runs and prints the address players enter (if the router allows NAT-PMP or
UPnP);
otherwise forward the port on your router by hand, run the relay on a server, or have all players join one VPN (for
example Tailscale) and use its address.

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
- **The Patch tab says another client is connected to the game.** The game talks to one client at a time: close the
  other Links Awakening Switch Client window. Right after a client closes, the game takes a few seconds to notice.

## Known issues

- Some checks have not been played yet and may not reach the server: the owl statues' gifts (option Owl Statues), the
  golden leaf of the guard Link bombs in Kanalet Castle, Dampé's rewards (Dampe), the fishing pond's rewards
  (Fishing), the Rapids Ride (Rapids) and the Armos Knight's drop. If one gives nothing or the server does not mark it
  checked, report it.
- The option Easy Fishing (on by default) has not been fished with yet. If the line still breaks or a fish behaves
  oddly, report it; `easy_fishing: false` in your options file turns it off.
- The option Nice Hookshot and the settings Music and Blur Removal are not confirmed in play.
- Windows only so far: Linux and macOS are untested, and so is a Switch.
- Dungeon entrance shuffle is not available.

## Reporting a problem

Tell the person who gave you the world, with:

- what happened, what you expected, and where (the area, or the check's name from the map or the server);
- your slot name and the seed: the Patch tab's "Patch file" line names both;
- the client's log: the newest `LASClient` file in Archipelago's `logs` folder (the Launcher's "Browse Files" opens
  Archipelago's folder);
- your emulator and its version, and the look and sound settings you changed in the Patch tab;
- a screenshot or a short video of anything you see in the game;
- after a crash, the emulator's newest log file as well (Ryujinx: File, Open Logs Folder).
