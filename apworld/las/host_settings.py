"""host.yaml section `las_options`. The client's Patch tab shows and edits these values."""
import settings


class LASSettings(settings.Group):
    class Emulator(str):
        """
        Emulator executable, e.g. "C:/Ryujinx/Ryujinx.exe" or "C:/Eden/eden.exe". The client finds the emulator's SD
        card, mod folder, game and (yuzu-based emulators) RomFS dump from it, and starts it with game_file.
        """

    class GameFile(str):
        """The game file the emulator opens, e.g. "D:/Games/Link's Awakening.nsp". Empty: the emulator starts
        without a game."""

    class RomfsFolder(str):
        """
        RomFS dump of Link's Awakening 1.0.0, without an update.
        Ryujinx: right-click the game, Extract Data, RomFS. Other emulators have a similar "Dump RomFS" entry.
        """

    class SdCardFolder(str):
        """
        The SD card the game sees, when it is not the emulator's own: a Switch's SD card root, or a Ryujinx data
        folder used with --root-data-dir (its sdcard folder). Empty: the emulator's own SD card.
        The client writes config/lasr-exl/config.ini here, and the game keeps its collected checks in lasap/.
        """

    class ModFolder(str):
        """
        Folder the mod's romfs and exefs folders go into. The client replaces these two folders on each new seed.
        Empty: <SD card>/atmosphere/contents/01006BB00C6F0000 (Ryujinx, Atmosphere), or
        <data folder>/load/01006BB00C6F0000/Archipelago for yuzu-based emulators.
        """

    class PatcherCommand(str):
        """
        Command that runs the LAS patcher (patcher/las_ap_patch.py in the LASAP repository, in its Python 3.12 venv).
        The client appends: <patch file> --romfs <romfs_folder> --out <temporary folder>
        Example: "D:/LASAP/.venv-lasr/Scripts/python.exe D:/LASAP/patcher/las_ap_patch.py"
        """

    class GameHost(str):
        """Address of the console or emulator running the game. 127.0.0.1 for an emulator on this computer."""

    emulator: Emulator = Emulator("")
    game_file: GameFile = GameFile("")
    romfs_folder: RomfsFolder = RomfsFolder("")
    sd_card_folder: SdCardFolder = SdCardFolder("")
    mod_folder: ModFolder = ModFolder("")
    patcher_command: PatcherCommand = PatcherCommand("")
    game_host: GameHost = GameHost("127.0.0.1")
