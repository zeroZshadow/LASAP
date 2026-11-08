"""LASR (LAS Randomizer) data: logic, item pool and location categories, loaded from the bundled YAML files."""
import pkgutil
from typing import Any, Dict, List, Optional

import yaml

from .logic import SPEC, lower_routes

_Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)


def _load_yaml(name: str) -> Any:
    return yaml.load(pkgutil.get_data(__name__, f"data/{name}"), Loader=_Loader)


LOGIC: Dict[str, Dict[str, Any]] = lower_routes(_load_yaml("logic.yml"), _load_yaml("doors.yml"))
_items_yml = _load_yaml("items.yml")
ITEM_DEFS: Dict[str, Dict[str, Any]] = _items_yml["Item_Pool"]
CATEGORIES: Dict[str, List[str]] = _load_yaml("locations.yml")

# The companions the logic's companion locations hold, which items.yml does not have.
COMPANION_ITEMS = {"bow-wow": {"type": "important", "quantity": 1, "item-key": "None"},
                   "rooster": {"type": "important", "quantity": 1, "item-key": "None"}}
ITEM_DEFS.update(COMPANION_ITEMS)
if set(ITEM_DEFS) != set(SPEC["items"]):
    raise ValueError(f"data/logic_spec.yml and items.yml name other items: {sorted(set(ITEM_DEFS) ^ set(SPEC['items']))}")

ITEM_LOCATIONS: List[str] = [k for k, v in LOGIC.items() if v["type"] == "item"]

# Dungeon number used in LASR names (D1..D8, D0 = Color Dungeon) -> in-game dungeon level index.
# Same as LASR shuffler: level = number - 1, and the Color Dungeon is level 9.
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

# The counted groups of data/logic_spec.yml: "instruments:N" holds with N different instruments (logic.py, counted),
# which the egg's condition reads.
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


def goal_logic(instruments_required: int, logic: Dict[str, Dict[str, Any]] = LOGIC,
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


# Dungeon entrance shuffle. A dungeon's door leads into a region node (dungeon_region) whose conditions are the door's:
# the dungeon's own region, or for the Face Shrine, whose region is either of its entrance rooms (D6-8D, D6-3A, which
# its side entrance reaches), the region in front of D6-8D.
# Both come from data/logic_spec.yml, in the dungeons' order.
DUNGEONS = tuple(SPEC["dungeons"])
FACE_SHRINE_FRONT = SPEC["dungeons"]["face-shrine"]


def _conditions(node: Dict[str, Any]) -> Dict[str, Any]:
    return {k: v for k, v in node.items() if k.startswith("condition-")}


def _without_conditions(node: Dict[str, Any]) -> Dict[str, Any]:
    return {k: v for k, v in node.items() if not k.startswith("condition-")}


def dungeon_region(dungeon: str) -> str:
    """The region a dungeon's door leads into, which holds the door's conditions."""
    return SPEC["dungeons"][dungeon]


def _door(logic: Dict[str, Dict[str, Any]], dungeon: str) -> Dict[str, Any]:
    """The conditions of a dungeon's overworld door."""
    return _conditions(logic[dungeon_region(dungeon)])


def dungeon_logic(logic: Dict[str, Dict[str, Any]], entrances: Dict[str, str]) -> Dict[str, Dict[str, Any]]:
    """The logic with dungeon doors leading elsewhere: entrances maps each dungeon's door to the dungeon it leads into
    (the patch's dungeon_entrances), whose dungeon_region then needs that door."""
    if all(door == dungeon for door, dungeon in entrances.items()):
        return logic
    assert sorted(entrances) == sorted(DUNGEONS) and sorted(entrances.values()) == sorted(DUNGEONS), entrances
    shuffled = dict(logic)
    for door, dungeon in entrances.items():
        region = dungeon_region(dungeon)
        shuffled[region] = {**_without_conditions(logic[region]), **_door(logic, door)}
    return shuffled


def dungeon_name(dungeon: str) -> str:
    """The game's name of a dungeon (DUNGEONS name): Tail Cave, ..., Color Dungeon."""
    return DUNGEON_NAMES[(DUNGEONS.index(dungeon) + 1) % len(DUNGEONS)]


def door_node(dungeon: str) -> str:
    """The logic node of a dungeon's overworld door (door_logic)."""
    return f"{dungeon}-door"


def door_entrance(dungeon: str) -> str:
    """The AP entrance through a dungeon's door, into the dungeon it leads into."""
    return f"{dungeon_name(dungeon)} Entrance"


def door_logic(logic: Dict[str, Dict[str, Any]]) -> Dict[str, Dict[str, Any]]:
    """The logic with each dungeon's door a region of its own (door_node), holding the door's conditions, and the
    regions doors lead into (dungeon_region) entered only through a door's entrance: they keep no conditions. The same
    logic as dungeon_logic's for any map of the doors, with the map in the entrances."""
    doors = dict(logic)
    for dungeon in DUNGEONS:
        assert door_node(dungeon) not in logic, dungeon
        doors[door_node(dungeon)] = {"type": "region", **_door(logic, dungeon)}
        region = dungeon_region(dungeon)
        doors[region] = _without_conditions(logic[region])
    return doors


DOOR_LOGIC = door_logic(LOGIC)
# Regions with no way in but a door's entrance
ENTERED_BY_DOOR = frozenset(dungeon_region(dungeon) for dungeon in DUNGEONS)


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
    return DUNGEONS[(number - 1) % len(DUNGEONS)] if number >= 0 else None


def item_dungeon(item: str) -> int:
    """Dungeon number of a LASR dungeon item name (key-D3, map-D0, ...), or -1."""
    if len(item) > 3 and item[-3] == "-" and item[-2] == "D" and item[-1].isdigit():
        return int(item[-1])
    return -1
