"""Drives a scripted AP session: a MultiServer and an LAS client whose stdin are fed from files (see TESTING.md).

python e2e.py send <item name> [<item name> ...]   server /send to the slot
python e2e.py srv <server command>                 any server console command
python e2e.py dbg <args>                           client /dbg (game debug command)
python e2e.py cli <client command>                 any client console command
python e2e.py state                                prints the game's STATE line
python e2e.py goto <x> <y> <z> [step]              teleports in hops (default 9 units) so each room loads first
python e2e.py wait <regex> [seconds] [client|server]  waits for a new log line
python e2e.py tail [n]                             last lines of both logs
python e2e.py run <script.txt>                     runs a drive.py script through the client (HELLO = client connected);
                                                   also `state` (print the STATE line), `free [secs]` (wait until
                                                   the player can move), `loaded [secs]` (free in a level entered
                                                   after the last send, e.g. a warp), `at <x> <z> [tol]` (stop
                                                   unless the player is there), `level <name>` (stop unless the
                                                   player is in that level), `warp <level> <x> <y> <z>` (reload
                                                   there and wait), `goto <x> <y> <z>` and `shot <png>`

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
    sent = time.time()
    for raw in script.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        op, _, rest = line.partition(" ")
        if op == "send":
            append(DIR / "cmds.txt", "/dbg " + rest[4:] if rest.startswith("DBG ") else rest)
            sent = time.time()
        elif op == "wait":
            time.sleep(float(rest))
        elif op == "expect":
            parts = rest.split()
            rx = "Connected to the game" if parts[0] == "HELLO" else parts[0]
            print(wait(rx, float(parts[1]) if len(parts) > 1 else 10, "client", origin if parts[0] == "HELLO" else size("client")))
        elif op == "mash":
            # mash <buttons hex> <count> [hold ms] [gap s]
            p = rest.split()
            for _ in range(int(p[1])):
                append(DIR / "cmds.txt", f"/dbg input {p[0]} {p[2] if len(p) > 2 else '400'}")
                time.sleep(float(p[3]) if len(p) > 3 else 0.9)
        elif op == "shot":
            subprocess.run([sys.executable, str(Path(__file__).parent / "emu.py"), "shot", rest], check=True)
        elif op == "state":
            print(state())
        elif op == "screen":
            # screen <intro|title|menu|loading|level> [secs]: stops the script unless that screen shows in time
            p = rest.split()
            wait_screen(p[0], float(p[1]) if len(p) > 1 else 5)
        elif op == "press":
            # press <buttons hex> <ms> <screen> [tries]: presses until that screen shows, 2 s per try
            p = rest.split()
            for _ in range(int(p[3]) if len(p) > 3 else 3):
                append(DIR / "cmds.txt", f"/dbg input {p[0]} {p[1]}")
                sent = time.time()
                if wait_screen(p[2], 2, fail=False):
                    break
            else:
                raise SystemExit(f"pressing {p[0]} never reached screen={p[2]}: {state()}")
        elif op == "free":
            wait_free(float(rest or 30))
        elif op == "dismiss":
            # dismiss [secs]: presses B until the player can move. B closes text without starting a talk.
            end = time.time() + float(rest or 60)
            while not is_free(state()):
                if time.time() > end:
                    raise SystemExit("timeout dismissing")
                append(DIR / "cmds.txt", "/dbg input 2 200")
                time.sleep(0.8)
        elif op == "loaded":
            wait_free(float(rest or 40), since=sent)
        elif op == "inlevel":
            # inlevel [secs]: waits until a level entered after the last send has loaded, free or not
            end = time.time() + float(rest or 60)
            while True:
                line = state()
                ms = re.search(r"ms_in_level=(\d+)", line)
                if "player=1" in line and ms and time.time() - int(ms.group(1)) / 1000 > sent:
                    break
                if time.time() > end:
                    raise SystemExit(f"timeout waiting for a level: {line}")
                time.sleep(1)
        elif op == "warp":
            # warp <level> <x> <y> <z>: reloads the level at that position, then waits until the player is free.
            # A warp within the current level does not reload it, so it passes through another level first.
            level = rest.split()[0]
            if f"level={level} " in state():
                other = "Field 38.25 10.5 128.25" if level != "Field" else "MarinTarinHouse 6.85 0 3.83"
                append(DIR / "cmds.txt", f"/dbg warp {other}")
                wait_free(40, since=time.time())
            append(DIR / "cmds.txt", f"/dbg warp {rest}")
            wait_free(40, since=time.time())
        elif op == "goto":
            print(goto(*[float(v) for v in rest.split()][:3], step=9.0))
        elif op == "level":
            # level <name>: stops the script unless the player is in that level
            line = state()
            if f" level={rest} " not in line:
                raise SystemExit(f"not in level {rest}: {line}")
        elif op == "at":
            # at <x> <z> [tolerance]: stops the script unless the player stands there
            p = [float(v) for v in rest.split()]
            line = state()
            pos = re.search(r"pos=([-\d.]+),([-\d.]+),([-\d.]+)", line)
            if pos is None:
                raise SystemExit(f"no player position: {line}")
            x, z = float(pos.group(1)), float(pos.group(3))
            if abs(x - p[0]) > (p[2] if len(p) > 2 else 0.75) or abs(z - p[1]) > (p[2] if len(p) > 2 else 0.75):
                raise SystemExit(f"player not at ({p[0]}, {p[1]}): {line}")


def wait_free(seconds: float, since: float = 0) -> None:
    """Waits until the player can move (after a dialogue or an item-get). With `since`, the level must also have
    been entered after that time, so a warp has finished loading."""
    end = time.time() + seconds
    while True:
        line = state()
        if is_free(line, since):
            return
        if time.time() > end:
            raise SystemExit(f"timeout waiting for the player to be free: {line}")
        time.sleep(1)


def wait_screen(name: str, seconds: float, fail: bool = True) -> bool:
    """Waits until the STATE line shows `screen=<name>`; `name` may list alternatives: `loading|level`."""
    end = time.time() + seconds
    while True:
        line = state()
        if any(f"screen={n} " in line for n in name.split("|")):
            return True
        if time.time() > end:
            if fail:
                raise SystemExit(f"timeout waiting for screen={name}: {line}")
            return False
        time.sleep(0.3)


def is_free(line: str, since: float = 0) -> bool:
    """The STATE line shows the player free in a level entered after `since`, for at least 1.5 s, with no item-get
    showing or queued."""
    ms = re.search(r"ms_in_level=(\d+)", line)
    if not ms or "player=1" not in line or "event=0 paused=0" not in line or not re.search(r"showq=0(\s|$)", line):
        return False
    return int(ms.group(1)) > 1500 and time.time() - int(ms.group(1)) / 1000 > since


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
