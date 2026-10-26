"""Headless Ryubing harness for the sandbox (see TESTING.md).

python emu.py start [--fresh-log]   boot the game headless; prints the PID
python emu.py stop                  kill the instance started by this script (by PID)
python emu.py shot <out.png>        capture the game window
python emu.py log [regex]           print matching lines of the newest emulator log
python emu.py wait <regex> [secs]   wait until the newest log matches
python emu.py lock <owner>          take the sandbox for a session; fails while another owner holds it
python emu.py unlock <owner>        give it back
python emu.py check                 fail unless the sandbox is free or held by $LASAP_SANDBOX_OWNER

Sessions that run side by side share the sandbox (data folder, emu.pid, port 43385, the e2e folder). A session takes
the lock before it deploys, resets saves or starts the emulator, runs those with LASAP_SANDBOX_OWNER set to its
owner name, and unlocks when its test run is over. `start`, `stop`, deploy_mod.sh and reset_saves.sh refuse while
another owner holds the lock.

LASAP_INSTANCE=<n> (n >= 1) works on suite instance n instead (scripts/suite.py): a copy of the sandbox's data folder
in suite/<n>, with its own emu.pid and game port (instance_port). Instance 0 is the sandbox itself.

The emulator's windows open without taking focus and stay behind other windows (`tuck`), so the sandbox can run
while the user works or plays. They stay open: a minimized window may stop presenting frames.
"""
import ctypes
import glob
import os
import re
import subprocess
import sys
import time

RYU = r"D:\Games\Ryujinx\Ryujinx.exe"
SANDBOX = r"D:\DEV\LASAP-sandbox"
GAME = r"D:\Games\Switch\The Legend of Zelda Links Awakening [01006BB00C6F0000][v0].nsp"
LOGS = r"D:\Games\Ryujinx\Logs"
LOCKFILE = os.path.join(SANDBOX, "sandbox.lock")


def instance_home(n: int) -> str:
    """The folder of suite instance n: its data folder (ryujinx), emu.pid and e2e folder. 0 is the sandbox."""
    return SANDBOX if n == 0 else os.path.join(SANDBOX, "suite", str(n))


def instance_port(n: int) -> int:
    """The game port of instance n; its debug port is 100 above it and its ghost relay's 6 above it."""
    return 43385 + 10 * n


INSTANCE = int(os.environ.get("LASAP_INSTANCE", "0"))
HOME = instance_home(INSTANCE)
DATA = os.path.join(HOME, "ryujinx")
PIDFILE = os.path.join(HOME, "emu.pid")
TUCK_SECONDS = 90  # how long after the start new emulator windows are sent behind the others
SW_SHOWNOACTIVATE = 4


def newest_log() -> str:
    logs = glob.glob(os.path.join(LOGS, "*.log"))
    if not logs:
        return ""
    path = max(logs, key=os.path.getctime)
    # Only logs from the instance started by this script count.
    started = os.path.getmtime(PIDFILE) if os.path.exists(PIDFILE) else 0
    return path if os.path.getctime(path) >= started - 1 else ""


def holder() -> str:
    try:
        return open(LOCKFILE, encoding="utf-8").read().split("	")[0]
    except FileNotFoundError:
        return ""


def held_by_other(owner: str) -> str:
    """The lock's holder when someone other than owner holds it, with how long ago they took it."""
    who = holder()
    if not who or who == owner:
        return ""
    minutes = (time.time() - os.path.getmtime(LOCKFILE)) / 60
    return f"{who} (for {minutes:.0f} min)"


def lock(owner: str) -> None:
    try:
        fd = os.open(LOCKFILE, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        other = held_by_other(owner)
        if other:
            sys.exit(f"sandbox is held by {other}; wait for it to unlock")
        os.utime(LOCKFILE)
        print("sandbox already held by", owner)
        return
    os.write(fd, f"{owner}	{time.strftime('%Y-%m-%d %H:%M:%S')}".encode())
    os.close(fd)
    print("sandbox locked by", owner)


def unlock(owner: str) -> None:
    other = held_by_other(owner)
    if other:
        sys.exit(f"sandbox is held by {other}, not {owner}")
    if os.path.exists(LOCKFILE):
        os.remove(LOCKFILE)
    print("sandbox unlocked")


def check() -> None:
    other = held_by_other(os.environ.get("LASAP_SANDBOX_OWNER", ""))
    if other:
        sys.exit(f"sandbox is held by {other}; set LASAP_SANDBOX_OWNER to the lock's owner or wait for it")


def start() -> None:
    check()
    stop(quiet=True)
    args = [RYU, "--no-gui", "--root-data-dir", DATA, "--graphics-backend", os.environ.get("LASAP_GFX", "Vulkan"),
            "--input-id-1", "0", "--enable-keyboard", "--skip-user-profiles-manager",
            "--ignore-controller-applet", "--audio-volume", "0", GAME]
    if os.environ.get("LASAP_PTC") != "1":
        args.insert(-1, "--disable-ptc")
    if os.environ.get("LASAP_INPUT") == "config":
        # a person plays: the data folder's Config.json input (its controller) instead of the keyboard as player 1
        i = args.index("--input-id-1")
        del args[i:i + 2]
    out = open(os.path.join(HOME, "emu_stdout.txt"), "w")
    # --audio-volume 0 does not mute; SDL's dummy driver keeps the sandbox silent. --disable-ptc keeps translations
    # of one run from the next; LASAP_PTC=1 keeps the translation cache on, as players have it. LASAP_AUDIO=1 plays
    # the game's sound, for the user to listen.
    env = dict(os.environ, SDL_AUDIO_DRIVER="dummy", SDL_AUDIODRIVER="dummy")
    if os.environ.get("LASAP_AUDIO") == "1":
        args[args.index("--audio-volume") + 1] = "1"
        env = dict(os.environ)
    foreground = ctypes.windll.user32.GetForegroundWindow()
    info = subprocess.STARTUPINFO()
    info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    info.wShowWindow = SW_SHOWNOACTIVATE
    proc = subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT, env=env, startupinfo=info)
    with open(PIDFILE, "w") as f:
        f.write(str(proc.pid))
    # pythonw has no console: a console process here would open a terminal window over the user's.
    pythonw = os.path.join(os.path.dirname(sys.executable), "pythonw.exe")
    subprocess.Popen([pythonw, os.path.abspath(__file__), "tuck", str(proc.pid), str(foreground or 0)],
                     creationflags=subprocess.CREATE_NO_WINDOW, close_fds=True)
    print(proc.pid)


def tuck(pid: int, foreground: int) -> None:
    """Sends each new window of the emulator behind the other windows without activating it, and gives the
    foreground back to the window that had it when the emulator started."""
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND] + [ctypes.c_int] * 4 + [wintypes.UINT]
    HWND_BOTTOM, SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE = 1, 0x1, 0x2, 0x10
    proc = wintypes.DWORD()
    seen = set()

    def windows() -> list:
        found = []

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def each(hwnd, _):
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc))
            if proc.value == pid and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
            return True
        user32.EnumWindows(each, 0)
        return found

    deadline = time.time() + TUCK_SECONDS
    while time.time() < deadline:
        for hwnd in windows():
            if hwnd not in seen:
                seen.add(hwnd)
                user32.SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE)
            if foreground and user32.GetForegroundWindow() == hwnd:
                user32.SetForegroundWindow(foreground)
                user32.SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE)
        time.sleep(0.2)


def stop(quiet: bool = False) -> None:
    check()
    if not os.path.exists(PIDFILE):
        return
    pid = open(PIDFILE).read().strip()
    subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
    # The process keeps the mod files open for a moment; wait until it is gone so they can be replaced.
    for _ in range(40):
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True).stdout
        if pid not in out:
            break
        time.sleep(0.25)
    os.remove(PIDFILE)
    if not quiet:
        print("stopped", pid)


def shot(out: str, pid: int | None = None) -> None:
    """Captures the emulator's game window (PrintWindow: it works while the window is covered) into a PNG, or the
    window of another process (pid), such as the sandbox PopTracker's."""
    import struct
    import zlib
    from ctypes import wintypes
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    for fn in (user32.GetDC, gdi32.CreateCompatibleDC, gdi32.CreateCompatibleBitmap, gdi32.SelectObject):
        fn.restype = wintypes.HANDLE
    user32.GetDC.argtypes = [wintypes.HWND]
    gdi32.CreateCompatibleDC.argtypes = [wintypes.HANDLE]
    gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_int]
    gdi32.SelectObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    gdi32.DeleteObject.argtypes = gdi32.DeleteDC.argtypes = [wintypes.HANDLE]
    gdi32.GetDIBits.argtypes = [wintypes.HANDLE, wintypes.HANDLE, wintypes.UINT, wintypes.UINT, ctypes.c_void_p,
                                ctypes.c_void_p, wintypes.UINT]
    user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HANDLE, wintypes.UINT]
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HANDLE]
    if pid is None:
        pid = int(open(PIDFILE).read().strip())
    found = []
    proc = wintypes.DWORD()

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(proc))
        if proc.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(each, 0)
    for hwnd in found:
        rect = wintypes.RECT()
        user32.GetClientRect(hwnd, ctypes.byref(rect))
        w, h = rect.right - rect.left, rect.bottom - rect.top
        if w < 100:
            continue
        screen = user32.GetDC(None)
        dc = gdi32.CreateCompatibleDC(screen)
        bmp = gdi32.CreateCompatibleBitmap(screen, w, h)
        old = gdi32.SelectObject(dc, bmp)
        ok = user32.PrintWindow(hwnd, dc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
        gdi32.SelectObject(dc, old)
        # BITMAPINFOHEADER of a top-down 32-bit DIB
        info = struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
        bgra = ctypes.create_string_buffer(w * h * 4)
        gdi32.GetDIBits(dc, bmp, 0, h, bgra, info, 0)
        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(dc)
        user32.ReleaseDC(None, screen)
        rgb = bytearray(w * h * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = bgra.raw[2::4], bgra.raw[1::4], bgra.raw[0::4]
        stride = w * 3
        raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(h))

        def chunk(kind: bytes, data: bytes) -> bytes:
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
        with open(out, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                    chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b""))
        print(f"captured {w}x{h} ok={ok} -> {out}")
        return
    print("no emulator window to capture")


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
     "log": lambda: log(rest[0] if rest else ""), "tuck": lambda: tuck(int(rest[0]), int(rest[1])),
     "wait": lambda: wait(rest[0], float(rest[1]) if len(rest) > 1 else 120),
     "lock": lambda: lock(rest[0]), "unlock": lambda: unlock(rest[0]), "check": lambda: check()}[cmd]()
