"""The client's changes to Universal Tracker's map tab (DESIGN.md, Universal Tracker map)."""
from typing import Any, Dict, List, Tuple

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

# Universal Tracker's item panels: the room around and between their icons, and above and below a section's name
ITEM_GRID_PADDING, ITEM_GRID_SPACING, ITEM_HEADER_PADDING = 2, 2, 1
ITEM_PANELS_KEY = "tracker_folded_panels"  # Archipelago's persistent storage, under "las": {"left"/"right": folded}
PANEL_SIDES = {"item_tracker_left": "left", "item_tracker_right": "right"}
# A panel's handle: its chevron points the way a click moves the panel's edge, by side and whether it is folded
HANDLE_ICONS = {("left", False): "chevron-left", ("left", True): "chevron-right",
                ("right", False): "chevron-right", ("right", True): "chevron-left"}
ROUTE_COLOR = (0.19, 0.25, 1.0, 0.85)  # Universal Tracker's path blue (its "hinted" colour)
ROUTE_WIDTH = 0.25  # the route's line, in pin sizes
ROUTE_SUGGESTIONS = 12  # the search field's menu shows at most this many locations


def adjust_map_page(page, ctx) -> None:
    tighten_item_grids(page, getattr(ctx, "tracker_world", None))
    add_panel_handles(page)
    fix_map_view(page)
    add_doors_box(page, ctx)
    add_route_search(page, ctx)


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


class RouteLine(Widget):
    """The route on Universal Tracker's map (TrackerMixin.route_points): a line in the map image's pixels, drawn
    under the pins."""

    def __init__(self, **kwargs: Any) -> None:
        super().__init__(**kwargs)
        from kivy.graphics import Color, Line
        with self.canvas:
            Color(*ROUTE_COLOR)
            self.line = Line(points=[], width=1.0, joint="round", cap="round")


def draw_route(page, points: List[Tuple[float, float]], pin_size: float) -> None:
    """Draws the route on the map page's pins' canvas, which loading a map empties."""
    canvas = page.ids.location_canvas
    line = next((w for w in canvas.children if isinstance(w, RouteLine)), None)
    if line is None:
        line = RouteLine()
        canvas.add_widget(line, index=len(canvas.children))
    line.line.points = [c for p in points for c in p] if len(points) > 1 else []
    line.line.width = max(1.0, pin_size * ROUTE_WIDTH)


def add_route_search(page, ctx) -> None:
    """A field in a row below the map's checkboxes, with Universal Tracker's Clear Path beside it. As it is typed in, a
    menu below it suggests the locations in logic whose names hold the text; picking one, or Enter when one matches,
    gets its route (TrackerMixin.route_to)."""
    from kivy.uix.relativelayout import RelativeLayout
    field = ConnectBarTextInput(size_hint_x=None, width=dp(260), pos_hint={"x": 0, "center_y": 0.5})
    # a label over the empty field: KivyMD's hint widget rests where a taller field has its text, on this one's border
    hint = label("Route to a check", 14, Palette().muted, one_line=True, pos_hint={"x": 0.05, "center_y": 0.5})
    holder = RelativeLayout(size_hint=(None, None), size=(dp(260), dp(30)), pos_hint={"center_y": 0.5})
    holder.add_widget(field)
    holder.add_widget(hint)
    field.bind(text=lambda *_: show_hint(), focus=lambda *_: show_hint())

    def show_hint() -> None:
        hint.opacity = 0 if field.text or field.focus else 1
    shown: Dict[str, Any] = {"menu": None, "picked": None}

    def matches(text: str) -> List[str]:
        return [name for name in ctx.tracker_in_logic if text in name.lower()]

    def close() -> None:
        menu, shown["menu"] = shown["menu"], None
        if menu is not None:
            menu.dismiss()

    def pick(name: str) -> None:
        close()
        shown["picked"] = name
        field.text = name
        ctx.route_to(name)

    def suggest(*_: Any) -> None:
        text = field.text.strip().lower()
        found = matches(text) if text and field.text != shown["picked"] else []
        if not found:
            close()
            return
        items = [{"text": name, "on_release": lambda n=name: pick(n)} for name in found[:ROUTE_SUGGESTIONS]]
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
        exact = [name for name in found if name.lower() == text]
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
