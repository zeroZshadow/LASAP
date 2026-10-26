"""The client window's Patch tab. Until the setup is complete it walks through four steps: the patch file, where the
game runs (an emulator or a Switch), the emulator or the Switch, and the game files. Then it shows the seed's state
with one Play button, and one-line summaries that open the setup, look and sound, ghost and advanced settings.
Every screen fits the window's default size (800 x 600)."""
import asyncio
from pathlib import Path
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional, Tuple

# kvui sets kivy up and has to be imported before the kivy modules
from kvui import ConnectBarTextInput, MDNavigationItemBase  # isort: skip
import Utils
from CommonClient import logger
from kivy.clock import Clock
from kivy.metrics import dp, sp
from kivy.uix.behaviors import ButtonBehavior
from kivy.uix.gridlayout import GridLayout
from kivy.uix.scrollview import ScrollView
from kivy.uix.widget import Widget
from kivy.utils import escape_markup, get_color_from_hex
from kivymd.app import MDApp
from kivymd.uix.boxlayout import MDBoxLayout
from kivymd.uix.button import MDButton, MDButtonIcon, MDButtonText
from kivymd.uix.label import MDIcon, MDLabel
from kivymd.uix.menu import MDDropdownMenu
from kivymd.uix.progressindicator import MDCircularProgressIndicator
from kivymd.uix.selectioncontrol import MDCheckbox

from . import checklist, emulators, file_types
from .checklist import EMULATOR, ERROR, INFO, OK, SWITCH, TODO
from .host_settings import PLAYER_SETTINGS, PlayerSetting
from .install import OTHER_SEED

if TYPE_CHECKING:
    from .client import LASContext

ICONS = {OK: "check", INFO: "information-outline", TODO: "alert-circle-outline", ERROR: "alert-outline"}
STATUS_DARK = {OK: "7fd483", INFO: "8cc2ff", TODO: "ffca28", ERROR: "ff8a80"}
STATUS_LIGHT = {OK: "2e7d32", INFO: "1565c0", TODO: "8a6100", ERROR: "c62828"}
STEPS = ("Patch file", "Where you play", "Emulator or Switch", "Game files")
PLACE_STEPS = {EMULATOR: "Your emulator", SWITCH: "Your Switch"}  # step 3 once the platform is chosen
# Advanced settings: key, label, what an empty field means, when to set it
ADVANCED = (
    ("sd_card_folder", "SD card folder", "The emulator's own",
     "For a Switch, an unknown emulator or a Ryujinx --root-data-dir folder."),
    ("mod_folder", "Mod folder", "The default", "Only when mods go somewhere else."),
    ("patcher_command", "Patcher command", "Built-in patcher", "Only to run another patcher program."),
    ("game_host", "Game address", "This PC", "127.0.0.1 for an emulator on this PC, else the Switch's IP address."),
)
# key -> how Browse picks it: a file filter, or None for a folder
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
    "sd_card_folder": "the Switch's SD card",
    "mod_folder": "the mod folder",
}


class Palette:
    """The window theme's colors, and status colors that read on its background."""

    def __init__(self) -> None:
        theme = MDApp.get_running_app().theme_cls
        self.text = theme.onSurfaceColor
        self.muted = theme.onSurfaceVariantColor
        self.accent = theme.primaryColor
        self.on_accent = theme.onPrimaryColor
        self.card = theme.surfaceContainerColor
        self.line = theme.outlineVariantColor
        self.outline = theme.outlineColor
        self.error_bg = theme.errorContainerColor
        self.on_error_bg = theme.onErrorContainerColor
        self.clear = (0, 0, 0, 0)
        status = STATUS_DARK if theme.theme_style == "Dark" else STATUS_LIGHT
        self.status = {level: get_color_from_hex(value) for level, value in status.items()}


class Tile(ButtonBehavior, MDBoxLayout):
    """A box that acts as a button: setup steps, choices, SD cards, summary rows."""


class HoverButton(MDButton):
    """MDButton whose hover follows the pointer inside the tab's ScrollView. KivyMD tests a RelativeLayout's hover
    with the window position as if it were the parent's, which the scrolled content's offset moves away from the
    button."""

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, **kwargs)
        self.detect_visible = False  # its walk up the tree mixes the same two coordinate systems

    def on_mouse_update(self, *args: Any) -> None:
        window, pos = args[0], args[1]
        if self.parent is not None and self.get_root_window():
            pos = self.parent.to_widget(*pos)
        super().on_mouse_update(window, pos)


def label(text: str, size: float = 14, color: Any = None, bold: bool = False, one_line: bool = False,
          **kwargs: Any) -> MDLabel:
    widget = MDLabel(text=text, markup=True, theme_font_size="Custom", font_size=sp(size), bold=bold, **kwargs)
    if color is not None:
        widget.theme_text_color, widget.text_color = "Custom", color
    if one_line:
        widget.shorten, widget.shorten_from, widget.max_lines = True, "right", 1
        widget.size_hint_y, widget.height = None, dp(size + 8)
    else:
        widget.adaptive_height = True
    return widget


def icon(name: str, color: Any, size: float = 18, **kwargs: Any) -> MDIcon:
    # MDLabel's color rule (text_color) runs again on theme changes and would replace icon_color
    return MDIcon(icon=name, theme_icon_color="Custom", icon_color=color, theme_text_color="Custom",
                  text_color=color, theme_font_size="Custom", font_size=sp(size),
                  pos_hint=kwargs.pop("pos_hint", {"center_y": 0.5}), **kwargs)


def button(text: str, action: Callable[[], None], style: str = "filled", disabled: bool = False) -> MDButton:
    b = HoverButton(MDButtonText(text=text), style=style, radius=dp(8), disabled=disabled, pos_hint={"center_y": 0.5})
    b.bind(on_release=lambda *_: action())
    return b


def row(*widgets: Widget, spacing: float = 8, height: Optional[float] = None, **kwargs: Any) -> MDBoxLayout:
    box = MDBoxLayout(orientation="horizontal", spacing=dp(spacing), **kwargs)
    if height is None:
        box.adaptive_height = True
    else:
        box.size_hint_y, box.height = None, dp(height)
    for w in widgets:
        box.add_widget(w)
    return box


def column(spacing: float = 8, **kwargs: Any) -> MDBoxLayout:
    return MDBoxLayout(orientation="vertical", spacing=dp(spacing), adaptive_height=True, **kwargs)


def fill() -> Widget:
    return Widget()


def short_path(path: str) -> str:
    return Path(path).name or path


class PatchPanel(ScrollView):
    def __init__(self, ctx: "LASContext", **kwargs):
        super().__init__(do_scroll_x=False, **kwargs)
        self.ctx = ctx
        self.content = column(spacing=12, padding=(dp(16), dp(14)))
        self.add_widget(self.content)
        self.view: Optional[str] = None  # "setup", "ready" or "panel"; chosen on the first refresh
        self.step: Optional[int] = None  # the setup step shown
        self.panel = ""  # the settings screen shown from the ready screen
        self.editing = False  # the setup was opened from the ready screen
        self.register_wanted = True  # the step 1 box: open .aplas files here
        self.fields: Dict[str, ConnectBarTextInput] = {}
        self.shown: Optional[Dict[str, Any]] = None
        self.screen: Optional[Tuple[Any, ...]] = None  # the view, step, panel and editing last built
        self.colors = Palette()
        self.menu: Optional[MDDropdownMenu] = None
        self.refresh()
        Clock.schedule_interval(lambda dt: self.refresh(), 5)

    # state

    def model(self) -> Dict[str, Any]:
        """Everything the tab shows; it is built again when this changes."""
        ctx = self.ctx
        checks = ctx.check_setup()
        c = checks.checks
        platform = checks.platform
        done = checks.steps_done()
        if self.view is None or self.view == "ready" and not all(done.values()):
            self.view, self.step = ("ready", None) if all(done.values()) else ("setup", None)
        if self.view == "setup" and self.step is None:
            self.step = next((n for n in range(1, 5) if not done[n]), 4)
        drives = tuple(emulators.switch_drives()) if (self.view, self.step, platform) == ("setup", 3, SWITCH) else ()
        windows = Utils.is_windows
        return {
            "view": self.view, "step": self.step, "panel": self.panel, "editing": self.editing,
            "checks": c, "platform": platform, "done": done, "ready": checks.ready, "target": checks.target,
            "mod_state": checks.mod_state, "other_mods": tuple(checks.other_mods), "sd_card_out": checks.sd_card_out,
            "slot": checks.player_name, "seed": checks.seed_hash, "server": checks.server,
            "patch_file": ctx.patch_file or "", "options": ctx.options(),
            "player": tuple(ctx.player_option(s.key) for s in PLAYER_SETTINGS),
            "ghosts": ctx.ghosts_on(), "ghost_names": ctx.ghost_names_on(), "ghost_status": ctx.ghost_status,
            "server_status": ctx.server_status, "game_status": ctx.game_status,
            "game_connected": ctx.game_writer is not None, "patching": ctx.patching,
            "steps": tuple(ctx.install_steps), "step_at": ctx.install_step, "report": ctx.last_report,
            "drives": drives, "register_wanted": self.register_wanted,
            "ours": windows and file_types.is_registered(), "other_program": windows and file_types.other_program(),
        }

    def refresh(self) -> None:
        if self.menu is not None or any(field.focus for field in self.fields.values()):
            return  # built again once the menu closes or the field loses focus
        model = self.model()
        if model == self.shown:
            return
        self.shown = model
        self.colors = Palette()
        self.fields = {}
        screen = (model["view"], model["step"], model["panel"], model["editing"])
        if screen != self.screen:
            # New widgets size their text in the frame after they are laid out: hide that frame on another screen
            self.screen = screen
            self.content.opacity = 0
            Clock.schedule_once(lambda dt: Clock.schedule_once(lambda dt: setattr(self.content, "opacity", 1)))
            self.scroll_y = 1
        self.content.clear_widgets()
        {"setup": self.build_setup, "ready": self.build_ready, "panel": self.build_panel}[model["view"]](model)

    def show(self, view: str, step: Optional[int] = None, panel: str = "", editing: bool = False) -> None:
        self.release_fields()
        self.view, self.step, self.panel, self.editing = view, step, panel, editing
        self.refresh()

    def release_fields(self) -> None:
        """Takes the focus from a field being typed in, which commits it, before a button acts on the settings."""
        for f in list(self.fields.values()):
            f.focus = False

    # actions

    def value(self, key: str) -> str:
        return (self.ctx.patch_file or "") if key == "patch_file" else self.ctx.option(key)

    def commit(self, key: str, value: str) -> None:
        value = value.strip().strip('"')
        if value != self.value(key):
            if key == "patch_file":
                self.ctx.set_patch_file(value)
            else:
                self.ctx.set_option(key, value)
        self.refresh()

    def browse(self, key: str) -> None:
        filetypes = BROWSE[key]
        current = self.value(key)
        if filetypes is None:
            chosen = Utils.open_directory(f"Select {TITLES[key]}", current)
        else:
            chosen = Utils.open_filename(f"Select {TITLES[key]}", filetypes, current)
        if chosen:
            self.picked(key, chosen)

    def picked(self, key: str, path: str) -> None:
        """A file or folder chosen with Browse; a patch file chosen in the setup moves on to the next step."""
        self.commit(key, path)
        if key == "patch_file" and self.view == "setup" and self.shown and self.shown["done"][1]:
            if self.register_wanted and Utils.is_windows and not file_types.is_registered():
                self.register_file_type()
            if not self.editing:
                self.next_step()

    def field(self, key: str, width: Optional[float] = None) -> ConnectBarTextInput:
        f = ConnectBarTextInput(text=self.value(key))
        if key == "game_host" and self.shown and self.shown["platform"] == SWITCH and f.text in checklist.LOCAL_HOSTS:
            f.text = ""
        if width is not None:
            f.size_hint_x, f.width = None, dp(width)
        f.bind(on_text_validate=lambda w, k=key: self.commit(k, w.text))
        f.bind(focus=lambda w, focused, k=key: None if focused else self.commit(k, w.text))
        self.fields[key] = f
        return f

    def next_step(self) -> None:
        self.release_fields()
        done = self.model()["done"]
        self.step = next((n for n in range(1, 5) if not done[n]), 4)
        self.refresh()

    def choose_platform(self, platform: str) -> None:
        self.ctx.set_option("platform", platform)
        self.show("setup", 3, editing=self.editing)

    def finish(self) -> None:
        self.show("ready")

    def play(self) -> None:
        switch = self.shown is not None and self.shown["platform"] == SWITCH
        asyncio.create_task(self.ctx.install_patch(start=not switch))

    def reinstall(self) -> None:
        asyncio.create_task(self.ctx.install_patch(force=True))

    def check_romfs(self) -> None:
        asyncio.create_task(self.ctx.detect_from_emulator(game=False))

    def register_file_type(self) -> None:
        error = file_types.register()
        if error:
            logger.error(error)
        else:
            logger.info("Double-clicking a .aplas file now opens it in this client.")
        self.shown = None
        self.refresh()

    def toggle_register(self, on: bool) -> None:
        """The step 1 box: ticked, a chosen patch file registers .aplas; unticked removes the registration."""
        self.register_wanted = on
        if on and self.shown and self.shown["done"][1] and not file_types.is_registered():
            self.register_file_type()
        elif not on and file_types.is_registered():
            error = file_types.unregister()
            if error:
                logger.error(error)
            else:
                logger.info("Double-clicking a .aplas file no longer opens it in this client.")
            self.shown = None
            self.refresh()

    def choose(self, setting: PlayerSetting, caller: Widget) -> None:
        def pick(value: str) -> None:
            menu.dispatch("on_dismiss")  # dismiss() would close it without calling closed
            self.ctx.set_player_option(setting.key, value)

        def closed(*_: Any) -> None:
            self.menu = None
            self.refresh()

        items = [{"text": value, "on_release": lambda v=value: pick(v)} for value in setting.choices or ()]
        self.menu = menu = MDDropdownMenu(caller=caller, items=items, position="bottom")
        menu.bind(on_dismiss=closed)
        menu.open()

    # pieces

    def card(self, strong: bool = False, padding: Tuple[float, float] = (18, 16), spacing: float = 12) -> MDBoxLayout:
        k = self.colors
        return column(spacing=spacing, padding=(dp(padding[0]), dp(padding[1])), md_bg_color=k.card,
                      line_color=k.accent if strong else k.line, radius=[dp(10)])

    def title(self, text: str, size: float = 18) -> MDLabel:
        return label(text, size, self.colors.text, bold=True)

    def status_icon(self, level: str, size: float = 18) -> MDIcon:
        return icon(ICONS[level], self.colors.status[level], size)

    def status_line(self, check: checklist.Check, size: float = 14) -> MDBoxLayout:
        k = self.colors
        text = label(escape_markup(check.text), size, k.status[check.level] if check.level == ERROR else k.text)
        return row(self.status_icon(check.level), text, spacing=10)

    def checkbox(self, text: str, active: bool, action: Callable[[bool], None], height: float = 40) -> MDBoxLayout:
        box = MDCheckbox(size_hint=(None, None), size=(dp(32), dp(32)), active=active, pos_hint={"center_y": 0.5})
        box.bind(active=lambda w, on: action(bool(on)))
        caption = Tile(size_hint_y=None, height=dp(height))
        caption.add_widget(label(text, 14, self.colors.text, one_line=True, pos_hint={"center_y": 0.5}))
        caption.bind(on_release=lambda *_: setattr(box, "active", not box.active))
        return row(box, caption, spacing=6, height=height)

    # setup

    def build_setup(self, m: Dict[str, Any]) -> None:
        k = self.colors
        done = m["done"]
        first_open = next((n for n in range(1, 5) if not done[n]), 5)
        steps = GridLayout(cols=4, spacing=dp(6), size_hint_y=None, height=dp(40))
        names = STEPS[:2] + (PLACE_STEPS.get(m["platform"], STEPS[2]),) + STEPS[3:]
        for n, name in enumerate(names, 1):
            current = n == self.step
            locked = n > first_open and not current
            tile = Tile(orientation="horizontal", spacing=dp(8), padding=(dp(10), 0), disabled=locked,
                        md_bg_color=k.card if current else k.clear, line_color=k.accent if current else k.line,
                        radius=[dp(8)])
            color = k.text if not locked else k.muted
            if done[n] and not current:
                tile.add_widget(self.status_icon(OK, 16))
            else:
                badge = MDBoxLayout(size_hint=(None, None), size=(dp(20), dp(20)), radius=[dp(10)], line_color=color,
                                    pos_hint={"center_y": 0.5})
                badge.add_widget(label(str(n), 11, color, halign="center", valign="middle", one_line=True,
                                       pos_hint={"center_y": 0.5}))
                tile.add_widget(badge)
            tile.add_widget(label(name, 13, color, bold=current, one_line=True, pos_hint={"center_y": 0.5}))
            tile.bind(on_release=lambda *_, n=n: self.show("setup", n, editing=self.editing))
            steps.add_widget(tile)
        self.content.add_widget(steps)
        card = self.card(strong=True)
        if self.step == 1:
            self.step_patch(card, m)
        elif self.step == 2:
            self.step_platform(card, m)
        elif self.step == 3 and m["platform"] == SWITCH:
            self.step_switch(card, m)
        elif self.step == 3:
            self.step_emulator(card, m)
        else:
            self.step_files(card, m)
        self.content.add_widget(card)

    def next_button(self, enabled: bool) -> MDButton:
        return button("Done" if self.editing else "Next", self.finish if self.editing else self.next_step,
                      disabled=not enabled)

    def step_patch(self, card: MDBoxLayout, m: Dict[str, Any]) -> None:
        k = self.colors
        check = m["checks"]["patch_file"]
        card.add_widget(self.title("Your patch file"))
        if check.level == OK:
            card.add_widget(row(self.status_icon(OK), label(f"Slot {m['slot']} · seed {m['seed']}", 14, k.text,
                                                            one_line=True), spacing=10, height=22))
            card.add_widget(label(escape_markup(short_path(m["patch_file"])), 13, k.muted, one_line=True))
        else:
            card.add_widget(label("The .aplas file for your slot. It is on the website's room page, or in the zip "
                                  "the generator made.", 14, k.text))
            if check.level == ERROR:
                card.add_widget(self.status_line(check))
        choose = button("Choose another…" if check.level == OK else "Choose patch file…",
                        lambda: self.browse("patch_file"), "outlined" if check.level == OK else "filled")
        card.add_widget(row(choose, fill(), *([button("Done" if self.editing else "Next", self.finish if
                                                      self.editing else self.next_step)]
                                              if check.level == OK else [])))
        if Utils.is_windows:
            card.add_widget(self.checkbox("Open .aplas patch files in this client when I double-click them",
                                          m["ours"] or m["register_wanted"] and not m["done"][1],
                                          self.toggle_register))
        if m["other_program"]:
            card.add_widget(label("Windows opens .aplas files with another program. Right-click a .aplas file, choose "
                                  "Open with, then Archipelago.", 13, k.muted))

    def step_platform(self, card: MDBoxLayout, m: Dict[str, Any]) -> None:
        k = self.colors
        card.add_widget(self.title("Where do you play?"))
        choices = GridLayout(cols=2, spacing=dp(12), size_hint_y=None, height=dp(96))
        for platform, name, glyph, text in (
                (EMULATOR, "Emulator on this PC", "monitor", "Ryujinx, Eden, Citron or another yuzu-based emulator"),
                (SWITCH, "Nintendo Switch", "nintendo-switch", "Modded with Atmosphère. The mod goes on its SD card.")):
            chosen = m["platform"] == platform
            tile = Tile(orientation="horizontal", spacing=dp(14), padding=(dp(16), dp(14)),
                        line_color=k.accent if chosen else k.outline, radius=[dp(10)])
            tile.add_widget(icon(glyph, k.accent, 30, pos_hint={"top": 1}))
            words = column(spacing=4, pos_hint={"top": 1})
            words.add_widget(label(name, 16, k.text, bold=True))
            words.add_widget(label(text, 13, k.muted))
            tile.add_widget(words)
            tile.bind(on_release=lambda *_, p=platform: self.choose_platform(p))
            choices.add_widget(tile)
        card.add_widget(choices)

    def step_emulator(self, card: MDBoxLayout, m: Dict[str, Any]) -> None:
        k = self.colors
        check = m["checks"]["emulator"]
        card.add_widget(self.title("Your emulator"))
        card.add_widget(label("Choose the emulator's program. The client finds its SD card, its mod folder and your "
                              "game from it.", 14, k.text))
        if m["options"]["emulator"]:
            box = row(self.status_icon(check.level), label(escape_markup(check.text), 14,
                                                           k.status[ERROR] if check.level == ERROR else k.text),
                      button("Change", lambda: self.browse("emulator"), "text"), spacing=10,
                      padding=(dp(12), dp(4)), line_color=k.line, radius=[dp(8)])
            card.add_widget(box)
        else:
            card.add_widget(row(button("Choose emulator program…", lambda: self.browse("emulator"))))
        card.add_widget(row(fill(), self.next_button(m["done"][3])))

    def step_switch(self, card: MDBoxLayout, m: Dict[str, Any]) -> None:
        k = self.colors
        c = m["checks"]
        sd = m["options"]["sd_card_folder"]
        card.add_widget(self.title("Your Switch"))
        card.add_widget(label("[b]SD card[/b] · put the Switch's SD card in this PC", 14, k.text))
        drives = list(m["drives"])
        if sd and not any(emulators.same_path(d.root, Path(sd)) for d in drives):
            drives.append(emulators.Drive(Path(sd), "", emulators.has_atmosphere(Path(sd))))
        cards = column(spacing=6)
        for drive in drives:
            chosen = bool(sd) and emulators.same_path(drive.root, Path(sd))
            note = label("has Atmosphère" if drive.atmosphere else "no atmosphere folder", 13,
                         k.status[OK] if drive.atmosphere else k.muted, one_line=True, size_hint_x=None, width=dp(150),
                         pos_hint={"center_y": 0.5})
            tile = Tile(orientation="horizontal", spacing=dp(12), padding=(dp(12), 0), size_hint_y=None,
                        height=dp(40), line_color=k.accent if chosen else k.outline, radius=[dp(8)])
            tile.add_widget(icon("radiobox-marked" if chosen else "radiobox-blank", k.accent))
            tile.add_widget(label(escape_markup(f"{drive.root} {drive.label}".strip()), 14, k.text, bold=chosen,
                                  one_line=True, pos_hint={"center_y": 0.5}))
            tile.add_widget(note)
            tile.bind(on_release=lambda *_, d=drive: self.commit("sd_card_folder", str(d.root)))
            cards.add_widget(tile)
        if not drives:
            cards.add_widget(label("No SD card found. Put it in this PC, or choose its folder.", 14, k.muted))
        card.add_widget(cards)
        card.add_widget(row(button("Another folder…", lambda: self.browse("sd_card_folder"), "text"),
                            button("Look again", self.refresh_drives, "text"), fill()))
        if sd and c["sd_card_folder"].level != OK:
            card.add_widget(self.status_line(c["sd_card_folder"]))
        host = self.field("game_host", width=180)
        card.add_widget(row(label("Switch's IP address", 14, k.text, bold=True, one_line=True, size_hint_x=None,
                                  width=dp(160), pos_hint={"center_y": 0.5}), host, fill(), height=32))
        card.add_widget(label("Such as 192.168.1.42. On the Switch: System Settings, Internet, Connection Status. "
                              "The Switch and this PC must be on the same network.", 13, k.muted))
        card_done = c["sd_card_folder"].level == OK or m["sd_card_out"]
        next_button = self.next_button(m["done"][3])
        # the address counts as typed, before the field commits it when it loses focus
        host.bind(text=lambda w, text: setattr(next_button, "disabled",
                                               not card_done or text.strip() in checklist.LOCAL_HOSTS))
        card.add_widget(row(fill(), next_button))

    def refresh_drives(self) -> None:
        self.shown = None
        self.refresh()

    def file_row(self, level: str, title: str, text: str, color: Any, buttons: List[Widget]) -> MDBoxLayout:
        """A game files line: status, name, what was found or what to do, and its buttons at the right."""
        line = row(self.status_icon(level), label(title, 14, self.colors.text, bold=True, one_line=True,
                                                  size_hint_x=None, width=dp(96), pos_hint={"center_y": 0.5}),
                   label(text, 14, color, pos_hint={"center_y": 0.5}), spacing=10)
        for b in buttons:
            line.add_widget(b)
        return line

    def step_files(self, card: MDBoxLayout, m: Dict[str, Any]) -> None:
        k = self.colors
        c = m["checks"]
        switch = m["platform"] == SWITCH
        exe = m["options"]["emulator"] if not switch else ""
        emulator = emulators.emulator(exe)
        name = emulator.name
        romfs = c["romfs_folder"]
        card.add_widget(self.title("Game files"))
        found = romfs.level == OK
        finds = not switch and emulator.finds_romfs  # the client looks for the dump; the player makes it
        actions = []
        if not found and finds:
            actions.append(button("Check again", self.check_romfs))
        actions.append(button("Change" if found else "Choose folder…" if finds else "Choose RomFS folder…",
                              lambda: self.browse("romfs_folder"), "outlined" if found or finds else "filled"))
        state = escape_markup(m["options"]["romfs_folder"]) if found else "needed once"
        card.add_widget(self.file_row(OK if found else TODO, "RomFS dump", state,
                                      k.muted if found else k.status[TODO], actions))
        if not found:
            steps = column(spacing=2, padding=(dp(28), 0, 0, 0))
            for i, text in enumerate(checklist.romfs_steps(exe, m["platform"]), 1):
                steps.add_widget(label(f"{i}. {text}", 14, k.text))
            card.add_widget(steps)
            if romfs.level == ERROR:
                card.add_widget(label(escape_markup(romfs.text), 14, k.status[ERROR], padding=(dp(28), 0, 0, 0)))
        if not switch:
            game = c["game_file"]
            path = m["options"]["game_file"]
            if game.level == OK:
                text = escape_markup(short_path(path)) + ": Play starts it."
            elif not path:
                text = f"Not in {name}'s game list. Play then opens {name} and you start the game there."
            else:
                text = escape_markup(game.text)
            level = OK if game.level == OK else ERROR if game.level == ERROR else INFO
            card.add_widget(self.file_row(level, "Game", text + "  [color=#7f93b3](optional)[/color]", k.muted,
                                          [button("Change" if path else "Choose game file…",
                                                  lambda: self.browse("game_file"), "outlined")]))
        ready = all(m["done"].values())
        if ready and c["patcher_command"].level != OK:
            card.add_widget(self.status_line(c["patcher_command"]))
        card.add_widget(row(fill(), button("Done" if self.editing else "Finish setup", self.finish,
                                           disabled=not ready)))

    # ready

    def build_ready(self, m: Dict[str, Any]) -> None:
        k = self.colors
        c = m["checks"]
        switch = m["platform"] == SWITCH
        name = emulators.emulator(m["options"]["emulator"] if not switch else "").name
        card = self.card(strong=True, padding=(18, 14), spacing=10)
        where = f" · {m['server']}" if m["server"] else ""
        header = row(label(escape_markup(f"Slot {m['slot']}"), 20, k.text, bold=True, adaptive_width=True,
                           pos_hint={"center_y": 0.5}),
                     label(escape_markup(f"seed {m['seed']}{where}"), 14, k.muted, adaptive_width=True,
                           pos_hint={"center_y": 0.5}), spacing=10, height=32)
        if m["mod_state"] == OTHER_SEED:
            pill = MDBoxLayout(adaptive_width=True, size_hint_y=None, height=dp(20), padding=(dp(8), 0),
                               md_bg_color=k.accent, radius=[dp(10)], pos_hint={"center_y": 0.5})
            pill.add_widget(label("New seed", 12, k.on_accent, bold=True, adaptive_width=True,
                                  pos_hint={"center_y": 0.5}))
            header.add_widget(pill)
        header.add_widget(fill())
        header.add_widget(button("Other patch file…", lambda: self.browse("patch_file"), "text",
                                 disabled=m["patching"]))
        card.add_widget(header)

        others = m["other_mods"]
        if m["patching"] and m["steps"]:
            for i, step in enumerate(m["steps"]):
                if i < m["step_at"]:
                    mark: Widget = self.status_icon(OK)
                elif i == m["step_at"]:
                    mark = MDCircularProgressIndicator(size_hint=(None, None), size=(dp(16), dp(16)), line_width=dp(2),
                                                       pos_hint={"center_y": 0.5})
                else:
                    mark = icon("circle-outline", k.outline)
                card.add_widget(row(mark, label(step, 14, k.text if i <= m["step_at"] else k.muted, one_line=True,
                                                pos_hint={"center_y": 0.5}), spacing=10, height=22))
        elif others:
            red = k.status[ERROR]
            box = column(spacing=8, padding=(dp(14), dp(12)), md_bg_color=red[:3] + [0.12], line_color=red,
                         radius=[dp(10)])
            box.add_widget(row(self.status_icon(ERROR, 20), label(
                f"[b]Another Link's Awakening mod is installed.[/b] It would load together with this one in {name}. "
                f"Remove its folder, then check again.", 14, k.text), spacing=10))
            for path in others:
                box.add_widget(label(escape_markup(str(path)), 13, k.text, one_line=True, padding=(dp(30), 0, 0, 0)))
            box.add_widget(row(button("Open the folder", lambda: Utils.open_file(str(others[0]))),
                               button("Check again", self.refresh_drives, "outlined"), fill(),
                               padding=(dp(30), 0, 0, 0)))
            card.add_widget(box)
        else:
            installed = c["installed"]
            if installed.text:
                card.add_widget(self.status_line(installed))
            if switch and c["sd_card_folder"].level != OK:
                card.add_widget(self.status_line(c["sd_card_folder"]))
            elif switch:
                sd = m["options"]["sd_card_folder"]
                card.add_widget(self.status_line(checklist.Check(
                    OK, f"SD card {sd} · Switch at {m['options']['game_host']}")))
            elif c["game_file"].level == OK:
                card.add_widget(self.status_line(checklist.Check(
                    OK, f"{name} starts {short_path(m['options']['game_file'])}")))
            elif m["options"]["game_file"]:
                card.add_widget(self.status_line(c["game_file"]))
            else:
                card.add_widget(self.status_line(checklist.Check(
                    INFO, f"Play opens {name}: start Link's Awakening from its game list.")))
            card.add_widget(self.status_line(m["server_status"]))
            card.add_widget(self.status_line(m["game_status"]))
        if m["report"] is not None and not (m["patching"] and m["report"].level == INFO):
            card.add_widget(self.status_line(m["report"]))
        if c["patcher_command"].level != OK:
            card.add_widget(self.status_line(c["patcher_command"]))

        if m["patching"]:
            text = "Starting…" if m["steps"] and m["steps"][0].startswith("Start") else "Installing…"
        elif switch:
            text = "Install to SD card"
        else:
            text = "Game running" if m["game_connected"] else "Play"
        blocked = m["patching"] or bool(others) or not m["ready"] or (not switch and m["game_connected"])
        play = HoverButton(MDButtonText(text=text, theme_font_size="Custom", font_size=sp(17)), style="filled",
                        radius=dp(10), disabled=blocked, theme_height="Custom", height=dp(46),
                        pos_hint={"center_y": 0.5})
        play.bind(on_release=lambda *_: self.play())
        actions = row(play, spacing=16, height=46)
        if not switch and not m["patching"] and not others:
            actions.add_widget(button("Reinstall the mod without starting", self.reinstall, "text"))
        actions.add_widget(fill())
        card.add_widget(actions)
        self.content.add_widget(card)

        summary = column(spacing=0, line_color=k.line, radius=[dp(10)])
        for title, text, level, panel in self.summaries(m, name):
            tile = Tile(orientation="horizontal", spacing=dp(12), padding=(dp(16), 0), size_hint_y=None,
                        height=dp(40), disabled=m["patching"])
            tile.add_widget(label(title, 14, k.text, bold=True, one_line=True, size_hint_x=None, width=dp(120),
                                  pos_hint={"center_y": 0.5}))
            tile.add_widget(label(escape_markup(text), 14, k.status[level] if level != INFO else k.muted,
                                  one_line=True, pos_hint={"center_y": 0.5}))
            tile.add_widget(icon("chevron-right", k.accent))
            tile.bind(on_release=lambda *_, p=panel: self.show("panel", panel=p))
            summary.add_widget(tile)
        self.content.add_widget(summary)

    def summaries(self, m: Dict[str, Any], name: str) -> List[Tuple[str, str, str, str]]:
        o = m["options"]
        if m["platform"] == SWITCH:
            setup = f"Switch at {o['game_host']} · SD card {o['sd_card_folder']} · RomFS dump found"
        else:
            setup = f"{name} · RomFS dump found · " + ("game found" if o["game_file"] else "no game file")
        changed = [f"{s.label}: {v}" if s.choices else s.label
                   for s, v in zip(PLAYER_SETTINGS, m["player"]) if v != s.default]
        look = "All as the game ships" if not changed else changed[0] + (
            f" · {len(changed) - 1} more" if len(changed) > 1 else "")
        status = m["ghost_status"]
        ghosts = ("On · " + status.text.removeprefix("Ghosts: ")) if m["ghosts"] else "Off"
        ghost_level = status.level if m["ghosts"] and status.level in (TODO, ERROR) else INFO
        return [("Setup", setup, INFO, "setup"), ("Look and sound", look, INFO, "look"),
                ("Ghosts", ghosts, ghost_level, "ghosts"), ("Advanced", "Folders, patcher, game address", INFO,
                                                            "advanced")]

    # settings screens

    def build_panel(self, m: Dict[str, Any]) -> None:
        k = self.colors
        titles = {"setup": ("Setup", ""), "look": ("Look and sound", "a change reinstalls the mod on the next Play"),
                  "ghosts": ("Ghosts", ""), "advanced": ("Advanced", "most players never need these")}
        title, note = titles[self.panel]
        back = HoverButton(MDButtonIcon(icon="chevron-left"), MDButtonText(text="Back"), style="outlined", radius=dp(8),
                        pos_hint={"center_y": 0.5})
        back.bind(on_release=lambda *_: self.show("ready"))
        header = row(back, label(title, 18, k.text, bold=True, one_line=True, adaptive_width=True,
                                 pos_hint={"center_y": 0.5}),
                     label(note, 13, k.muted, one_line=True, pos_hint={"center_y": 0.5}), spacing=12, height=40)
        if self.panel == "advanced" and Utils.is_windows and not m["ours"]:
            header.add_widget(button("Open .aplas files with a double-click", self.register_file_type, "outlined"))
        self.content.add_widget(header)
        getattr(self, f"panel_{self.panel}")(m)

    def panel_setup(self, m: Dict[str, Any]) -> None:
        k = self.colors
        o = m["options"]
        switch = m["platform"] == SWITCH
        exe = o["emulator"]
        name = emulators.emulator(exe).name
        rows = [("Patch file", short_path(m["patch_file"]), 1),
                ("Where you play", "Nintendo Switch" if switch else "Emulator on this PC", 2),
                ("Your Switch", f"SD card {o['sd_card_folder']} · {o['game_host']}", 3) if switch else
                ("Emulator", exe, 3),
                ("RomFS dump", o["romfs_folder"], 4)]
        if not switch:
            rows.append(("Game", o["game_file"] or f"None: you start it in {name}", 4))
        box = column(spacing=0, line_color=k.line, radius=[dp(10)])
        for title, value, step in rows:
            box.add_widget(row(label(title, 14, k.text, bold=True, one_line=True, size_hint_x=None, width=dp(130),
                                     pos_hint={"center_y": 0.5}),
                               label(escape_markup(value), 14, k.muted, one_line=True, pos_hint={"center_y": 0.5}),
                               button("Change", lambda s=step: self.show("setup", s, editing=True), "text"),
                               spacing=12, height=50, padding=(dp(16), 0, dp(6), 0)))
        self.content.add_widget(box)

    def panel_look(self, m: Dict[str, Any]) -> None:
        k = self.colors
        grid = GridLayout(cols=2, spacing=(dp(24), dp(2)), size_hint_y=None, row_force_default=True,
                          row_default_height=dp(44), padding=(dp(12), dp(6)))
        grid.bind(minimum_height=grid.setter("height"))
        for setting in PLAYER_SETTINGS:
            value = self.ctx.player_option(setting.key)
            if setting.choices is None:
                grid.add_widget(self.checkbox(setting.label, bool(value),
                                              lambda on, s=setting: self.ctx.set_player_option(s.key, on), height=44))
            else:
                choice = HoverButton(MDButtonIcon(icon="menu-down"), MDButtonText(text=value), style="outlined",
                                  radius=dp(6), pos_hint={"center_y": 0.5})
                choice.bind(on_release=lambda b, s=setting: self.choose(s, b))
                grid.add_widget(row(label(setting.label, 14, k.text, one_line=True, size_hint_x=None, width=dp(120),
                                          pos_hint={"center_y": 0.5}), choice, fill(), spacing=8, height=44))
        box = column(spacing=0, line_color=k.line, radius=[dp(10)])
        box.add_widget(grid)
        self.content.add_widget(box)
        self.content.add_widget(label("These apply to every seed. How the game plays is set in your options file.",
                                      13, k.muted))

    def panel_ghosts(self, m: Dict[str, Any]) -> None:
        k = self.colors
        box = self.card(spacing=8, padding=(16, 12))
        box.add_widget(label("The other Link's Awakening players of your multiworld show as ghosts when they are where "
                             "you are.", 14, k.text))
        status = m["ghost_status"]
        box.add_widget(row(self.checkbox("Show ghosts", m["ghosts"], self.ctx.set_ghosts),
                           label(escape_markup(status.text) if m["ghosts"] else "Ghosts are off.", 13,
                                 k.status[status.level] if m["ghosts"] and status.level != INFO else k.muted,
                                 pos_hint={"center_y": 0.5}), spacing=16, height=40))
        box.add_widget(self.checkbox("Show their slot names over their heads", m["ghost_names"],
                                     self.ctx.set_ghost_names))
        box.add_widget(row(label("Ghost relay", 14, k.text, bold=True, one_line=True, size_hint_x=None, width=dp(110),
                                 pos_hint={"center_y": 0.5}), self.field("ghost_relay"), spacing=12, height=32))
        box.add_widget(label("Leave it empty for the default relay. host:port, password@host:port or a wss:// "
                             "address.", 13, k.muted, padding=(dp(122), 0, 0, 0)))
        self.content.add_widget(box)

    def panel_advanced(self, m: Dict[str, Any]) -> None:
        k = self.colors
        target = m["target"]
        in_use = {
            "sd_card_folder": str(target.sd_card) if target else "",
            "mod_folder": str(target.mods) if target else "",
            "patcher_command": m["options"]["patcher_command"] or "Built-in patcher in las.apworld",
            "game_host": f"{self.ctx.game_host}, port {self.ctx.game_port}",
        }
        exe = m["options"]["emulator"] if m["platform"] == EMULATOR else ""
        name = emulators.emulator(exe).name if exe else ""
        box = self.card(spacing=10, padding=(16, 10))
        for key, title, empty, help_text in ADVANCED:
            if name and key in ("sd_card_folder", "mod_folder"):
                empty = f"{name}'s own" if key == "sd_card_folder" else f"{name}'s default"
            setting = column(spacing=2)
            setting.add_widget(row(label(title, 14, k.text, bold=True, one_line=True, size_hint_x=None, width=dp(140),
                                         pos_hint={"center_y": 0.5}), self.field(key), spacing=10, height=32))
            if not m["options"][key]:
                help_text = f"Empty: {empty}. {help_text}"
            used = row(label("In use:", 13, k.muted, one_line=True, size_hint_x=None, width=dp(50),
                             pos_hint={"center_y": 0.5}),
                       label(escape_markup(in_use[key]) or "-", 13, k.text, one_line=True, pos_hint={"center_y": 0.5}),
                       spacing=6, height=20, padding=(dp(150), 0, 0, 0))
            if key in ("sd_card_folder", "mod_folder") and in_use[key]:
                folder = Path(in_use[key])
                used.add_widget(button("Open", lambda f=folder: Utils.open_file(str(f if f.is_dir() else f.parent)),
                                       "text"))
            setting.add_widget(used)
            setting.add_widget(label(help_text, 12, k.muted, one_line=True, padding=(dp(150), 0, 0, 0)))
            box.add_widget(setting)
        if m["other_program"]:
            box.add_widget(label("Windows opens .aplas files with another program. Right-click a .aplas file, choose "
                                 "Open with, then Archipelago.", 13, k.muted))
        self.content.add_widget(box)


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
