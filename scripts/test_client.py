"""Talks to the LAS AP module's TCP line protocol (see DESIGN.md) for manual and scripted tests.

Usage: python test_client.py [--host H] [--port P] [--wait S] "CMD ..." ["CMD ..." ...]
Each command is sent in order; every received line is printed. A PING is sent every 2 s.
"""
import argparse
import os
import socket
import sys
import time


def run(host: str, port: int, commands: list[str], wait: float, connect_timeout: float = 60.0) -> list[str]:
    deadline = time.time() + connect_timeout
    while True:
        try:
            sock = socket.create_connection((host, port), timeout=2)
            break
        except OSError:
            if time.time() > deadline:
                raise
            time.sleep(1)
    sock.settimeout(0.1)
    received: list[str] = []
    buf = b""
    last_ping = 0.0

    def pump(until: float) -> None:
        nonlocal buf, last_ping
        while time.time() < until:
            if time.time() - last_ping > 2:
                sock.sendall(b"PING\n")
                last_ping = time.time()
            try:
                data = sock.recv(4096)
                if not data:
                    raise ConnectionError("game closed the connection")
                buf += data
            except socket.timeout:
                continue
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace")
                if text != "PONG":
                    print("<", text, flush=True)
                received.append(text)

    pump(time.time() + 1.0)
    for cmd in commands:
        print(">", cmd, flush=True)
        sock.sendall(cmd.encode() + b"\n")
        pump(time.time() + 1.0)
    pump(time.time() + wait)
    sock.close()
    return received


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=int(os.environ.get("LASAP_GAME_PORT", "43385")),
                        help="game port; the sandbox game's by default (deploy_mod.sh)")
    parser.add_argument("--wait", type=float, default=2.0)
    parser.add_argument("commands", nargs="*")
    args = parser.parse_args()
    run(args.host, args.port, args.commands, args.wait)


if __name__ == "__main__":
    sys.exit(main())
