"""host.yaml section `las_options`. The client's Patch tab shows and edits these values."""
from typing import Any, Dict, NamedTuple, Optional, Tuple, Union

import settings


class PlayerSetting(NamedTuple):
    """A LASR setting that changes how the game looks or sounds, not how it plays (those are options, the same for
    everyone in a seed). The player picks it in the Patch tab; the client hands it to the patcher (--settings), so it
    applies to every seed and a change only needs a reinstall."""
    key: str  # in las_options
    lasr: str  # LASR's setting, as the patch file and the patcher name it
    label: str
    default: Any
    choices: Optional[Tuple[str, ...]] = None  # None: on or off


# The patcher's PLAYER_SETTINGS has the same LASR settings and defaults.
PLAYER_SETTINGS = (
    PlayerSetting("blur_removal", "Blur Removal", "No blur at the screen's edges", False),
    PlayerSetting("music", "Music", "Music", "Vanilla", ("Vanilla", "Shuffled", "Removed")),
    PlayerSetting("randomize_text", "Randomize Text", "Shuffle the dialogue", False),
    PlayerSetting("randomize_environments", "Randomize Environments", "Shuffle the rooms' lighting", False),
    PlayerSetting("no_low_health_beep", "Disable Low Health Beep", "No low-health beep", False),
    PlayerSetting("no_30fps_drops", "No 30 FPS Drops", "No drops to 30 fps", "On emulators",
                  ("On emulators", "Always", "Never")),
    PlayerSetting("received_items", "Received Items", "Received items", "Item-get", ("Item-get", "Popup")),
)
PLAYER_DEFAULTS = {s.lasr: s.default for s in PLAYER_SETTINGS}


def player_value(setting: PlayerSetting, value: Any) -> Any:
    """A host.yaml value as the patcher takes it; anything else is the default."""
    if setting.choices is None:
        return bool(value)
    return value if value in setting.choices else setting.default


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
        Command that runs the LAS patcher as a separate program. Empty: the client runs the patcher the world
        carries. Otherwise, for example, patcher/las_ap_patch.py in the LASAP repository, in its Python 3.12 venv.
        The client appends: <patch file> --romfs <romfs_folder> --out <temporary folder> --settings <JSON object>
        Example: "D:/LASAP/.venv-lasr/Scripts/python.exe D:/LASAP/patcher/las_ap_patch.py"
        """

    class GameHost(str):
        """Address of the console or emulator running the game. 127.0.0.1 for an emulator on this computer."""

    class Ghosts(settings.Bool):
        """Show the multiworld's other Link's Awakening players as ghosts where you are, and show them you. Needs a
        ghost relay."""

    class GhostNames(settings.Bool):
        """Show each nearby ghost's slot name over its head."""

    class GhostRelay(str):
        """The ghost relay: host:port, password@host:port for a relay with a password, or wss://host for a relay
        behind a tunnel (password@wss://host with a password). Generating writes it into each LAS patch file; the
        client uses it, or when it is empty the patch file's. With neither: the default relay,
        wss://ghosts.zerozshadow.com."""

    emulator: Emulator = Emulator("")
    game_file: GameFile = GameFile("")
    romfs_folder: RomfsFolder = RomfsFolder("")
    sd_card_folder: SdCardFolder = SdCardFolder("")
    mod_folder: ModFolder = ModFolder("")
    patcher_command: PatcherCommand = PatcherCommand("")
    game_host: GameHost = GameHost("127.0.0.1")
    ghosts: Union[Ghosts, bool] = True
    ghost_names: Union[GhostNames, bool] = True
    ghost_relay: GhostRelay = GhostRelay("")

    # Player settings (PLAYER_SETTINGS): the Patch tab's look and sound settings, for every seed.
    class BlurRemoval(settings.Bool):
        """Remove the blur at the edges of the screen."""

    class Music(str):
        """Vanilla, Shuffled or Removed."""

    class RandomizeText(settings.Bool):
        """Shuffle the dialogue."""

    class RandomizeEnvironments(settings.Bool):
        """Shuffle the rooms' lighting and environments."""

    class NoLowHealthBeep(settings.Bool):
        """No warning beep at low health."""

    class No30FpsDrops(str):
        """
        The game waits for the screen's next refresh after each frame, so a frame that is a little late halves the
        frame rate (60 to 30 fps). Without the wait a late frame costs only its own time. On emulators (default),
        Always or Never.
        """

    class ReceivedItems(str):
        """
        How an item from the server shows. Item-get (default): Link holds it up and its text shows, as when he finds
        it. Popup: its model shows above Link's head and the popup at the bottom right names it; play goes on.
        """

    blur_removal: Union[BlurRemoval, bool] = False
    music: Music = Music("Vanilla")
    randomize_text: Union[RandomizeText, bool] = False
    randomize_environments: Union[RandomizeEnvironments, bool] = False
    no_low_health_beep: Union[NoLowHealthBeep, bool] = False
    no_30fps_drops: No30FpsDrops = No30FpsDrops("On emulators")
    received_items: ReceivedItems = ReceivedItems("Item-get")

    def player_settings(self) -> Dict[str, Any]:
        """The player settings under LASR's names, as the patcher takes them."""
        return {s.lasr: player_value(s, getattr(self, s.key)) for s in PLAYER_SETTINGS}
