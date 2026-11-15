"""Universal Tracker in the client (DESIGN.md, Universal Tracker map): the context it gives the client, and the
client's changes to it."""
import asyncio
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import Utils
from CommonClient import ClientCommandProcessor, CommonContext, logger
from NetUtils import NetworkItem

from . import emulators
from .install import tracker_images
from .items import ITEM_TABLE
from .tracker import known_doors
from .ut_map import DOOR_STEPS, door_out, hidden_door_entrances

TRACKER_DOORS_KEY = "tracker_doors_shown"  # Archipelago's persistent storage, under "las": TrackerMixin.show_doors
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
        # The Tracker tab's route (route_points): its target check, the locations in logic, and the pins by section
        self.route_target: Optional[str] = None
        self.tracker_in_logic: List[str] = []
        self.route_pins: Optional[Tuple[int, Dict[str, List[Tuple[str, float, float]]]]] = None
        # whether the map tab shows the doors' pins (its Entrances box), kept between runs
        self.tracker_doors_shown: bool = Utils.persistent_load().get("las", {}).get(TRACKER_DOORS_KEY, True)
        # the dungeon door entrances each map hid when the map shown was loaded (load_map, refresh_door_pins)
        self.tracker_doors_hidden: Dict[str, List[str]] = {}

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
        """Universal Tracker's map, without the doors' pins while its Entrances box is off: the world's
        ut_map_page_hidden_entrances, which it reads on loading a map, then lists every entrance. A dungeon's map
        shows only the dungeon door entrance known to lead into it (ut_map.hidden_door_entrances)."""
        world = self.tracker_core.get_current_world()
        if world is not None:
            names = [] if self.tracker_doors_shown else list(
                self.tracker_core.multiworld.regions.entrance_cache[self.tracker_core.player_id])
            self.tracker_doors_hidden = doors = self.hidden_doors()
            world.ut_map_page_hidden_entrances = {m["name"]: names + doors.get(m["name"], []) for m in self.maps or ()}
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
        """Universal Tracker's sweep, keeping the locations it finds in logic for the Tracker tab's search, then the
        route drawn again (a path found or cleared runs one)."""
        tracker_state = super().updateTracker()
        self.tracker_in_logic = sorted(tracker_state.in_logic_locations)
        self.draw_route()
        return tracker_state

    def route_to(self, location: str) -> None:
        self.command_processor(self)._cmd_get_logical_path(location)

    def start_route(self, text: str) -> None:
        """Before /get_logical_path: no route, so that one it cannot find leaves none, and the location the route ends
        at (route_points), the one UT takes the name for when it matches it to locations, regions and entrances."""
        self.logical_path_entrances.clear()
        self.route_target = None
        multiworld, player = self.tracker_core.multiworld, self.tracker_core.player_id
        if multiworld is None or player is None or not text.strip():
            return
        regions = multiworld.regions
        locations = set(regions.location_cache[player])
        names = locations | set(regions.region_cache[player]) | set(regions.entrance_cache[player])
        name, usable, _ = Utils.get_intended_text(text, names)
        if usable and name in locations:
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
            points.append(min(ends, key=lambda p: (p[0] - last[0]) ** 2 + (p[1] - last[1]) ** 2))
        return points

    def clear_path(self) -> None:
        self.route_target = None
        super().clear_path()

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
        if self.map_id is not None:
            self.reload_map()
