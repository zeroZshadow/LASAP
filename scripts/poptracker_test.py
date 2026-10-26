"""Tests the LASR PopTracker pack's Archipelago auto-tracking (the Br00ty/lasr_brooty fork) in a sandbox PopTracker.

python scripts/poptracker_test.py <pack folder> [--variant <variant>] [<step> ...]
in .venv-ap; steps: manual full marks lean (all, in this order); variant: tracker (default) or var_itemsonly

The PopTracker folder is this machine's: "poptracker" in scripts/local_paths.json (gitignored, TESTING.md). Its
sandbox PopTracker (app, a copy in portable mode: config, saves and log.txt stay in its portable-config folder) loads
the pack from <pack folder> with a test hook from its user-override folder: the pack's scripts/init.lua followed by
scripts/poptracker_test.lua, which prints the tracker's items, locations and sections on commands (DESIGN in
poptracker_test.lua). A local MultiServer on PORT serves the newest seed in its out folder, generated from the slots
Full and Lean in its players folder; the test connects to it as the slot to check locations and write data storage,
and sends items with the server console's /send. Each step checks the tracker against the slot's received items,
checked locations and settings and stops at the first difference. Screenshots of the tracker's window go to its shots
folder for review.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

from websockets.sync.client import connect

import emu

ROOT = Path(__file__).resolve().parent.parent
LOCAL_PATHS = ROOT / "scripts" / "local_paths.json"  # this machine's folders, gitignored


def local_path(key: str) -> Path:
    try:
        return Path(json.loads(LOCAL_PATHS.read_text(encoding="utf-8"))[key])
    except (OSError, KeyError, ValueError):
        raise SystemExit(f'{LOCAL_PATHS} needs "{key}" (TESTING.md, Requirements)')


HOME = local_path("poptracker")
APP = HOME / "app"
EXE = APP / "poptracker.exe"
CONFIG = APP / "portable-config" / "PopTracker"
LOG = CONFIG / "log.txt"
SHOTS = HOME / "shots"
PORT = 38297
GAME = "Links Awakening Switch"
PYTHON = ROOT / ".venv-ap" / "Scripts" / "python.exe"
CREATE_NO_WINDOW = 0x08000000
# AccessibilityLevel values
NONE, NORMAL = 0, 6


class Fail(Exception):
    pass


def jsonc(path: Path):
    """A pack JSON file: comments and trailing commas allowed."""
    text = path.read_text(encoding="utf-8-sig")
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            i = text.find("\n", i)
            i = n if i < 0 else i
        elif text.startswith("/*", i):
            i = text.index("*/", i) + 2
        else:
            out.append(c)
            i += 1
    return json.loads(re.sub(r",(\s*[}\]])", r"\1", "".join(out)))


class Pack:
    def __init__(self, path: Path):
        self.path = path
        self.uid = jsonc(path / "manifest.json")["package_uid"]
        self.codes = []
        self.item_of = {}  # code -> the item (index in items.json) that has it
        for index, item in enumerate(jsonc(path / "items" / "items.json")):
            for codes in [item.get("codes", "")] + [stage.get("codes", "") for stage in item.get("stages", [])]:
                for code in codes.split(","):
                    if code.strip() and code.strip() not in self.codes:
                        self.codes.append(code.strip())
                        self.item_of[code.strip()] = f"{index}:{item['name']}"
        self.locations, self.sections = [], []
        init = (path / "scripts" / "init.lua").read_text(encoding="utf-8")
        for name in re.findall(r'^Tracker:AddLocations\("([^"]+)"\)', init, re.M):
            self._walk(jsonc(path / name), "")
        self.location_map = {}  # AP location id -> (LASR key, [section codes])
        text = (path / "scripts" / "autotracking" / "location_mapping.lua").read_text(encoding="utf-8")
        for m in re.finditer(r'\[(\d+)\] = \{(.*)\},\s*-- (\S+)', text):
            self.location_map[int(m.group(1))] = (m.group(3), re.findall(r'"([^"]*)"', m.group(2)))
        self.item_map = {}  # AP item id -> (LASR key, pack code, kind: toggle/progressive/consumable or a stage)
        text = (path / "scripts" / "autotracking" / "item_mapping.lua").read_text(encoding="utf-8")
        for m in re.finditer(r'\[(\d+)\] = \{"([^"]+)", (?:"(\w+)"|(\d+))\},\s*-- (\S+)', text):
            self.item_map[int(m.group(1))] = (m.group(5), m.group(2), m.group(3) or int(m.group(4)))

    def _walk(self, nodes: list, parent: str) -> None:
        for node in nodes:
            path = f"{parent}/{node['name']}" if parent else node["name"]
            self.locations.append(path)
            for section in node.get("sections", []):
                self.sections.append(f"@{path}/{section.get('name', '')}")
            self._walk(node.get("children", []), path)

    def location_id(self, key: str) -> int:
        return next(i for i, (k, _) in self.location_map.items() if k == key)

    def item_id(self, key: str) -> int:
        return next(i for i, (k, _, _) in self.item_map.items() if k == key)

    def install_hook(self) -> Path:
        """Writes the test hook into the sandbox PopTracker's override folder for this pack."""
        override = APP / "user-override" / self.uid
        if override.exists():
            shutil.rmtree(override)
        (override / "scripts").mkdir(parents=True)
        init = (self.path / "scripts" / "init.lua").read_text(encoding="utf-8")
        (override / "scripts" / "init.lua").write_text(
            init + '\nScriptHost:LoadScript("scripts/lasap_test.lua")\n', encoding="utf-8")
        shutil.copy(ROOT / "scripts" / "poptracker_test.lua", override / "scripts" / "lasap_test.lua")

        def table(name: str, values: list) -> str:
            return f"{name} = {{\n" + "".join(f"    {json.dumps(v)},\n" for v in values) + "}\n"
        (override / "scripts" / "lasap_test_codes.lua").write_text(
            table("LASAP_TEST_ITEMS", self.codes) + table("LASAP_TEST_LOCATIONS", self.locations) +
            table("LASAP_TEST_SECTIONS", self.sections), encoding="utf-8")
        return override


class Dump:
    def __init__(self, lines: list):
        self.items, self.locations, self.sections, self.globals = {}, {}, {}, {}
        for line in lines:
            kind, key, *rest = line.split("\t")
            {"I": self.items, "L": self.locations, "S": self.sections, "G": self.globals}[kind][key] = rest

    def item(self, code: str) -> dict:
        row = self.items[code]
        if row[0] == "missing":
            raise Fail(f"no item for code {code}")
        increment, active, stage, count, badge = (row + [""])[:5]
        return {"increment": int(increment) if increment.isdigit() else increment, "active": active == "true",
                "stage": int(stage) if stage.isdigit() else stage,
                "count": int(count) if count.lstrip("-").isdigit() else count, "badge": badge}

    def section(self, code: str) -> tuple[int, int, int]:
        row = self.sections[code]
        if row[0] == "missing":
            raise Fail(f"no section {code}")
        return int(row[0]), int(row[1]), int(row[2])

    def find(self, suffix: str) -> str:
        found = [code for code in self.sections if code.endswith(suffix)]
        if len(found) != 1:
            raise Fail(f"{len(found)} sections end in {suffix}")
        return found[0]


class Tracker:
    """The sandbox PopTracker: started by path with the hook, read through its log, given commands by file."""

    def __init__(self, pack: Pack, variant: str):
        self.pack, self.variant = pack, variant
        self.override = pack.install_hook()
        self.proc = None
        self.offset = 0
        self.seq = 0
        self.tags = 0

    def start(self, slot: str | None = None) -> None:
        (self.override / "scripts" / "lasap_cmd.lua").write_text(f"LASAP_CMD = {{seq = {self.seq}}}\n")
        LOG.unlink(missing_ok=True)  # PopTracker starts it anew
        self.offset = 0
        args = [str(EXE), "--pack-variant", self.variant]
        if slot:
            args += ["--ap-host", f"localhost:{PORT}", "--ap-slot", slot]
        args.append(str(self.pack.path))  # the pack last, after its options
        foreground = emu.ctypes.windll.user32.GetForegroundWindow()
        info = subprocess.STARTUPINFO()
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = emu.SW_SHOWNOACTIVATE
        self.proc = subprocess.Popen(args, cwd=APP, startupinfo=info, stdin=subprocess.DEVNULL,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pythonw = Path(sys.executable).with_name("pythonw.exe")
        subprocess.Popen([str(pythonw), str(ROOT / "scripts" / "emu.py"), "tuck", str(self.proc.pid),
                          str(foreground or 0)], creationflags=CREATE_NO_WINDOW, close_fds=True)
        print(f"PopTracker {self.proc.pid} {self.variant} {slot or '(no AP)'}")
        self.wait(r"^LASAPTEST LOADED", 30)

    def stop(self) -> None:
        """Closes the window as a person would, so PopTracker saves its state."""
        if not self.proc:
            return
        subprocess.run(["taskkill", "/PID", str(self.proc.pid)], capture_output=True)
        try:
            self.proc.wait(15)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(self.proc.pid), "/F"], capture_output=True)
            self.proc.wait(5)
            raise Fail("PopTracker did not close on WM_CLOSE")
        self.proc = None

    def lines(self) -> list:
        if not LOG.exists():
            return []
        with open(LOG, "rb") as f:
            f.seek(self.offset)
            data = f.read()
        end = data.rfind(b"\n") + 1
        self.offset += end
        return data[:end].decode("utf-8", "replace").splitlines()

    def wait(self, pattern: str, seconds: float, collect: str | None = None) -> tuple[str, list]:
        """Waits for a log line matching pattern; fails on a Lua error, a closed tracker or the timeout. Lines that
        match collect on the way are returned with it."""
        rx, got = re.compile(pattern), []
        deadline = time.time() + seconds
        while time.time() < deadline:
            for line in self.lines():
                if line.startswith("LASAPTEST ERROR") or re.search(r"\.lua:\d+:", line):
                    raise Fail(f"tracker: {line}")
                if collect and line.startswith(collect):
                    got.append(line[len(collect):])
                if rx.search(line):
                    return line, got
            if self.proc and self.proc.poll() is not None:
                raise Fail(f"PopTracker exited ({self.proc.returncode}) waiting for {pattern}")
            time.sleep(0.05)
        raise Fail(f"tracker: no {pattern} in {seconds} s")

    def commands(self, *commands: str) -> list:
        """Runs hook commands; returns the LASAPTEST lines they printed."""
        self.seq += 1
        body = "".join(f"    {json.dumps(c)},\n" for c in commands)
        path = self.override / "scripts" / "lasap_cmd.lua"
        tmp = path.with_suffix(".tmp")
        tmp.write_text(f"LASAP_CMD = {{seq = {self.seq},\n{body}}}\n", encoding="utf-8")
        os.replace(tmp, path)
        return self.wait(rf"^LASAPTEST DONE {self.seq}$", 10, "LASAPTEST ")[1]

    def tag(self) -> str:
        self.tags += 1
        return f"t{self.tags}"

    def dump(self) -> Dump:
        tag = self.tag()
        lines = self.commands(f"dump {tag}")
        return Dump([line[len(tag) + 1:] for line in lines if line.startswith(tag + "\t") and "\tEND" not in line])

    def barrier_dump(self, client: "Client") -> Dump:
        """A dump taken after every event the server sent before: the hook dumps on the barrier's SetReply."""
        tag = self.tag()
        client.set("lasap_test_barrier", tag)
        prefix = f"LASAPTEST {tag}\t"
        lines = self.wait(rf"^LASAPTEST {tag}\tEND$", 10, prefix)[1]
        return Dump([line for line in lines if line != "END"])

    def call(self, function: str) -> str:
        """Calls a global Lua function of the pack (logic.lua's) without arguments."""
        tag = self.tag()
        for line in self.commands(f"call {tag} {function}"):
            if line.startswith(f"{tag}\tCALL\t"):
                return line.split("\t", 2)[2]
        raise Fail(f"no result for {function}()")

    def shot(self, name: str) -> None:
        SHOTS.mkdir(exist_ok=True)
        emu.shot(str(SHOTS / f"{name}.png"), self.proc.pid)


class Server:
    def __init__(self):
        self.proc = None
        self.multidata = max(HOME.glob("out/AP_*.zip"), key=lambda p: p.stat().st_mtime)

    def start(self, fresh: bool = False) -> None:
        if fresh:  # the server keeps its save next to the seed
            self.multidata.with_suffix(".apsave").unlink(missing_ok=True)
        log = open(HOME / "server.log", "a", encoding="utf-8")
        self.proc = subprocess.Popen(
            [str(PYTHON), str(ROOT / "scripts" / "ap.py"), "MultiServer.py", str(self.multidata), "--port", str(PORT),
             "--host", "localhost"],
            stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, text=True, creationflags=CREATE_NO_WINDOW)
        print(f"MultiServer {self.proc.pid} {self.multidata.name}")

    def command(self, line: str) -> None:
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def stop(self) -> None:
        """/exit, so the server saves the checks and data storage on its way out."""
        if not self.proc:
            return
        try:
            self.command("/exit")
            self.proc.wait(15)
        except (OSError, subprocess.TimeoutExpired):
            subprocess.run(["taskkill", "/PID", str(self.proc.pid), "/T", "/F"], capture_output=True)
            self.proc.wait(10)
        self.proc = None


class Client:
    """Connects to the server as the slot: checks locations, writes data storage, sees what the slot receives."""

    def __init__(self, slot: str):
        self.slot = slot
        deadline = time.time() + 60
        while True:
            try:
                self.ws = connect(f"ws://localhost:{PORT}", max_size=None, open_timeout=5)
                break
            except OSError:
                if time.time() > deadline:
                    raise Fail("the server does not take connections")
                time.sleep(0.2)
        self.messages, self.cond = [], threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()
        self.received = []
        self.wait(lambda m: m["cmd"] == "RoomInfo", 10)
        self.send({"cmd": "GetDataPackage", "games": [GAME]})
        package = self.wait(lambda m: m["cmd"] == "DataPackage", 10)["data"]["games"][GAME]
        self.item_names = {i: n for n, i in package["item_name_to_id"].items()}
        self.send({"cmd": "Connect", "password": "", "name": slot, "game": GAME, "uuid": uuid.uuid4().hex,
                   "version": {"major": 0, "minor": 6, "build": 8, "class": "Version"}, "items_handling": 7,
                   "tags": [], "slot_data": True})
        reply = self.wait(lambda m: m["cmd"] in ("Connected", "ConnectionRefused"), 10)
        if reply["cmd"] != "Connected":
            raise Fail(f"connect {slot}: {reply}")
        self.team, self.number, self.slot_data = reply["team"], reply["slot"], reply["slot_data"]
        self.checked = set(reply["checked_locations"])
        self.slot_locations = self.checked | set(reply["missing_locations"])

    def _read(self) -> None:
        try:
            for raw in self.ws:
                for message in json.loads(raw):
                    with self.cond:
                        if message["cmd"] == "ReceivedItems":
                            if message["index"] == 0:
                                self.received = []
                            self.received += [item["item"] for item in message["items"]]
                        elif message["cmd"] == "RoomUpdate" and "checked_locations" in message:
                            self.checked |= set(message["checked_locations"])
                        self.messages.append(message)
                        self.cond.notify_all()
        except Exception:
            pass

    def send(self, message: dict) -> None:
        self.ws.send(json.dumps([message]))

    def mark(self) -> int:
        with self.cond:
            return len(self.messages)

    def wait(self, test, seconds: float, since: int = 0) -> dict:
        deadline = time.time() + seconds
        with self.cond:
            while True:
                for message in self.messages[since:]:
                    if test(message):
                        return message
                since = len(self.messages)
                left = deadline - time.time()
                if left <= 0:
                    raise Fail(f"{self.slot}: no such message in {seconds} s")
                self.cond.wait(left)

    def set(self, key: str, value) -> None:
        self.send({"cmd": "Set", "key": key, "default": None, "want_reply": False,
                   "operations": [{"operation": "replace", "value": value}]})

    def check(self, *ids: int) -> None:
        if not set(ids) <= self.slot_locations:
            raise Fail(f"not locations of {self.slot}: {set(ids) - self.slot_locations}")
        since = self.mark()
        self.send({"cmd": "LocationChecks", "locations": list(ids)})
        self.wait(lambda m: m["cmd"] == "RoomUpdate" and set(ids) <= set(m.get("checked_locations", [])), 10, since)

    def close(self) -> None:
        self.ws.close()


def give(server: Server, client: Client, pack: Pack, key: str) -> None:
    """Sends the slot an item by its LASR key through the server console and waits until the server sent it."""
    name = client.item_names[pack.item_id(key)]
    since = client.mark()
    server.command(f"/send {client.slot} {name}")
    text = f'sending "{name}" to {client.slot}'
    client.wait(lambda m: m["cmd"] == "PrintJSON" and text in "".join(p.get("text", "") for p in m["data"]), 10,
                since)


def check(what: str, got, want) -> None:
    if got != want:
        raise Fail(f"{what}: {got!r}, expected {want!r}")


def check_items(dump: Dump, pack: Pack, received: list) -> None:
    """Every mapped item as the slot's received items make it: a toggle on, a progressive item one level per copy,
    a consumable its increment per copy, a stage item at least its stage."""
    want = {}
    for _, code, kind in pack.item_map.values():
        want.setdefault(pack.item_of[code], {"active": False, "stage": 0, "count": 0, "code": code, "kind": kind})
    for item_id in received:
        if item_id not in pack.item_map:
            continue
        _, code, kind = pack.item_map[item_id]
        state = want[pack.item_of[code]]
        if isinstance(kind, int):
            state["stage"] = max(state["stage"], kind) if state["active"] else kind
            state["active"] = True
        elif kind == "consumable":
            state["count"] += dump.item(code)["increment"]
        elif kind == "progressive":
            # Lua's CurrentStage of a progressive item that can be off counts the off stage: 1 is the first level
            state["stage"] += 1
            state["active"] = True
        else:
            state["active"] = True
    for name, state in want.items():
        item = dump.item(state["code"])
        if state["kind"] == "consumable":
            check(f"{name} count", item["count"], state["count"])
        elif isinstance(state["kind"], int):  # the tunics' item, never off: its stage says it all
            check(f"{name} stage", item["stage"], state["stage"])
        else:
            check(f"{name} active", item["active"], state["active"])
            if state["kind"] != "toggle":
                check(f"{name} stage", item["stage"], state["stage"])


def check_sections(dump: Dump, pack: Pack, client: Client, marks: dict | None = None) -> None:
    """Every mapped section empties in proportion to its checked locations; one with none of the slot's locations
    shows nothing to check; a section marked by hand keeps its mark."""
    total, checked = {}, {}
    for location_id, (_, codes) in pack.location_map.items():
        for code in codes:
            if location_id in client.slot_locations:
                total[code] = total.get(code, 0) + 1
                checked[code] = checked.get(code, 0) + (location_id in client.checked)
    for code in {c for _, codes in pack.location_map.values() for c in codes}:
        available, chests, _ = dump.section(code)
        want = chests - chests * min(checked[code], total[code]) // total[code] if code in total else 0
        if marks and code in marks:
            want = min(want, marks[code])
        check(f"{code} available", available, want)


SETTING_TOGGLES = {"Heart Pieces": "s_hp", "Seashells": "s_ss", "Trade Quest": "s_tq", "Trendy Game": "trendy",
                   "Rapids": "rapids", "Dampe": "dampe", "Blue Rupees": "d0rupee", "Golden Leaves": "kanalet",
                   "Open Kanalet": "opencastle", "Completed Bridge": "completedbridge", "Free Book": "freebook",
                   "Open Mabe": "openmabe", "Open Mamu": "openmamu", "Fast Stalfos": "faststalfos"}
SETTING_STAGES = {"Preset": ("l_bas", {"Basic": 0, "Advanced": 1, "Glitched": 2, "Hell": 2}),
                  "Shop": ("shop_off", {False: 0, True: 1}), "Stealing": ("s_never", {"Never": 0, "Standard": 1,
                                                                                       "Always": 2}),
                  "Classic D2": ("classic_off", {False: 0, True: 1}),
                  "Shuffled Dungeons": ("dungeon_er_off", {False: 0, True: 1})}


def check_settings(dump: Dump, slot_data: dict) -> None:
    settings = slot_data["settings"]
    for setting, code in SETTING_TOGGLES.items():
        check(f"{setting} ({code})", dump.item(code)["active"], settings[setting] is True)
    for setting, (code, stages) in SETTING_STAGES.items():
        check(f"{setting} ({code})", dump.item(code)["stage"], stages[settings[setting]])
    fishing = 0 if not settings["Fishing"] else 2 if settings["Fast Fishing"] else 1
    check("Fishing stage", dump.item("fishing_off")["stage"], fishing)
    check("Seashell Mansion", dump.item("mansion")["active"], settings["Seashell Mansion"] > 0)
    check("Owl Gifts", dump.item("owlstatue")["active"], settings["Owl Gifts"] != "None")


def check_instruments(dump: Dump, slot_data: dict, sign_read: bool) -> None:
    hidden = slot_data["instruments_known"] is False and not sign_read
    item = dump.item("instruments_required")
    check("Instruments Required count", item["count"], 0 if hidden else slot_data["instruments_required"])
    check("Instruments Required badge", item["badge"], "?" if hidden else "")
    check("INSTRUMENTS_HIDDEN", dump.globals["INSTRUMENTS_HIDDEN"][0], str(hidden).lower())


def check_all(tracker: Tracker, pack: Pack, client: Client, sign_read: bool = False, marks: dict | None = None):
    dump = tracker.barrier_dump(client)
    check_settings(dump, client.slot_data)
    check_instruments(dump, client.slot_data, sign_read)
    check_items(dump, pack, client.received)
    check_sections(dump, pack, client, marks)
    return dump


def step_manual(pack: Pack, tracker: Tracker, server: Server) -> None:
    """Without Archipelago, from an empty state: the settings' defaults and the logic the fork changed."""
    shutil.rmtree(CONFIG / "saves", ignore_errors=True)
    tracker.start()
    dump = tracker.dump()
    check("Fishing stage", dump.item("fishing_off")["stage"], 0)
    check("Instruments Required", (dump.item("instruments_required")["count"],
                                   dump.item("instruments_required")["badge"]), (8, ""))
    check("Completed Bridge", dump.item("completedbridge")["active"], False)
    check("INSTRUMENTS_HIDDEN", dump.globals["INSTRUMENTS_HIDDEN"][0], "false")
    tracker.shot("manual_start")
    castle = next(loc for loc in pack.locations if loc.endswith("/Kanalet Castle") and loc.count("/") == 2)
    cases = [
        ([], NONE),
        (["bracelet", "feather"], NONE),  # the old logic's way in; the bridge needs more now
        (["bracelet", "feather", "bananas"], NORMAL),
        (["bracelet", "feather", "completedbridge"], NORMAL),
        (["bracelet", "feather", "flippers"], NORMAL),
        (["bracelet", "opencastle"], NORMAL),
        (["openmabe", "feather", "bananas"], NONE),  # no bush breaker
        (["openmabe", "feather", "bananas", "sword"], NORMAL),
    ]
    codes = ["bracelet", "feather", "bananas", "completedbridge", "flippers", "opencastle", "openmabe", "sword"]
    for have, want in cases:
        tracker.commands(*[f"set {c} Active {'true' if c in have else 'false'}" for c in codes])
        dump = tracker.dump()
        check(f"{castle} with {have or 'nothing'}", int(dump.locations[castle][0]), want)
    tracker.commands(*[f"set {c} Active false" for c in codes])
    # With Dungeon ER the Face Shrine's door pin needs the door and the Face Key, not a door marked as leading to D6
    door = next(loc for loc in pack.locations if loc.endswith("/Face Shrine Entrance"))
    cases = [
        (["openmabe", "flippers", "hookshot"], NONE),
        (["openmabe", "flippers", "hookshot", "face"], NORMAL),
        (["openmabe", "hookshot", "face"], NONE),
    ]
    codes = ["openmabe", "flippers", "hookshot", "face"]
    tracker.commands("set dungeon_er_off CurrentStage 1")
    for have, want in cases:
        tracker.commands(*[f"set {c} Active {'true' if c in have else 'false'}" for c in codes])
        dump = tracker.dump()
        check(f"{door} with {have}", int(dump.locations[door][0]), want)
    tracker.commands(*[f"set {c} Active false" for c in codes])
    # With Dungeon ER the Rapids from D6 need D6 through the door marked as leading into it, here Tail Cave's
    rapids = next(loc for loc in pack.locations if loc.endswith("/Rapids From D6"))
    # the bracelet's stages count from 1 here: 2 is the level 2 Bracelet; Tail Cave's door marker's 6 is the Face Shrine
    tracker.commands("set bracelet Active true", "set bracelet CurrentStage 2", "set hookshot Active true")
    check("bracelet2()", tracker.call("bracelet2"), "true")
    for stage, want in [(0, NONE), (6, NORMAL)]:
        tracker.commands(f"set tcdX CurrentStage {stage}")
        check(f"{rapids} with Tail Cave's door at stage {stage}", int(tracker.dump().locations[rapids][0]), want)
    tracker.commands("set tcdX CurrentStage 0", "set bracelet CurrentStage 0", "set bracelet Active false",
                     "set hookshot Active false", "set dungeon_er_off CurrentStage 0")
    instruments = ["cello", "conch", "bell", "harp", "marimba", "triangle", "organ", "drum"]
    for required, held, want in [(8, 8, "true"), (8, 7, "false"), (3, 3, "true"), (3, 2, "false"), (0, 0, "true")]:
        tracker.commands(f"set instruments_required AcquiredCount {required}",
                         *[f"set {c} Active {'true' if i < held else 'false'}" for i, c in enumerate(instruments)])
        check(f"instruments() with {held} of {required}", tracker.call("instruments"), want)
    tracker.commands("set instruments_required AcquiredCount 8", *[f"set {c} Active false" for c in instruments])
    for stage in (1, 2, 0):
        tracker.commands(f"set fishing_off CurrentStage {stage}")
        check("Fishing stage", tracker.dump().item("fishing_off")["stage"], stage)
        tracker.shot(f"manual_fishing_{stage}")
    tracker.stop()


def step_full(pack: Pack, tracker: Tracker, server: Server) -> None:
    """Slot Full: settings, received items, checked locations, the hidden instrument count and the level."""
    server.start(fresh=True)
    client = Client("Full")
    shutil.rmtree(CONFIG / "saves", ignore_errors=True)
    tracker.start("Full")
    tracker.wait(r"^LASAPTEST READY", 30)
    check_all(tracker, pack, client)
    tracker.shot("full_connected")
    for key in ("sword", "feather", "seashell", "seashell", "red-tunic", "full-moon-cello", "key-D1"):
        give(server, client, pack, key)
    dump = check_all(tracker, pack, client)
    check("sword stage with the starting one and one more", dump.item("sword")["stage"], 2)
    # Shop Slot5 is never a location of the world: its sections show nothing to check
    check("Slot5 1st-2nd", dump.section(dump.find("/Slot5 1st-2nd"))[0], 0)
    client.check(pack.location_id("tarin"))
    check_all(tracker, pack, client)
    # the section standing for most of the slot's locations empties step by step
    shared = {}
    for location_id, (_, codes) in pack.location_map.items():
        if location_id in client.slot_locations and len(codes) == 1:
            shared.setdefault(codes[0], []).append(location_id)
    code, ids = max(shared.items(), key=lambda entry: len(entry[1]))
    print(f"  {code}: {len(ids)} locations")
    for n, location_id in enumerate(ids[:len(ids) // 2], 1):  # step marks checks one more
        client.check(location_id)
        available, chests, _ = check_all(tracker, pack, client).section(code)
        check(f"{code} after {n} of {len(ids)}", available, chests - chests * n // len(ids))
    client.check(pack.location_id("D0-fairy-1"))
    check("fairyq after one", check_all(tracker, pack, client).item("fairyq")["active"], False)
    client.check(pack.location_id("D0-fairy-2"))
    check("fairyq after both", check_all(tracker, pack, client).item("fairyq")["active"], True)
    for key in ("trendy-prize-1", "trendy-prize-3"):
        if pack.location_id(key) in client.slot_locations:
            client.check(pack.location_id(key))
    check_all(tracker, pack, client)
    client.set(f"las_level_{client.team}_{client.number}", "Lv01TailCave")
    check_all(tracker, pack, client)
    tracker.shot("full_level_tailcave")
    client.set(f"las_level_{client.team}_{client.number}", "Field")
    check_all(tracker, pack, client)
    tracker.shot("full_level_field")
    client.set(f"las_sign_{client.team}_{client.number}", True)
    check_all(tracker, pack, client, sign_read=True)
    tracker.shot("full_sign_read")
    client.close()


def step_marks(pack: Pack, tracker: Tracker, server: Server) -> None:
    """Marks by hand survive a reconnect to the same seed and a restart of PopTracker. Runs after step full."""
    if not server.proc:
        server.start()
    if not tracker.proc:
        tracker.start("Full")
        tracker.wait(r"^LASAPTEST READY", 30)
    client = Client("Full")
    sign = True  # read in step full, kept in data storage
    dump = tracker.barrier_dump(client)
    unchecked = [code for code in sorted({c for _, cs in pack.location_map.values() for c in cs})
                 if dump.section(code)[0] > 0]
    marks = {unchecked[0]: 0, unchecked[1]: 0}
    tracker.commands(*[f"mark {code} 0" for code in marks], "set up1 CurrentStage 2")
    check_all(tracker, pack, client, sign, marks)
    # a section cleared by hand stays cleared when one more of its locations is checked
    rupees = dump.find("/Rupee Room/Rupees")
    left = [i for i, (_, codes) in pack.location_map.items()
            if codes == [rupees] and i in client.slot_locations and i not in client.checked]
    tracker.commands(f"mark {rupees} 0")
    marks[rupees] = 0
    client.check(left[0])
    check("Rupee Room cleared by hand", check_all(tracker, pack, client, sign, marks).section(rupees)[0], 0)
    client.close()
    server.stop()
    server.start()
    tracker.wait(r"^LASAPTEST READY", 60)
    client = Client("Full")
    check("egg arrow after reconnect", check_all(tracker, pack, client, sign, marks).item("up1")["stage"], 2)
    tracker.stop()
    tracker.start("Full")
    tracker.wait(r"^LASAPTEST READY", 30)
    check("egg arrow after restart", check_all(tracker, pack, client, sign, marks).item("up1")["stage"], 2)
    tracker.shot("marks_restarted")
    client.close()
    tracker.stop()


def step_lean(pack: Pack, tracker: Tracker, server: Server) -> None:
    """Slot Lean, another seed, in the state Full left: everything resets; Lean's settings."""
    tracker.start("Lean")
    tracker.wait(r"^LASAPTEST READY", 30)
    client = Client("Lean")
    dump = check_all(tracker, pack, client)
    check("egg arrow on another seed", dump.item("up1")["stage"], 0)
    check("fairyq on another seed", dump.item("fairyq")["active"], False)
    check("Fairy Queen with Free Gifts off", dump.section(dump.find("/Fairy Queen/Red|Blue Tunic"))[0], 0)
    tracker.shot("lean_connected")
    client.close()
    tracker.stop()


STEPS = {"manual": step_manual, "full": step_full, "marks": step_marks, "lean": step_lean}


def main() -> None:
    args = sys.argv[1:]
    variant = "tracker"
    if "--variant" in args:
        i = args.index("--variant")
        variant = args[i + 1]
        del args[i:i + 2]
    pack = Pack(Path(args[0]).resolve())
    steps = args[1:] or list(STEPS)
    tracker, server = Tracker(pack, variant), Server()
    name = ""
    try:
        for name in steps:
            start = time.time()
            STEPS[name](pack, tracker, server)
            print(f"PASS {name} {time.time() - start:.1f} s")
    except Fail as e:
        print(f"FAIL {name}: {e}")
        if tracker.proc:
            tracker.shot(f"fail_{name}")
        sys.exit(1)
    finally:
        if tracker.proc:
            tracker.stop()
        server.stop()


if __name__ == "__main__":
    main()
