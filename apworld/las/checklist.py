"""What the Patch tab shows next to each setting, and whether everything is ready to install."""
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Optional

from . import emulators
from .install import bundled_patcher_resource, installed_seed

OK, TODO, ERROR, INFO = "ok", "todo", "error", "info"


@dataclass
class Check:
    level: str
    text: str


@dataclass
class Checklist:
    checks: Dict[str, Check] = field(default_factory=dict)
    target: Optional[emulators.Target] = None
    seed_hash: str = ""
    player_name: str = ""
    server: str = ""

    @property
    def ready(self) -> bool:
        """Every setting needed to install is valid."""
        return all(self.checks[k].level == OK for k in ("patch_file", "emulator", "romfs_folder", "patcher_command"))

    def problems(self) -> str:
        """The first thing to fix, as one sentence."""
        for key in ("patch_file", "emulator", "romfs_folder", "patcher_command"):
            if self.checks[key].level != OK:
                return self.checks[key].text
        return ""


def romfs_help(exe: str) -> str:
    name = emulators.display_name(exe) if exe else "your emulator"
    if exe and emulators.kind_of(exe) == "yuzu":
        return (f"Needed once: in {name}, right-click Link's Awakening and choose Dump RomFS. "
                f"The client finds the dump by itself.")
    if exe and emulators.kind_of(exe) == "ryujinx":
        return ("Needed once: in Ryujinx, right-click Link's Awakening, choose Extract Data, then RomFS, and pick a "
                "folder. Then choose that folder here.")
    return f"Needed once: dump the game's RomFS with {name} (\"Extract RomFS\" or \"Dump RomFS\"), then choose it here."


def check(patch_file: str, options: Dict[str, str]) -> Checklist:
    from .container import LASContainer
    result = Checklist()
    checks = result.checks
    exe = options.get("emulator", "")

    # patch file
    if not patch_file:
        checks["patch_file"] = Check(TODO, "Choose your .aplas patch file: it is on the website's room page, or in "
                                           "the zip the generator made.")
    elif not Path(patch_file).is_file():
        checks["patch_file"] = Check(ERROR, "This patch file does not exist.")
    else:
        container = LASContainer(patch_file)
        try:
            container.read()
            result.seed_hash = container.patch["seed_hash"]
            result.player_name = container.player_name
            result.server = container.server or ""
            server = f", server {result.server}" if result.server else ""
            checks["patch_file"] = Check(OK, f"Slot {result.player_name}, seed {result.seed_hash}{server}")
        except Exception:
            checks["patch_file"] = Check(ERROR, "This is not a Link's Awakening (Switch) patch file, or it was made "
                                                "by another version of the world.")

    # emulator and where the mod goes
    sd_card = options.get("sd_card_folder", "")
    result.target = emulators.target(exe, sd_card, options.get("mod_folder", ""))
    name = emulators.display_name(exe) if exe else ""
    if not exe and not sd_card:
        checks["emulator"] = Check(TODO, "Choose your emulator's program: Ryujinx.exe, or Eden, Citron or another "
                                         "yuzu-based emulator.")
    elif exe and not Path(exe).is_file():
        checks["emulator"] = Check(ERROR, "This program does not exist.")
    elif result.target is None:
        checks["emulator"] = Check(ERROR, f"The client does not know where {name} keeps its SD card. Set the SD card "
                                          f"folder under Advanced settings.")
    elif not result.target.sd_card.is_dir():
        where = f"Start {name} once so it creates its SD card folder, then try again" if exe and not sd_card \
            else "Check the SD card folder under Advanced settings"
        checks["emulator"] = Check(ERROR, f"{result.target.sd_card} does not exist. {where}.")
    else:
        prefix = f"{name}: " if exe else ""
        checks["emulator"] = Check(OK, f"{prefix}the mod goes to {result.target.mods}")

    # other copies of a Link's Awakening mod, which the emulator would load together with this one
    if result.target is not None:
        others = emulators.other_mods(result.target)
        if others:
            checks["other_mods"] = Check(ERROR, "Another Link's Awakening mod is installed and would mix with this "
                                                "one. Delete: " + ", ".join(str(p) for p in others))

    # game file
    game = options.get("game_file", "")
    if not game:
        checks["game_file"] = Check(INFO, "Optional: the game's .nsp or .xci file, so Install and play starts it.")
    elif not Path(game).is_file():
        checks["game_file"] = Check(ERROR, "This game file does not exist.")
    else:
        checks["game_file"] = Check(OK, "Install and play starts this game.")

    # RomFS dump
    romfs = options.get("romfs_folder", "")
    if not romfs:
        checks["romfs_folder"] = Check(TODO, romfs_help(exe))
    elif emulators.romfs_root(romfs) is None:
        checks["romfs_folder"] = Check(ERROR, "This folder is not a RomFS dump of Link's Awakening (it has no "
                                              "region_common folder). " + romfs_help(exe))
    else:
        checks["romfs_folder"] = Check(OK, "RomFS dump found.")

    # patcher
    if options.get("patcher_command", "").strip():
        checks["patcher_command"] = Check(OK, "")
    elif bundled_patcher_resource() is not None:
        checks["patcher_command"] = Check(OK, "Built-in patcher.")
    else:
        checks["patcher_command"] = Check(ERROR, "The patcher is not set up: set the patcher command under Advanced "
                                                 "settings (see the setup guide).")

    # installed mod
    if result.target is not None and result.target.sd_card.is_dir():
        installed = installed_seed(result.target.sd_card, result.target.mods)
        if installed and installed == result.seed_hash:
            checks["installed"] = Check(OK, "The mod for this seed is installed.")
        elif installed:
            checks["installed"] = Check(INFO, f"The mod for another seed ({installed}) is installed; Install and play "
                                              f"replaces it.")
        else:
            checks["installed"] = Check(INFO, "No mod installed yet.")
    else:
        checks["installed"] = Check(INFO, "")
    return result
