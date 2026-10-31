"""Runs the sandbox test suite (scripts/tests/suite.txt) on several emulator instances at once, with the world's
tests beside it (TESTING.md, "Test suite"). Run in .venv-ap, with the sandbox lock held or free.

python suite.py [-j <instances>] [--no-world] [<group> ...]

Each group runs on a suite instance (emu.py, LASAP_INSTANCE 1 and up): a copy of the sandbox's data folder with the
mod the sandbox has installed (deploy_mod.sh), which must be suite.txt's (its mod: line), this checkout's test
module (patcher/exl-test, build_exl.sh) and the group's config.ini changes, from the new-game snapshot's saves, in a
fresh emulator, with a fresh client session when the group needs one. The sandbox itself is left as it is.
Instances run their groups side by side, the longest first (by the last run's times). Logs go to the sandbox folder's
suite/logs.
"""
import argparse
import json
import os
import queue
import shlex
import shutil
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

import emu

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "scripts" / "tests"
SUITE = Path(emu.SANDBOX) / "suite"
LOGS = SUITE / "logs"
TIMES = SUITE / "times.json"
TITLE = "01006BB00C6F0000"
MODULE = ROOT / "patcher" / "exl-test" / "subsdk9"
NEWGAME = Path(emu.SANDBOX) / "save-newgame"
GHOST_SECRET = "test"
SCRIPT_TIMEOUT = 300
PY = sys.executable


@dataclass
class Group:
    name: str
    scripts: list[str]
    config: dict[tuple[str, str], str] = field(default_factory=dict)
    session: str = ""   # "", "client" or "ghost"


def read_groups(path: Path) -> list[Group]:
    """suite.txt: one group per line, `<name> [<section>.<key>=<value> ...] [client|ghost]: <script> ...`."""
    groups = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("mod:"):
            continue
        head, _, scripts = line.partition(":")
        name, *opts = head.split()
        group = Group(name, scripts.split())
        for opt in opts:
            if opt in ("client", "ghost"):
                group.session = opt
            else:
                key, _, value = opt.partition("=")
                section, _, option = key.partition(".")
                group.config[(section, option)] = value
        groups.append(group)
    return groups


def set_option(text: str, section: str, option: str, value: str) -> str:
    """config.ini with `option = value` in [section], replaced or added at the section's end."""
    lines = text.splitlines()
    current, last = None, None
    for i, line in enumerate(lines):
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            current = s[1:-1]
        elif current == section:
            if s.partition("=")[0].strip() == option:
                lines[i] = f"{option} = {value}"
                return "\n".join(lines) + "\n"
            if s:
                last = i
    if last is None:
        raise SystemExit(f"config.ini has no [{section}]")
    lines.insert(last + 1, f"{option} = {value}")
    return "\n".join(lines) + "\n"


class Instance:
    """A suite instance: emu.py's LASAP_INSTANCE n, its data folder, ports and e2e folder."""

    def __init__(self, n: int):
        self.n = n
        self.home = Path(emu.instance_home(n))
        self.data = self.home / "ryujinx"
        self.e2e = self.home / "e2e"
        self.port = emu.instance_port(n)
        self.relay = self.port + 6
        self.env = dict(os.environ, LASAP_INSTANCE=str(n), LASAP_E2E=str(self.e2e), LASAP_GAME_PORT=str(self.port),
                        PYTHONUNBUFFERED="1")

    def prepare(self) -> None:
        """Copies the sandbox's data folder once (without its translation caches), and its emulator settings and
        installed mod with this checkout's test module each run."""
        sandbox = Path(emu.SANDBOX) / "ryujinx"
        if not self.data.exists():
            def skip(folder: str, _names: list[str]) -> list[str]:
                return ["cpu"] if Path(folder).name == "cache" else ["lasap"] if Path(folder).name == "sdcard" else []
            shutil.copytree(sandbox, self.data, ignore=skip)
        shutil.copyfile(sandbox / "Config.json", self.data / "Config.json")
        for part in (Path("sdcard/atmosphere/contents") / TITLE, Path("sdcard/config/lasr-exl")):
            shutil.rmtree(self.data / part, ignore_errors=True)
            shutil.copytree(sandbox / part, self.data / part)
        shutil.copyfile(MODULE, self.data / "sdcard/atmosphere/contents" / TITLE / "exefs/subsdk9")
        self.e2e.mkdir(parents=True, exist_ok=True)

    def emu(self, cmd: str) -> None:
        subprocess.run([PY, str(ROOT / "scripts" / "emu.py"), cmd], env=self.env, check=True,
                       stdout=subprocess.DEVNULL)

    def reset(self, base_config: str, group: Group) -> None:
        """The group's config.ini, the new-game snapshot's saves, no checks file and empty session files."""
        config = set_option(base_config, "archipelago", "port", str(self.port))
        for (section, option), value in group.config.items():
            config = set_option(config, section, option, value)
        (self.data / "sdcard/config/lasr-exl/config.ini").write_text(config, encoding="utf-8")
        save = self.data / "bis/user/save"
        shutil.rmtree(save, ignore_errors=True)
        shutil.copytree(NEWGAME, save)
        shutil.rmtree(self.data / "sdcard/lasap", ignore_errors=True)
        for name in ("cmds.txt", "peer_cmds.txt", "server_cmds.txt", "client.log", "peer.log", "relay.log"):
            (self.e2e / name).write_bytes(b"")


def kill_tree(proc: subprocess.Popen) -> None:
    # The venv's python.exe starts the interpreter as a child process: kill the launcher's tree, by its PID.
    if proc.poll() is None:
        subprocess.run(["taskkill", "/PID", str(proc.pid), "/T", "/F"], capture_output=True)


def feed(path: Path, proc: subprocess.Popen) -> None:
    """Writes each line appended to path to the process's stdin, as `tail -f path | process` does."""
    stdin = proc.stdin
    assert stdin is not None
    with open(path, "rb") as f:
        while proc.poll() is None:
            line = f.readline()
            if line.endswith(b"\n"):
                try:
                    stdin.write(line)
                    stdin.flush()
                except OSError:
                    return
            else:
                f.seek(-len(line), os.SEEK_CUR)
                time.sleep(0.05)


class Session:
    """The scripted client session a group needs: the LAS client fed from cmds.txt, and for ghost groups a ghost
    relay and scripts/ghost_peer.py fed from peer_cmds.txt."""

    def __init__(self, inst: Instance, kind: str):
        self.procs: list[subprocess.Popen] = []
        if not kind:
            return
        args = []
        if kind == "ghost":
            self.spawn(inst, [str(ROOT / "apworld" / "las" / "ghost_relay.py"), "--host", "127.0.0.1",
                              "--port", str(inst.relay)], "relay.log")
            wait_port(inst.relay, 20)
            self.spawn(inst, [str(ROOT / "scripts" / "ghost_peer.py"), "--relay", f"127.0.0.1:{inst.relay}",
                              "--secret", GHOST_SECRET, "--seconds", "3600"], "peer.log", "peer_cmds.txt")
            args = ["--ghost-relay", f"127.0.0.1:{inst.relay}", "--ghost-secret", GHOST_SECRET]
        self.spawn(inst, [str(ROOT / "scripts" / "las_client.py"), "--nogui", "--game-host", "127.0.0.1",
                          "--game-port", str(inst.port), "--name", "Link", *args], "client.log", "cmds.txt")

    def spawn(self, inst: Instance, args: list[str], log: str, stdin: str = "") -> None:
        proc = subprocess.Popen([PY, *args], env=inst.env, stdout=open(inst.e2e / log, "wb"),
                                stderr=subprocess.STDOUT, stdin=subprocess.PIPE if stdin else subprocess.DEVNULL,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        self.procs.append(proc)
        if stdin:
            threading.Thread(target=feed, args=(inst.e2e / stdin, proc), daemon=True).start()

    def stop(self) -> None:
        for proc in reversed(self.procs):
            kill_tree(proc)


def wait_port(port: int, seconds: float) -> None:
    end = time.time() + seconds
    while True:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=1).close()
            return
        except OSError:
            if time.time() > end:
                raise
            time.sleep(0.1)


@dataclass
class Result:
    group: Group
    instance: int
    seconds: float = 0
    times: list[tuple[str, float]] = field(default_factory=list)
    failed: str = ""   # the script that failed
    tail: str = ""


def script_path(name: str) -> Path:
    path = Path(name)
    return path if path.suffix else TESTS / f"{name}.txt"


def run_group(inst: Instance, group: Group, base_config: str) -> Result:
    result = Result(group, inst.n)
    began = time.time()
    inst.emu("stop")
    inst.reset(base_config, group)
    session = Session(inst, group.session)
    try:
        inst.emu("start")
        mark = time.time()
        for name in group.scripts:
            log = LOGS / f"{group.name}.{Path(name).stem}.log"
            with open(log, "wb") as out:
                proc = subprocess.Popen([PY, str(ROOT / "scripts" / "e2e.py"), "run", str(script_path(name))],
                                        env=inst.env, stdout=out, stderr=subprocess.STDOUT)
                try:
                    code = proc.wait(SCRIPT_TIMEOUT)
                except subprocess.TimeoutExpired:
                    kill_tree(proc)
                    code = -1
            now = time.time()
            result.times.append((Path(name).stem, now - mark))
            mark = now
            if code != 0:
                result.failed = Path(name).stem
                result.tail = "\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-6:])
                break
    finally:
        session.stop()
        inst.emu("stop")
    result.seconds = time.time() - began
    return result


def run_world(results: dict) -> None:
    """The world's tests (pytest), in this checkout's vendor/Archipelago."""
    began = time.time()
    log = LOGS / "world.log"
    with open(log, "wb") as out:
        code = subprocess.run([PY, "-m", "pytest", "worlds/las/test", "-q", "-p", "no:cacheprovider"],
                              cwd=ROOT / "vendor" / "Archipelago", stdout=out, stderr=subprocess.STDOUT).returncode
    last = log.read_text(encoding="utf-8", errors="replace").strip().splitlines()[-1:]
    results["world"] = (code, time.time() - began, last[0] if last else "")


def base_config_hash() -> str:
    config = (Path(emu.SANDBOX) / "ryujinx/sdcard/config/lasr-exl/config.ini").read_text(encoding="utf-8")
    return next((l.partition("=")[2].strip() for l in config.splitlines() if l.startswith("seed_hash")), "?")


def check_mod(path: Path) -> None:
    """Refuses a sandbox whose installed mod is not the suite's: the make_test_patch.py patch of suite.txt's `mod:`
    options, whose seed_hash (a hash of the patch) the mod's config.ini carries."""
    line = next((l for l in path.read_text(encoding="utf-8").splitlines() if l.startswith("mod:")), None)
    if line is None:
        raise SystemExit(f"{path}: no mod: line")
    SUITE.mkdir(parents=True, exist_ok=True)
    patch = SUITE / "suite.aplas"
    subprocess.run([PY, str(ROOT / "scripts" / "make_test_patch.py"), str(patch), *shlex.split(line[4:])],
                   check=True, stdout=subprocess.DEVNULL)
    wanted = json.loads(patch.read_text(encoding="utf-8"))["seed_hash"]
    if base_config_hash() != wanted:
        out = SUITE / "mod"
        raise SystemExit(
            f"the sandbox's mod {base_config_hash()} is not the suite's ({wanted}, suite.txt's mod: line). Install it "
            f"(with the sandbox lock):\n"
            f"  python patcher/las_ap_patch.py {patch.as_posix()} --romfs \"$(python scripts/local_paths.py romfs)\" "
            f"--out {out.as_posix()}\n"
            f"  bash scripts/deploy_mod.sh {out.as_posix()}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-j", type=int, default=4, help="emulator instances (default 4)")
    parser.add_argument("--no-world", action="store_true", help="skip the world's tests")
    parser.add_argument("groups", nargs="*", help="groups to run (default all)")
    args = parser.parse_args()
    emu.check()
    if not MODULE.exists():
        raise SystemExit(f"no test module {MODULE}: run scripts/build_exl.sh")
    groups = read_groups(TESTS / "suite.txt")
    if args.groups:
        unknown = set(args.groups) - {g.name for g in groups}
        if unknown:
            raise SystemExit(f"no such group: {', '.join(sorted(unknown))}")
        groups = [g for g in groups if g.name in args.groups]
    for g in groups:
        for name in g.scripts:
            if not script_path(name).exists():
                raise SystemExit(f"group {g.name}: no script {script_path(name)}")
    check_mod(TESTS / "suite.txt")
    print(f"test module built {time.strftime('%Y-%m-%d %H:%M', time.localtime(MODULE.stat().st_mtime))}; mod "
          f"{base_config_hash()}", flush=True)
    began = time.time()
    LOGS.mkdir(parents=True, exist_ok=True)
    try:
        times = json.loads(TIMES.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        times = {}
    # The longest groups first, so the last ones to finish are short.
    groups.sort(key=lambda g: -times.get(g.name, 60))
    base_config = (Path(emu.SANDBOX) / "ryujinx/sdcard/config/lasr-exl/config.ini").read_text(encoding="utf-8")
    instances = [Instance(n) for n in range(1, min(args.j, len(groups)) + 1)]
    world: dict = {}
    world_thread = threading.Thread(target=run_world, args=(world,))
    if not args.no_world:
        world_thread.start()
    todo: queue.Queue[Group] = queue.Queue()
    for g in groups:
        todo.put(g)
    results: list[Result] = []
    printing = threading.Lock()
    stopping = threading.Event()

    def work(inst: Instance) -> None:
        inst.prepare()
        while not stopping.is_set():
            try:
                group = todo.get_nowait()
            except queue.Empty:
                return
            result = run_group(inst, group, base_config)
            with printing:
                results.append(result)
                steps = ", ".join(f"{name} {s:.1f}" for name, s in result.times)
                print(f"[{time.time() - began:6.1f}] {'FAIL' if result.failed else 'PASS'} {group.name} "
                      f"(instance {inst.n}, {result.seconds:.1f} s: {steps})", flush=True)
                if result.failed:
                    print(f"    {result.failed}: {LOGS / (group.name + '.' + result.failed + '.log')}\n    " +
                          result.tail.replace("\n", "\n    "), flush=True)

    workers = [threading.Thread(target=work, args=(inst,)) for inst in instances]
    for w in workers:
        w.start()
    try:
        for w in workers:
            w.join()
    except KeyboardInterrupt:
        # Stopping the emulators ends the running scripts; each worker then stops its session.
        stopping.set()
        for inst in instances:
            inst.emu("stop")
        for w in workers:
            w.join()
        raise
    times.update({r.group.name: round(r.seconds, 1) for r in results if not r.failed})
    TIMES.write_text(json.dumps(times, indent=1, sort_keys=True), encoding="utf-8")
    failed = [r.group.name for r in results if r.failed]
    print(f"sandbox: {len(results) - len(failed)}/{len(results)} groups passed in {time.time() - began:.1f} s "
          f"({sum(r.seconds for r in results):.0f} s of group time on {len(instances)} instances)")
    if not args.no_world:
        world_thread.join()
        code, seconds, last = world["world"]
        print(f"world: {'passed' if code == 0 else 'FAILED'} in {seconds:.1f} s: {last}")
        if code != 0:
            failed.append("world")
    print(f"all done in {time.time() - began:.1f} s" + (f"; failed: {', '.join(failed)}" if failed else ""))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
