"""What the Patch tab shows next to each setting, and whether everything is ready to install."""
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from . import emulators
from .install import (CHANGED, CURRENT, NONE, OTHER_SEED, builtin_patcher_available, installed_seed, mod_state,
                      romfs_problem)

OK, TODO, ERROR, INFO = "ok", "todo", "error", "info"
EMULATOR, SWITCH = "emulator", "switch"
LOCAL_HOSTS = ("", "127.0.0.1", "localhost", "::1")
SWITCH_ROMFS_STEPS = ("On the Switch, dump Link's Awakening's RomFS (version 1.0.0, no update) with a dump tool such as "
                      "nxdumptool.",
                      "Copy the dumped folder to this computer.",
                      "Choose that folder here.")


@dataclass
class Check:
    level: str
    text: str


@dataclass
class Checklist:
    checks: Dict[str, Check] = field(default_factory=dict)
    target: Optional[emulators.Target] = None
    platform: str = ""
    mod_state: str = ""  # install.mod_state's answer, "" without an SD card
    other_mods: List[Path] = field(default_factory=list)  # emulators.other_mods
    sd_card_out: bool = False  # the Switch's SD card is set but not in this computer, as while the game runs
    seed_hash: str = ""
    player_name: str = ""
    server: str = ""

    @property
    def where(self) -> Tuple[str, ...]:
        """The settings of where the game runs."""
        return {EMULATOR: ("emulator",), SWITCH: ("sd_card_folder", "game_host")}.get(self.platform, ("platform",))

    @property
    def required(self) -> Tuple[str, ...]:
        """The settings an install needs, in the order the Patch tab's setup asks for them."""
        return ("patch_file",) + self.where + ("romfs_folder", "patcher_command")

    def steps_done(self) -> Dict[int, bool]:
        """Which of the Patch tab's four setup steps are complete. A Switch's SD card that is out of this computer does
        not undo its step: the player takes it out to play."""
        def done(*keys: str) -> bool:
            return all(self.checks[k].level == OK or k == "sd_card_folder" and self.sd_card_out for k in keys)
        return {1: done("patch_file"), 2: bool(self.platform), 3: bool(self.platform) and done(*self.where),
                4: done("romfs_folder")}

    @property
    def ready(self) -> bool:
        """Every setting needed to install is valid."""
        return all(self.checks[k].level == OK for k in self.required)

    def problems(self) -> str:
        """The first thing to fix, as one sentence."""
        for key in self.required:
            if self.checks[key].level != OK:
                return self.checks[key].text
        return ""


def platform_of(options: Dict[str, str]) -> str:
    """The platform setting; when it is empty, the one the other settings imply (an emulator, or an SD card with the
    address of a game on another device), else ""."""
    platform = options.get("platform", "")
    if platform in (EMULATOR, SWITCH):
        return platform
    if options.get("emulator"):
        return EMULATOR
    if options.get("sd_card_folder") and options.get("game_host", "").strip() not in LOCAL_HOSTS:
        return SWITCH
    return ""


def romfs_steps(exe: str, platform: str = EMULATOR) -> List[str]:
    """How to make the RomFS dump, one sentence per step."""
    if platform == SWITCH:
        return list(SWITCH_ROMFS_STEPS)
    emulator = emulators.emulator(exe)
    return [step.format(name=emulator.name) for step in emulator.romfs_steps]


def check(patch_file: str, options: Dict[str, str], settings: Optional[Dict[str, Any]] = None) -> Checklist:
    """The status of each setting in `options` (las_options); `settings` are the player settings to install with."""
    from .container import LASContainer
    result = Checklist()
    checks = result.checks
    result.platform = platform = platform_of(options)
    exe = options.get("emulator", "") if platform == EMULATOR else ""
    name = emulators.emulator(exe).name
    checks["platform"] = Check(OK, "") if platform else Check(TODO, "Choose where you play: an emulator on this "
                                                                     "computer or a Nintendo Switch.")

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
    host = options.get("game_host", "").strip()
    if platform == SWITCH:
        checks["emulator"] = Check(INFO, "")
        if not sd_card:
            checks["sd_card_folder"] = Check(TODO, "Choose the Switch's SD card: put it in this computer first.")
        elif not Path(sd_card).is_dir():
            result.sd_card_out = True
            checks["sd_card_folder"] = Check(TODO, f"The SD card ({sd_card}) is not in this computer: put it in to "
                                                   f"install the mod.")
        elif not emulators.has_atmosphere(Path(sd_card)):
            checks["sd_card_folder"] = Check(TODO, "This folder has no atmosphere folder: choose the SD card of a "
                                                   "Switch with Atmosphère.")
        else:
            checks["sd_card_folder"] = Check(OK, f"The mod goes to {result.target.mods}")  # type: ignore[union-attr]
        if host in LOCAL_HOSTS:
            checks["game_host"] = Check(TODO, "Enter the Switch's IP address: on the Switch, System Settings, "
                                              "Internet, Connection Status.")
        else:
            checks["game_host"] = Check(OK, f"The client connects to the game at {host}.")
    elif not exe and not sd_card:
        checks["emulator"] = Check(TODO, "Choose your emulator's program: Ryujinx, or Eden, Citron or another "
                                         "yuzu-based emulator.")
    elif exe and Path(emulators.program(exe)).is_dir():
        checks["emulator"] = Check(ERROR, "This is a folder: choose the emulator's program in it (Ryujinx, "
                                          "Eden, ...).")
    elif exe and not Path(emulators.program(exe)).is_file():
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
        result.other_mods = emulators.other_mods(result.target)
        if result.other_mods:
            checks["other_mods"] = Check(ERROR, "Another Link's Awakening mod is installed and would mix with this "
                                                "one. Delete: " + ", ".join(str(p) for p in result.other_mods))

    # game file
    game = options.get("game_file", "")
    if platform == SWITCH:
        checks["game_file"] = Check(INFO, "")
    elif not game:
        checks["game_file"] = Check(INFO, "Optional: the game's .nsp or .xci file, so Play starts it.")
    elif not Path(game).is_file():
        checks["game_file"] = Check(ERROR, "This game file does not exist.")
    else:
        checks["game_file"] = Check(OK, "Play starts this game.")

    # RomFS dump
    romfs = options.get("romfs_folder", "")
    if not romfs:
        checks["romfs_folder"] = Check(TODO, "Needed once: " + " ".join(romfs_steps(exe, platform)))
    elif (root := emulators.romfs_root(romfs)) is None:
        checks["romfs_folder"] = Check(ERROR, "That isn't a RomFS dump of version 1.0.0 (no region_common folder). "
                                              "Dump version 1.0.0.")
    elif problem := romfs_problem(root):
        checks["romfs_folder"] = Check(ERROR, problem)
    else:
        checks["romfs_folder"] = Check(OK, "RomFS dump found.")

    # patcher
    if options.get("patcher_command", "").strip():
        checks["patcher_command"] = Check(OK, "")
    elif builtin_patcher_available():
        checks["patcher_command"] = Check(OK, "Built-in patcher.")
    else:
        checks["patcher_command"] = Check(ERROR, "The patcher is not set up: set the patcher command under Advanced "
                                                 "settings (see the setup guide).")

    # installed mod
    play = "Install to SD card" if platform == SWITCH else "Play"
    if result.target is not None and result.target.sd_card.is_dir():
        result.mod_state = state = mod_state(result.target.sd_card, result.target.mods, result.seed_hash,
                                             options.get("patcher_command", ""), settings)
        if state == CURRENT:
            checks["installed"] = Check(OK, "The mod for this seed is installed.")
        elif state == OTHER_SEED:
            installed = installed_seed(result.target.sd_card, result.target.mods)
            checks["installed"] = Check(INFO, f"The mod of your last seed ({installed}) is installed. {play} "
                                              f"replaces it.")
        elif state == CHANGED:
            checks["installed"] = Check(INFO, f"The look and sound settings changed: {play} reinstalls the mod.")
        elif state == NONE:
            checks["installed"] = Check(INFO, f"No mod installed yet: {play} installs it.")
        else:
            checks["installed"] = Check(INFO, f"The mod for this seed comes from an older version of the world: {play} "
                                              f"reinstalls it.")
    else:
        checks["installed"] = Check(INFO, "")
    return result
