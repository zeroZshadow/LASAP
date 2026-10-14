"""The client window's Patch tab: choose the patch file and the emulator, then Play. Every setting shows
what it found or what to do next."""
import asyncio
from typing import TYPE_CHECKING, Callable, Dict, List, Optional, Tuple

# kvui sets kivy up and has to be imported before the kivy modules
from kvui import ConnectBarTextInput, MDNavigationItemBase  # isort: skip
import Utils
from CommonClient import logger
from kivy.clock import Clock
from kivy.metrics import dp
from kivy.uix.gridlayout import GridLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.widget import Widget
from kivy.utils import escape_markup
from kivymd.uix.boxlayout import MDBoxLayout
from kivymd.uix.button import MDButton, MDButtonText
from kivymd.uix.label import MDLabel
from kivymd.uix.selectioncontrol import MDCheckbox

from . import checklist, file_types
from .host_settings import PLAYER_SETTINGS, PlayerSetting

if TYPE_CHECKING:
    from .client import LASContext

ROW_HEIGHT = dp(40)
FIELD_HEIGHT = dp(30)
LABEL_WIDTH = dp(140)
COLORS = {checklist.OK: "66bb6a", checklist.TODO: "ffca28", checklist.ERROR: "ef5350", checklist.INFO: "9e9e9e"}

MAIN: List[Tuple[str, str]] = [
    ("patch_file", "Patch file"),
    ("emulator", "Emulator"),
    ("game_file", "Game"),
    ("romfs_folder", "RomFS dump"),
]
ADVANCED: List[Tuple[str, str]] = [
    ("sd_card_folder", "SD card folder"),
    ("mod_folder", "Mod folder"),
    ("patcher_command", "Patcher command"),
    ("game_host", "Game address"),
    ("ghost_relay", "Ghost relay"),
]
ADVANCED_HELP = ("SD card folder: leave it empty to use the emulator's own. Set it for an unknown emulator, a Switch's "
                 "SD card, or a Ryujinx data folder used with --root-data-dir (its sdcard folder). Mod folder: leave "
                 "it empty for the emulator's default. Game address: 127.0.0.1 when the game runs on this computer, "
                 "else the Switch's IP address. Ghost relay: leave it empty for the community relay.")

# key -> how Browse picks it: a file filter, or None for a folder. Keys without an entry have no Browse button.
BROWSE: Dict[str, Optional[List[Tuple[str, List[str]]]]] = {
    "patch_file": [("Link's Awakening (Switch) patch", [".aplas"])],
    "emulator": [("Programs", [".exe"])] if Utils.is_windows else [("Programs", ["*"])],
    "game_file": [("Switch games", [".nsp", ".xci"])],
    "romfs_folder": None,
    "sd_card_folder": None,
    "mod_folder": None,
}
TITLES = {
    "patch_file": "your .aplas patch file",
    "emulator": "your emulator's program (Ryujinx.exe, eden.exe, ...)",
    "game_file": "Link's Awakening's .nsp or .xci file",
    "romfs_folder": "the RomFS dump of Link's Awakening",
    "sd_card_folder": "the SD card folder",
    "mod_folder": "the mod folder",
}


def button(text: str, action: Callable[[], None], width: float, style: str = "filled") -> MDButton:
    b = MDButton(MDButtonText(text=text), style=style, size=(width, FIELD_HEIGHT), size_hint_x=None,
                 size_hint_y=None, radius=5, pos_hint={"center_y": 0.5})
    b.bind(on_release=lambda *_: action())
    return b


def text(value: str = "", small: bool = False) -> MDLabel:
    label = MDLabel(text=value, markup=True, adaptive_height=True)
    if small:
        label.font_style, label.role = "Body", "small"
    return label


def status(check: Optional[checklist.Check]) -> str:
    if check is None or not check.text:
        return ""
    return f"[color={COLORS[check.level]}]{escape_markup(check.text)}[/color]"


class PatchPanel(ScrollView):
    def __init__(self, ctx: "LASContext", **kwargs):
        super().__init__(do_scroll_x=False, **kwargs)
        self.ctx = ctx
        self.fields: Dict[str, ConnectBarTextInput] = {}
        self.statuses: Dict[str, MDLabel] = {}
        self.content = MDBoxLayout(orientation="vertical", padding=dp(12), spacing=dp(2), adaptive_height=True)
        self.add_widget(self.content)

        self.content.add_widget(text("[b]Set up and play[/b]"))
        self.content.add_widget(text("Choose your patch file and your emulator. The client finds the rest, installs "
                                     "the mod and starts the game.", small=True))
        for key, label in MAIN:
            self.add_setting(self.content, key, label)

        actions = MDBoxLayout(orientation="horizontal", size_hint_y=None, height=ROW_HEIGHT + dp(8), spacing=dp(8),
                              padding=(0, dp(8), 0, 0))
        self.play_button = button("Play", self.play, dp(110))
        self.reinstall_button = button("Reinstall mod", self.reinstall, dp(150), "outlined")
        actions.add_widget(self.play_button)
        actions.add_widget(self.reinstall_button)
        actions.add_widget(Widget())
        self.content.add_widget(actions)
        self.content.add_widget(text("Play installs the mod when it is missing or out of date, then starts the game. "
                                     "Reinstall mod installs it again without starting the game.", small=True))
        self.progress = text()
        self.progress_lines: List[str] = []
        self.content.add_widget(self.progress)
        self.game_line = text(small=True)
        self.content.add_widget(self.game_line)
        self.installed = text(small=True)
        self.content.add_widget(self.installed)

        self.content.add_widget(text("[b]Look and sound[/b]"))
        self.content.add_widget(text("For every seed; how the game plays is in your options file. After a change, "
                                     "Play reinstalls the mod.", small=True))
        grid = GridLayout(cols=2, size_hint_y=None, row_force_default=True, row_default_height=ROW_HEIGHT)
        grid.bind(minimum_height=grid.setter("height"))
        self.player_checks: Dict[str, MDCheckbox] = {}
        self.player_choices: Dict[str, MDButton] = {}
        for setting in PLAYER_SETTINGS:
            grid.add_widget(self.player_setting(setting))
        self.content.add_widget(grid)

        self.content.add_widget(text("[b]Ghosts[/b]"))
        self.content.add_widget(text("The other players of this seed show as ghosts where you are. Players who enter "
                                     "the same password (or all none) see each other.", small=True))
        row = MDBoxLayout(orientation="horizontal", size_hint_y=None, height=ROW_HEIGHT, spacing=dp(6))
        self.ghosts_check = MDCheckbox(size_hint=(None, None), size=(dp(36), dp(36)), pos_hint={"center_y": 0.5})
        self.ghosts_check.bind(active=lambda c, active: self.ctx.set_ghosts(bool(active)))
        row.add_widget(self.ghosts_check)
        row.add_widget(MDLabel(text="Show ghosts"))
        self.content.add_widget(row)
        self.add_setting(self.content, "ghost_password", "Ghost password")
        self.ghost_line = text(small=True)
        self.content.add_widget(self.ghost_line)

        self.advanced = MDBoxLayout(orientation="vertical", spacing=dp(2), adaptive_height=True)
        self.advanced.add_widget(text(ADVANCED_HELP, small=True))
        for key, label in ADVANCED:
            self.add_setting(self.advanced, key, label)
        self.advanced_shown = False
        more = MDBoxLayout(orientation="horizontal", size_hint_y=None, height=ROW_HEIGHT + dp(8), spacing=dp(8),
                           padding=(0, dp(8), 0, 0))
        self.advanced_button = button("Show advanced settings", self.toggle_advanced, dp(230), "text")
        more.add_widget(self.advanced_button)
        if Utils.is_windows:
            more.add_widget(button("Open .aplas files with a double-click", self.register_file_type, dp(330), "text"))
        more.add_widget(Widget())
        self.content.add_widget(more)
        self.file_type = text(small=True)
        self.content.add_widget(self.file_type)

        self.refresh()
        Clock.schedule_interval(lambda dt: self.refresh(), 5)

    def add_setting(self, parent: MDBoxLayout, key: str, label: str) -> None:
        row = MDBoxLayout(orientation="horizontal", size_hint_y=None, height=ROW_HEIGHT, spacing=dp(6))
        row.add_widget(MDLabel(text=label, size_hint_x=None, width=LABEL_WIDTH))
        field = ConnectBarTextInput(text="")  # a one-line field, 30dp high by its kv rule
        field.bind(on_text_validate=lambda f, k=key: self.commit(k, f.text))
        field.bind(focus=lambda f, focused, k=key: None if focused else self.commit(k, f.text))
        row.add_widget(field)
        if key in BROWSE:
            row.add_widget(button("Browse", lambda k=key: self.browse(k), dp(90)))
        self.fields[key] = field
        parent.add_widget(row)
        line = MDBoxLayout(orientation="horizontal", adaptive_height=True, padding=(LABEL_WIDTH + dp(6), 0, 0, dp(4)))
        self.statuses[key] = text(small=True)
        line.add_widget(self.statuses[key])
        parent.add_widget(line)

    def player_setting(self, setting: PlayerSetting) -> MDBoxLayout:
        """A check box for an on/off setting; for a choice, a button that steps through the values."""
        row = MDBoxLayout(orientation="horizontal", spacing=dp(6))
        if setting.choices is None:
            check = MDCheckbox(size_hint=(None, None), size=(dp(36), dp(36)), pos_hint={"center_y": 0.5})
            check.bind(active=lambda c, active, k=setting.key: self.ctx.set_player_option(k, bool(active)))
            self.player_checks[setting.key] = check
            row.add_widget(check)
            row.add_widget(MDLabel(text=setting.label))
        else:
            row.add_widget(MDLabel(text=setting.label, size_hint_x=None, width=dp(80)))
            choice = button("", lambda s=setting: self.next_choice(s), dp(120), "outlined")
            self.player_choices[setting.key] = choice
            row.add_widget(choice)
            row.add_widget(Widget())
        return row

    def next_choice(self, setting: PlayerSetting) -> None:
        choices = setting.choices or ()
        current = self.ctx.player_option(setting.key)
        self.ctx.set_player_option(setting.key, choices[(choices.index(current) + 1) % len(choices)])

    def value(self, key: str) -> str:
        return (self.ctx.patch_file or "") if key == "patch_file" else self.ctx.option(key)

    def commit(self, key: str, value: str) -> None:
        value = value.strip().strip('"')
        if value == self.value(key):
            return
        if key == "patch_file":
            self.ctx.set_patch_file(value)
            self.refresh()
        else:
            self.ctx.set_option(key, value)

    def browse(self, key: str) -> None:
        filetypes = BROWSE[key]
        current = self.value(key)
        if filetypes is None:
            chosen = Utils.open_directory(f"Select {TITLES[key]}", current)
        else:
            chosen = Utils.open_filename(f"Select {TITLES[key]}", filetypes, current)
        if chosen:
            self.commit(key, chosen)

    def commit_all(self) -> None:
        for key, field in self.fields.items():
            self.commit(key, field.text)

    def play(self) -> None:
        self.commit_all()
        asyncio.create_task(self.ctx.install_patch(start=True))

    def reinstall(self) -> None:
        self.commit_all()
        asyncio.create_task(self.ctx.install_patch(force=True))

    def toggle_advanced(self) -> None:
        self.advanced_shown = not self.advanced_shown
        if self.advanced_shown:
            index = self.content.children.index(self.advanced_button.parent)
            self.content.add_widget(self.advanced, index=index)
        else:
            self.content.remove_widget(self.advanced)
        self.advanced_button.children[0].text = ("Hide" if self.advanced_shown else "Show") + " advanced settings"

    def register_file_type(self) -> None:
        error = file_types.register()
        if error:
            logger.error(error)
        else:
            logger.info("Double-clicking a .aplas file now opens this client and installs its mod.")
        self.refresh()

    def clear_progress(self) -> None:
        self.progress_lines = []
        self.progress.text = ""

    def show_progress(self, level: str, message: str, replace: bool = False) -> None:
        color = {"warning": COLORS[checklist.TODO], "error": COLORS[checklist.ERROR],
                 "info": COLORS[checklist.OK] if message.startswith("Done") else None}.get(level)
        message = escape_markup(message)
        if replace and self.progress_lines:
            self.progress_lines.pop()
        self.progress_lines.append(f"[color={color}]{message}[/color]" if color else message)
        self.progress.text = "\n".join(self.progress_lines)

    def refresh(self) -> None:
        self.play_button.disabled = self.reinstall_button.disabled = self.ctx.patching
        for key, field in self.fields.items():
            if not field.focus:
                field.text = self.value(key)
        for key, check in self.player_checks.items():
            check.active = self.ctx.player_option(key)
        self.ghosts_check.active = self.ctx.ghosts_on()
        for key, choice in self.player_choices.items():
            choice.children[0].text = self.ctx.player_option(key)
        if not self.ctx.option("romfs_folder"):
            self.ctx.detect_from_emulator(game=False)  # the dump the Patch tab asks for, made while the client runs
        checks = self.ctx.check_setup()
        for key, label in self.statuses.items():
            label.text = status(checks.checks.get(key))
        self.game_line.text = "\n".join(filter(None, (status(self.ctx.server_status), status(self.ctx.game_status))))
        self.ghost_line.text = status(self.ctx.ghost_status)
        self.installed.text = "\n".join(
            filter(None, (status(checks.checks.get("installed")), status(checks.checks.get("other_mods")))))
        if Utils.is_windows:
            if file_types.is_registered() and not file_types.other_program():
                self.file_type.text = status(checklist.Check(checklist.OK, "Double-clicking a .aplas file opens it "
                                                                           "here."))
            elif file_types.other_program():
                self.file_type.text = status(checklist.Check(
                    checklist.INFO, "Windows opens .aplas files with another program. Right-click a .aplas file, "
                                    "choose Open with, then Archipelago."))
            else:
                self.file_type.text = ""


def make_manager(base):
    class LASManager(base):
        base_title = "Archipelago Link's Awakening (Switch) Client"
        patch_panel: PatchPanel
        patch_tab: MDNavigationItemBase

        def build(self):
            container = super().build()
            self.patch_panel = PatchPanel(self.ctx)
            self.patch_tab = self.add_client_tab("Patch", self.patch_panel)
            return container

        def show_patch_tab(self) -> None:
            if not hasattr(self, "patch_tab") or self.screens.current_tab is self.patch_tab:
                return
            self.screens.current_tab.active = False
            self.screens.switch_screens(self.patch_tab)
            self.patch_tab.active = True

    return LASManager
