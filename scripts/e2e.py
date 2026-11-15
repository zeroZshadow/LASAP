"""Drives the sandbox game for tests (TESTING.md, "Observing and driving the game"): the test module's debug port for
DBG commands and the game's state, and a scripted AP session (a MultiServer and an LAS client whose stdin are fed from
files) for everything that goes through Archipelago.

python e2e.py send <item name> [<item name> ...]   server /send to the slot
python e2e.py srv <server command>                 any server console command
python e2e.py dbg <args>                           a DBG command on the debug port; prints its replies
python e2e.py cli <client command>                 any client console command
python e2e.py state                                prints the game's STATE line
python e2e.py goto <x> <y> <z>                     teleports within the loaded room, waits until Link stands there
python e2e.py wait <regex> [seconds] [client|server]  waits for a new log line
python e2e.py trace [seconds] [file]               prints (or writes) the module's trace for a while
python e2e.py tail [n]                             last lines of both logs
python e2e.py run <script.txt> [trace file]        runs a test script (TESTING.md, "Script ops")

Environment: LASAP_INSTANCE (a suite instance, emu.py; default 0, the sandbox), LASAP_E2E (session folder, default
the instance's e2e folder, the sandbox folder's e2e for the sandbox), LASAP_SLOT (default Link), LASAP_GAME_PORT
(the game port, default the instance's, 43385 for the sandbox; the debug port is 100 above it).
"""
import math
import os
import re
import socket
import sys
import threading
import time
from pathlib import Path
from typing import TextIO

import emu

DIR = Path(os.environ.get("LASAP_E2E", os.path.join(emu.HOME, "e2e")))
SLOT = os.environ.get("LASAP_SLOT", "Link")
LOGS = {"client": DIR / "client.log", "server": DIR / "server.log", "peer": DIR / "peer.log"}
GAME_PORT = int(os.environ.get("LASAP_GAME_PORT", str(emu.instance_port(emu.INSTANCE))))
DEBUG_PORT = GAME_PORT + 100
DROP_STILL = 0.05  # a drop actor's body at rest: each velocity component at most this (units a second)
DROP_FLOATING = 0.5  # in water, where a ball (Bouncy Items) drifts and dips: each component at most this
DROP_TOUCH = 0.35  # gotodrop: Link this far from the drop on his side, within its touch
REPLY = re.compile(r"^(OK|ERR|DONE|FAIL|HELD|HOLDS|CHOICE|STATE|FLAG|UI|FRAME|POPFRAME|PEEK|RECV|MAP|TEXT|MUSIC|map=)")


class Debug:
    """A connection to the test module's debug port. A reader thread keeps every line; trace lines (T ...) also go
    to the trace file when one is open. Connecting resets the module's guards to "dead" alone; an ALERT (a guard
    tripped) ends any wait."""

    def __init__(self, wait: float = 60):
        deadline = time.time() + wait
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", DEBUG_PORT), timeout=2)
                break
            except OSError:
                if time.time() > deadline:
                    raise SystemExit(f"no debug port on {DEBUG_PORT}: is the sandbox game running the test module?")
                time.sleep(0.2)
        self.sock.settimeout(None)
        self.lines: list[str] = []
        self.cond = threading.Condition()
        self.trace_file: TextIO | None = None
        self.closed = False
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._ping, daemon=True).start()
        if self.wait(r"^HELLO", 5, 0) is None:
            raise SystemExit("the debug port did not greet")
        self.request("DBG guard reset", r"^OK guard")

    def _read(self) -> None:
        buf = b""
        while True:
            try:
                data = self.sock.recv(65536)
            except OSError:
                data = b""
            if not data:
                with self.cond:
                    self.closed = True
                    self.cond.notify_all()
                return
            buf += data
            *done, buf = buf.split(b"\n")
            with self.cond:
                for raw in done:
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    if line == "PONG":
                        continue
                    self.lines.append(line)
                    if self.trace_file and (line.startswith("T ") or line.startswith("AP LOG level")):
                        self.trace_file.write(line + "\n")
                        self.trace_file.flush()
                self.cond.notify_all()

    def _ping(self) -> None:
        # Keeps this connection the newest-heard, so the module never drops it for another client.
        while not self.closed:
            time.sleep(2)
            try:
                self.sock.sendall(b"PING\n")
            except OSError:
                return

    def mark(self) -> int:
        with self.cond:
            return len(self.lines)

    def send(self, line: str) -> int:
        start = self.mark()
        self.sock.sendall(line.encode() + b"\n")
        return start

    def wait(self, pattern: str, seconds: float, start: int) -> str | None:
        """The first line from index `start` on that matches, or None after `seconds`."""
        rx = re.compile(pattern)
        end = time.time() + seconds
        with self.cond:
            i = start
            while True:
                while i < len(self.lines):
                    if rx.search(self.lines[i]):
                        return self.lines[i]
                    if self.lines[i].startswith("ALERT "):
                        raise SystemExit(f"game alert: {self.lines[i]}")
                    i += 1
                left = end - time.time()
                if left <= 0 or self.closed:
                    return None
                self.cond.wait(left)

    def alert(self, start: int) -> str | None:
        """The first ALERT line from index `start` on."""
        with self.cond:
            return next((l for l in self.lines[start:] if l.startswith("ALERT ")), None)

    def request(self, line: str, pattern: str, seconds: float = 10) -> str:
        start = self.send(line)
        reply = self.wait(pattern, seconds, start)
        if reply is None:
            raise SystemExit(f"no reply /{pattern}/ to {line}")
        return reply


_debug: Debug | None = None


def game(wait: float = 60) -> Debug:
    global _debug
    if _debug is None or _debug.closed:
        _debug = Debug(wait)
    return _debug


def dbg(command: str) -> int:
    """Sends a DBG command; returns the line index its replies start at."""
    return game().send(command if command.startswith("DBG ") else "DBG " + command)


def append(path: Path, line: str) -> None:
    with open(path, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def size(log: str) -> int:
    return LOGS[log].stat().st_size if LOGS[log].exists() else 0


def wait(pattern: str, seconds: float, log: str, start: int, count: int = 1) -> str:
    """Waits until `count` lines logged after `start` match; returns the last of them."""
    rx = re.compile(pattern)
    end = time.time() + seconds
    while time.time() < end:
        if LOGS[log].exists():
            with open(LOGS[log], "rb") as f:
                f.seek(start)
                found = [line for line in f.read().decode("utf-8", "replace").splitlines() if rx.search(line)]
            if len(found) >= count:
                return found[count - 1]
        time.sleep(0.1)
    raise SystemExit(f"timeout waiting for /{pattern}/ in {log}.log")


def state() -> str:
    return game().request("DBG state", r"^STATE ")


def position(line: str) -> tuple[float, float, float] | None:
    m = re.search(r"pos=([-\d.]+),([-\d.]+),([-\d.]+)", line)
    return (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else None


def goto(x: float, y: float, z: float) -> str:
    """Teleports within the loaded room and waits until Link stands still there. A room that is not loaded has no
    floor: warp to a target in another room. The game ignores `pos` during an event, so it waits for none first."""
    until("event=0", 10)
    game().request(f"DBG pos {x:.2f} {y:.2f} {z:.2f}", r"^OK pos")
    return landed(settle(x, z), x, y, z)


def landed(line: str, x: float, y: float, z: float) -> str:
    """Stops the script unless a teleport put Link on the floor at its target: a bush (ObjGrass), rock or pot there is
    solid, and Link stands on top of it or is pushed off it (TESTING.md, Finding a location)."""
    pos = position(line)
    if pos is None:
        raise SystemExit(f"no player position: {line}")
    if pos[1] > y + 1 or abs(pos[0] - x) > 0.75 or abs(pos[2] - z) > 0.75:
        raise SystemExit(f"Link landed at ({pos[0]:.2f}, {pos[1]:.2f}, {pos[2]:.2f}), not on the floor at "
                         f"({x:.2f}, {y:.2f}, {z:.2f}): on or in something solid there; {line}")
    return line


def settle(x: float, z: float, seconds: float = 10) -> str:
    """Waits until Link stands at (x, z) with the same position in two STATE lines 0.1 s apart, in no event (a
    landing plays one, and `pos` is ignored during it)."""
    end = time.time() + seconds
    last = None
    while True:
        line = state()
        pos = position(line)
        if pos and abs(pos[0] - x) < 0.75 and abs(pos[2] - z) < 0.75 and last and " event=0 " in line and \
                max(abs(a - b) for a, b in zip(pos, last)) < 0.02:
            return line
        if time.time() > end:
            raise SystemExit(f"Link did not settle at ({x:.2f}, {z:.2f}): {line}")
        last = pos
        time.sleep(0.1)


DROP_TAKEN = (0.0, 0.0, 0.0)  # what drop_rest returns for a ball Link took as it landed


def drop_rest(seconds: float, show: bool = False) -> tuple[float, float, float] | None:
    """Polls DBG drop every 0.2 s until the drop actor is active, its body still and where it was at the poll before
    (returned), or gone once it was there (None); stops the script when neither happens within `seconds`. A bouncing
    item can stand at the top of two bounces in a row. Floating, still allows DROP_FLOATING. A ball that lands on Link
    is taken on its way: its take event turns off its enabled flag (entity flags bit 0), and this returns DROP_TAKEN."""
    end = time.time() + seconds
    last = seen = None
    while True:
        reply = game().request("DBG drop", r"^OK drop")
        if show and reply != seen:
            print(reply)
        seen = reply
        taken = re.search(r" flags=([0-9a-f]+) ", reply)
        if taken and not int(taken.group(1), 16) & 1:
            return DROP_TAKEN
        m = re.search(r"OK drop ([-\d.]+),([-\d.]+),([-\d.]+) active=1", reply)
        at = tuple(float(v) for v in m.groups()) if m else None
        v = re.search(r" v=([-\d.]+),([-\d.]+),([-\d.]+)", reply)
        limit = DROP_STILL if " water=- " in reply else DROP_FLOATING
        still = v is None or all(abs(float(c)) <= limit for c in v.groups())
        near = at is not None and last is not None and all(abs(a - b) <= limit * 0.2 for a, b in zip(at, last))
        if near and still:
            return at
        if at is None and last is not None and reply.startswith("OK drop -"):
            return None
        if time.time() > end:
            raise SystemExit(f"no drop actor at rest: {reply}; {state()}")
        last = at if at is not None else last
        time.sleep(0.2)


def poll(test, seconds: float, what: str, interval: float = 0.05) -> str:
    """Polls the STATE line until test(line) holds."""
    end = time.time() + seconds
    while True:
        line = state()
        if test(line):
            return line
        if time.time() > end:
            raise SystemExit(f"timeout waiting for {what}: {line}")
        time.sleep(interval)


def until(cond: str, seconds: float) -> str:
    """Waits for a module condition (DBG await): exact to the frame, without polling."""
    start = dbg(f"await {cond} {int(seconds * 1000)}")
    reply = game().wait(rf"^(HELD|FAIL await) {re.escape(cond)}( |$)", seconds + 5, start)
    if reply is None or reply.startswith("FAIL"):
        raise SystemExit(f"timeout waiting for {cond}: {state()}")
    return reply


def module_op(command: str, name: str, seconds: float) -> str:
    """Runs a module op that replies DONE <name> or FAIL <name>; stops the script on FAIL."""
    start = dbg(command)
    reply = game().wait(rf"^(DONE|FAIL) {name}", seconds + 5, start)
    if reply is None or reply.startswith("FAIL"):
        raise SystemExit(f"{command}: {reply or 'no reply'}; {state()}")
    print(reply)
    return reply


def expand(line: str) -> str:
    """${NAME} in a script line is the environment variable NAME."""
    def value(m: re.Match) -> str:
        if m.group(1) not in os.environ:
            raise SystemExit(f"the script needs {m.group(1)} in the environment")
        return os.environ[m.group(1)]
    return re.sub(r"\$\{(\w+)\}", value, line)


class Run:
    """A script's state between ops."""

    def __init__(self) -> None:
        self.began = self.sent = time.time()
        self.mark = 0  # debug lines from here on answer the last send
        self.client_mark = size("client")
        self.origins = {log: size(log) for log in LOGS}


def op_send(r: Run, rest: str) -> None:
    """send <client command> | send DBG <command>"""
    if rest.startswith("DBG "):
        r.mark = game().send(rest)
    else:
        append(DIR / "cmds.txt", rest)
    r.client_mark = size("client")
    r.sent = time.time()


def op_wait(r: Run, rest: str) -> None:
    """wait <s>"""
    time.sleep(float(rest))


def op_connect(r: Run, rest: str) -> None:
    """connect [s]"""
    game(float(rest or 60))


def op_expect(r: Run, rest: str) -> None:
    """expect <regex> [s]: a game reply since the last send, or a client log line"""
    parts = rest.split()
    secs = float(parts[1]) if len(parts) > 1 else 10
    if parts[0] == "HELLO":
        # The game reports whether the AP client is connected, so a client that connected before the script started
        # counts too.
        print(until("client=1", secs))
        return
    reply = game().wait(parts[0], min(secs, 0.5), r.mark)
    if reply is None and LOGS["client"].exists():
        try:
            reply = wait(parts[0], secs, "client", r.client_mark)
        except SystemExit:
            reply = None
    if reply is None:
        reply = game().wait(parts[0], 0.1, r.mark)
    if reply is None:
        raise SystemExit(f"timeout waiting for /{parts[0]}/")
    r.mark = game().mark()
    print(reply)


def op_poll(r: Run, rest: str) -> None:
    """poll <regex> <s> <command>"""
    pattern, secs, command = rest.split(" ", 2)
    end = time.time() + float(secs)
    while True:
        start = game().send(command)
        reply = game().wait(pattern, 0.2, start)
        if reply is not None:
            break
        if time.time() > end:
            replies = game().lines[start:]
            raise SystemExit(f"timeout polling /{pattern}/; last: {replies[-1] if replies else '-'}")
    r.mark = game().mark()
    print(reply)


def op_logged(r: Run, rest: str) -> None:
    """logged <regex> [s] [log] [count]"""
    parts = rest.split()
    log = parts[2] if len(parts) > 2 else "client"
    count = int(parts[3]) if len(parts) > 3 else 1
    print(wait(parts[0], float(parts[1]) if len(parts) > 1 else 10, log, r.origins[log], count))


def op_logcount(r: Run, rest: str) -> None:
    """logcount <regex> <count> [log]"""
    parts = rest.split()
    log = parts[2] if len(parts) > 2 else "client"
    found = []
    if LOGS[log].exists():
        with open(LOGS[log], "rb") as f:
            f.seek(r.origins[log])
            found = [l for l in f.read().decode("utf-8", "replace").splitlines() if re.search(parts[0], l)]
    if len(found) != int(parts[1]):
        raise SystemExit(f"{log}.log has {len(found)} lines with /{parts[0]}/, not {parts[1]}: {found}")


def op_game(r: Run, rest: str) -> None:
    """game <line>"""
    r.mark = game().send(rest)


def op_stall(r: Run, rest: str) -> None:
    """stall"""
    stalled = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    stalled.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    stalled.connect(("127.0.0.1", GAME_PORT))
    threading.Thread(target=ping_unread, args=(stalled,), daemon=True).start()


def op_peer(r: Run, rest: str) -> None:
    """peer <command>"""
    append(DIR / "peer_cmds.txt", rest)


def op_srv(r: Run, rest: str) -> None:
    """srv <command>"""
    append(DIR / "server_cmds.txt", rest)


def op_shot(r: Run, rest: str) -> None:
    """shot <file>"""
    emu.shot(os.path.join(emu.SANDBOX, rest))


def op_state(r: Run, rest: str) -> None:
    """state"""
    print(state())


def op_screen(r: Run, rest: str) -> None:
    """screen <name>[|<name>] [s]"""
    p = rest.split()
    wait_screen(p[0], float(p[1]) if len(p) > 1 else 5)


def op_tap(r: Run, rest: str) -> None:
    """tap <buttons> [when <cond> <ms>]"""
    # The level change or event a tap starts counts as after the last send.
    r.sent = time.time()
    start = dbg("press " + rest)
    timeout = 10.0
    m = re.search(r"\bwhen \S+ (\d+)", rest)
    if m:
        timeout = int(m.group(1)) / 1000
    reply = game().wait(r"^(DONE|FAIL) press", timeout + 5, start)
    if reply is None or reply.startswith("FAIL"):
        raise SystemExit(f"tap {rest}: {reply or 'no reply'}; {state()}")


def op_until(r: Run, rest: str) -> None:
    """until <cond> [s]"""
    p = rest.split()
    until(p[0], float(p[1]) if len(p) > 1 else 10)


def op_tapuntil(r: Run, rest: str) -> None:
    """tapuntil <buttons> <cond> [when <cond>] [s]"""
    p = rest.split()
    secs = float(p[-1]) if len(p) in (3, 5) else 10
    when = f" when {p[3]}" if len(p) >= 4 and p[2] == "when" else ""
    r.sent = time.time()
    module_op(f"tapuntil {p[0]} {p[1]}{when} {int(secs * 1000)}", "tapuntil", secs)


def op_interact(r: Run, rest: str) -> None:
    """interact [n] [s]"""
    p = rest.split()
    secs = float(p[1]) if len(p) > 1 else 10
    r.sent = time.time()
    module_op(f"interact {p[0] if p else '1'} {int(secs * 1000)}", "interact", secs)


def op_guard(r: Run, rest: str) -> None:
    """guard <args>"""
    print(game().request(f"DBG guard {rest}", r"^OK guard"))


def op_hold(r: Run, rest: str) -> None:
    """hold <buttons> <lx> <ly> until <cond> [s]"""
    p = rest.split()
    secs = float(p[5]) if len(p) > 5 else 10
    r.sent = time.time()
    module_op(f"hold {p[0]} {p[1]} {p[2]} until {p[4]} {int(secs * 1000)}", "hold", secs)


def op_face(r: Run, rest: str) -> None:
    """face up|down|left|right"""
    lx, ly = {"up": (0, 32767), "down": (0, -32767), "left": (-32767, 0), "right": (32767, 0)}[rest]
    module_op(f"hold 0 {lx} {ly} until face={rest} 3000", "hold", 3)


def op_select(r: Run, rest: str) -> None:
    """select <file>"""
    m = re.search(r" file=(\d+)", until("files:DataSelectKeyWait", 15) and state())
    if m is None:
        raise SystemExit(f"no file menu cursor: {state()}")
    if int(m.group(1)) != int(rest):
        button = "2000" if int(m.group(1)) > int(rest) else "8000"
        module_op(f"tapuntil {button} file={rest} when files:DataSelectKeyWait 5000", "tapuntil", 5)


def op_choose(r: Run, rest: str) -> None:
    """choose <n> [s]"""
    p = rest.split()
    secs = float(p[1]) if len(p) > 1 else 10
    r.sent = time.time()
    module_op(f"choose {p[0]} {int(secs * 1000)}", "choose", secs)


def op_free(r: Run, rest: str) -> None:
    """free [s]"""
    wait_free(float(rest or 30))


def advance(op: str, rest: str) -> None:
    p = rest.split()
    fast = "fast" in p or op == "dismiss"
    p = [v for v in p if v != "fast"]
    buttons = p[0] if op == "advance" and p else "2"
    secs = float(p[-1]) if p and (op == "dismiss" or len(p) > 1) else 60
    start = dbg(f"advance {buttons} {int(secs * 1000)}" + (" fast" if fast else ""))
    reply = game().wait(r"^(DONE|FAIL|CHOICE) advance", secs + 5, start)
    if reply is None or reply.startswith("FAIL") or (op == "dismiss" and reply.startswith("CHOICE")):
        raise SystemExit(f"{op}: {reply or 'no reply'}; {state()}")
    print(reply)
    if reply.startswith("DONE"):
        wait_free(10)


def op_dismiss(r: Run, rest: str) -> None:
    """dismiss [fast] [s]"""
    advance("dismiss", rest)


def op_advance(r: Run, rest: str) -> None:
    """advance [buttons] [fast] [s]"""
    advance("advance", rest)


def op_loaded(r: Run, rest: str) -> None:
    """loaded [s]"""
    wait_free(float(rest or 40), since=r.sent)


def op_inlevel(r: Run, rest: str) -> None:
    """inlevel [s]"""
    def entered(l: str) -> bool:
        ms = re.search(r"ms_in_level=(\d+)", l)
        return "player=1" in l and ms is not None and time.time() - int(ms.group(1)) / 1000 > r.sent
    poll(entered, float(rest or 60), "a level")


def op_warp(r: Run, rest: str) -> None:
    """warp <level> <x> <y> <z>"""
    # A warp within the current level does not reload it, so it passes through another level first.
    level = rest.split()[0]
    if f"level={level} " in state():
        other = "Field 38.25 10.5 128.25" if level != "Field" else "MarinTarinHouse 6.85 0 3.83"
        at = time.time()
        game().request(f"DBG warp {other}", r"^OK warp", 30)
        wait_free(40, since=at)
    at = time.time()
    game().request(f"DBG warp {rest}", r"^OK warp", 30)
    wait_free(40, since=at)
    landed(state(), *[float(v) for v in rest.split()[1:4]])


def op_goto(r: Run, rest: str) -> None:
    """goto <x> <y> <z>"""
    print(goto(*[float(v) for v in rest.split()][:3]))


def op_droprest(r: Run, rest: str) -> None:
    """droprest [s]"""
    drop_rest(float(rest or 10), True)


def op_gotodrop(r: Run, rest: str) -> None:
    """gotodrop [s]"""
    # Against the drop on Link's side (right on it, against a wall, the wall pushes him off). No settling, as the
    # pickup's event starts at once.
    at = drop_rest(float(rest or 10))
    if at is None:
        raise SystemExit(f"the drop actor is gone; {state()}")
    if at is DROP_TAKEN:
        return
    link = position(state())
    x, z = at[0], at[2]
    if link is not None:
        away = math.hypot(link[0] - x, link[2] - z)
        if away > DROP_TOUCH:
            x += (link[0] - x) / away * DROP_TOUCH
            z += (link[2] - z) / away * DROP_TOUCH
    print(game().request(f"DBG pos {x:.2f} {at[1]:.2f} {z:.2f}", r"^OK pos"))


def op_check(r: Run, rest: str) -> None:
    """check <regex>"""
    line = state()
    if not re.search(rest, line):
        raise SystemExit(f"STATE does not match /{rest}/: {line}")


def op_await(r: Run, rest: str) -> None:
    """await <s> <regex>"""
    secs, _, rx = rest.partition(" ")
    poll(lambda l: re.search(rx, l) is not None, float(secs), f"STATE /{rx}/")


def op_level(r: Run, rest: str) -> None:
    """level <name>"""
    line = state()
    if f" level={rest} " not in line:
        raise SystemExit(f"not in level {rest}: {line}")


def op_at(r: Run, rest: str) -> None:
    """at <x> <z> [tolerance]"""
    p = [float(v) for v in rest.split()]
    line = state()
    pos = position(line)
    if pos is None:
        raise SystemExit(f"no player position: {line}")
    tol = p[2] if len(p) > 2 else 0.75
    if abs(pos[0] - p[0]) > tol or abs(pos[2] - p[1]) > tol:
        raise SystemExit(f"player not at ({p[0]}, {p[1]}): {line}")


OPS = {name[3:]: fn for name, fn in globals().items() if name.startswith("op_")}


def run(script: Path, trace: str | None = None) -> None:
    r = Run()
    if trace:
        game().trace_file = open(trace, "w", encoding="utf-8")
        game().request("DBG trace on", r"^OK trace")
    script_mark: int | None = None   # alerts from here on stop the script
    for raw in script.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        line = expand(line)
        if _debug is not None:
            if script_mark is None:
                script_mark = _debug.mark()
            alert = _debug.alert(script_mark)
            if alert:
                raise SystemExit(f"game alert: {alert}; {state()}")
        print(f"[{time.time() - r.began:7.2f}] {line}", flush=True)
        op, _, rest = line.partition(" ")
        if op not in OPS:
            raise SystemExit(f"unknown op {op}")
        OPS[op](r, rest)
    if trace:
        game().request("DBG trace off", r"^OK trace")
    print(f"SCRIPT OK in {time.time() - r.began:.2f} s")


def ping_unread(sock: socket.socket) -> None:
    """Keeps a connection alive for the game's keep-alive without reading what the game sends (op stall)."""
    while True:
        try:
            sock.sendall(b"PING\n")
        except OSError:
            return
        time.sleep(2)


def wait_free(seconds: float, since: float = 0) -> None:
    """Waits until the player can move (after a dialogue or an item-get). With `since`, the level must also have
    been entered after that time, so a warp has finished loading."""
    poll(lambda l: is_free(l, since), seconds, "the player to be free")


def wait_screen(name: str, seconds: float) -> None:
    """Waits until the STATE line shows `screen=<name>`; `name` may list alternatives: `loading|level`."""
    poll(lambda l: any(f"screen={n} " in l for n in name.split("|")), seconds, f"screen={name}")


def is_free(line: str, since: float = 0) -> bool:
    """The STATE line shows the player free in a level entered after `since`, for at least 1.5 s, with no message
    holding him (one in EventWaiting does not) and no item-get showing or queued."""
    ms = re.search(r"ms_in_level=(\d+)", line)
    if not ms or "player=1" not in line or "event=0 paused=0" not in line or not re.search(r"showq=0(\s|$)", line):
        return False
    if not re.search(r"msg=(Closed|EventWaiting|none)(\s|$)", line):
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
        start = dbg(" ".join(args))
        end = time.time() + 1
        i = start
        while time.time() < end:
            line = game().wait(r".", end - time.time(), i)
            if line is None:
                break
            i = game().lines.index(line, i) + 1
            if REPLY.match(line):
                print(line)
                if not line.startswith("OK await") and not line.startswith("OK press") and \
                        not line.startswith("OK advance") and not line.startswith("POPFRAME"):
                    break
    elif cmd == "cli":
        append(DIR / "cmds.txt", " ".join(args))
    elif cmd == "state":
        print(state())
    elif cmd == "goto":
        print(goto(*map(float, args[:3])))
    elif cmd == "wait":
        log = args[2] if len(args) > 2 else "client"
        print(wait(args[0], float(args[1]) if len(args) > 1 else 10, log, size(log)))
    elif cmd == "trace":
        secs = float(args[0]) if args else 10
        g = game()
        if len(args) > 1:
            g.trace_file = open(args[1], "w", encoding="utf-8")
        start = g.mark()
        g.request("DBG trace on", r"^OK trace")
        time.sleep(secs)
        g.request("DBG trace off", r"^OK trace")
        if len(args) <= 1:
            print("\n".join(l for l in g.lines[start:] if l.startswith("T ")))
    elif cmd == "run":
        run(Path(args[0]), args[1] if len(args) > 1 else None)
    elif cmd == "tail":
        n = int(args[0]) if args else 8
        for log, path in LOGS.items():
            if not path.exists():
                continue
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()[-n:]
            print(f"--- {log}")
            print("\n".join(re.sub(r"\x1b\[[0-9;]*m", "", l) for l in lines))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
