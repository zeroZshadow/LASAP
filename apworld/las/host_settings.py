"""host.yaml section `las_options`: where the client finds the patcher, the RomFS dump and the game's SD card."""
import os
import sys

import settings


def _ryujinx_sd_card() -> str:
    if sys.platform == "win32":
        base = os.environ.get("APPDATA", "")
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_CONFIG_HOME", os.path.expanduser("~/.config"))
    return os.path.join(base, "Ryujinx", "sdcard").replace("\\", "/")


class LASSettings(settings.Group):
    class RomfsFolder(settings.UserFolderPath):
        """
        RomFS dump of Link's Awakening 1.0.0, without an update.
        Ryujinx: right-click the game, Extract Data, RomFS. Other emulators have a similar "Dump RomFS" entry.
        """
        description = "Link's Awakening RomFS dump"

    class SdCardFolder(settings.UserFolderPath):
        """
        The SD card the game sees. The client writes config/lasr-exl/config.ini here, and the game keeps its
        collected checks in lasap/.
          Ryujinx: the sdcard folder in the Ryujinx folder (File, Open Ryujinx Folder)
          Yuzu-based emulators: the sdmc folder in the emulator's data folder
          Switch: the SD card root
        """
        description = "SD card folder of the emulator or Switch"

    class ModFolder(str):
        """
        Folder the mod's romfs and exefs folders go into. The client replaces these two folders on each new seed.
        Empty: <sd_card_folder>/atmosphere/contents/01006BB00C6F0000 (Ryujinx and Atmosphere).
          Yuzu-based emulators: "<data folder>/load/01006BB00C6F0000/Archipelago"
        """

    class PatcherCommand(str):
        """
        Command that runs the LAS patcher (patcher/las_ap_patch.py in the LASAP repository, in its Python 3.12 venv).
        The client appends: <patch file> --romfs <romfs_folder> --out <temporary folder>
        Example: "D:/LASAP/.venv-lasr/Scripts/python.exe D:/LASAP/patcher/las_ap_patch.py"
        """

    class GameStart(str):
        """
        Command that starts the game after a patch file is opened. Empty: the client starts nothing.
        Example: "C:/Ryujinx/Ryujinx.exe D:/Games/LinksAwakening.nsp"
        """

    class GameHost(str):
        """Address of the console or emulator running the game. 127.0.0.1 for an emulator on this computer."""

    romfs_folder: RomfsFolder = RomfsFolder("las_romfs")
    sd_card_folder: SdCardFolder = SdCardFolder(_ryujinx_sd_card())
    mod_folder: ModFolder = ModFolder("")
    patcher_command: PatcherCommand = PatcherCommand("")
    game_start: GameStart = GameStart("")
    game_host: GameHost = GameHost("127.0.0.1")
