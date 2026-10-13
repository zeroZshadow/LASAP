"""Turns an .aplas into an installed mod: runs the patcher, then copies its output to the game's SD card."""
import configparser
import importlib
import io
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import traceback
import zipfile
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional

from .emulators import TITLE_ID
from .host_settings import PLAYER_DEFAULTS

CONFIG_PATH = Path("config", "lasr-exl", "config.ini")
# Config files the patcher writes only for some options (the map tracker's icons); an install without one removes it.
OPTIONAL_CONFIG = ("ap_map.txt",)
# Next to the config: the build of the built-in patcher that wrote the installed mod, so a newer world reinstalls it.
BUILD_PATH = CONFIG_PATH.with_name("patcher_build.txt")
# Next to the config: the player settings the installed mod was patched with; the patch's defaults when missing.
SETTINGS_PATH = CONFIG_PATH.with_name("player_settings.json")
MOD_PARTS = ("romfs", "exefs")
# mod_state results
CURRENT, OUTDATED, OTHER_SEED, NONE, CHANGED = "current", "outdated", "other seed", "none", "settings changed"
# The release world carries the patcher in <world>/patcher (scripts/build_release.py); from source, it is the
# repository's patcher folder.
PATCHER_PREFIX = f"{Path(__file__).parent.name}/patcher/"
PATCHER_BUILD = PATCHER_PREFIX + "build.txt"
SOURCE_PATCHER = Path(__file__).resolve().parent.parent.parent / "patcher"
# Top-level modules of the patcher and the libraries it carries (patcher/lib)
PATCHER_MODULES = {"las_ap_patch", "ap_assets", "ap_ghost", "ap_goal", "ap_map", "ap_map_icons", "ap_model", "ap_ui",
                   "lasr_qt", "RandomizerCore", "evfl", "lms", "appdirs", "PySide6"}
_PATCHER_LOCK = threading.Lock()


class InstallError(Exception):
    pass


def split_command(command: str) -> List[str]:
    """Splits a command line; on Windows, backslashes stay path separators and quotes only group."""
    if sys.platform != "win32":
        return shlex.split(command)
    return [part[1:-1] if len(part) > 1 and part[0] == part[-1] == '"' else part
            for part in shlex.split(command, posix=False)]


def installed_seed(sd_card: Path, mods: Path) -> Optional[str]:
    """The seed hash of the installed mod, or None when no complete mod is installed."""
    config = configparser.ConfigParser()
    try:
        config.read(sd_card / CONFIG_PATH, encoding="utf-8")
    except configparser.Error:
        return None
    if not all((mods / part).is_dir() for part in MOD_PARTS):
        return None
    return config.get("archipelago", "seed_hash", fallback=None)


def installed_settings(sd_card: Path) -> Dict[str, Any]:
    """The player settings the installed mod was patched with."""
    try:
        return {**PLAYER_DEFAULTS, **json.loads((sd_card / SETTINGS_PATH).read_text(encoding="utf-8"))}
    except (OSError, ValueError, TypeError):
        return dict(PLAYER_DEFAULTS)


def mod_state(sd_card: Path, mods: Path, seed_hash: Optional[str], command: str,
              settings: Optional[Dict[str, Any]] = None) -> str:
    """Whether the installed mod is the current one for `seed_hash`: CURRENT, OUTDATED (written by another build of
    the built-in patcher), CHANGED (patched with other player `settings`), OTHER_SEED or NONE."""
    installed = installed_seed(sd_card, mods)
    if installed is None:
        return NONE
    if installed != seed_hash:
        return OTHER_SEED
    build = None if command.strip() else builtin_patcher_build()
    try:
        installed_build = (sd_card / BUILD_PATH).read_text(encoding="utf-8").strip()
    except OSError:
        installed_build = None
    if build is not None and installed_build != build:
        return OUTDATED
    if settings is not None and installed_settings(sd_card) != {**PLAYER_DEFAULTS, **settings}:
        return CHANGED
    return CURRENT


class _ThreadOutput(io.TextIOBase):
    """Stands in for stdout and stderr while the built-in patcher runs: its thread's lines go to the log, other
    threads' writes (and anything the log itself prints) to the original stream."""
    def __init__(self, original, log: Callable[[str], None]) -> None:
        self.original = original
        self.log = log
        self.thread = threading.get_ident()
        self.buffer = ""
        self.logging = False

    def write(self, text: str) -> int:
        if threading.get_ident() != self.thread or self.logging:
            return self.original.write(text) if self.original else len(text)
        self.buffer += text
        *lines, self.buffer = self.buffer.split("\n")
        self.logging = True
        try:
            for line in lines:
                if line.strip():
                    self.log(line.rstrip())
        finally:
            self.logging = False
        return len(text)

    def flush(self) -> None:
        if self.original:
            self.original.flush()


def _world_archive() -> Optional[Path]:
    """The .apworld this module was loaded from, or None when the world runs from a folder."""
    archive = getattr(globals().get("__loader__"), "archive", None)
    return Path(archive) if archive else None


def builtin_patcher_available() -> bool:
    archive = _world_archive()
    if archive is not None:
        with zipfile.ZipFile(archive) as zf:
            return PATCHER_BUILD in zf.namelist()
    return (SOURCE_PATCHER / "las_ap_patch.py").is_file()


def builtin_patcher_build() -> Optional[str]:
    """The build of the patcher the release world carries; None from source, where it changes with every edit."""
    archive = _world_archive()
    if archive is None:
        return None
    try:
        with zipfile.ZipFile(archive) as zf:
            return zf.read(PATCHER_BUILD).decode().strip()
    except (OSError, KeyError, zipfile.BadZipFile):
        return None


def builtin_patcher_dir(log: Callable[[str], None]) -> Path:
    """The patcher the world carries, unpacked into Archipelago's cache folder once per build; from source, the
    repository's patcher folder."""
    archive = _world_archive()
    if archive is None:
        if not (SOURCE_PATCHER / "las_ap_patch.py").is_file():
            raise InstallError("This copy of the world has no patcher: set the patcher command under Advanced "
                               "settings (see the setup guide).")
        return SOURCE_PATCHER
    import Utils
    with zipfile.ZipFile(archive) as zf:
        try:
            build = zf.read(PATCHER_BUILD).decode().strip()
        except KeyError:
            raise InstallError("This copy of the world has no patcher: set the patcher command under Advanced "
                               "settings (see the setup guide).") from None
        cache = Path(Utils.cache_path("las", "patcher"))
        target = cache / build
        if not (target / ".complete").is_file():
            log("Unpacking the patcher. This happens once for each version of the world.")
            if cache.is_dir():
                for old in cache.iterdir():
                    shutil.rmtree(old, ignore_errors=True)
            cache.mkdir(parents=True, exist_ok=True)
            tmp = Path(tempfile.mkdtemp(dir=cache))
            try:
                for name in zf.namelist():
                    if name.startswith(PATCHER_PREFIX) and not name.endswith("/"):
                        dest = tmp / name[len(PATCHER_PREFIX):]
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        dest.write_bytes(zf.read(name))
                tmp.rename(target)
            except BaseException:
                shutil.rmtree(tmp, ignore_errors=True)
                raise
            (target / ".complete").touch()
    return target


def _forget_patcher_modules() -> None:
    for name in list(sys.modules):
        if name.split(".")[0] in PATCHER_MODULES:
            del sys.modules[name]


def run_builtin_patcher(patch_file: Path, romfs: Path, out: Path, log: Callable[[str], None],
                        extra: List[str]) -> None:
    """Runs the patcher in this process. Its modules are imported fresh for each run, as it keeps state in module
    globals, and are dropped afterwards."""
    root = builtin_patcher_dir(log)
    with _PATCHER_LOCK:
        saved_path = list(sys.path)
        _forget_patcher_modules()
        sys.path[:0] = [str(root / "lib"), str(root)]
        saved_streams = sys.stdout, sys.stderr
        sys.stdout = _ThreadOutput(saved_streams[0], log)
        sys.stderr = _ThreadOutput(saved_streams[1], log)
        try:
            patcher = importlib.import_module("las_ap_patch")
            code = patcher.main([str(patch_file), "--romfs", str(romfs), "--out", str(out), *extra])
        except Exception as e:
            log(traceback.format_exc())
            raise InstallError(f"The patcher failed: {e}") from e
        finally:
            sys.stdout.write("\n")
            sys.stderr.write("\n")
            sys.stdout, sys.stderr = saved_streams
            sys.path[:] = saved_path
            _forget_patcher_modules()
    if code != 0:
        raise InstallError(f"The patcher failed (exit code {code}).")


def run_patcher(command: str, patch_file: Path, romfs: Path, out: Path, log: Callable[[str], None],
                settings: Optional[Dict[str, Any]] = None) -> None:
    """Runs the patcher command, or the patcher the world carries when it is empty, with these player settings."""
    extra = ["--settings", json.dumps(settings)] if settings else []
    if not command.strip():
        run_builtin_patcher(patch_file, romfs, out, log, extra)
        return
    args = split_command(command) + [str(patch_file), "--romfs", str(romfs), "--out", str(out), *extra]
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    try:
        proc = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors="replace", creationflags=flags)
    except OSError as e:
        raise InstallError(f"Could not start the patcher ({args[0]}): {e}") from e
    assert proc.stdout is not None
    for line in proc.stdout:
        if line.strip():
            log(line.rstrip())
    if proc.wait() != 0:
        raise InstallError(f"The patcher failed (exit code {proc.returncode}).")


def install(out: Path, sd_card: Path, mods: Path, build: Optional[str] = None,
            settings: Optional[Dict[str, Any]] = None) -> None:
    """Replaces the mod's romfs and exefs folders, then the config, which carries the seed hash. `build` names the
    built-in patcher's build that wrote the mod, `settings` the player settings it was patched with."""
    built = out / "atmosphere" / "contents" / TITLE_ID
    for part in MOD_PARTS:
        if not (built / part).is_dir():
            raise InstallError(f"The patcher wrote no {part} folder.")
    mods.mkdir(parents=True, exist_ok=True)
    for part in MOD_PARTS:
        if (mods / part).exists():
            shutil.rmtree(mods / part)
        shutil.copytree(built / part, mods / part)
    # the config folder (config.ini, the location names), config.ini last as it names the installed seed
    config_dir = (sd_card / CONFIG_PATH).parent
    config_dir.mkdir(parents=True, exist_ok=True)
    if build is None:
        (sd_card / BUILD_PATH).unlink(missing_ok=True)
    else:
        (sd_card / BUILD_PATH).write_text(build + "\n", encoding="utf-8")
    (sd_card / SETTINGS_PATH).write_text(json.dumps({**PLAYER_DEFAULTS, **(settings or {})}) + "\n",
                                         encoding="utf-8")
    for name in OPTIONAL_CONFIG:
        if not ((out / CONFIG_PATH).parent / name).exists():
            (config_dir / name).unlink(missing_ok=True)
    for file in sorted((out / CONFIG_PATH).parent.iterdir(), key=lambda f: f.name == CONFIG_PATH.name):
        if file.is_file():
            shutil.copyfile(file, config_dir / file.name)


def patch_and_install(command: str, patch_file: Path, seed_hash: str, romfs: Path, sd_card: Path, mods: Path,
                      log: Callable[[str], None], force: bool = False,
                      settings: Optional[Dict[str, Any]] = None) -> bool:
    """Installs the mod for `patch_file` with these player settings unless it is installed already. Returns whether
    it patched."""
    if not sd_card.is_dir():
        raise InstallError(f"The SD card folder {sd_card} does not exist.")
    if not force and mod_state(sd_card, mods, seed_hash, command, settings) == CURRENT:
        log(f"The mod for seed {seed_hash} is already installed in {mods}.")
        return False
    with tempfile.TemporaryDirectory(prefix="las-ap-") as tmp:
        log(f"Patching {patch_file.name}.")
        run_patcher(command, patch_file, romfs, Path(tmp), log, settings)
        install(Path(tmp), sd_card, mods, None if command.strip() else builtin_patcher_build(), settings)
    log(f"Installed the mod for seed {seed_hash}: {mods} and {sd_card / CONFIG_PATH}.")
    return True
