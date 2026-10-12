"""Headless Ryubing harness for the sandbox (see TESTING.md).

python emu.py start [--fresh-log]   boot the game headless; prints the PID
python emu.py stop                  kill the instance started by this script (by PID)
python emu.py shot <out.png>        capture the game window
python emu.py log [regex]           print matching lines of the newest emulator log
python emu.py wait <regex> [secs]   wait until the newest log matches
"""
import glob
import os
import re
import subprocess
import sys
import time

RYU = r"D:\Games\Ryujinx\Ryujinx.exe"
SANDBOX = r"D:\DEV\LASAP-sandbox"
DATA = os.path.join(SANDBOX, "ryujinx")
GAME = r"D:\Games\Switch\The Legend of Zelda Links Awakening [01006BB00C6F0000][v0].nsp"
LOGS = r"D:\Games\Ryujinx\Logs"
PIDFILE = os.path.join(SANDBOX, "emu.pid")
HERE = os.path.dirname(os.path.abspath(__file__))


def newest_log() -> str:
    logs = glob.glob(os.path.join(LOGS, "*.log"))
    if not logs:
        return ""
    path = max(logs, key=os.path.getctime)
    # Only logs from the instance started by this script count.
    started = os.path.getmtime(PIDFILE) if os.path.exists(PIDFILE) else 0
    return path if os.path.getctime(path) >= started - 1 else ""


def start() -> None:
    stop(quiet=True)
    args = [RYU, "--no-gui", "--root-data-dir", DATA, "--graphics-backend", os.environ.get("LASAP_GFX", "Vulkan"),
            "--input-id-1", "0", "--enable-keyboard", "--skip-user-profiles-manager",
            "--ignore-controller-applet", "--audio-volume", "0", GAME]
    out = open(os.path.join(SANDBOX, "emu_stdout.txt"), "w")
    proc = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT)
    with open(PIDFILE, "w") as f:
        f.write(str(proc.pid))
    print(proc.pid)


def stop(quiet: bool = False) -> None:
    if not os.path.exists(PIDFILE):
        return
    pid = open(PIDFILE).read().strip()
    subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
    os.remove(PIDFILE)
    if not quiet:
        print("stopped", pid)


def shot(out: str) -> None:
    pid = open(PIDFILE).read().strip()
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "capwin.ps1"), "-ProcId", pid, "-Out", out])


def log(pattern: str = "") -> None:
    rx = re.compile(pattern) if pattern else None
    logs = glob.glob(os.path.join(LOGS, "*.log"))
    with open(max(logs, key=os.path.getctime), encoding="utf-8", errors="replace") as f:
        for line in f:
            if rx is None or rx.search(line):
                print(line.rstrip()[:300])


def wait(pattern: str, secs: float = 120) -> None:
    rx = re.compile(pattern)
    deadline = time.time() + secs
    while time.time() < deadline:
        path = newest_log()
        if path:
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    if rx.search(line):
                        print(line.rstrip()[:300])
                        return
        time.sleep(1)
    print("timeout waiting for", pattern)
    sys.exit(1)


if __name__ == "__main__":
    cmd, rest = sys.argv[1], sys.argv[2:]
    {"start": lambda: start(), "stop": lambda: stop(), "shot": lambda: shot(rest[0]),
     "log": lambda: log(rest[0] if rest else ""),
     "wait": lambda: wait(rest[0], float(rest[1]) if len(rest) > 1 else 120)}[cmd]()
