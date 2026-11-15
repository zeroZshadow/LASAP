"""Universal Tracker in the client (DESIGN.md, Universal Tracker map): the context it gives the client, and the
client's changes to it."""
import asyncio
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import Utils
from CommonClient import ClientCommandProcessor, CommonContext, logger
from NetUtils import NetworkItem

from . import GOAL_LOCATION, emulators
from .install import tracker_images
from .items import ITEM_TABLE, LASR_TO_ITEM
from .lasr import INSTRUMENTS
from .tracker import known_doors
from .ut_map import DOOR_STEPS, door_out, hidden_door_entrances, hidden_locations

TRACKER_DOORS_KEY = "tracker_doors_shown"  # Archipelago's persistent storage, under "las": TrackerMixin.show_doors
# A pin status's colour in its tooltip (TrackerMixin.pin_text): Universal Tracker's, by its colour names
STATUS_COLOURS = {"path": "hinted", "passable": "in_logic", "passed": "collected_light", "impassable": "out_of_logic",
                  "collected": "collected_light", "none": "collected_light"}
# Universal Tracker from this version on (the one the embedding is tested with) makes the client its context too, with
# its map tab.
TRACKER_MIN_VERSION = (0, 3, 8)


def tracker_version(text: Optional[str]) -> Optional[Tuple[int, int, int]]:
    m = re.match(r"v?(\d+)\.(\d+)\.(\d+)", text or "")
    return (int(m[1]), int(m[2]), int(m[3])) if m else None


try:
    from worlds.tracker.TrackerClient import TrackerCommandProcessor, TrackerGameContext, UT_VERSION
except ImportError:
    TrackerCommandProcessor, TrackerGameContext, UT_VERSION = None, None, None
TRACKER_LOADED = (tracker_version(UT_VERSION) or (0, 0, 0)) >= TRACKER_MIN_VERSION
ClientContext = TrackerGameContext if TRACKER_LOADED else CommonContext
ClientCommands = TrackerCommandProcessor if TRACKER_LOADED else ClientCommandProcessor  # with UT's /explain and others


class TrackerMixin:
    """LASContext's Universal Tracker parts; super() is Universal Tracker's context when it is loaded."""

    def __init__(self, *args: Any) -> None:
        super().__init__(*args)
        # Universal Tracker's map images (start_tracker_images), and the slot's Connected it waits for them with
        self.tracker_images: Optional[Path] = None
        self.tracker_held: Optional[dict] = None
        self.tracker_task: Optional[asyncio.Task] = None
        # The Tracker tab's route (route_points): its target check or door, the locations in logic, the sweep's state
        # (the doors it reaches: route_doors), and the pins by section
        self.route_target: Optional[str] = None
        self.tracker_in_logic: List[str] = []
        self.tracker_state: Any = None
        self.reached_doors: Optional[Tuple[int, List[str]]] = None
        self.route_pins: Optional[Tuple[int, Dict[str, List[Tuple[str, float, float]]]]] = None
        # whether the map tab shows the doors' pins (its Entrances box), kept between runs
        self.tracker_doors_shown: bool = Utils.persistent_load().get("las", {}).get(TRACKER_DOORS_KEY, True)
        # the dungeon door entrances each map hid when the map shown was loaded (load_map, refresh_door_pins)
        self.tracker_doors_hidden: Dict[str, List[str]] = {}

    def build_gui(self, manager) -> None:
        """Universal Tracker's tabs, its map page drawing the client's pins (ut_gui.use_own_pins)."""
        super().build_gui(manager)
        page = getattr(self.map_page_coords_func, "__self__", None)
        if page is not None:
            try:
                from .ut_gui import use_own_pins
                use_own_pins(page, self)
            except Exception as e:  # Universal Tracker's own pins
                logger.exception(f"Could not draw the tracker's pins: {e}")

    def tracker_package(self, cmd: str, args: dict) -> None:
        """Universal Tracker's handling of a package, for its map tab. A failure there stays there: Universal Tracker
        marks the connection as closed on purpose when it fails, which would stop the reconnecting."""
        if self.tracker_images is None:  # its map tab needs the images: it gets the slot once they exist
            if cmd == "Connected":
                self.tracker_held = args
                self.start_tracker_images()
            return
        intentionally = self.disconnected_intentionally
        try:
            super().on_package(cmd, args)
        except Exception:
            logger.exception("Universal Tracker could not handle the server's " + cmd)
            self.disconnected_intentionally = intentionally

    def start_tracker_images(self) -> None:
        """Builds Universal Tracker's map images from the RomFS dump in the background, once per dump
        (install.tracker_images), and points las_options.ut_pack_path at them. Without a dump, a zip the client built
        before serves; else there is no map tab."""
        if not TRACKER_LOADED or self.tracker_images is not None or self.tracker_task is not None:
            return
        romfs = emulators.romfs_root(self.option("romfs_folder"))
        if romfs is None:
            known = Path(self.option("ut_pack_path")) if self.option("ut_pack_path") else None
            if known and known.is_file() and known.parent == Path(Utils.cache_path("las", "tracker")):
                self.tracker_ready(known)
            return
        self.tracker_task = asyncio.create_task(self.build_tracker_images(romfs), name="tracker images")

    async def build_tracker_images(self, romfs: Path) -> None:
        try:
            path = await asyncio.to_thread(tracker_images, romfs, logger.info)
        except Exception as e:
            logger.exception(f"Could not build the tracker's map: {e}")
            return
        finally:
            self.tracker_task = None
        self.set_option("ut_pack_path", str(path), detect=False)
        self.tracker_ready(path)

    def tracker_ready(self, path: Path) -> None:
        """Universal Tracker's map images exist: it gets the slot it waited for, and the doors found."""
        self.tracker_images = path
        held, self.tracker_held = self.tracker_held, None
        if held is not None and self.slot is not None:
            self.tracker_package("Connected", held)
            keys = getattr(self, "defered_entrance_datastorage_keys", None)
            if keys:
                asyncio.create_task(self.send_msgs([{"cmd": "Get", "keys": list(keys)}]))

    @property
    def tracker_items_received(self) -> List[NetworkItem]:
        """Universal Tracker's items: the slot's starting items, which UT leaves out of the world it generates and the
        server sends only to clients that ask for them (items_handling), then the items received."""
        starting = [NetworkItem(ITEM_TABLE[name].code, -2, 0) for name in self.logic_starting if name in ITEM_TABLE]
        return starting + super().tracker_items_received

    def load_map(self, map_id) -> None:
        """Universal Tracker's map, by the world's ut_map_page_hidden_locations and ut_map_page_hidden_entrances,
        which it reads on loading a map: without the checks the map leaves off (ut_map.hidden_locations), and on a
        dungeon's map only the dungeon door entrance known to lead into it (ut_map.hidden_door_entrances). The client
        draws the pins (ut_gui.load_pins)."""
        world = self.tracker_core.get_current_world()
        if world is not None:
            ids = world.location_name_to_id
            hidden = [ids[name] for name in hidden_locations(world) if name in ids]
            self.tracker_doors_hidden = doors = self.hidden_doors()
            world.ut_map_page_hidden_locations = {m["name"]: list(hidden) for m in self.maps or ()}
            world.ut_map_page_hidden_entrances = {m["name"]: doors.get(m["name"], []) for m in self.maps or ()}
        super().load_map(map_id)
        self.draw_route()

    def hidden_doors(self) -> Dict[str, List[str]]:
        """Each dungeon map's dungeon door entrances to hide, by the doors known now (ut_map.hidden_door_entrances)."""
        return hidden_door_entrances(self.slot_data.get("dungeon_entrances"), known_doors(self.found_doors))

    def refresh_door_pins(self) -> None:
        """A dungeon door became known: the map shown loads again when that changes the door pins it hides, as
        Universal Tracker reads them only on loading a map."""
        if TRACKER_LOADED and self.map_id is not None and self.hidden_doors() != self.tracker_doors_hidden:
            self.reload_map()

    def reload_map(self) -> None:
        map_id, self.map_id = self.map_id, None
        self.load_map(map_id)
        self.updateTracker()

    def updateTracker(self):
        """Universal Tracker's sweep, keeping the locations it finds in logic but those the map leaves off, and its
        state, for the Tracker tab's search; the egg's pin shows whether the goal is in logic; then the route drawn
        again (a path found or cleared runs one) and the tooltip shown with the new statuses."""
        tracker_state = super().updateTracker()
        world = self.tracker_core.get_current_world()
        hidden = set(hidden_locations(world)) if world is not None else set()
        self.tracker_in_logic = sorted(set(tracker_state.in_logic_locations) - hidden)
        self.tracker_state = tracker_state.state
        if tracker_state.state is not None and world is not None:
            go = self.tracker_core.multiworld.has_beaten_game(tracker_state.state, world.player)
            for pin in getattr(self, "ldeferred_dict", {}).get(GOAL_LOCATION, ()):
                if hasattr(pin, "paint"):  # the client's pin (ut_gui.MapPin)
                    pin.go_mode = go
                    pin.paint()
        self.draw_route()
        hover = getattr(self.map_page, "las_hover", None)
        if hover is not None:
            hover.refresh()
        return tracker_state

    def route_doors(self) -> List[str]:
        """The doors' entrances (ut_map.DOOR_STEPS) the last sweep reaches, for the Tracker tab's search."""
        state = self.tracker_state
        world = self.tracker_core.get_current_world()
        if state is None or world is None:
            return []
        if self.reached_doors is None or self.reached_doors[0] != id(state):
            names = set(self.tracker_core.multiworld.regions.entrance_cache[world.player])
            self.reached_doors = (id(state), sorted(name for name in DOOR_STEPS & names
                                                    if world.get_entrance(name).can_reach(state)))
        return self.reached_doors[1]

    def route_to(self, name: str) -> None:
        self.command_processor(self)._cmd_get_logical_path(name)

    def start_route(self, text: str) -> None:
        """Before /get_logical_path: no route, so that one it cannot find leaves none, and the location or door the
        route ends at (route_points), the one UT takes the name for when it matches it to locations, regions and
        entrances."""
        self.logical_path_entrances.clear()
        self.route_target = None
        multiworld, player = self.tracker_core.multiworld, self.tracker_core.player_id
        if multiworld is None or player is None or not text.strip():
            return
        regions = multiworld.regions
        locations = set(regions.location_cache[player])
        entrances = set(regions.entrance_cache[player])
        name, usable, _ = Utils.get_intended_text(text, locations | set(regions.region_cache[player]) | entrances)
        if usable and (name in locations or name in DOOR_STEPS & entrances):
            self.route_target = name

    def route_points(self, map_name: str) -> List[Tuple[float, float]]:
        """The way /get_logical_path found, on a map: the pins of the doors it goes through, in order (a door Link
        comes out of at that door's pin), then the check's pin nearest the last door."""
        if not self.logical_path_entrances:
            return []
        if self.route_pins is None or self.route_pins[0] != id(self.locs):
            pins: Dict[str, List[Tuple[str, float, float]]] = {}
            nodes = list(self.locs or ())
            while nodes:
                node = nodes.pop()
                nodes.extend(node.get("children", ()))
                for section in node.get("sections", ()):
                    for spot in node.get("map_locations", ()):
                        pins.setdefault(section.get("name", ""), []).append((spot["map"], spot["x"], spot["y"]))
            self.route_pins = (id(self.locs), pins)
        pins = self.route_pins[1]
        world = self.tracker_core.get_current_world()
        points: List[Tuple[float, float]] = []
        for name in self.logical_path_entrances:
            if name not in DOOR_STEPS:
                continue
            for door in (name, door_out(world.get_entrance(name))):
                spot = next(((x, y) for m, x, y in pins.get(door, ()) if m == map_name), None)
                if spot is not None:
                    if not points or points[-1] != spot:
                        points.append(spot)
                    break
        ends = [(x, y) for m, x, y in pins.get(self.route_target or "", ()) if m == map_name]
        if ends:
            last = points[-1] if points else ends[0]
            end = min(ends, key=lambda p: (p[0] - last[0]) ** 2 + (p[1] - last[1]) ** 2)
            if not points or points[-1] != end:  # a door's route ends at the last door it goes through
                points.append(end)
        return points

    def clear_path(self) -> None:
        self.route_target = None
        super().clear_path()
        if self.map_page is not None:
            from .ut_gui import clear_route_search
            clear_route_search(self.map_page)

    def draw_route(self) -> None:
        if self.ui is None or self.map_page is None or self.map_id is None:
            return
        from .ut_gui import draw_route
        draw_route(self.map_page, self.route_points(self.maps[self.map_id]["name"]), self.ui.loc_size)

    def show_doors(self, shown: bool) -> None:
        """The map tab's Entrances box: shows or hides the doors' pins on the map shown, and remembers it."""
        if shown == self.tracker_doors_shown:
            return
        self.tracker_doors_shown = shown
        Utils.persistent_store("las", TRACKER_DOORS_KEY, shown)
        if self.map_page is not None:
            from .ut_gui import show_door_pins
            show_door_pins(self.map_page, shown)

    def pin_text(self, pin) -> str:
        """A map pin's tooltip (ut_gui.MapHover): each of its checks or doors with its status, a door gone through
        with the door it leads to; the egg's whether the goal is in logic, with the goal's items."""
        from worlds.tracker.TrackerClient import get_ut_color
        world = self.tracker_core.get_current_world()
        if pin.go_mode is not None:
            colour = get_ut_color("in_logic" if pin.go_mode else "out_of_logic")
            return "\n".join([f"Go mode: [color={colour}]{'yes' if pin.go_mode else 'no'}[/color]"]
                             + self.goal_lines(world))
        lines = []
        for key, status in pin.statuses.items():
            name = world.location_id_to_name.get(key, str(key)) if pin.kind == "location" else key
            lines.append(f"{name}: [color={get_ut_color(STATUS_COLOURS.get(status, status))}]{status}[/color]")
            if pin.kind == "entrance" and world is not None and world.get_entrance(key).connected_region is not None:
                lines.append(f"  -> {door_out(world.get_entrance(key))}")
        return "\n".join(lines)

    def goal_lines(self, world) -> List[str]:
        """What the goal takes and how much of it the slot has: the instruments, or the seashells of a hunt."""
        state = self.tracker_state
        if world is None or state is None:
            return []
        if world.goal_seashells:
            have = state.count(LASR_TO_ITEM["seashell"], world.player)
            return [f"Secret Seashells: {have}/{world.goal_seashells}"]
        have = sum(state.has(LASR_TO_ITEM[i], world.player) for i in INSTRUMENTS)
        return [f"Instruments: {have}/{world.goal_instruments}"]
