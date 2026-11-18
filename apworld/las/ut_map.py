"""Universal Tracker's map tab (DESIGN.md, Universal Tracker map): the data behind tracker/*.json (scripts/ut_map.py
writes them), the door entrances a route goes through and the route search, by the zones' places
(tracker/zone_places.json)."""
import functools
import heapq
import json
import math
import pkgutil
import re
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Tuple

from .items import LASR_TO_ITEM
from .lasr import (CATEGORIES, DOORS, DROP_NAMES, DUNGEON_DOORS, DUNGEON_NAMES, DUNGEON_NUMBERS, DUNGEONS, INSTRUMENTS,
                   LOGIC, WARP_LOCATORS, WARP_MAP, WARP_ZONES, WRITTEN_LOGIC, door_entrance, door_region, drop_route,
                   dungeon_of, dungeon_region, in_dungeon)
from .locations import LOCATION_NAMES
from .logic import FIELD_DOOR, START
from .tracker import LEVEL_KEY

OVERWORLD_MAP = "Koholint Island"


def dungeon_label(number: int) -> str:
    """A dungeon's name on the map tab, its maps and its item section: "D1 - Tail Cave"."""
    return f"D{number} - {DUNGEON_NAMES[number]}"


def check_label(name: str) -> str:
    """A check's name in a pin's tooltip: its location name without the area, which the pin's place shows."""
    return name.partition(" - ")[2] or name


def dungeon_map(dungeon: str) -> str:
    return dungeon_label(DUNGEON_NUMBERS[dungeon])



def hidden_door_entrances(dungeon_entrances: Optional[Mapping[str, str]], known: Iterable[str]) -> Dict[str, List[str]]:
    """Each dungeon map's dungeon door entrances to hide. A dungeon's own door pin inside carries every dungeon door's
    entrance (scripts/ut_map.py): its map shows only that of the door known (tracker.known_doors) to lead into it.
    `dungeon_entrances`: the slot data's, None for dungeons not shuffled."""
    leads = dict(dungeon_entrances) if dungeon_entrances else {door: door for door in DUNGEONS}
    known = set(known)
    return {dungeon_map(dungeon): [door_entrance(door) for door in DUNGEONS
                                   if not (door in known and leads[door] == dungeon)] for dungeon in DUNGEONS}


# AP location name -> its dungeon (lasr.dungeon_of), for the dungeons' locations
LOCATION_DUNGEONS: Dict[str, str] = {name: dungeon_of(loc) for loc, name in LOCATION_NAMES.items() if dungeon_of(loc)}


def dungeon_squares(dungeon_entrances: Mapping[str, str], known: Iterable[str]) -> Dict[str, str]:
    """The island's dungeon check squares: each dungeon door known (tracker.known_doors) to lead into a dungeon, with
    that dungeon, whose checks its square holds, as the pause map's door groups do. `dungeon_entrances`: the world's
    (each door's dungeon, itself when dungeons are not shuffled among themselves)."""
    known = set(known)
    return {door: dungeon_entrances[door] for door in DUNGEONS if door in known}


def hidden_checks(world) -> List[str]:
    """The checks the trackers leave off, by LASR names: the optional ones (LASWorld.optional), and the Trendy Game's
    prizes whose item the slot does not need: the Yoshi Doll without the trade quest, a seashell when neither the
    Seashell Mansion nor a seashell hunt takes seashells, heart pieces and chamber stones always. Universal Tracker's
    map (hidden_locations) and the in-game map (the patch's map_hidden_locations) both leave them off."""
    needed = set()
    if world.options.trade_quest:
        needed.add("yoshi-doll")
    if world.options.seashell_mansion.value or world.goal_seashells:
        needed.add("seashell")
    trendy = [loc for loc in CATEGORIES["Trendy_Rewards"] if LOGIC[loc]["content"] not in needed]
    return [*world.optional, *trendy]


def hidden_locations(world) -> List[str]:
    """The checks Universal Tracker's map leaves off (hidden_checks), by AP names."""
    return [LOCATION_NAMES[loc] for loc in hidden_checks(world)]


# The pack's maps, in its order: the overworld's, then the dungeons' d1 ... d8 and d0
MAPS = (OVERWORLD_MAP, *(dungeon_label(n) for n in range(1, 9)), dungeon_label(0))


def map_index(level: Any) -> int:
    """The map of the level the game shows (LEVEL_KEY): a dungeon's own, else the overworld's."""
    m = re.match(r"Lv(\d\d)", level) if isinstance(level, str) else None
    number = int(m.group(1)) if m else 0
    if 1 <= number <= 8:
        return number
    return len(MAPS) - 1 if number == 10 else 0


# The item panel's sections: (name, LASR items, columns)
ITEM_SECTIONS = (
    ("Items", ("sword", "shield", "bracelet", "boots", "flippers", "feather", "bomb", "shovel", "ocarina", "hookshot",
               "bow", "rod", "boomerang", "powder", "mushroom", "bottle", "red-tunic", "blue-tunic", "seashell",
               "medicine"), 5),
    ("Songs", ("song-ballad", "song-mambo", "song-soul"), 5),
    ("Instruments", INSTRUMENTS, 4),
    ("Keys", ("tail-key", "slime-key", "angler-key", "face-key", "bird-key"), 5),
    ("Trade", ("yoshi-doll", "ribbon", "dog-food", "bananas", "stick", "honeycomb", "pineapple", "hibiscus", "letter",
               "broom", "fishing-hook", "necklace", "mermaid-scale", "magnifying-lens"), 5),
)
# Right of the map, a section per dungeon: its small keys, nightmare key, map, compass and stone beak
DUNGEON_SECTIONS = tuple((dungeon_label(n), (f"key-D{n}", f"nightmare-key-D{n}", f"map-D{n}", f"compass-D{n}",
                                              f"stone-beak-D{n}"), 5) for n in (*range(1, 9), 0))
ITEM_SIZE, DUNGEON_ITEM_SIZE = (40, 40), (32, 32)
# The ways down the overworld's drops (lasr.drop_route): the island's map shows them as doors
DROP_ROUTES = frozenset(drop_route(door) for door in DROP_NAMES)
# The entrances that go through a door (Entrances, Shuffled dungeons): each door's, named after the door, each
# dungeon's door's (lasr.door_entrance), and the drops'
DOOR_STEPS = frozenset(DOORS["pairs"]) | frozenset(DOORS["pairs"].values()) | {door_entrance(d) for d in DUNGEONS} |     DROP_ROUTES


# The region Link stands in once a jump landed on a door's spot, when the door is no shuffled one, or on a warp point's
# (spot_region)
SPOT_ZONES = {**{door: zone for zone, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})},
              **{DOORS["pairs"][DUNGEON_DOORS[dungeon]]: dungeon_region(dungeon) for dungeon in DUNGEONS},
              **{locator: WARP_ZONES[item] for locator, item in WARP_LOCATORS.items()}}


def spot_region(locator: str, regions: Mapping[str, Any]) -> Any:
    """The region of `regions` (by name) Link stands in after a level jump to this door's or warp point's spot (the
    game's DOOR <locator> <to>): a shuffled door's region (lasr.door_region), else the zone that has the door or the
    point (lasr.WARP_LOCATORS). None for others."""
    for name in (door_region(locator), SPOT_ZONES.get(locator)):
        if name in regions:
            return regions[name]
    return None


# The warps Link takes from where he stands (route_jumps), by their entrances: the warp home into Tarin's house, Menu's
# way into the start (DESIGN.md, Warp menu), and Manbo's Mambo onto the warp map, the start's way onto it (Warp points)
WARP_HOME, MAMBO = f"To {START}", f"{START} -> {WARP_MAP}"
JUMP_LABELS = {WARP_HOME: "Warp home", MAMBO: "Manbo's Mambo"}


def route_jumps(world: Any) -> List[Any]:
    """find_route's jumps: (entrance, whether Link can take it in a region). The warp home works anywhere, Manbo's Mambo
    outside a dungeon (in one it leads to the dungeon's entrance: the dungeon's zones' own routes)."""
    tests: Dict[str, Callable[[Any], bool]] = {WARP_HOME: lambda region: True,
                                               MAMBO: lambda region: not in_dungeon(region.name)}
    found = []
    for name, test in tests.items():
        try:
            found.append((world.get_entrance(name), test))
        except KeyError:
            pass
    return found


def warp_step(way: Any) -> Optional[str]:
    """What a step does on the warp map: "into" from a warp point or Manbo's Mambo, "out" onto a warp point, else
    None."""
    if way.connected_region is not None and way.connected_region.name == WARP_MAP:
        return "into"
    return "out" if way.parent_region is not None and way.parent_region.name == WARP_MAP else None


def held_warps(zone: str, state: Any, player: int) -> List[str]:
    """The warp points in a zone whose items `state` has (lasr.WARP_ZONES), in WARP_POINTS' order."""
    return [item for item, at in WARP_ZONES.items() if at == zone and state.has(LASR_TO_ITEM[item], player)]


# A route's costs (find_route), in the levels' units (a tile is 1.5, an overworld screen 15 by 12): a walk between two
# places in a level its length, a door about a screen's walk, a warp about three, a step between places not placed a
# tile's walk
DOOR_COST, WARP_COST, STEP_COST = 15.0, 45.0, 1.5
Place = Tuple[str, float, float]  # a level and a spot in its coordinates (tracker/zone_places.json)


@functools.cache
def zone_places() -> Tuple[Dict[str, Place], Dict[str, Place]]:
    """The regions' places (tracker/zone_places.json, scripts/ut_map.py): each zone's middle, a door's region's (lasr.
    door_region) and the region just inside a dungeon's door (lasr.dungeon_region) at the door's spot; and the warp
    points' spots by their items."""
    data = json.loads(pkgutil.get_data(__name__, "tracker/zone_places.json").decode("utf-8"))
    doors = {door: tuple(spot) for door, spot in data["doors"].items()}
    regions = {zone: tuple(spot) for zone, spot in data["zones"].items()}
    regions.update({door_region(door): spot for door, spot in doors.items()})
    regions.update({dungeon_region(dungeon): doors[DOORS["pairs"][DUNGEON_DOORS[dungeon]]] for dungeon in DUNGEONS
                    if DOORS["pairs"][DUNGEON_DOORS[dungeon]] in doors})
    return regions, {item: tuple(spot) for item, spot in data["warps"].items()}


def walk(here: Optional[Place], there: Optional[Place]) -> Optional[float]:
    """The length of a walk between two places in one level; None for places not in one level or not placed."""
    return math.dist(here[1:], there[1:]) if here is not None and there is not None and here[0] == there[0] else None


def step_cost(way: Any, region: Any, state: Any, jump: bool) -> float:
    """What a route's step costs (DOOR_COST and the others): a warp (`jump`, or onto the warp map from a warp point,
    with the walk to its nearest point whose item Link has); through a door, or to another level; a walk in a level;
    a step to or from a place not placed."""
    regions, warps = zone_places()
    here, there = regions.get(region.name), regions.get(way.connected_region.name)
    if jump or warp_step(way) == "into":
        spots = [warps[item] for item in held_warps(region.name, state, way.player) if item in warps]
        return WARP_COST + min((walk(here, spot) or 0.0 for spot in spots), default=0.0)
    if way.name in DOOR_STEPS or (here is not None and there is not None and here[0] != there[0]):
        return DOOR_COST
    length = walk(here, there)
    return STEP_COST if length is None else max(length, STEP_COST)


def find_route(start: Any, goal: Any, state: Any, jumps: Iterable[Any] = ()) -> Optional[List[Any]]:
    """The entrances from region `start` to region `goal` whose rules `state` passes that cost the least (step_cost):
    the shortest walk, with each door and warp costing a walk of its own; None when there is no way. Each entrance
    starts where the one before ends, but `jumps` (route_jumps), which Link takes from any region its test passes."""
    jumps = list(jumps)
    best = {start: 0}
    came: Dict[Any, Any] = {}  # region -> (the entrance into it, the region it was taken from)
    queue = [(0, 0, start)]
    pushed = 1
    while queue:
        cost, _, region = heapq.heappop(queue)
        if region is goal:
            steps = []
            while region is not start:
                way, region = came[region]
                steps.append(way)
            return steps[::-1]
        if cost > best[region]:
            continue
        ways = [(way, False) for way in region.exits]
        ways += [(way, True) for way, test in jumps if way.parent_region is not region and test(region)]
        for way, jump in ways:
            to = way.connected_region
            if to is None:
                continue
            step = cost + step_cost(way, region, state, jump)
            if step >= best.get(to, step + 1) or not way.access_rule(state):
                continue
            best[to], came[to] = step, (way, region)
            heapq.heappush(queue, (step, pushed, to))
            pushed += 1
    return None


# Universal Tracker's entrance statuses, the strongest first, and the colour each gives a door's pin
DOOR_COLOURS = {"path": "hinted", "passable": "in_logic", "impassable": "out_of_logic"}
OUTSIDE_DOORS, INSIDE_DOORS = frozenset(DOORS["pairs"]), frozenset(DOORS["pairs"].values())


def level_of(inside: str) -> str:
    """The level of an inside door, named after its room (FaceShrineEnter_01B, BedRoom_01A_b)."""
    return re.match(r"(.+?)_\d\d[A-Z]", inside).group(1)


# The connectors' inside doors by level: a connector is a level with more than one overworld door (doors.yml)
CONNECTOR_INSIDES: Dict[str, List[str]] = {}
for _door in DOORS["groups"]["connector"]:
    CONNECTOR_INSIDES.setdefault(level_of(DOORS["pairs"][_door]), []).append(DOORS["pairs"][_door])


_ZONE_OF_DOOR = {door: zone for zone, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})}


def _level_ways() -> Dict[str, set]:
    """The zones a way leads to from each zone (logic.yml), by any rule: the connections and one-ways, and the doors
    that lead to other doors (doors.yml); none through a door to or from the overworld."""
    zone_of = _ZONE_OF_DOOR
    ways: Dict[str, set] = {}
    for zone, node in WRITTEN_LOGIC.items():
        for target in node.get("connections") or {}:
            ways.setdefault(zone, set()).add(target)
            ways.setdefault(target, set()).add(zone)
        for target in node.get("one-way") or {}:
            ways.setdefault(zone, set()).add(target)
    leads = [*DOORS["pairs"].items(), *((b, a) for a, b in DOORS["pairs"].items()), *DOORS["one-way"].items()]
    for door, to in leads:
        if not door.startswith(FIELD_DOOR) and not to.startswith(FIELD_DOOR) and door in zone_of and to in zone_of:
            ways.setdefault(zone_of[door], set()).add(zone_of[to])
    return ways


def _reached(zone: str, ways: Mapping[str, set]) -> set:
    """The zones the ways (_level_ways) lead to from a zone, and the zone."""
    reached, todo = {zone}, [zone]
    while todo:
        for target in ways.get(todo.pop(), ()):
            if target not in reached:
                reached.add(target)
                todo.append(target)
    return reached


def connector_ends() -> Dict[str, List[str]]:
    """Each connector inside door's other ends (CONNECTOR_INSIDES) that a way leads to from its zone (_level_ways)."""
    ways, ends = _level_ways(), {}
    for insides in CONNECTOR_INSIDES.values():
        for inside in insides:
            reached = _reached(_ZONE_OF_DOOR[inside], ways)
            ends[inside] = [other for other in insides if other != inside and _ZONE_OF_DOOR[other] in reached]
    return ends


def drop_ends() -> Dict[str, List[str]]:
    """Each drop's way down (DROP_ROUTES) with the inside doors out of the area it lands in: those of doors.yml's pairs a
    way leads to from the zone it lands in (_level_ways)."""
    ways, insides = _level_ways(), sorted(DOORS["pairs"].values())
    ends = {}
    for drop in DROP_NAMES:
        reached = _reached(_ZONE_OF_DOOR[DOORS["one-way"][drop]], ways)
        ends[drop_route(drop)] = [door for door in insides if _ZONE_OF_DOOR.get(door) in reached]
    return ends


CONNECTOR_ENDS, DROP_ENDS = connector_ends(), drop_ends()


def connector_exits(entrances: Mapping[str, str], door: str) -> List[str]:
    """The AP entrances out of the other inside doors of the connector `door` leads into in a slot (`entrances`, the
    slot data's {door: inside door}) that Link can walk to from where he comes in (CONNECTOR_ENDS): the connector's
    other ends, which a door's pin also shows (ut_gui.load_pins)."""
    inside = entrances.get(door)
    return list(CONNECTOR_ENDS.get(inside, ())) if inside is not None else []


def drop_exits(drop: str, entrances: Iterable[str]) -> List[str]:
    """The AP entrances out of the area a drop (DROP_ROUTES) lands in (DROP_ENDS) that the slot has (`entrances`):
    the drop's ends, which its pin also shows (ut_gui.load_pins). None without shuffled doors."""
    entrances = set(entrances)
    return [door for door in DROP_ENDS.get(drop, ()) if door in entrances]


def door_status(name: str, status: str, statuses: Mapping[str, str]) -> str:
    """An entrance's status on a door's pin with these `statuses`: Universal Tracker's, but a drop (DROP_ROUTES) Link
    can reach is passable on a pin without ends (drop_exits). UT gives an entrance that is not shuffled "passed" once
    reachable."""
    if status == "passed" and name in DROP_ROUTES and not any(other in INSIDE_DOORS for other in statuses):
        return "passable"
    return status


def door_pin_colour(statuses: Mapping[str, str], connected: Callable[[str], bool] = lambda name: False) -> str:
    """A door pin's colour from Universal Tracker's statuses of its entrances (door_status), the strongest
    (DOOR_COLOURS), else collected. On an overworld door's pin, once the player went through the door (`connected`),
    the connector's other ends (connector_exits) he has not gone out of count too: one in logic keeps the pin in logic,
    one out of reach makes it out of logic, and the pin is collected once he has gone out of every end. UT gives an end
    out of reach the same status whether he has gone out of it or not, so `connected` tells them apart. Before he went
    through the door its ends do not count, as they would tell where it leads. A drop's pin counts as gone through
    while Link can reach the drop, as where it leads is known: its ends (drop_exits) count then, and it is collected
    once he has gone through the doors of the area it lands in; out of reach it is out of logic."""
    outside = [name for name in statuses if name in OUTSIDE_DOORS or name in DROP_ROUTES]
    entered = any(statuses[name] != "impassable" if name in DROP_ROUTES else connected(name) for name in outside)
    shown = set()
    for name, status in statuses.items():
        if outside and name in INSIDE_DOORS and (not entered or connected(name)):
            continue  # an end of a door not gone through, or one gone out of
        shown.add(door_status(name, status, statuses))
    return next((colour for status, colour in DOOR_COLOURS.items() if status in shown), "collected")


def door_out(entrance: Any) -> str:
    """Where a door's entrance comes out: the door whose region it leads into (lasr.door_region), else that region."""
    region = entrance.connected_region
    if region is None:
        return "?"
    return region.name[:-5].replace("-", "_") if region.name.endswith("-door") else region.name


def item_layouts(sections, size) -> list:
    return [{"name": name, "items": [LASR_TO_ITEM[item] for item in items], "cols": cols, "item_size": size}
            for name, items, cols in sections]


TRACKER_WORLD: Dict[str, Any] = {
    "map_page_folder": "tracker",
    "external_pack_key": "ut_pack_path",
    "map_page_maps": "maps.json",
    "map_page_locations": "locations.json",
    "map_page_groups": [OVERWORLD_MAP, ("Dungeons", list(MAPS[1:]))],
    "map_page_setting_key": LEVEL_KEY,
    "map_page_index": map_index,
    "item_page_items": "items.json",
    "map_page_item_layouts_left": item_layouts(ITEM_SECTIONS, ITEM_SIZE),
    "map_page_item_layouts_right": item_layouts(DUNGEON_SECTIONS, DUNGEON_ITEM_SIZE),
}
