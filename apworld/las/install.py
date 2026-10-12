"""Turns an .aplas into an installed mod: runs the patcher, then copies its output to the game's SD card."""
import configparser
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Callable, List, Optional

from .emulators import TITLE_ID

CONFIG_PATH = Path("config", "lasr-exl", "config.ini")
MOD_PARTS = ("romfs", "exefs")


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


def run_patcher(command: str, patch_file: Path, romfs: Path, out: Path, log: Callable[[str], None]) -> None:
    if not command.strip():
        raise InstallError("The patcher command is empty; set it to the command that runs patcher/las_ap_patch.py.")
    args = split_command(command) + [str(patch_file), "--romfs", str(romfs), "--out", str(out)]
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


def install(out: Path, sd_card: Path, mods: Path) -> None:
    """Replaces the mod's romfs and exefs folders, then the config, which carries the seed hash."""
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
    for file in sorted((out / CONFIG_PATH).parent.iterdir(), key=lambda f: f.name == CONFIG_PATH.name):
        if file.is_file():
            shutil.copyfile(file, config_dir / file.name)


def patch_and_install(command: str, patch_file: Path, seed_hash: str, romfs: Path, sd_card: Path, mods: Path,
                      log: Callable[[str], None], force: bool = False) -> bool:
    """Installs the mod for `patch_file` unless it is installed already. Returns whether it patched."""
    if not sd_card.is_dir():
        raise InstallError(f"The SD card folder {sd_card} does not exist.")
    if not force and installed_seed(sd_card, mods) == seed_hash:
        log(f"The mod for seed {seed_hash} is already installed in {mods}.")
        return False
    with tempfile.TemporaryDirectory(prefix="las-ap-") as tmp:
        log(f"Patching {patch_file.name}.")
        run_patcher(command, patch_file, romfs, Path(tmp), log)
        install(Path(tmp), sd_card, mods)
    log(f"Installed the mod for seed {seed_hash}: {mods} and {sd_card / CONFIG_PATH}.")
    return True
