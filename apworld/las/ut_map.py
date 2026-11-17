"""Universal Tracker's map tab (DESIGN.md, Universal Tracker map): the data behind tracker/*.json (scripts/ut_map.py
writes them), the door entrances a route goes through and the route search."""
import heapq
import re
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional

from .items import LASR_TO_ITEM
from .lasr import (CATEGORIES, DOORS, DUNGEON_DOORS, DUNGEON_NAMES, DUNGEON_NUMBERS, DUNGEONS, INSTRUMENTS, LOGIC,
                   WRITTEN_LOGIC, door_entrance, door_region, dungeon_of, dungeon_region)
from .locations import LOCATION_NAMES
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
# The entrances that go through a door (Entrances, Shuffled dungeons): each door's, named after the door, and each
# dungeon's door's (lasr.door_entrance)
DOOR_STEPS = frozenset(DOORS["pairs"]) | frozenset(DOORS["pairs"].values()) | {door_entrance(d) for d in DUNGEONS}


# A route's step through a door costs more than any number of steps between zones: the fewest doors first
DOOR_COST = 1000
# The region Link stands in once a jump landed on a door's spot, when the door is no shuffled one (spot_region)
SPOT_ZONES = {**{door: zone for zone, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})},
              **{DOORS["pairs"][DUNGEON_DOORS[dungeon]]: dungeon_region(dungeon) for dungeon in DUNGEONS}}


def spot_region(locator: str, regions: Mapping[str, Any]) -> Any:
    """The region of `regions` (by name) Link stands in after a level jump to this door's spot (the game's DOOR
    <locator> <to>): a shuffled door's region (lasr.door_region), else the zone that has the door. None for others."""
    for name in (door_region(locator), SPOT_ZONES.get(locator)):
        if name in regions:
            return regions[name]
    return None


def find_route(start: Any, goal: Any, state: Any) -> Optional[List[Any]]:
    """The entrances from region `start` to region `goal` whose rules `state` passes, through the fewest doors
    (DOOR_STEPS); None when there is no way."""
    best = {start: 0}
    came: Dict[Any, Any] = {}
    queue = [(0, 0, start)]
    pushed = 1
    while queue:
        cost, _, region = heapq.heappop(queue)
        if region is goal:
            steps = []
            while region is not start:
                steps.append(came[region])
                region = came[region].parent_region
            return steps[::-1]
        if cost > best[region]:
            continue
        for way in region.exits:
            to = way.connected_region
            step = cost + (DOOR_COST if way.name in DOOR_STEPS else 1)
            if to is None or step >= best.get(to, step + 1) or not way.access_rule(state):
                continue
            best[to], came[to] = step, way
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


def connector_exits(entrances: Mapping[str, str], door: str) -> List[str]:
    """The AP entrances out of the other inside doors of the connector `door` leads into in a slot (`entrances`, the
    slot data's {door: inside door}): the connector's other ends, which a door's pin also shows (ut_gui.load_pins)."""
    inside = entrances.get(door)
    if inside is None or inside not in INSIDE_DOORS:
        return []
    return [other for other in CONNECTOR_INSIDES.get(level_of(inside), ()) if other != inside]


def door_pin_colour(statuses: Mapping[str, str], connected: Callable[[str], bool] = lambda name: False) -> str:
    """A door pin's colour from Universal Tracker's statuses of its entrances, the strongest (DOOR_COLOURS), else
    collected. On an overworld door's pin, once the player went through the door (`connected`), the connector's other
    ends (connector_exits) he has not gone out of count too: one in logic keeps the pin in logic, one out of reach makes
    it out of logic, and the pin is collected once he has gone out of every end. UT gives an end out of reach the same
    status whether he has gone out of it or not, so `connected` tells them apart. Before he went through the door its
    ends do not count, as they would tell where it leads."""
    outside = [name for name in statuses if name in OUTSIDE_DOORS]
    entered = any(connected(name) for name in outside)
    shown = set()
    for name, status in statuses.items():
        if outside and name in INSIDE_DOORS and (not entered or connected(name)):
            continue  # an end of a door not gone through, or one gone out of
        shown.add(status)
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
