# Hosting a ghost relay

Players use the default relay without setup. This page is for running your own. The relay is one file, `apworld/las/ghost_relay.py`, and needs only Python 3.8+:

```bash
python3 ghost_relay.py --port 43390
```

It takes TCP and WebSocket on the same port. Open that port in the firewall, and players enter `host:port` as "Ghost relay" in the client's Patch tab.

| Option | Environment variable | What it does |
|---|---|---|
| `--host <address>` | | Address to listen on. Default: all |
| `--port <port>` | | Default: 43390 |
| `--forward` | | Has the router forward the port (NAT-PMP, else UPnP) |
| `--password <password>` | `LAS_GHOST_RELAY_PASSWORD` | Players enter `password@host:port` or `password@wss://<hostname>` |
| `--nogroups` | | All multiworlds on the relay see each other |
| `--max-group <players>` | `LAS_GHOST_RELAY_MAX_GROUP` | Players per group, or on the relay with `--nogroups`. Default: 64 |
| `--trusted-proxy <address, network or host name>` | `LAS_GHOST_RELAY_TRUSTED_PROXY` | A remote proxy whose headers the relay trusts |
| `--quiet` | | Logs errors only |

## Behind a proxy or tunnel

Point the proxy's hostname at `http://localhost:43390` and run the relay with `--host 127.0.0.1`. Players enter `wss://<hostname>`. The proxy must pass WebSocket.

The relay limits connections per address, and reads the real address from `CF-Connecting-IP` or `X-Forwarded-For`. It trusts those headers from this computer, and from a proxy elsewhere only with `--trusted-proxy`. Otherwise every player counts as the proxy.

`cloudflared tunnel --url http://localhost:43390` gives a temporary address without a Cloudflare account.

## As a container

Each release publishes `ghcr.io/<owner>/las-ghost-relay`, from `docker/ghost-relay/Dockerfile`. Tags are `latest`, the release's version, `protocol-<PROTOCOL_VERSION>` and `sha-<commit>`. It listens on 43390, and arguments after the image go to the relay.

Behind a reverse proxy container, publish no port, pass `command: ["--trusted-proxy", "<proxy container>"]`, and forward the hostname to `<relay container>:43390`.

## As a systemd service

In `/etc/systemd/system/las-ghost-relay.service`, then `systemctl enable --now las-ghost-relay`:

```ini
[Unit]
Description=LAS Archipelago ghost relay
After=network-online.target

[Service]
ExecStart=/usr/bin/python3 /opt/las-ghost-relay/ghost_relay.py --port 43390
DynamicUser=yes
Restart=always
# Environment=LAS_GHOST_RELAY_PASSWORD=...

[Install]
WantedBy=multi-user.target
```
