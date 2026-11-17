"""The client's changes to Universal Tracker's map tab (DESIGN.md, Universal Tracker map)."""
import functools
import math
from typing import Any, Callable, Dict, List, Optional, Tuple

# kvui sets kivy up and has to be imported before the kivy modules
from kvui import ConnectBarTextInput  # isort: skip
import Utils
from kivy.clock import Clock
from kivy.metrics import dp
from kivy.uix.widget import Widget
from kivymd.uix.boxlayout import MDBoxLayout
from kivymd.uix.button import MDButton
from kivymd.uix.menu import MDDropdownMenu

from .client_gui import Palette, label
from .lasr import door_label
from .ut_map import door_pin_colour

# Universal Tracker's item panels: the room around and between their icons, and above and below a section's name
ITEM_GRID_PADDING, ITEM_GRID_SPACING, ITEM_HEADER_PADDING = 2, 2, 1
ITEM_PANELS_KEY = "tracker_folded_panels"  # Archipelago's persistent storage, under "las": {"left"/"right": folded}
PANEL_SIDES = {"item_tracker_left": "left", "item_tracker_right": "right"}
# A panel's handle: its chevron points the way a click moves the panel's edge, by side and whether it is folded
HANDLE_ICONS = {("left", False): "chevron-left", ("left", True): "chevron-right",
                ("right", False): "chevron-right", ("right", True): "chevron-left"}
ROUTE_COLOR = (0.19, 0.25, 1.0, 1.0)  # Universal Tracker's path blue (its "hinted" colour): where Link walks
ROUTE_THROUGH_COLOR = (1.0, 0.2, 0.85, 1.0)  # magenta: through a level the map does not show (a connector)
ROUTE_EDGE_COLOR = (0.05, 0.05, 0.1, 0.9)  # the route's dark edge, for contrast on the map's greens and browns
ROUTE_START_COLOR = (1.0, 1.0, 1.0, 1.0)  # the ring at the route's start, over the pins
ROUTE_WIDTH = 0.1  # the route's line, in pin sizes
ROUTE_EDGE = 1.0  # the dark edge on both sides, in map pixels
ROUTE_DASH, ROUTE_GAP = 4.0, 5.0  # a dashed leg's dashes and gaps, in line widths
ROUTE_SMOOTH = 1.2  # the lines' soft edges, in screen pixels
ROUTE_RING, ROUTE_START_WIDTH = 0.8, 0.12  # the start's ring: its radius and its line, in pin sizes
ROUTE_SUGGESTIONS = 12  # the search field's menu shows at most this many locations


LETTER_DISC = 12  # a dungeon map's stair letters (StairLetters): their discs' size, in map pixels
LETTER_FONT = 72  # the letters' font size in texture pixels, so they stay sharp zoomed in
PIN_REACH = 8  # a pin answers the pointer this many dp around its middle, if its drawn square is smaller
CLICK_SLOP = 6  # dp a press may move and still be a click on the map, not a drag


def adjust_map_page(page, ctx) -> None:
    tighten_item_grids(page, getattr(ctx, "tracker_world", None))
    add_panel_handles(page)
    fix_map_view(page)
    add_doors_box(page, ctx)
    add_route_search(page, ctx)
    add_route_clicks(page, ctx)
    follow_zoom(page, ctx)


def use_own_pins(page, ctx) -> None:
    """Universal Tracker's map page draws the client's pins (load_pins) with one tooltip for all (MapHover), from its
    first map on."""
    page.las_hover = MapHover(page, ctx)
    ctx.map_page_coords_func = lambda *args: load_pins(page, ctx, *args)


class ItemPanels:
    def __init__(self) -> None:
        self.widths: Dict[str, float] = {}
        self.folded: Dict[str, bool] = dict(Utils.persistent_load().get("las", {}).get(ITEM_PANELS_KEY, {}))
        self.handles: Dict[str, Any] = {}
        self.places: Dict[str, Tuple[Any, Any]] = {}  # side -> (its scroll view, the row it stands in)


def item_panels(page) -> ItemPanels:
    panels = getattr(page, "las_item_panels", None)
    if panels is None:
        panels = page.las_item_panels = ItemPanels()
    return panels


def show_panel(page, side: str) -> None:
    """A panel at its sections' width, or out of its row while folded (at no width its icons still draw); its handle
    only while it has sections."""
    panels = item_panels(page)
    groups = page.ids.get(f"item_tracker_{side}")
    width = panels.widths.get(side, 0)
    folded = panels.folded.get(side, False) and bool(width)
    if groups is not None and width:
        groups.parent.width = width
    handle = panels.handles.get(side)
    if side in panels.places:
        scroll, row = panels.places[side]
        if folded and scroll.parent is row:
            row.remove_widget(scroll)
        elif not folded and scroll.parent is None:  # beside its handle: a BoxLayout lays its children out last first
            at = row.children.index(handle)
            row.add_widget(scroll, index=at + 1 if side == "left" else at)
    if handle is not None:
        handle.icon = HANDLE_ICONS[side, folded]
        handle.opacity, handle.disabled = (1, False) if width else (0, True)
        handle.width = dp(24) if width else 0


def tighten_item_grids(page, tracker_world) -> None:
    """Universal Tracker's map page lays its item sections out with ITEM_GRID_PADDING and ITEM_GRID_SPACING instead
    of its own wider gaps: each time its init_grid sized the grids for those, they are sized again, and those it made
    before the tab was added are sized now."""
    init_grid = page.init_grid

    def tight(layout_id, layout_configs, ctx, items, vert=True):
        init_grid(layout_id, layout_configs, ctx, items, vert)
        tighten(page, layout_id, layout_configs, vert)

    page.init_grid = tight
    if tracker_world is not None:
        for layout_id, configs in (("item_tracker_left", tracker_world.map_page_item_layouts_left),
                                   ("item_tracker_right", tracker_world.map_page_item_layouts_right)):
            tighten(page, layout_id, configs or [], True)


def tighten(page, layout_id: str, layout_configs, vert: bool) -> None:
    groups = page.ids.get(layout_id)
    if groups is None:
        return
    configs = {config.name: config for config in layout_configs}
    widest = 0
    for container in groups.children:
        grid, header = container.children  # the header was added first
        config = configs.get(header.text)
        if config is None or config.orientation != "lr":
            continue
        grid.padding, grid.spacing = ITEM_GRID_PADDING, ITEM_GRID_SPACING
        grid.width = config.cols * config.item_width + (config.cols - 1) * ITEM_GRID_SPACING + 2 * ITEM_GRID_PADDING
        grid.do_layout()
        grid.height = grid.minimum_height
        header.padding = (0, ITEM_HEADER_PADDING)
        header.texture_update()
        container.do_layout()
        container.size = container.minimum_size
        widest = max(widest, container.width)
    if vert and widest:
        groups.parent.width = widest
        if layout_id in PANEL_SIDES:
            item_panels(page).widths[PANEL_SIDES[layout_id]] = widest
            show_panel(page, PANEL_SIDES[layout_id])


def add_panel_handles(page) -> None:
    """A chevron on the map's side of each item panel folds the panel away, giving the map its room, and opens it
    again; Archipelago's persistent storage keeps which are folded. A panel without sections has none."""
    from kivymd.uix.button import MDIconButton
    panels = item_panels(page)
    for side in PANEL_SIDES.values():
        scroll = page.ids[f"item_tracker_{side}"].parent
        row = scroll.parent
        handle = MDIconButton(icon=HANDLE_ICONS[side, False], style="standard", size_hint=(None, None),
                              size=(dp(24), dp(48)), pos_hint={"center_y": 0.5})

        def toggle(_: Any, side: str = side) -> None:
            panels.folded[side] = not panels.folded.get(side, False)
            Utils.persistent_store("las", ITEM_PANELS_KEY, dict(panels.folded))
            show_panel(page, side)

        handle.bind(on_release=toggle)
        at = row.children.index(scroll)  # a BoxLayout lays its children out last first
        row.add_widget(handle, index=at if side == "left" else at + 1)
        panels.handles[side] = handle
        panels.places[side] = (scroll, row)
        show_panel(page, side)


def fix_map_view(page) -> None:
    """Universal Tracker's map view: its stencil does not clip the map on every renderer, so it could draw over the
    items left of it, and Recenter put the map at the window's corner. A scissor clips the map to its box, placed
    as each frame draws (the tabs slide), and Recenter puts the map back at the box's corner."""
    from kivy.graphics import Callback
    from kivy.graphics.scissor_instructions import ScissorPop, ScissorPush
    from kivy.graphics.transformation import Matrix
    container = page.ids.tracker_map_container
    box = container.parent.__self__
    scissor = ScissorPush()

    def clip(*_):
        x, y = box.to_window(*box.pos)
        scissor.x, scissor.y, scissor.width, scissor.height = round(x), round(y), round(box.width), round(box.height)

    box.canvas.before.add(Callback(clip))
    box.canvas.before.add(scissor)
    box.canvas.after.add(ScissorPop())

    def recenter():
        container.transform = Matrix()
        container.pos = box.pos

    container.recenter_view = recenter


def add_doors_box(page, ctx) -> None:
    """An Entrances box beside Universal Tracker's Auto Tab: shows or hides the doors' pins (TrackerMixin.show_doors)."""
    auto_tab = page.ids.auto_tab_checkitem.__self__  # the widget, not ids' weak proxy
    doors = type(auto_tab)(text="Entrances", active=ctx.tracker_doors_shown, pos_hint={"center_y": 0.5})
    doors.bind(active=lambda _, value: ctx.show_doors(value))
    row = auto_tab.parent
    row.add_widget(doors, index=row.children.index(auto_tab))


@functools.lru_cache(maxsize=None)
def hex_color(name: str) -> Tuple[float, float, float]:
    from kivy.utils import get_color_from_hex
    from worlds.tracker.TrackerClient import get_ut_color
    return tuple(get_color_from_hex("#" + get_ut_color(name))[:3])


def mixed_status(statuses) -> str:
    """Universal Tracker's colour of a check pin with these statuses (its APLocationMixed)."""
    glitches = any(s.endswith("glitched") for s in statuses)
    in_logic = any(s.endswith("in_logic") for s in statuses)
    out_of_logic = any(s.endswith("out_of_logic") for s in statuses)
    hinted = any(s.startswith("hinted") for s in statuses)
    for colour, when in (("mixed_logic", in_logic and (out_of_logic or (glitches and hinted))),
                         ("hinted_glitched", glitches and hinted), ("hinted_out_of_logic", hinted and out_of_logic),
                         ("hinted", hinted), ("in_logic_glitched", glitches and in_logic),
                         ("out_of_logic_glitched", glitches and out_of_logic), ("in_logic", in_logic),
                         ("out_of_logic", out_of_logic), ("glitched", glitches)):
        if when:
            return colour
    return "collected"


def split_statuses(statuses) -> List[str]:
    """Universal Tracker's four colours of a split check pin (its APLocationSplit)."""
    from collections import Counter
    counts = Counter(s for s in statuses if s != "collected")

    def order(pair) -> float:
        status, n = pair
        return {"out_of_logic": 0, "in_logic": 999999999, "hinted_in_logic": 8888888}.get(status, n + ord(status[0]) / 10)
    found = [s for s, _ in sorted(counts.items(), key=order, reverse=True)]
    return (found * max(2, 4 // len(found)))[:4] if found else ["collected"] * 4


class MapPin(Widget):
    """A pin on Universal Tracker's map in place of its own (load_pins), drawn as it draws them: a check's square (four
    triangles when split), a door's (an entrance) or event's diamond. It has no tooltip or hover binding of its own,
    which made loading a map slow and hovering unreliable: MapHover shows one tooltip for all. `statuses` is what
    Universal Tracker's updateTracker sets through update_status, by location id, entrance or event name."""

    def __init__(self, kind: str, names: List[Any], pos: Tuple[float, float], size: float, border: float,
                 split: bool, connected: Optional[Callable[[str], bool]] = None) -> None:
        super().__init__(size_hint=(None, None), pos=pos, size=(size, size))
        from kivy.graphics import Color, PopMatrix, PushMatrix, Rectangle, Rotate, Scale, Triangle
        self.kind, self.split = kind, split
        self.statuses: Dict[Any, str] = {name: "none" for name in names}
        self.go_mode: Any = None  # the egg's pin: whether the goal is in logic (TrackerMixin.updateTracker)
        # a door's pin: whether an entrance is connected (door_pin_colour), TrackerMixin.entrance_connected
        self.connected: Callable[[str], bool] = connected or (lambda name: False)
        x, y, half = pos[0], pos[1], size / 2
        with self.canvas:
            if kind != "location":
                PushMatrix()
                Rotate(angle=45, origin=pos)
                Scale(0.7, 0.7, 1, origin=pos)
            Color(0, 0, 0)
            Rectangle(pos=(x - half - border, y - half - border), size=(size + 2 * border, size + 2 * border))
            if split:  # Universal Tracker's order: its colours 1, 3, 2 and 4, from the pin's top clockwise
                self.colours = [None] * 4
                for index, (ax, ay, bx, by) in ((0, (-1, 1, 1, 1)), (2, (1, -1, 1, 1)), (1, (-1, -1, 1, -1)),
                                                (3, (-1, -1, -1, 1))):
                    self.colours[index] = Color(1, 1, 1)
                    Triangle(points=[x, y, x + ax * half, y + ay * half, x + bx * half, y + by * half])
            else:
                self.colours = [Color(1, 1, 1)]
                Rectangle(pos=(x - half, y - half), size=(size, size))
            if kind != "location":
                PopMatrix()
        self.paint()

    def update_status(self, name: Any, status: str) -> None:
        if name in self.statuses and self.statuses[name] != status:
            self.statuses[name] = status
            self.paint()

    def paint(self) -> None:
        statuses = list(self.statuses.values())
        if self.go_mode is not None:
            names = ["in_logic" if self.go_mode else "out_of_logic"]
        elif self.kind != "location":
            names = [door_pin_colour(self.statuses, self.connected)]
        else:
            names = split_statuses(statuses) if self.split else [mixed_status(statuses)]
        for colour, name in zip(self.colours, names):
            colour.rgb = hex_color(name)

    def reach(self, scale: float) -> float:
        """How far from its middle, in map pixels, the pin answers the pointer at `scale` window pixels per map pixel."""
        drawn = self.width / 2 * (0.7 if self.kind != "location" else 1)
        return max(drawn, dp(PIN_REACH) / scale if scale else drawn)


def load_pins(page, ctx, coords, door_coords, event_coords, use_split, default_size):
    """Universal Tracker's map pins (its map_page_coords_func), as MapPins: each table maps a place (x, y) to its
    sections and size. With them the island's dungeon check squares (TrackerMixin.dungeon_square_pins), under the
    doors' pins. Returns its tables of pins by location id, entrance and event."""
    from collections import defaultdict
    canvas = page.ids.location_canvas
    canvas.clear_widgets()
    page.location_icons = []
    tables = (defaultdict(list), defaultdict(list), defaultdict(list))
    border = ctx.ui.loc_border
    squares = ctx.dungeon_square_pins(ctx.maps[ctx.map_id]["name"]) if ctx.map_id is not None else {}
    checks = list(coords.items()) + [(spot, (ids, None)) for spot, ids in squares.items()]
    stairs = ctx.maps[ctx.map_id].get("stairs", ()) if ctx.map_id is not None else ()
    if stairs:
        canvas.add_widget(StairLetters(stairs))
    for kind, places, table in (("location", checks, tables[0]), ("entrance", door_coords.items(), tables[1]),
                                ("event", event_coords.items(), tables[2])):
        for spot, (names, size) in places:
            if kind == "entrance":  # with the ways out of the connector's other ends
                names = ctx.door_pin_names(names)
            pin = MapPin(kind, names, spot, size if size is not None else default_size, border,
                         use_split and kind == "location", ctx.entrance_connected)
            canvas.add_widget(pin)
            for name in names:
                table[name].append(pin)
    show_door_pins(page, ctx.tracker_doors_shown)
    page.las_hover.reset()
    return tables


@functools.lru_cache(maxsize=None)
def letter_texture(letter: str):
    """A stair letter's texture: bold, white, LETTER_FONT pixels."""
    from kivy.core.text import Label as CoreLabel
    text = CoreLabel(text=letter, font_size=LETTER_FONT, bold=True, color=(1, 1, 1, 1))
    text.refresh()
    return text.texture


class StairLetters(Widget):
    """A dungeon map's stair letters (tracker/maps.json's "stairs"), under its pins: a black disc with the letter in
    white at each stair, drawn in the map's pixels from a font texture, so they stay sharp at every zoom."""

    def __init__(self, stairs) -> None:
        super().__init__(size_hint=(None, None), size=(0, 0))
        from kivy.graphics import Color, Rectangle, SmoothEllipse
        radius = LETTER_DISC / 2
        with self.canvas:
            for stair in stairs:
                x, y = stair["x"], stair["y"]
                Color(0, 0, 0)
                SmoothEllipse(pos=(x - radius, y - radius), size=(LETTER_DISC, LETTER_DISC))
                texture = letter_texture(stair["letter"])
                scale = LETTER_DISC * 0.8 / texture.height
                w, h = texture.width * scale, texture.height * scale
                t = texture.tex_coords  # upside down: the pins' canvas has y down, as PopTracker's maps
                Color(1, 1, 1)
                Rectangle(texture=texture, pos=(x - w / 2, y - h / 2), size=(w, h),
                          tex_coords=(t[6], t[7], t[4], t[5], t[2], t[3], t[0], t[1]))


def show_door_pins(page, shown: bool) -> None:
    """The Entrances box: the doors' pins drawn or not, without loading the map again."""
    for pin in page.ids.location_canvas.children:
        if isinstance(pin, MapPin) and pin.kind == "entrance":
            pin.opacity = 1 if shown else 0
    page.las_hover.reset()


class MapHover:
    """One tooltip for the map's pins (MapPin): the pin under the pointer, or nearest it where pins overlap, shows its
    text at once; a click on a pin keeps it shown until a click elsewhere or on it again. The text is
    ctx.pin_text's."""

    def __init__(self, page, ctx) -> None:
        from kivy.core.window import Window
        from kvui import ToolTip
        self.page, self.ctx, self.window = page, ctx, Window
        self.tip = ToolTip(text="", markup=True)
        self.tip.pos_hint = {}
        self.tip.bind(size=lambda *_: self.place())
        self.shown: Optional[MapPin] = None
        self.pinned: Optional[MapPin] = None
        self.at: Tuple[float, float] = (0, 0)  # the pointer
        self.anchor: Tuple[float, float] = (0, 0)  # where the shown pin was entered or clicked
        self.press: Optional[Tuple[Any, Tuple[float, float]]] = None
        Window.bind(mouse_pos=self.moved, on_cursor_leave=lambda *_: self.show(self.pinned),
                    on_touch_down=self.touch_down, on_touch_up=self.touch_up)

    def map_point(self, pos) -> Optional[Tuple[float, float, float]]:
        """A window point on the map: its place in map pixels (PopTracker's, from the top left) and the window pixels
        per map pixel there; None when the map does not show it."""
        box = self.page.ids.tracker_map_container.parent
        if box.get_root_window() is None or not box.collide_point(*box.to_widget(*pos)) or self.covered(box, pos):
            return None
        image = self.page.ids.tracker_map
        (nw, nh), (tw, th) = image.norm_image_size, image.texture_size
        if not tw or not th:
            return None
        x, y = image.to_widget(*pos)
        scale = nw / tw
        left, top = image.x + (image.width - nw) / 2, image.y + (image.height + nh) / 2
        return (x - left) / scale, (top - y) / scale, scale * self.page.ids.tracker_map_container.scale

    def covered(self, box, pos) -> bool:
        """Whether a popup or menu above the window's content covers the point."""
        branch = box
        while branch.parent is not None and branch.parent is not self.window:
            branch = branch.parent
        for child in self.window.children:
            if child is branch:
                return False
            if child is not self.tip and child.collide_point(*pos):
                return True
        return False

    def pin_at(self, pos) -> Optional[MapPin]:
        point = self.map_point(pos)
        if point is None:
            return None
        x, y, scale = point
        best, distance = None, None
        for pin in self.page.ids.location_canvas.children:
            if isinstance(pin, MapPin) and pin.opacity:
                dx, dy = abs(pin.x - x), abs(pin.y - y)
                if max(dx, dy) <= pin.reach(scale) and (distance is None or dx * dx + dy * dy < distance):
                    best, distance = pin, dx * dx + dy * dy
        return best

    def moved(self, _, pos) -> None:
        self.at = tuple(pos)
        if self.pinned is not None:
            if self.pinned.parent is not None and self.page.ids.tracker_map_container.get_root_window() is not None:
                return
            self.pinned = None
        self.show(self.pin_at(pos))

    def touch_down(self, _, touch) -> None:
        if getattr(touch, "button", "left") == "left" and not getattr(touch, "is_mouse_scrolling", False):
            self.press = (touch.uid, tuple(touch.pos))

    def touch_up(self, _, touch) -> None:
        press, self.press = self.press, None
        if press is None or press[0] != touch.uid or max(abs(touch.x - press[1][0]),
                                                         abs(touch.y - press[1][1])) > dp(CLICK_SLOP):
            return
        pin = self.pin_at(touch.pos)
        self.pinned = pin if pin is not self.pinned else None
        self.at = tuple(touch.pos)
        self.show(pin)

    def show(self, pin: Optional[MapPin]) -> None:
        """The pin's tooltip, with those of the other pins drawn at its place (a dungeon's square under its door)."""
        if pin is None or pin.parent is None:
            self.shown = None
            if self.tip.parent is not None:
                self.tip.parent.remove_widget(self.tip)
            return
        if pin is not self.shown:
            self.shown = pin
            self.anchor = self.at
        here = [p for p in self.page.ids.location_canvas.children if isinstance(p, MapPin) and p.opacity
                and p is not pin and (p.x, p.y) == (pin.x, pin.y)]
        self.tip.text = "\n\n".join(self.ctx.pin_text(p) for p in [pin, *here])
        self.tip.opacity, self.tip.scale_value_x, self.tip.scale_value_y = 1, 1, 1
        if self.tip.parent is None:
            self.window.add_widget(self.tip)
        self.place()

    def place(self) -> None:
        """Below the point the pin was entered or clicked at, inside the window; above it when there is no room."""
        if self.shown is None:
            return
        x, y = self.anchor
        width, height = self.tip.size
        top = y - dp(20)
        bottom = top - height if top - height >= dp(4) else y + dp(20)
        self.tip.pos = (min(max(dp(4), x - width / 2), self.window.width - width - dp(4)),
                        min(bottom, self.window.height - height - dp(4)))

    def refresh(self) -> None:
        """The shown pin's text again: its statuses changed."""
        if self.shown is not None:
            self.show(self.shown)

    def reset(self) -> None:
        """The map's pins changed: a pin no longer drawn lets go of the tooltip."""
        if self.pinned is not None and (self.pinned.parent is None or not self.pinned.opacity):
            self.pinned = None
        self.show(self.pinned or None)


def outlined(lines: List[Tuple[Tuple[float, ...], Dict[str, Any]]], width: float, smooth: float) -> None:
    """Lines (their colour, their Line arguments) on the canvas being drawn, each over a ROUTE_EDGE_COLOR edge; every
    edge first, so no edge covers another line. Kivy's SmoothLine, as a wide Line has jagged edges: its soft edge
    `smooth` wide, in the canvas's units."""
    from kivy.graphics import Color, SmoothLine
    Color(*ROUTE_EDGE_COLOR)
    for _, line in lines:
        SmoothLine(width=width + ROUTE_EDGE, overdraw_width=smooth, **line)
    for color, line in lines:
        Color(*color)
        SmoothLine(width=width, overdraw_width=smooth, **line)


class RouteLine(Widget):
    """The route on Universal Tracker's map (TrackerMixin.route_points), in the map image's pixels, under the pins:
    a solid ROUTE_COLOR line where Link walks on the map and a dashed ROUTE_THROUGH_COLOR one where he goes through a
    level the map does not show, both with a dark edge."""

    def draw(self, points: List[Tuple[float, float, bool]], pin_size: float, smooth: float) -> None:
        self.canvas.clear()
        width = max(1.0, pin_size * ROUTE_WIDTH)
        lines: List[Tuple[Tuple[float, ...], Dict[str, Any]]] = []
        walked: List[float] = []  # the solid run being gathered, as one line for its joints
        for (x, y, _), (x2, y2, walk) in zip(points, points[1:]):
            if walk:
                walked += [x, y, x2, y2] if not walked else [x2, y2]
                continue
            if walked:
                lines.append((ROUTE_COLOR, {"points": walked}))
                walked = []
            length = math.hypot(x2 - x, y2 - y)
            step = width * (ROUTE_DASH + ROUTE_GAP)
            for at in range(int(length // step) + 1 if length else 0):
                a, b = at * step / length, min(1.0, (at * step + width * ROUTE_DASH) / length)
                lines.append((ROUTE_THROUGH_COLOR, {"points": [x + (x2 - x) * a, y + (y2 - y) * a,
                                                               x + (x2 - x) * b, y + (y2 - y) * b]}))
        if walked:
            lines.append((ROUTE_COLOR, {"points": walked}))
        with self.canvas:
            outlined(lines, width, smooth)


class RouteStart(Widget):
    """The ring around the pin of the door the route starts at, over the pins."""

    def draw(self, spot: Optional[Tuple[float, float]], pin_size: float, smooth: float) -> None:
        self.canvas.clear()
        if spot is None:
            return
        with self.canvas:
            outlined([(ROUTE_START_COLOR, {"circle": (spot[0], spot[1], pin_size * ROUTE_RING)})],
                     max(1.5, pin_size * ROUTE_START_WIDTH), smooth)


def draw_route(page, points: List[Tuple[float, float, bool]], started: bool, pin_size: float) -> None:
    """Draws the route on the map page's pins' canvas, which loading a map empties: its line under the pins, its start
    over them, moved back on top as Universal Tracker adds pins and icons. Their soft edges are ROUTE_SMOOTH screen
    pixels wide at the map's zoom (follow_zoom)."""
    canvas = page.ids.location_canvas
    image = page.ids.tracker_map
    scale = (image.norm_image_size[0] / image.texture_size[0] if image.texture_size[0] else 1) *         page.ids.tracker_map_container.scale
    smooth = ROUTE_SMOOTH / scale if scale > 0 else ROUTE_SMOOTH
    line = next((w for w in canvas.children if isinstance(w, RouteLine)), None)
    if line is None:
        line = RouteLine()
        canvas.add_widget(line, index=len(canvas.children))
    line.draw(points, pin_size, smooth)
    start = next((w for w in canvas.children if isinstance(w, RouteStart)), None)
    if start is None:
        start = RouteStart()
    if canvas.children[0] is not start:
        if start.parent is not None:
            canvas.remove_widget(start)
        canvas.add_widget(start)
    start.draw(points[0][:2] if started and points else None, pin_size, smooth)


def follow_zoom(page, ctx) -> None:
    """The route drawn again as the map's zoom or size changes, for its soft edges (draw_route)."""
    redraw = Clock.create_trigger(lambda *_: ctx.draw_route(), 0.05)
    page.ids.tracker_map_container.bind(scale=lambda *_: redraw())
    page.ids.tracker_map.bind(norm_image_size=lambda *_: redraw())


def add_route_clicks(page, ctx) -> None:
    """A right-click on a map pin routes to its location or door (TrackerMixin.route_to), with the other pins drawn at
    its place (a dungeon's square under its door); to one of a menu where they have several not yet checked. The pin
    is the one MapHover shows the tooltip of."""
    canvas = page.ids.location_canvas.__self__

    def names(pin: MapPin) -> List[str]:
        if pin.kind == "entrance":
            return list(pin.statuses)
        world = ctx.tracker_core.get_current_world()
        if pin.kind != "location" or world is None:
            return []
        return [world.location_id_to_name[loc] for loc, status in pin.statuses.items() if status != "collected"]

    def clicked(widget, touch) -> bool:
        hover = getattr(page, "las_hover", None)
        if getattr(touch, "button", None) != "right" or hover is None:
            return False
        x, y = widget.to_window(*touch.pos)
        pin = hover.pin_at((x, y))
        if pin is None:
            return False
        here = [p for p in canvas.children if isinstance(p, MapPin) and p.opacity and p is not pin
                and (p.x, p.y) == (pin.x, pin.y)]
        found = [name for p in [pin, *here] for name in names(p)]
        if not found:
            return False
        if len(found) == 1:
            ctx.route_to(found[0])
            return True
        menu = MDDropdownMenu(caller=Widget(pos=(x, y), size=(1, 1)), position="bottom", width=dp(360))

        def pick(name: str) -> None:
            menu.dismiss()
            ctx.route_to(name)

        menu.items = [{"text": door_label(name), "on_release": lambda n=name: pick(n)} for name in found]
        menu.open()
        return True

    canvas.bind(on_touch_down=clicked)


def clear_route_search(page) -> None:
    """Clear Path empties the search field too."""
    field = getattr(page, "las_route_field", None)
    if field is not None:
        field.text = ""


def add_route_search(page, ctx) -> None:
    """A field in a row below the map's checkboxes, with Universal Tracker's Clear Path beside it. As it is typed in, a
    menu below it suggests the locations in logic and the doors the sweep reaches (TrackerMixin.route_doors) whose
    names hold the text; picking one, or Enter when one matches, gets its route (TrackerMixin.route_to)."""
    from kivy.uix.relativelayout import RelativeLayout
    field = ConnectBarTextInput(size_hint_x=None, width=dp(260), pos_hint={"x": 0, "center_y": 0.5})
    # a label over the empty field: KivyMD's hint widget rests where a taller field has its text, on this one's border
    hint = label("Route to a check or door", 14, Palette().muted, one_line=True, pos_hint={"x": 0.05, "center_y": 0.5})
    holder = RelativeLayout(size_hint=(None, None), size=(dp(260), dp(30)), pos_hint={"center_y": 0.5})
    holder.add_widget(field)
    holder.add_widget(hint)
    page.las_route_field = field
    field.bind(text=lambda *_: show_hint(), focus=lambda *_: show_hint())

    def show_hint() -> None:
        hint.opacity = 0 if field.text or field.focus else 1
    shown: Dict[str, Any] = {"menu": None, "picked": None}

    def matches(text: str) -> List[str]:
        """The locations and doors whose names hold the text: a door by its player-facing name (lasr.door_label)."""
        return [name for name in (*ctx.tracker_in_logic, *ctx.route_doors()) if text in door_label(name).lower()]

    def close() -> None:
        menu, shown["menu"] = shown["menu"], None
        if menu is not None:
            menu.dismiss()

    def pick(name: str) -> None:
        close()
        shown["picked"] = door_label(name)
        field.text = door_label(name)
        ctx.route_to(name)

    def suggest(*_: Any) -> None:
        text = field.text.strip().lower()
        found = matches(text) if text and field.text != shown["picked"] else []
        if not found:
            close()
            return
        items = [{"text": door_label(name), "on_release": lambda n=name: pick(n)} for name in found[:ROUTE_SUGGESTIONS]]
        menu = shown["menu"]
        if menu is None:  # the field keeps the keyboard; a touch outside the menu closes it
            menu = shown["menu"] = MDDropdownMenu(caller=field, items=items, position="bottom", width=dp(420))
            menu.bind(on_dismiss=lambda *_: shown.update(menu=None) if shown["menu"] is menu else None)
            menu.open()
        else:  # the open menu with the new items, sized and placed as open() does, without its animation
            menu.items = items
            menu.set_menu_properties()
            menu.height = menu.target_height
            menu.set_menu_pos()

    def search(*_: Any) -> None:
        text = field.text.strip().lower()
        found = matches(text) if text else []
        exact = [name for name in found if door_label(name).lower() == text]
        if len(found) == 1 or exact:
            pick((exact or found)[0])

    suggest_soon = Clock.create_trigger(suggest, 0.15)
    field.bind(text=lambda *_: suggest_soon())
    field.bind(on_text_validate=search)
    header = page.ids.auto_tab_checkitem.__self__.parent
    below = MDBoxLayout(holder, size_hint=(None, None), padding=(dp(12), dp(4), dp(12), dp(8)), spacing=dp(16))
    clear = next((w for w in header.children if isinstance(w, MDButton)
                  and any(getattr(c, "text", "") == "Clear Path" for c in w.children)), None)
    if clear is not None:  # else it stays where Universal Tracker put it
        header.remove_widget(clear)
        below.add_widget(clear)
    below.bind(minimum_size=below.setter("size"))
    box = header.parent

    def align(*_: Any) -> None:  # under the checkboxes: at the header's left, which centres itself
        below.pos_hint = {"x": (header.x - box.x) / box.width if box.width else 0}

    header.bind(pos=align, size=align)
    box.bind(pos=align, size=align)
    box.add_widget(below, index=box.children.index(header))
