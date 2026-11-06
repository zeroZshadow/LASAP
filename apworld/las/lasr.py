"""LASR (LAS Randomizer) data: logic, item pool and location categories, loaded from the bundled YAML files."""
import pkgutil
from typing import Any, Dict, List, Optional

import yaml

_Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)


def _load_yaml(name: str) -> Any:
    return yaml.load(pkgutil.get_data(__name__, f"data/{name}"), Loader=_Loader)


LOGIC: Dict[str, Dict[str, Any]] = _load_yaml("logic.yml")
_items_yml = _load_yaml("items.yml")
ITEM_DEFS: Dict[str, Dict[str, Any]] = _items_yml["Item_Pool"]
CATEGORIES: Dict[str, List[str]] = _load_yaml("locations.yml")

# The companions the logic's companion locations hold, which items.yml does not have.
COMPANION_ITEMS = {"bow-wow": {"type": "important", "quantity": 1, "item-key": "None"},
                   "rooster": {"type": "important", "quantity": 1, "item-key": "None"}}
ITEM_DEFS.update(COMPANION_ITEMS)

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

INSTRUMENTS = (
    "full-moon-cello",
    "conch-horn",
    "sea-lilys-bell",
    "surf-harp",
    "wind-marimba",
    "coral-triangle",
    "evening-calm-organ",
    "thunder-drum",
)
INSTRUMENT_LOCATIONS = tuple(f"D{i}-instrument" for i in range(1, 9))
DUNGEON_REWARD_LOCATIONS = INSTRUMENT_LOCATIONS + ("D0-fairy-1", "D0-fairy-2")

# The count token of the egg's condition: "instruments:N" holds with N different instruments (logic.py, counted).
INSTRUMENTS_TOKEN = "instruments"
COUNTED = {INSTRUMENTS_TOKEN: INSTRUMENTS}
# What the egg's condition takes to play the Ballad there, and the goal's node: the Nightmare beaten.
BALLAD = ("ocarina", "song-ballad")
GOAL_NODE = "kill-nightmare"


def goal_logic(instruments_required: int, logic: Dict[str, Dict[str, Any]] = LOGIC,
               seashells_required: Optional[int] = None) -> Dict[str, Dict[str, Any]]:
    """The logic with the Wind Fish's egg opening at this many instruments, or on a seashell hunt (seashells not None)
    at that many secret seashells and no instruments. On a hunt the egg opens by itself, without the Ballad, and its
    door leads to the ending: the goal is the open egg."""
    if instruments_required >= len(INSTRUMENTS) and seashells_required is None:
        return logic
    parts = [part.strip() for part in logic["egg"]["condition-basic"].split("&")]
    assert set(INSTRUMENTS) <= set(parts) and set(BALLAD) <= set(parts), parts
    parts = [part for part in parts if part not in INSTRUMENTS]
    if seashells_required is not None:
        parts = [part for part in parts if part not in BALLAD] + [f"seashell:{seashells_required}"]
    else:
        parts.append(f"{INSTRUMENTS_TOKEN}:{instruments_required}")
    goal = {**logic, "egg": {**logic["egg"], "condition-basic": " & ".join(parts)}}
    if seashells_required is not None:
        goal[GOAL_NODE] = {"type": logic[GOAL_NODE]["type"], "condition-basic": "egg"}
    return goal


# Dungeon entrance shuffle. The dungeons' region nodes are their insides, entered through their overworld door: the
# region's conditions are the door's. The Face Shrine's region is either of its entrance rooms (D6-8D, D6-3A, which
# its side entrance reaches); its door is the outside part of D6-8D's conditions.
DUNGEONS = ("tail-cave", "bottle-grotto", "key-cavern", "angler-tunnel", "catfish-maw", "face-shrine", "eagle-tower",
            "turtle-rock", "color-dungeon")
FACE_SHRINE_ROOM = "D6-8D"
_FACE_SHRINE_DOOR = "river & face-key & (hookshot | rooster)"
_FACE_SHRINE_INSIDE = "D6-3A & ((ocarina & song-mambo) | bombs | shoot-arrows | boomerang | rod)"


def _conditions(node: Dict[str, Any]) -> Dict[str, Any]:
    return {k: v for k, v in node.items() if k.startswith("condition-")}


def _door(logic: Dict[str, Dict[str, Any]], dungeon: str) -> Dict[str, Any]:
    """The conditions of a dungeon's overworld door."""
    if dungeon != "face-shrine":
        return _conditions(logic[dungeon])
    room = logic[FACE_SHRINE_ROOM]
    assert room["condition-basic"] == f"({_FACE_SHRINE_DOOR}) | ({_FACE_SHRINE_INSIDE})", room
    return {**_conditions(room), "condition-basic": _FACE_SHRINE_DOOR}


def dungeon_logic(logic: Dict[str, Dict[str, Any]], entrances: Dict[str, str]) -> Dict[str, Dict[str, Any]]:
    """The logic with dungeon doors leading elsewhere: entrances maps each dungeon's door to the dungeon it leads into
    (the patch's dungeon_entrances), whose inside then needs that door. LASR's shuffler moves only condition-basic and
    moves the Face Shrine's region, entrance rooms and all."""
    if all(door == dungeon for door, dungeon in entrances.items()):
        return logic
    assert sorted(entrances) == sorted(DUNGEONS) and sorted(entrances.values()) == sorted(DUNGEONS), entrances
    shuffled = dict(logic)
    for door, dungeon in entrances.items():
        conditions = _door(logic, door)
        if dungeon == "face-shrine":
            room = logic[FACE_SHRINE_ROOM]
            basic = f"({conditions['condition-basic']}) | ({_FACE_SHRINE_INSIDE})"
            shuffled[FACE_SHRINE_ROOM] = {**{k: v for k, v in room.items() if not k.startswith("condition-")},
                                          **conditions, "condition-basic": basic}
        else:
            node = logic[dungeon]
            shuffled[dungeon] = {**{k: v for k, v in node.items() if not k.startswith("condition-")}, **conditions}
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


def dungeon_region(dungeon: str) -> str:
    """The region a door leads into: the dungeon's, or the Face Shrine's entrance room."""
    return FACE_SHRINE_ROOM if dungeon == "face-shrine" else dungeon


def door_logic(logic: Dict[str, Dict[str, Any]]) -> Dict[str, Dict[str, Any]]:
    """The logic with each dungeon's door a region of its own (door_node), holding the door's conditions, and the
    regions doors lead into (dungeon_region) entered only through a door's entrance: they keep no conditions, but the
    Face Shrine's entrance room keeps the way in from its side room. The same logic as dungeon_logic's for any map of
    the doors, with the map in the entrances."""
    doors = dict(logic)
    for dungeon in DUNGEONS:
        assert door_node(dungeon) not in logic, dungeon
        doors[door_node(dungeon)] = {"type": "region", **_door(logic, dungeon)}
        region = dungeon_region(dungeon)
        inside = {"condition-basic": _FACE_SHRINE_INSIDE} if dungeon == "face-shrine" else {}
        doors[region] = {**{k: v for k, v in logic[region].items() if not k.startswith("condition-")}, **inside}
    return doors


DOOR_LOGIC = door_logic(LOGIC)
# Regions with no way in but a door's entrance
ENTERED_BY_DOOR = frozenset(dungeon_region(dungeon) for dungeon in DUNGEONS) - {FACE_SHRINE_ROOM}


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


def item_dungeon(item: str) -> int:
    """Dungeon number of a LASR dungeon item name (key-D3, map-D0, ...), or -1."""
    if len(item) > 3 and item[-3] == "-" and item[-2] == "D" and item[-1].isdigit():
        return int(item[-1])
    return -1
