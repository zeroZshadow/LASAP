# Hosting a ghost relay

Players see each other's ghosts through the default relay without any setup ([setup guide, Ghosts](../apworld/las/docs/setup_en.md#ghosts)). This is for running your own.

Multiplayer ghosts ([DESIGN.md](../DESIGN.md), Ghosts) go through a small relay that whoever hosts it runs as a service. It is one file, `apworld/las/ghost_relay.py`, with no dependencies beyond Python 3.8+:

```bash
python3 ghost_relay.py --port 43390
```

- It listens on all addresses by default (`--host` to change), plain TCP and WebSocket on the same port; open the port in the firewall. It logs one line when a slot joins (slot, player name, the first 8 hex digits of the group id, TCP or WebSocket) and one when it leaves (how long it stayed and why: disconnected, silent, replaced by a reconnect, a malformed frame); connections that never join (scanners, browsers, healthchecks, refused joins) log nothing. `--quiet` logs errors only.
- Players enter its address (host:port) as "Ghost relay" in the client's Patch tab; the setting defaults to `wss://ghosts.zerozshadow.com` (`DEFAULT_RELAY` in `ghost_relay.py`); the patch file's relay applies only while the field is empty.
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
