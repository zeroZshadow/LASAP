"""LASR (LAS Randomizer) data: logic, item pool and location categories, loaded from the bundled YAML files."""
import json
import pkgutil
import re
from typing import Any, Dict, Iterable, List, Mapping, Optional, Set, Tuple

import yaml

from .logic import ALWAYS, SHUFFLE_GROUPS, SPEC, START, house_exits, lower_routes, start_doors, start_pockets

_Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)


def load_yaml(name: str) -> Any:
    return yaml.load(pkgutil.get_data(__name__, f"data/{name}"), Loader=_Loader)


def load_json(name: str) -> Any:
    return json.loads(pkgutil.get_data(__name__, f"data/{name}").decode("utf-8"))


WRITTEN_LOGIC: Dict[str, Dict[str, Any]] = load_yaml("logic.yml")  # with its routes, as logic.yml writes them
DOORS: Dict[str, Any] = load_yaml("doors.yml")
# Player-facing names of the shuffled overworld doors, by door: the spoiler, plando and the tracker show them (door_label)
DOOR_NAMES: Dict[str, str] = load_yaml("door_names.yml")

DUNGEONS = tuple(SPEC["dungeons"])
# Each dungeon's number in location and item names (D1-..., key-D1) and in its inside's level (Lv01 ... Lv08): D1 Tail
# Cave ... D8 Turtle Rock, D0 the Color Dungeon (Lv10).
DUNGEON_NUMBERS: Dict[str, int] = {dungeon: (index + 1) % len(DUNGEONS) for index, dungeon in enumerate(DUNGEONS)}
NUMBERED_DUNGEONS: Dict[int, str] = {number: dungeon for dungeon, number in DUNGEON_NUMBERS.items()}


def _inside_dungeon(door: str) -> str:
    """The dungeon a door of doors.yml's `dungeon` group leads into by default, by its inside's level."""
    return NUMBERED_DUNGEONS[int(DOORS["pairs"][door][2:4]) % 10]


DUNGEON_DOORS: Dict[str, str] = {_inside_dungeon(door): door for door in DOORS["groups"]["dungeon"]}
if sorted(DUNGEON_DOORS) != sorted(DUNGEONS):
    raise ValueError(f"doors.yml's dungeon group needs one door per dungeon: {sorted(DUNGEON_DOORS)}")


def _without_conditions(node: Dict[str, Any]) -> Dict[str, Any]:
    return {k: v for k, v in node.items() if not k.startswith("condition-")}


def dungeon_region(dungeon: str) -> str:
    """The region just inside a dungeon's door."""
    return SPEC["dungeons"][dungeon]


_DUNGEON_ZONE = re.compile(r"D\d\.")  # a dungeon level's zones: D1.a (logic-studio/game.yml, zone_prefixes)


def in_dungeon(region: str) -> bool:
    """Whether a region is inside a dungeon: one of a dungeon level's zones, or the region just inside its door."""
    return bool(_DUNGEON_ZONE.match(region)) or region in SPEC["dungeons"].values()


def slot_doors(entrances: Optional[Dict[str, str]] = None,
               dungeon_entrances: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
    """doors.yml with a slot's doors: the overworld doors leading as its entrances say, and each dungeon's door into the
    dungeon its dungeon_entrances say."""
    pairs = dict(entrances or {})
    for door, dungeon in (dungeon_entrances or {}).items():
        if door != dungeon:
            pairs[DUNGEON_DOORS[door]] = DOORS["pairs"][DUNGEON_DOORS[dungeon]]
    return {**DOORS, "pairs": {**DOORS["pairs"], **pairs}} if pairs else DOORS


def _with_dungeon_regions(written: Dict[str, Dict[str, Any]], *extra: str) -> Dict[str, Dict[str, Any]]:
    nodes = dict(written)
    for name in extra:
        nodes.setdefault(name, {"type": "region"})
    return nodes


_ENTERED = {DOORS["pairs"][DUNGEON_DOORS[dungeon]]: dungeon_region(dungeon) for dungeon in DUNGEONS}


def plain_logic(written: Dict[str, Dict[str, Any]], doors: Dict[str, Any]) -> Dict[str, Dict[str, Any]]:
    """The routes lowered, each dungeon's door leading into the region just inside it."""
    return lower_routes(_with_dungeon_regions(written, *_ENTERED.values()), doors, _ENTERED)


LOGIC: Dict[str, Dict[str, Any]] = plain_logic(WRITTEN_LOGIC, DOORS)


ITEM_DEFS: Dict[str, Dict[str, Any]] = load_yaml("items.yml")["Item_Pool"]
CATEGORIES: Dict[str, List[str]] = load_yaml("locations.yml")

# The companions the logic's companion locations hold, which items.yml does not have.
COMPANION_ITEMS = {"bow-wow": {"type": "important", "quantity": 1, "item-key": "None"},
                   "rooster": {"type": "important", "quantity": 1, "item-key": "None"}}
ITEM_DEFS.update(COMPANION_ITEMS)
# The items that open the overworld's warp points (option warp_points; DESIGN.md, Warp points), each with its point's
# WarpPoint.gsheet index. With the option on the pool holds each once (LASWorld.item_pool), else none.
WARP_POINTS: Dict[str, int] = {
    f"warp-{name}": index for index, name in enumerate(
        ("ukuku-prairie", "turtle-rock", "taltal-heights", "animal-village", "manbos-pond", "dampes-shack",
         "seashell-mansion", "taltal-east", "marthas-bay", "taltal-west"))}
WARP_ITEMS = {item: {"type": "important", "quantity": 0, "item-key": f"$WARP:{index}"}
              for item, index in WARP_POINTS.items()}
ITEM_DEFS.update(WARP_ITEMS)
# The warp map's region (logic.yml): a warp point and Manbo's Mambo lead onto it, and it on to each open point's zone
WARP_MAP = "warp-map"
# Each warp point's zone, by its item: where the warp map's way out with that item leads
WARP_ZONES: Dict[str, str] = {item: zone for zone, rule in WRITTEN_LOGIC[WARP_MAP]["one-way"].items()
                              for item in re.findall(r"warp-[a-z-]+", str(rule)) if item in WARP_POINTS}
if set(WARP_ZONES) != set(WARP_POINTS):
    raise ValueError(f"logic.yml's {WARP_MAP} leads nowhere with {sorted(set(WARP_POINTS) - set(WARP_ZONES))}")
if set(ITEM_DEFS) != set(SPEC["items"]):
    raise ValueError(f"data/logic_spec.yml and items.yml name other items: {sorted(set(ITEM_DEFS) ^ set(SPEC['items']))}")

ITEM_LOCATIONS: List[str] = [k for k, v in LOGIC.items() if v["type"] == "item"]

# LASR dungeon number (D1..D8, D0 the Color Dungeon) -> the game's level index.
DUNGEON_LEVEL = {n: (9 if n == 0 else n - 1) for n in range(9)}
DUNGEON_NAMES = {
    1: "Tail Cave",
    2: "Bottle Grotto",
    3: "Key Cavern",
    4: "Angler's Tunnel",
    5: "Catfish's Maw",
    6: "Face Shrine",
    7: "Eagle's Tower",
    8: "Turtle Rock",
    0: "Color Dungeon",
}

# "instruments:N" holds with N different instruments of the group.
COUNTED = {name: tuple(members) for name, members in SPEC["counted"].items()}
INSTRUMENTS_TOKEN = "instruments"
INSTRUMENTS = COUNTED[INSTRUMENTS_TOKEN]
INSTRUMENT_LOCATIONS = tuple(f"D{i}-instrument" for i in range(1, 9))
DUNGEON_REWARD_LOCATIONS = INSTRUMENT_LOCATIONS + ("D0-fairy-1", "D0-fairy-2")
# The shop's slots, bought with rupees. LASR shuffles those of locations.yml's Shop_Items; slot 5 sells its Chamber
# Stones one after another and keeps them.
SHOP_LOCATIONS = ("shop-slot3-1st", "shop-slot3-2nd", *(f"shop-slot5-{n}" for n in ("1st", "2nd", "3rd", "4th", "5th", "6th", "7th")),
                  "shop-slot6")
# Bow-wow and the rooster join by an event, not as an item: the patch cannot put an AP item there (las_ap_patch.COMPANION_FLAGS).
COMPANION_LOCATIONS = ("moblin-cave", "rooster-statue")
# What the egg's condition takes to play the Ballad there; the egg's node, and the goal's: the Nightmare beaten.
BALLAD = ("ocarina", "song-ballad")
EGG = SPEC["egg"]
GOAL_NODE = SPEC["goal"]


def goal_logic(instruments_required: int, logic: Dict[str, Dict[str, Any]],
               seashells_required: Optional[int] = None) -> Dict[str, Dict[str, Any]]:
    """The logic with the Wind Fish's egg opening at this many instruments, or on a seashell hunt (seashells not None)
    at that many secret seashells and no instruments. On a hunt the egg opens by itself, without the Ballad, and its
    door leads to the ending: the goal is the open egg."""
    if instruments_required >= len(INSTRUMENTS) and seashells_required is None:
        return logic
    parts = [part.strip() for part in logic[EGG]["condition-basic"].split("&")]
    assert set(INSTRUMENTS) <= set(parts) and set(BALLAD) <= set(parts), parts
    parts = [part for part in parts if part not in INSTRUMENTS]
    if seashells_required is not None:
        parts = [part for part in parts if part not in BALLAD] + [f"seashell:{seashells_required}"]
    else:
        parts.append(f"{INSTRUMENTS_TOKEN}:{instruments_required}")
    goal = {**logic, EGG: {**logic[EGG], "condition-basic": " & ".join(parts)}}
    if seashells_required is not None:
        goal[GOAL_NODE] = {"type": logic[GOAL_NODE]["type"], "condition-basic": EGG}
    return goal


def dungeon_name(dungeon: str) -> str:
    """The game's name of a dungeon (DUNGEONS name): Tail Cave, ..., Color Dungeon."""
    return DUNGEON_NAMES[DUNGEON_NUMBERS[dungeon]]


def door_node(dungeon: str) -> str:
    return f"{dungeon}-door"


def door_entrance(dungeon: str) -> str:
    return f"{dungeon_name(dungeon)} Entrance"


def door_region(door: str) -> str:
    """The region of a shuffled door: its way in, where its partner's way out lands. Rule names have no `_`."""
    return f"{door.replace('_', '-')}-door"


# Random start: the plando connections' and the spoiler's name of the house's way out, which leads out of the start door
HOUSE_EXIT = "Tarin's House Exit"
_DOOR_OF_INSIDE = {DOORS["pairs"][door]: door for door in DOOR_NAMES}
EXIT_SUFFIX = " Exit"  # an inside door's name: its overworld door's, then this
_DOOR_BY_LABEL = {**{f"{name}{EXIT_SUFFIX}".lower(): inside for inside, door in _DOOR_OF_INSIDE.items()
                     for name in (DOOR_NAMES[door],)},
                  **{name.lower(): door for door, name in DOOR_NAMES.items()}}
_DOOR_BY_LABEL.pop(HOUSE_EXIT.lower())  # the name of the start's way out (Random start), not of the house's inside door


def zone_of(door: str) -> str:
    """The logic zone that lists a door (logic.yml)."""
    (zone,) = [name for name, node in WRITTEN_LOGIC.items() if door in (node.get("doors") or {})]
    return zone


def dungeon_route(door: str) -> str:
    """The world's way into a dungeon through another overworld door (doors.yml's dungeon-other): from the zone
    outside the door to the zone inside, an entrance of the logic's routes."""
    return f"{zone_of(door)} -> {zone_of(DOORS['pairs'][door])}"


_DOOR_OF_ROUTE = {dungeon_route(door): door for door in DOORS["groups"]["dungeon-other"]}


def door_label(door: str) -> str:
    """A door's player-facing name: door_names.yml's for a shuffled overworld door, `<that name> Exit` for the inside
    door it leads to in the game, the door's for a way into a dungeon through another door (dungeon_route). Other
    names, and doors.yml's own, stay as they are: the world, the seed file and the tracker's data go by doors.yml's
    names."""
    door = _DOOR_OF_ROUTE.get(door, door)
    if door in DOOR_NAMES:
        return DOOR_NAMES[door]
    if door in _DOOR_OF_INSIDE:
        return DOOR_NAMES[_DOOR_OF_INSIDE[door]] + EXIT_SUFFIX
    return door


def door_of_label(name: str) -> str:
    """The doors.yml name a player-facing door name (door_label) stands for, any case; other names as they are."""
    return _DOOR_BY_LABEL.get(name.lower(), name)


def door_logic(written: Dict[str, Dict[str, Any]], doors: Dict[str, Any],
               open_doors: Iterable[str] = ()) -> Dict[str, Dict[str, Any]]:
    """The routes lowered with each closed dungeon's door a region of its own (door_node) and the region just inside
    it (dungeon_region) reached only through the door's entrance, without conditions. Each of `open_doors` and its
    inside door get a door_region leading on to the door's zone; the world connects those regions as the slot's
    entrances pair them, so the rules do not depend on where the doors lead. A way that drops onto such a door's spot
    leads to its zone."""
    open_doors = list(open_doors)
    closed = closed_dungeons(open_doors)
    through = {DUNGEON_DOORS[dungeon]: door_node(dungeon) for dungeon in closed}
    for dungeon in DUNGEONS:
        assert door_node(dungeon) not in written, dungeon
    nodes = _with_dungeon_regions(written, *_ENTERED.values(), *through.values())
    zones = {door: zone for zone, node in written.items() for door in (node.get("doors") or {})}
    for door in open_doors:
        for side in (door, doors["pairs"][door]):
            through[side] = door_region(side)
            assert through[side] not in written, through[side]
            nodes[through[side]] = {"type": "region", "one-way": {_ENTERED.get(side, zones.get(side)): ALWAYS}}
    lowered = lower_routes(nodes, doors, _ENTERED, through)
    for dungeon in closed:
        lowered[dungeon_region(dungeon)] = _without_conditions(lowered[dungeon_region(dungeon)])
    return lowered


def closed_dungeons(open_doors: Iterable[str]) -> List[str]:
    """The dungeons whose door is no door of `open_doors`: entered only through their door's entrance (door_logic)."""
    opened = set(open_doors)
    return [dungeon for dungeon in DUNGEONS if DUNGEON_DOORS[dungeon] not in opened]


def entered_by_door(open_doors: Iterable[str] = ()) -> frozenset:
    """The regions with no way in but a dungeon door's entrance: those just inside the closed_dungeons."""
    return frozenset(dungeon_region(dungeon) for dungeon in closed_dungeons(open_doors))


DOOR_LOGIC = door_logic(WRITTEN_LOGIC, DOORS)


def entrance_logic(entrances: Optional[Dict[str, str]], dungeon_entrances: Optional[Dict[str, str]] = None,
                   start: Optional[str] = None) -> Dict[str, Dict[str, Any]]:
    """DOOR_LOGIC with a slot's doors (slot_doors): its overworld entrances (Entrances) and its dungeons', and with
    the house's door swapped with the `start` door (Random start, START_DOORS; logic.start_doors)."""
    doors = start_doors(WRITTEN_LOGIC, slot_doors(entrances, dungeon_entrances), start)
    if doors is DOORS:
        return DOOR_LOGIC
    return door_logic(WRITTEN_LOGIC, doors)


def slot_logic(dungeon_entrances: Optional[Dict[str, str]], open_doors: Iterable[str] = (),
               start: Optional[str] = None) -> Dict[str, Dict[str, Any]]:
    """The logic with a slot's doors. With Entrances (`open_doors`: its door pool) where those doors lead stays out of
    the rules (door_logic); else the routes are lowered as the dungeon doors and the start lead (entrance_logic)."""
    open_doors = list(open_doors)
    if open_doors:
        return door_logic(WRITTEN_LOGIC, slot_doors(None, dungeon_entrances), open_doors)
    return entrance_logic(None, dungeon_entrances, start)


class PlacementError(Exception):
    """No placement in every try: a generation failure, which fuzzers count, not an option error they set aside."""


def shuffle_pool(doors: Mapping[str, Any]) -> List[str]:
    """The overworld doors Entrances moves: doors.yml's SHUFFLE_GROUPS, the door outside the house among them."""
    return [door for group in SHUFFLE_GROUPS for door in doors["groups"][group]]


# door_logic's open_doors for Entrances; DUNGEON_POOL adds the dungeons' doors for Shuffled dungeons everything.
ENTRANCE_POOL: List[str] = shuffle_pool(DOORS)
DUNGEON_POOL: List[str] = ENTRANCE_POOL + [door for door in DOORS["groups"]["dungeon"] if door not in ENTRANCE_POOL]

# Random start: {overworld door: its zone} for each door the house's door may be swapped with (logic.house_exits)
START_DOORS: Dict[str, str] = house_exits(WRITTEN_LOGIC, DOORS)
# Random start: the zones of START_DOORS that _starts leaves out, as a start there never gets out (logic.start_pockets)
START_POCKETS: Set[str] = start_pockets(WRITTEN_LOGIC, START_DOORS)
# The house's door, and the overworld door outside it, which leads into it without Random start
(HOUSE_DOOR,) = WRITTEN_LOGIC[START]["doors"]
HOME_DOOR = next(door for door, inside in DOORS["pairs"].items() if inside == HOUSE_DOOR)


def start_pairs(start: Optional[str]) -> Dict[str, str]:
    """{overworld door: the inside door it leads to} for the doors a start moves (logic.start_doors): the start door
    into the house, the door outside the house into the start door's inside. None for no start or Tarin's own."""
    moved = start_doors(WRITTEN_LOGIC, DOORS, start)["pairs"]
    return {door: inside for door, inside in moved.items() if inside != DOORS["pairs"][door]}


# Regions with no way in but a door's entrance, with the dungeons' doors out of the door pool
ENTERED_BY_DOOR = entered_by_door()


def shuffle_dungeons(random) -> Dict[str, str]:
    """A random dungeon_entrances map, as LASR's shuffler makes it."""
    targets = list(DUNGEONS)
    random.shuffle(targets)
    return dict(zip(DUNGEONS, targets))


TRAPS = ("zap-trap", "drown-trap", "squish-trap", "deathball-trap", "quake-trap")


def location_dungeon(location: str) -> int:
    """Dungeon number of a LASR location name (D1-..., D0-...), or -1."""
    if len(location) > 2 and location[0] == "D" and location[1].isdigit() and location[2] == "-":
        return int(location[1])
    return -1


def dungeon_of(location: str) -> Optional[str]:
    """The dungeon (DUNGEONS name) a location's name puts it in: D1-... Tail Cave, ..., D0-... the Color Dungeon; None
    outside the dungeons. A location's region need not be its dungeon's."""
    number = location_dungeon(location)
    return NUMBERED_DUNGEONS[number] if number >= 0 else None


def item_dungeon(item: str) -> int:
    """Dungeon number of a LASR dungeon item name (key-D3, map-D0, ...), or -1."""
    if len(item) > 3 and item[-3] == "-" and item[-2] == "D" and item[-1].isdigit():
        return int(item[-1])
    return -1


def dungeon_item_kind(item: str) -> Optional[str]:
    """A LASR dungeon item's kind (key-D3: key, nightmare-key, map, compass, stone-beak), or None."""
    return item[:-3] if item_dungeon(item) >= 0 else None

