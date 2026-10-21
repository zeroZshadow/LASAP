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

The player's guide is `apworld/las/docs/setup_en.md`: install the world, open the `.aplas`, choose the emulator and the RomFS dump in the client's Patch tab, and press Play.

The release's `las.apworld` is all a player installs, on any system Archipelago runs on: it carries the patcher and the game module, and the client runs the patcher in its own process. The world is needed only by whoever generates the seed and by the LAS player; other players need nothing new.

### Building a release

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

### Each seed

1. **Options.** In the Launcher, "Generate Template Options" writes `Links Awakening Switch.yaml` to `Players/Templates`. Its defaults are LASR's default settings:
   - Maps, compasses and stone beaks are in the starting inventory.
   - Small keys stay in their own dungeon.
   - Instruments can be anywhere.
   - Traps are set to many.
   - You start with the sword, shield, ocarina and Manbo's Mambo.

   Set your `name`, put the file in `Players` with everyone else's, and generate. Host the result as usual, on archipelago.gg or with the Launcher's "Host".
2. **Open the patch.** The output zip has a patch file per LAS slot, `AP_<seed>_P<n>_<slot>.aplas`. Open it with the Launcher (double-click, drag onto the Launcher, or "Open Patch"). The Links Awakening Switch Client opens on its Patch tab; the first time, choose the emulator and the RomFS dump there. Play patches and installs the mod unless that seed's mod from this version of the world is installed already, starts the emulator with the game, and the client connects to the server named in the patch.
3. **Play.**
   - Start a new save.
   - Connect the client to the server if the patch named none. The client finds the game at the game address, `127.0.0.1` by default; emulators need no settings for that.
   - Every randomized location holds an Archipelago item. It shows the item's model when the item is a LAS item or its name matches one (`apworld/las/data/item_models.yml`), otherwise the Archipelago model, and its text names the item and whose it is. Your items, including the ones you find yourself, arrive through the client. Checks made while the client is offline are sent when it reconnects.

The patcher also runs on its own: `.venv-lasr/Scripts/python.exe patcher/las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <out dir>` writes the SD card layout (`atmosphere/contents/01006BB00C6F0000`, `config/lasr-exl/config.ini`) to `<out dir>`.

## Ghost relay

Multiplayer ghosts (DESIGN.md, Ghosts) go through a small relay that whoever hosts it runs as a service. It is one file, `apworld/las/ghost_relay.py`, with no dependencies beyond Python 3.8+:

```bash
python3 ghost_relay.py --port 43390
```

- It listens on all addresses by default (`--host` to change), plain TCP and WebSocket on the same port; open the port in the firewall. It logs one line when a slot joins (slot, player name, the first 8 hex digits of the group id, TCP or WebSocket) and one when it leaves (how long it stayed and why: disconnected, silent, replaced by a reconnect, a malformed frame); connections that never join (scanners, browsers, healthchecks, refused joins) log nothing. `--quiet` logs errors only.
- Players enter its address (host:port) as "Ghost relay" in the client's Patch tab; the setting defaults to `wss://ghosts.zerozshadow.com` (`DEFAULT_RELAY` in `ghost_relay.py`), and while it holds that default a relay from the patch file wins over it.
- Behind a Cloudflare Tunnel (or another proxy that passes WebSocket) no port needs opening: point the tunnel's public hostname at `http://localhost:43390` and run the relay with `--host 127.0.0.1`. Players enter `wss://<hostname>` (the `https://` address works too). A WebSocket from this computer counts as the address in its `CF-Connecting-IP` header (else the last `X-Forwarded-For` entry), so the per-address limit counts players, not the proxy. A proxy on another host or in another container is trusted only with `--trusted-proxy <address, network or host name>` (or `LAS_GHOST_RELAY_TRUSTED_PROXY`); without it every player counts as the proxy's one address. `cloudflared tunnel --url http://localhost:43390` gives a temporary `https://….trycloudflare.com` address without a Cloudflare account.
- As a container: `docker/ghost-relay/Dockerfile` (build from the repository root), published by the "Ghost relay image" workflow as `ghcr.io/<owner>/las-ghost-relay` with the tags `latest`, `protocol-<PROTOCOL_VERSION>` and `sha-<commit>`. It listens on 43390 as user `nobody`, with a healthcheck; arguments after the image go to the relay. Behind a reverse proxy container on a shared network, publish no port and pass the proxy's container name: `command: ["--trusted-proxy", "<proxy container>"]`, with the proxy forwarding the relay's hostname to `<relay container>:43390` (proxies such as Caddy pass the WebSocket upgrade on by themselves). A `protocol-<n>` tag stays on the protocol its clients speak; `latest` follows the newest release.
- `--password <password>` (or the environment variable `LAS_GHOST_RELAY_PASSWORD`) lets in only players who enter the relay as `password@host:port` (`password@wss://<hostname>` through a tunnel); without it anyone may use the relay. For a service, put it in an `Environment=LAS_GHOST_RELAY_PASSWORD=...` line rather than on the command line.
- `--nogroups` lets the players of all multiworlds on the relay see each other, not only those of the same multiworld (at most `--max-group` players in all).
- `--max-group <players>` (or the environment variable `LAS_GHOST_RELAY_MAX_GROUP`) sets how many players a group holds, or with `--nogroups` the whole relay; default 64.
- As a systemd service (`/etc/systemd/system/las-ghost-relay.service`):
  ```ini
  [Unit]
  Description=LAS Archipelago ghost relay
  After=network-online.target

  [Service]
  ExecStart=/usr/bin/python3 /opt/las-ghost-relay/ghost_relay.py --port 43390
  DynamicUser=yes
  Restart=always

  [Install]
  WantedBy=multi-user.target
  ```
  Then `systemctl enable --now las-ghost-relay`.

## Status

The core loop works on Ryujinx: checks, received items with the game's own item-get display, resync, goal. TESTING.md lists what is verified and what is not.

**Known gaps:**
- Dungeon entrance shuffle isn't supported.
- Not tested on hardware.

## Licenses

- `las-ap-exl`: GPL-2.0, from exlaunch and Owen-Splat's fork; the socket code follows open-dread-rando-exlaunch.
- `patcher/RandomizerCore` and `apworld/las/data`: MIT, from LASR (see the LICENSE files in those folders). The bntx editor is GPLv3.
