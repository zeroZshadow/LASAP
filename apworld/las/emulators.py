"""Where a Switch emulator keeps its SD card, mods, games and RomFS dumps, and how to start it with a game.

Ryujinx keeps everything in its data folder: `portable` next to Ryujinx.exe, else the per-user Ryujinx folder, or
the folder given with --root-data-dir. Yuzu and its forks (Eden, Citron, ...) use their `user` folder next to the
executable, else the per-user folder, and their qt-config.ini can move the SD card, mod and dump folders.
"""
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional

TITLE_ID = "01006BB00C6F0000"
YUZU_NAMES = ("yuzu", "eden", "citron", "sudachi", "suyu", "torzu")
EMULATOR_NAMES = ("ryujinx", "ryubing") + YUZU_NAMES
GAME_EXTENSIONS = (".nsp", ".xci")


def kind_of(exe: str) -> str:
    """"ryujinx", "yuzu" (yuzu and its forks) or "other", from the executable's name."""
    name = Path(exe).stem.lower()
    if "ryujinx" in name or "ryubing" in name:
        return "ryujinx"
    if any(n in name for n in YUZU_NAMES):
        return "yuzu"
    return "other"


def display_name(exe: str) -> str:
    kind = kind_of(exe)
    if kind == "ryujinx":
        return "Ryujinx"
    if kind == "yuzu":
        return next(n for n in YUZU_NAMES if n in Path(exe).stem.lower()).capitalize()
    return Path(exe).stem or "the emulator"


def _config_home() -> Path:
    if sys.platform == "win32":
        return Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming"))
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support"
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))


def _data_home() -> Path:
    if sys.platform in ("win32", "darwin"):
        return _config_home()
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))


def default_data_dir(exe: str) -> Optional[Path]:
    """The emulator's data folder: its portable folder next to the executable, else the per-user one."""
    kind = kind_of(exe)
    exe_dir = Path(exe).parent
    if kind == "ryujinx":
        return exe_dir / "portable" if (exe_dir / "portable").is_dir() else _config_home() / "Ryujinx"
    if kind == "yuzu":
        name = next(n for n in YUZU_NAMES if n in Path(exe).stem.lower())
        return exe_dir / "user" if (exe_dir / "user").is_dir() else _data_home() / name
    return None


def same_path(a: Path, b: Path) -> bool:
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


def yuzu_config(data_dir: Path) -> Dict[str, object]:
    """Folders from a yuzu-based emulator's qt-config.ini: sdmc, load, dump (Paths) and gamedirs (list)."""
    result: Dict[str, object] = {"gamedirs": [], "recent": []}
    try:
        text = (data_dir / "config" / "qt-config.ini").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return result
    for line in text.splitlines():
        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if not value:
            continue
        m = re.fullmatch(r"(sdmc|load|dump)_directory", key)
        if m:
            result[m.group(1)] = Path(value)
        elif key.endswith("recentFiles"):
            result["recent"] = [Path(v.strip().strip('"')) for v in value.split(",") if v.strip()]
        elif key.endswith("romsPath"):
            result["gamedirs"].append(Path(value))  # type: ignore[union-attr]
        elif re.fullmatch(r"Paths\\gamedirs\\\d+\\path", key) and value.upper() not in ("SDMC", "USERNAND", "SYSNAND"):
            result["gamedirs"].append(Path(value))  # type: ignore[union-attr]
    return result


@dataclass
class Target:
    """Where the mod goes and how the game starts."""
    kind: str
    sd_card: Path
    mods: Path
    data_dir: Optional[Path]


def target(exe: str, sd_card: str, mod_folder: str) -> Optional[Target]:
    """The install target from the settings. An SD card folder overrides the emulator's own; None without either."""
    kind = kind_of(exe) if exe else "other"
    emulator_data = default_data_dir(exe) if exe else None
    config = yuzu_config(emulator_data) if kind == "yuzu" and emulator_data else {}
    if sd_card:
        sd = Path(sd_card)
        data_dir = emulator_data if kind == "yuzu" and emulator_data else sd.parent
    elif emulator_data is not None:
        data_dir = emulator_data
        sd = Path(config.get("sdmc") or data_dir / ("sdmc" if kind == "yuzu" else "sdcard"))  # type: ignore[arg-type]
    else:
        return None
    if mod_folder:
        mods = Path(mod_folder)
    elif kind == "yuzu":
        mods = Path(config.get("load") or data_dir / "load") / TITLE_ID / "Archipelago"  # type: ignore[arg-type]
    else:
        mods = sd / "atmosphere" / "contents" / TITLE_ID
    return Target(kind, sd, mods, data_dir)


def other_mods(t: Target) -> List[Path]:
    """Other Link's Awakening mod folders the emulator loads too: Ryujinx's mods folder, a yuzu-based emulator's other
    load folders and its SD card's atmosphere/contents (which loads only the RomFS)."""
    candidates: List[Path] = []
    if t.kind == "ryujinx" and t.data_dir is not None:
        mods = t.data_dir / "mods" / "contents"
        candidates += [p for d in (mods / TITLE_ID, mods / TITLE_ID.lower()) if d.is_dir() for p in d.iterdir()]
    elif t.kind == "yuzu":
        load = t.mods.parent
        if load.is_dir():
            candidates += [p for p in load.iterdir() if not same_path(p, t.mods)]
        candidates.append(t.sd_card / "atmosphere" / "contents" / TITLE_ID)
    found: List[Path] = []
    for p in candidates:
        if p.is_dir() and not same_path(p, t.mods) and any(p.iterdir()) and p not in found:
            found.append(p)
    return found


def start_args(exe: str, game: str, data_dir: Optional[Path]) -> List[str]:
    """Ryujinx gets --root-data-dir when its data folder is not the default one."""
    args = [exe]
    kind = kind_of(exe)
    default = default_data_dir(exe)
    if kind == "ryujinx" and data_dir is not None and default is not None and not same_path(data_dir, default):
        args += ["--root-data-dir", str(data_dir)]
    if game:
        args += ["-g", game] if kind == "yuzu" else [game]
    return args


# Finding the game and the RomFS dump

def game_dirs(exe: str, data_dir: Optional[Path]) -> List[Path]:
    """The folders the emulator lists games from."""
    if data_dir is None:
        return []
    if kind_of(exe) == "ryujinx":
        try:
            config = json.loads((data_dir / "Config.json").read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            return []
        return [Path(d) for d in config.get("game_dirs", []) if isinstance(d, str)]
    if kind_of(exe) == "yuzu":
        return list(yuzu_config(data_dir)["gamedirs"])  # type: ignore[call-overload]
    return []


def _nsp_names(path: Path) -> List[str]:
    """File names inside an .nsp (a PFS0 archive)."""
    try:
        with open(path, "rb") as f:
            header = f.read(16)
            if len(header) < 16 or header[:4] != b"PFS0":
                return []
            count = int.from_bytes(header[4:8], "little")
            strings = int.from_bytes(header[8:12], "little")
            if count > 1000 or strings > 0x10000:
                return []
            f.seek(16 + count * 24)
            table = f.read(strings)
    except OSError:
        return []
    return [n.decode("utf-8", "replace") for n in table.split(b"\0") if n]


def is_game_file(path: Path) -> bool:
    """Link's Awakening's base game: the title id in the file name, or its ticket inside an .nsp. Updates and DLC have
    other title ids."""
    name = path.name.lower()
    if TITLE_ID.lower() in name:
        return True
    if path.suffix.lower() == ".nsp":
        return any(n.lower().startswith(TITLE_ID.lower()) for n in _nsp_names(path))
    return bool(re.search(r"link'?s[ _.-]*awakening", name)) and "update" not in name and "[upd]" not in name


def recent_games(exe: str, data_dir: Optional[Path]) -> List[Path]:
    """Games the emulator opened recently (yuzu-based emulators remember files opened with File, Load File)."""
    if kind_of(exe) != "yuzu" or data_dir is None:
        return []
    return list(yuzu_config(data_dir)["recent"])  # type: ignore[call-overload]


def find_game(dirs: Iterable[Path], max_depth: int = 3, max_files: int = 20000,
              recent: Iterable[Path] = ()) -> Optional[Path]:
    for path in recent:
        if path.is_file() and is_game_file(path):
            return path
    seen = 0
    for root in dirs:
        root = Path(root)
        if not root.is_dir():
            continue
        for current, folders, files in os.walk(root):
            if len(Path(current).relative_to(root).parts) >= max_depth:
                folders[:] = []
            for file in files:
                seen += 1
                if seen > max_files:
                    return None
                path = Path(current) / file
                if path.suffix.lower() in GAME_EXTENSIONS and is_game_file(path):
                    return path
    return None


def romfs_root(path: str) -> Optional[Path]:
    """The RomFS dump in `path`: the folder itself or its `romfs` subfolder. None when it is not one."""
    if not path:
        return None
    for candidate in (Path(path), Path(path) / "romfs", Path(path) / TITLE_ID / "romfs"):
        if (candidate / "region_common" / "event" / "PlayerStart.bfevfl").is_file():
            return candidate
    return None


def find_romfs(exe: str, data_dir: Optional[Path]) -> Optional[Path]:
    """A RomFS dump where the emulator puts it: yuzu-based emulators dump to <dump>/<title id>/romfs."""
    if kind_of(exe) != "yuzu" or data_dir is None:
        return None
    dump = yuzu_config(data_dir).get("dump") or data_dir / "dump"
    return romfs_root(str(Path(dump) / TITLE_ID))  # type: ignore[arg-type]


# Running emulators

@dataclass
class Running:
    exe: str
    data_dir: Optional[Path]  # Ryujinx's --root-data-dir, when given


def running_emulators() -> List[Running]:
    """Emulators that are running now, with Ryujinx's --root-data-dir (Windows only; elsewhere empty)."""
    if sys.platform != "win32":
        return []
    names = ",".join(f"'{n}.exe'" for n in EMULATOR_NAMES)
    script = (f"Get-CimInstance Win32_Process | Where-Object {{ @({names}) -contains $_.Name.ToLower() }} | "
              "ForEach-Object { $_.ExecutablePath + \"`t\" + $_.CommandLine }")
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", script], capture_output=True,
                             text=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    found = []
    for line in out.splitlines():
        exe, _, command = line.partition("\t")
        if not exe.strip():
            continue
        m = re.search(r'(?:--root-data-dir|-r)\s+(?:"([^"]+)"|(\S+))', command)
        found.append(Running(exe.strip(), Path(m.group(1) or m.group(2)) if m else None))
    return found


def is_running(exe: str) -> bool:
    return any(same_path(Path(r.exe), Path(exe)) for r in running_emulators())
