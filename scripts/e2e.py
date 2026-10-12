"""Drives a scripted AP session: a MultiServer and an LAS client whose stdin are fed from files (see TESTING.md).

python e2e.py send <item name> [<item name> ...]   server /send to the slot
python e2e.py srv <server command>                 any server console command
python e2e.py dbg <args>                           client /dbg (game debug command)
python e2e.py cli <client command>                 any client console command
python e2e.py state                                prints the game's STATE line
python e2e.py goto <x> <y> <z> [step]              teleports in hops (default 9 units) so each room loads first
python e2e.py wait <regex> [seconds] [client|server]  waits for a new log line
python e2e.py tail [n]                             last lines of both logs
python e2e.py run <script.txt>                     runs a drive.py script through the client (HELLO = client connected)

Environment: LASAP_E2E (session folder, default D:/DEV/LASAP-sandbox/e2e), LASAP_SLOT (default Link).
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

DIR = Path(os.environ.get("LASAP_E2E", "D:/DEV/LASAP-sandbox/e2e"))
SLOT = os.environ.get("LASAP_SLOT", "Link")
LOGS = {"client": DIR / "client.log", "server": DIR / "server.log"}


def append(path: Path, line: str) -> None:
    with open(path, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def size(log: str) -> int:
    return LOGS[log].stat().st_size if LOGS[log].exists() else 0


def wait(pattern: str, seconds: float, log: str, start: int) -> str:
    rx = re.compile(pattern)
    end = time.time() + seconds
    while time.time() < end:
        with open(LOGS[log], "rb") as f:
            f.seek(start)
            for line in f.read().decode("utf-8", "replace").splitlines():
                if rx.search(line):
                    return line
        time.sleep(0.25)
    raise SystemExit(f"timeout waiting for /{pattern}/ in {log}.log")


def state() -> str:
    start = size("client")
    append(DIR / "cmds.txt", "/dbg state")
    return wait(r"game: STATE", 10, "client", start)


def goto(x: float, y: float, z: float, step: float) -> str:
    """A teleport into a room that is not loaded has no floor, so hop through the rooms in between."""
    m = re.search(r"pos=([-\d.]+),([-\d.]+),([-\d.]+)", state())
    if not m:
        raise SystemExit("no player position")
    cx, cy, cz = map(float, m.groups())
    hops = max(1, int(max(abs(x - cx), abs(z - cz)) / step + 0.999))
    for i in range(1, hops + 1):
        t = i / hops
        hy = y if i == hops else max(cy, y)
        append(DIR / "cmds.txt", f"/dbg pos {cx + (x - cx) * t:.2f} {hy:.2f} {cz + (z - cz) * t:.2f}")
        time.sleep(1.0)
    time.sleep(1.0)
    return state()


def run(script: Path) -> None:
    origin = size("client")
    for raw in script.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        op, _, rest = line.partition(" ")
        if op == "send":
            append(DIR / "cmds.txt", "/dbg " + rest[4:] if rest.startswith("DBG ") else rest)
        elif op == "wait":
            time.sleep(float(rest))
        elif op == "expect":
            parts = rest.split()
            rx = "Connected to the game" if parts[0] == "HELLO" else parts[0]
            print(wait(rx, float(parts[1]) if len(parts) > 1 else 10, "client", origin if parts[0] == "HELLO" else size("client")))
        elif op == "shot":
            subprocess.run([sys.executable, str(Path(__file__).parent / "emu.py"), "shot", rest], check=True)


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "send":
        for item in args:
            append(DIR / "server_cmds.txt", f"/send {SLOT} {item}")
    elif cmd == "srv":
        append(DIR / "server_cmds.txt", " ".join(args))
    elif cmd == "dbg":
        append(DIR / "cmds.txt", "/dbg " + " ".join(args))
    elif cmd == "cli":
        append(DIR / "cmds.txt", " ".join(args))
    elif cmd == "state":
        print(state())
    elif cmd == "goto":
        x, y, z = map(float, args[:3])
        print(goto(x, y, z, float(args[3]) if len(args) > 3 else 9.0))
    elif cmd == "wait":
        log = args[2] if len(args) > 2 else "client"
        print(wait(args[0], float(args[1]) if len(args) > 1 else 10, log, size(log)))
    elif cmd == "run":
        run(Path(args[0]))
    elif cmd == "tail":
        n = int(args[0]) if args else 8
        for log, path in LOGS.items():
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()[-n:]
            print(f"--- {log}")
            print("\n".join(re.sub(r"\x1b\[[0-9;]*m", "", l) for l in lines))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
