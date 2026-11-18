"""Universal Tracker in the client (DESIGN.md, Universal Tracker map): the context it gives the client, and the
client's changes to it."""
import asyncio
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import Utils
from BaseClasses import Location
from CommonClient import ClientCommandProcessor, CommonContext, logger
from NetUtils import NetworkItem

from . import GOAL_LOCATION, emulators
from .checklist import ERROR, INFO, TODO
from .install import tracker_images
from .items import ITEM_TABLE, LASR_TO_ITEM
from .lasr import DUNGEONS, INSTRUMENTS, door_entrance, door_label
from .tracker import known_doors
from .ut_map import (DOOR_STEPS, LOCATION_DUNGEONS, OVERWORLD_MAP, check_label, connector_exits, door_out,
                     dungeon_squares, find_route, hidden_door_entrances, hidden_locations, spot_region)

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


# Why Universal Tracker is installed but could not be imported, for the Tracker tab (TrackerMixin.tracker_status)
TRACKER_IMPORT_ERROR: Optional[str] = None
try:
    from worlds.tracker.TrackerClient import TrackerCommandProcessor, TrackerGameContext, UT_VERSION
except Exception as e:  # a broken Universal Tracker leaves the client without its map
    TrackerCommandProcessor, TrackerGameContext, UT_VERSION = None, None, None
    if not (isinstance(e, ModuleNotFoundError) and e.name == "worlds.tracker"):  # not installed
        TRACKER_IMPORT_ERROR = f"{type(e).__name__}: {e}"
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
        # why the map images could not be built, or Universal Tracker could not handle the slot, for the Tracker tab
        self.tracker_problem: Optional[str] = None
        # The Tracker tab's route (route_points): its target check or door, the door it starts at (None: the slot's
        # start), the locations in logic, the sweep's state (the doors it reaches: route_doors), and the pins by
        # section
        self.route_target: Optional[str] = None
        self.route_start: Optional[str] = None
        # the spot of the door Link came through last: the game's last DOOR line's <to> (LASContext)
        self.game_exit: Optional[str] = None
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
        if cmd == "Connected":
            self.tracker_problem = None
        try:
            super().on_package(cmd, args)
        except Exception as e:
            logger.exception("Universal Tracker could not handle the server's " + cmd)
            self.tracker_problem = f"Universal Tracker could not handle the server's {cmd}: " \
                                   f"{e.args[0] if e.args else e}"
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
        self.tracker_problem = None
        try:
            path = await asyncio.to_thread(tracker_images, romfs, logger.info)
        except Exception as e:
            logger.exception(f"Could not build the tracker's map: {e}")
            self.tracker_problem = f"Could not build the map: {e}"
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

    def tracker_status(self) -> Tuple[str, str]:
        """What keeps the Tracker tab from showing Universal Tracker's map, as a checklist level and a line, for while
        it shows none (client_gui.TrackerPanel)."""
        if TRACKER_IMPORT_ERROR:
            return ERROR, f"Universal Tracker is installed but could not be loaded: {TRACKER_IMPORT_ERROR}"
        if UT_VERSION is None:
            return TODO, ("Universal Tracker is not installed. Put tracker.apworld into the custom_worlds folder, then "
                          "restart the client.")
        if not TRACKER_LOADED:
            return TODO, (f"Universal Tracker {UT_VERSION} is too old. The map needs "
                          f"v{'.'.join(map(str, TRACKER_MIN_VERSION))} or newer.")
        if self.tracker_task is not None:
            return INFO, "Building the map from the RomFS dump. This happens once."
        if self.tracker_problem:
            return ERROR, self.tracker_problem
        if self.tracker_images is None and emulators.romfs_root(self.option("romfs_folder")) is None:
            return TODO, "The map is built from the RomFS dump: set its folder in the Patch tab."
        if self.server is None or not self.slot:
            return TODO, "Not connected to the server. Connect to see the map."
        if self.tracker_images is None:
            return INFO, "Waiting for the map images."
        core = getattr(self, "tracker_core", None)
        if core is not None and core.multiworld is None:
            error = [line for line in str(core.gen_error or "").splitlines() if line.strip()]
            return ERROR, "Universal Tracker could not generate the slot" + (f": {error[-1].strip()}" if error else
                                                                             ".") + " /faris_asked tells more."
        if getattr(self, "tracker_world", None) is None:
            return ERROR, "Universal Tracker could not load the map. The log may tell why."
        return INFO, "Loading the map."

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

    def entrance_connected(self, name: str) -> bool:
        """Whether the slot's entrance of that name leads somewhere: a door the player has gone through, with Universal
        Tracker's deferred entrances (door_pin_colour)."""
        entrance = self.tracker_core.multiworld.regions.entrance_cache[self.tracker_core.player_id].get(name) \
            if self.tracker_core.get_current_world() is not None else None
        return entrance is not None and entrance.connected_region is not None

    def door_pin_names(self, names: List[str]) -> List[str]:
        """A door pin's entrances, and the ways out of the other ends of the connector its door leads into in the slot
        (connector_exits): Universal Tracker keeps their statuses on the pin too (ut_gui.load_pins)."""
        entrances = self.slot_data.get("entrances") or {}
        return list(names) + [way for name in names for way in connector_exits(entrances, name) if way not in names]

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
            # a door's pin also follows which connector ends are connected, which may change with no status change
            for pin in {id(pin): pin for pins in getattr(self, "deferred_dict", {}).values() for pin in pins}.values():
                if hasattr(pin, "paint"):
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
        """A route to a location or a door: /get_logical_path, as the command box runs it."""
        self.command_processor(self)._cmd_get_logical_path(name)

    def start_route(self, text: str) -> None:
        """Before /get_logical_path: no route, so that one it cannot find leaves none, and the location or door the
        route ends at (route_points), the one UT takes the name for when it matches it to locations, regions and
        entrances."""
        self.logical_path_entrances.clear()
        self.route_target = self.route_start = None
        multiworld, player = self.tracker_core.multiworld, self.tracker_core.player_id
        if multiworld is None or player is None or not text.strip():
            return
        regions = multiworld.regions
        locations = set(regions.location_cache[player])
        entrances = set(regions.entrance_cache[player])
        name, usable, _ = Utils.get_intended_text(text, locations | set(regions.region_cache[player]) | entrances)
        if usable and (name in locations or name in DOOR_STEPS & entrances):
            self.route_target = name

    def route_from_exit(self) -> bool:
        """/get_logical_path from the door Link came through last (game_exit) instead of the slot's start: the way
        through the fewest doors to the route's target (ut_map.find_route), logged as UT logs its own. False, with no
        route, when there is none: UT's from the start then."""
        world = self.tracker_core.get_current_world()
        target, spot = self.route_target, self.game_exit
        if world is None or target is None or spot is None or not self.game or \
                (self.stored_data or {}).get("_read_race_mode"):
            return False
        state = self.updateTracker().state
        player = self.tracker_core.player_id
        start = spot_region(spot, self.tracker_core.multiworld.regions.region_cache[player])
        end = world.get_location(target) if target in self.tracker_core.multiworld.regions.location_cache[player] \
            else world.get_entrance(target)
        if state is None or start is None or end.parent_region is None or not end.can_reach(state):
            return False
        route = find_route(start, end.parent_region, state)
        if route is None:
            return False
        self.route_start = spot
        self.logical_path_entrances.extend(way.name for way in route)
        self.print_json([{"type": "text", "text": "From "}, {"type": "color", "color": "blue", "text": door_label(spot)}])

        def explained(text: List[dict], step: Any) -> List[dict]:  # with its rule, as UT logs a step it has no text for
            if hasattr(step.access_rule, "explain_json"):
                return [*text, {"type": "text", "text": ":\n    "}, *step.access_rule.explain_json(state)]
            return text

        for way in route:
            text = world.explain_path(way, state)
            if text is not None:
                self.print_json(text or explained([{"type": "color", "color": "blue", "text": door_label(way.name)}],
                                                  way))
        if isinstance(end, Location):
            self.print_json(explained([{"type": "text", "text": "->"},
                                       {"type": "color", "color": "green", "text": end.name}], end))
        elif world.explain_path(end, state):
            self.print_json(world.explain_path(end, state))
        return True

    def route_points(self, map_name: str) -> Tuple[List[Tuple[float, float, bool]], bool]:
        """The route on a map, and whether its first point is the door it starts at (route_from_exit): the pins of the
        doors it goes through, in order (a door Link comes out of at that door's pin), then the target's pin nearest
        the last door. Each point with whether Link walks the leg to it on this map: not after a door he goes into, nor
        to a door's pin he comes out of a level at, as the map does not show the level in between."""
        if not self.logical_path_entrances and self.route_start is None:
            return [], False
        pins = self.section_pins()
        world = self.tracker_core.get_current_world()
        entrances = self.tracker_core.multiworld.regions.entrance_cache[self.tracker_core.player_id]
        points: List[Tuple[float, float, bool]] = []
        inside = False  # Link is in a level the map does not show

        def add(doors: Tuple[Tuple[str, bool], ...]) -> bool:
            """The first of these doors with a pin on this map, and whether Link comes out of a level at it."""
            nonlocal inside
            for door, out in doors:
                spot = next(((x, y) for m, x, y in pins.get(door, ()) if m == map_name), None)
                if spot is None:
                    continue
                if points and points[-1][:2] == spot:
                    points[-1] = (*spot, points[-1][2])
                else:
                    points.append((*spot, not points or not (inside or out)))
                inside = not out
                return True
            return False

        start = self.route_start
        started = start is not None and add(((start, True),) + (
            ((door_out(world.get_entrance(start)), False),) if start in entrances else ()))
        for name in self.logical_path_entrances:
            if name in DOOR_STEPS:
                add(((name, False), (door_out(world.get_entrance(name)), True)))
        ends = [(x, y) for m, x, y in pins.get(self.route_target or "", ()) if m == map_name]
        if ends:
            last = points[-1][:2] if points else ends[0]
            end = min(ends, key=lambda p: (p[0] - last[0]) ** 2 + (p[1] - last[1]) ** 2)
            if not points or points[-1][:2] != end:  # a door's route ends at the last door it goes through
                points.append((*end, not points or not inside))
        return points, started and bool(points)

    def section_pins(self) -> Dict[str, List[Tuple[str, float, float]]]:
        """The pack's pins (tracker/locations.json) by section name: (map, x, y) each."""
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
        return self.route_pins[1]

    def dungeon_square_pins(self, map_name: str) -> Dict[Tuple[float, float], List[int]]:
        """The island's dungeon check squares (ut_map.dungeon_squares), for ut_gui.load_pins: at each dungeon door's
        pin (Doors' section of its dungeon door entrance), the ids of the server's locations of the dungeon known to
        be behind it, but those the map leaves off."""
        world = self.tracker_core.get_current_world()
        if map_name != OVERWORLD_MAP or world is None:
            return {}
        pins = self.section_pins()
        hidden = set(getattr(world, "ut_map_page_hidden_locations", {}).get(map_name, ()))
        ids = world.location_name_to_id
        checks: Dict[str, List[int]] = {dungeon: [] for dungeon in DUNGEONS}
        for name, dungeon in LOCATION_DUNGEONS.items():
            if name in ids and ids[name] in self.server_locations and ids[name] not in hidden:
                checks[dungeon].append(ids[name])
        squares = {}
        for door, dungeon in dungeon_squares(world.dungeon_entrances, known_doors(self.found_doors)).items():
            spot = next(((x, y) for m, x, y in pins.get(door_entrance(door), ()) if m == map_name), None)
            if spot is not None and checks[dungeon]:
                squares[spot] = checks[dungeon]
        return squares

    def clear_path(self) -> None:
        self.route_target = self.route_start = None
        super().clear_path()
        if self.map_page is not None:
            from .ut_gui import clear_route_search
            clear_route_search(self.map_page)

    def draw_route(self) -> None:
        if self.ui is None or self.map_page is None or self.map_id is None:
            return
        from .ut_gui import draw_route
        draw_route(self.map_page, *self.route_points(self.maps[self.map_id]["name"]), self.ui.loc_size)

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
        """A map pin's tooltip (ut_gui.MapHover): each of its checks (check_label) or doors with its status, a door
        gone through with the door it leads to; the egg's whether the goal is in logic, with the goal's items."""
        from worlds.tracker.TrackerClient import get_ut_color
        world = self.tracker_core.get_current_world()
        if pin.go_mode is not None:
            colour = get_ut_color("in_logic" if pin.go_mode else "out_of_logic")
            return "\n".join([f"Go mode: [color={colour}]{'yes' if pin.go_mode else 'no'}[/color]"]
                             + self.goal_lines(world))
        lines = []
        for key, status in pin.statuses.items():
            name = check_label(world.location_id_to_name.get(key, str(key))) if pin.kind == "location" \
                else door_label(key)
            lines.append(f"{name}: [color={get_ut_color(STATUS_COLOURS.get(status, status))}]{status}[/color]")
            if pin.kind == "entrance" and world is not None and world.get_entrance(key).connected_region is not None:
                lines.append(f"  -> {door_label(door_out(world.get_entrance(key)))}")
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
