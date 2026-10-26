"""Where a Switch emulator keeps its SD card, mods, games and RomFS dumps, and how to start it with a game. Each
family the client knows is an Emulator subclass; emulator() picks it from the program's name."""
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

TITLE_ID = "01006BB00C6F0000"
GAME_EXTENSIONS = (".nsp", ".xci")


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


def same_path(a: Path, b: Path) -> bool:
    return os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b))


class Emulator:
    """An emulator the client does not know: the SD card folder setting says where its SD card is."""
    # how to make the RomFS dump, one sentence per step ({name}: the emulator's name)
    romfs_steps: Tuple[str, ...] = ("Dump the game's RomFS with {name} (\"Extract RomFS\" or \"Dump RomFS\").",
                                    "Choose the dumped folder here.")
    finds_romfs = False  # the client finds the emulator's RomFS dumps by itself (find_romfs)

    def __init__(self, exe: str) -> None:
        self.exe = exe

    @property
    def name(self) -> str:
        return Path(self.exe).stem or "your emulator"

    def default_data_dir(self) -> Optional[Path]:
        """The data folder the emulator uses by itself; None when the client does not know it."""
        return None

    def data_dir(self, sd_card: Optional[Path]) -> Optional[Path]:
        """The data folder that goes with an SD card folder setting, or with none (the emulator's own SD card)."""
        return sd_card.parent if sd_card is not None else self.default_data_dir()

    def sd_card(self, data_dir: Path) -> Path:
        return data_dir / "sdcard"

    def mods(self, sd_card: Path, data_dir: Path) -> Path:
        return sd_card / "atmosphere" / "contents" / TITLE_ID

    def mod_folders(self, target: "Target") -> List[Path]:
        """The folders the emulator loads Link's Awakening mods from, besides or including target.mods."""
        return []

    def game_dirs(self, data_dir: Path) -> List[Path]:
        """The folders the emulator lists games from."""
        return []

    def recent_games(self, data_dir: Path) -> List[Path]:
        """Games the emulator opened recently."""
        return []

    def find_romfs(self, data_dir: Path) -> Optional[Path]:
        """A RomFS dump where the emulator puts its dumps."""
        return None

    def start_args(self, game: str, data_dir: Optional[Path]) -> List[str]:
        """data_dir: the target's, None without one."""
        return [self.exe] + ([game] if game else [])


class Ryujinx(Emulator):
    """Ryujinx (and Ryubing) keeps everything in its data folder: `portable` next to Ryujinx.exe, else the per-user
    Ryujinx folder, or the folder given with --root-data-dir, which an SD card folder setting stands for."""
    romfs_steps = ("In Ryujinx, right-click Link's Awakening.",
                   "Choose Extract Data, then RomFS, and pick an empty folder.",
                   "Choose that folder here.")

    @property
    def name(self) -> str:
        return "Ryujinx"

    def default_data_dir(self) -> Path:
        portable = Path(self.exe).parent / "portable"
        return portable if portable.is_dir() else _config_home() / "Ryujinx"

    def mod_folders(self, target: "Target") -> List[Path]:
        contents = target.data_dir / "mods" / "contents"
        return [p for d in (contents / TITLE_ID, contents / TITLE_ID.lower()) if d.is_dir() for p in d.iterdir()]

    def game_dirs(self, data_dir: Path) -> List[Path]:
        try:
            config = json.loads((data_dir / "Config.json").read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            return []
        return [Path(d) for d in config.get("game_dirs", []) if isinstance(d, str)]

    def start_args(self, game: str, data_dir: Optional[Path]) -> List[str]:
        args = [self.exe]
        if data_dir is not None and not same_path(data_dir, self.default_data_dir()):
            args += ["--root-data-dir", str(data_dir)]
        return args + ([game] if game else [])


@dataclass
class YuzuConfig:
    """The folders in a yuzu-based emulator's qt-config.ini."""
    sdmc: Optional[Path] = None
    load: Optional[Path] = None
    dump: Optional[Path] = None
    gamedirs: List[Path] = field(default_factory=list)
    recent: List[Path] = field(default_factory=list)


class Yuzu(Emulator):
    """Yuzu and its forks use their `user` folder next to the program, else the per-user folder; their qt-config.ini
    can move the SD card, mod and dump folders. An SD card folder setting moves only the SD card."""
    FORKS = ("yuzu", "eden", "citron", "sudachi", "suyu", "torzu")
    romfs_steps = ("In {name}, right-click Link's Awakening and choose Dump RomFS.",
                   "The client finds the dump by itself.")
    finds_romfs = True

    def __init__(self, exe: str, fork: str) -> None:
        super().__init__(exe)
        self.fork = fork

    @property
    def name(self) -> str:
        return self.fork.capitalize()

    def default_data_dir(self) -> Path:
        user = Path(self.exe).parent / "user"
        return user if user.is_dir() else _data_home() / self.fork

    def data_dir(self, sd_card: Optional[Path]) -> Path:
        return self.default_data_dir()

    @staticmethod
    def config(data_dir: Path) -> YuzuConfig:
        result = YuzuConfig()
        try:
            text = (data_dir / "config" / "qt-config.ini").read_text(encoding="utf-8", errors="replace")
        except OSError:
            return result
        for line in text.splitlines():
            key, _, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if not value:
                continue
            if key in ("sdmc_directory", "load_directory", "dump_directory"):
                setattr(result, key.removesuffix("_directory"), Path(value))
            elif key.endswith("recentFiles"):
                result.recent = [Path(v.strip().strip('"')) for v in value.split(",") if v.strip()]
            elif key.endswith("romsPath"):
                result.gamedirs.append(Path(value))
            elif re.fullmatch(r"Paths\\gamedirs\\\d+\\path", key) and value.upper() not in ("SDMC", "USERNAND",
                                                                                              "SYSNAND"):
                result.gamedirs.append(Path(value))
        return result

    def sd_card(self, data_dir: Path) -> Path:
        return self.config(data_dir).sdmc or data_dir / "sdmc"

    def mods(self, sd_card: Path, data_dir: Path) -> Path:
        return (self.config(data_dir).load or data_dir / "load") / TITLE_ID / "Archipelago"

    def mod_folders(self, target: "Target") -> List[Path]:
        # its other load folders, and the SD card's atmosphere/contents, from which it loads only the RomFS
        load = target.mods.parent
        others = [p for p in load.iterdir() if not same_path(p, target.mods)] if load.is_dir() else []
        return others + [target.sd_card / "atmosphere" / "contents" / TITLE_ID]

    def game_dirs(self, data_dir: Path) -> List[Path]:
        return self.config(data_dir).gamedirs

    def recent_games(self, data_dir: Path) -> List[Path]:
        # files opened with File, Load File
        return self.config(data_dir).recent

    def find_romfs(self, data_dir: Path) -> Optional[Path]:
        # Dump RomFS writes <dump>/<title id>/romfs
        return romfs_root(str((self.config(data_dir).dump or data_dir / "dump") / TITLE_ID))

    def start_args(self, game: str, data_dir: Optional[Path]) -> List[str]:
        return [self.exe] + (["-g", game] if game else [])


def emulator(exe: str) -> Emulator:
    """The emulator family of a program, from its name."""
    name = Path(exe).stem.lower()
    if "ryujinx" in name or "ryubing" in name:
        return Ryujinx(exe)
    fork = next((n for n in Yuzu.FORKS if n in name), None)
    return Yuzu(exe, fork) if fork else Emulator(exe)


@dataclass
class Target:
    """Where the mod goes and how the game starts."""
    emulator: Emulator
    sd_card: Path
    mods: Path
    data_dir: Path


def target(exe: str, sd_card: str, mod_folder: str) -> Optional[Target]:
    """The install target from the settings. An SD card folder overrides the emulator's own; None without either."""
    emu = emulator(exe)
    sd = Path(sd_card) if sd_card else None
    data_dir = emu.data_dir(sd)
    if data_dir is None:
        return None
    sd = sd or emu.sd_card(data_dir)
    return Target(emu, sd, Path(mod_folder) if mod_folder else emu.mods(sd, data_dir), data_dir)


def other_mods(t: Target) -> List[Path]:
    """Other Link's Awakening mods the emulator loads together with this one: folders with files in them."""
    found: List[Path] = []
    for p in t.emulator.mod_folders(t):
        if p.is_dir() and not same_path(p, t.mods) and any(p.iterdir()) and p not in found:
            found.append(p)
    return found


# A Switch's SD card

@dataclass
class Drive:
    """A drive that may be a Switch's SD card."""
    root: Path
    label: str
    atmosphere: bool  # it has Atmosphère's atmosphere folder


def has_atmosphere(sd_card: Path) -> bool:
    return (sd_card / "atmosphere").is_dir()


def switch_drives() -> List[Drive]:
    """Removable drives, and other drives that hold an atmosphere folder (card readers that show as fixed drives).
    Network and optical drives are left out: reading them can stall."""
    drives: List[Drive] = []
    if sys.platform == "win32":
        import ctypes
        kernel32 = ctypes.windll.kernel32
        mask = kernel32.GetLogicalDrives()
        for i in range(26):
            if not mask & (1 << i):
                continue
            root = f"{chr(65 + i)}:\\"
            kind = kernel32.GetDriveTypeW(root)  # 2 removable, 3 fixed, 4 network, 5 optical
            if kind not in (2, 3) or root == os.environ.get("SystemDrive", "C:") + "\\":
                continue
            name = ctypes.create_unicode_buffer(261)
            if not kernel32.GetVolumeInformationW(root, name, len(name), None, None, None, None, 0):
                continue  # no card in the reader
            atmosphere = has_atmosphere(Path(root))
            if kind == 2 or atmosphere:
                drives.append(Drive(Path(root), name.value, atmosphere))
    else:
        user = os.environ.get("USER", "")
        for parent in (Path("/Volumes"), Path("/media") / user, Path("/run/media") / user):
            try:
                mounts = sorted(p for p in parent.iterdir() if p.is_dir())
            except OSError:
                continue
            drives += [Drive(p, p.name, has_atmosphere(p)) for p in mounts]
    return drives


# Finding the game and the RomFS dump

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


def find_game(dirs: Iterable[Path], max_depth: int = 3, max_files: int = 20000,
              recent: Iterable[Path] = ()) -> Optional[Path]:
    """Link's Awakening in the recent games, else in the emulator's game folders. The search goes `max_depth`
    folders deep and gives up after `max_files` files, as a game folder can be a whole drive."""
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
