"""LASR (LAS Randomizer) data: logic, item pool and location categories, loaded from the bundled YAML files."""
import copy
import pkgutil
from typing import Any, Dict, List

import yaml

_Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)


def _load_yaml(name: str) -> Any:
    return yaml.load(pkgutil.get_data(__name__, f"data/{name}"), Loader=_Loader)


# Raw data, exactly as shipped by LASR.
RAW_LOGIC: Dict[str, Dict[str, Any]] = _load_yaml("logic.yml")
_items_yml = _load_yaml("items.yml")
ITEM_DEFS: Dict[str, Dict[str, Any]] = _items_yml["Item_Pool"]
CATEGORIES: Dict[str, List[str]] = _load_yaml("locations.yml")

# Fixes applied on top of LASR's logic.yml. The world and the logic cross-check both use the fixed data.
# - D6-4D and D6-2A are referenced but never defined, which makes the Face Shrine boss/instrument and the
#   rapids south island chest unreachable. They are defined after LADXR's Face Shrine layout: the boss door
#   is reached from the tile room (D6-5D); the raft-game chest is reached through the top-left exit.
# - kill-flower's "bow-wow & !rooster" is order-dependent in LASR's fixed point and not monotone; followers can
#   be swapped in-game, so it is reduced to "bow-wow".
EXTRA_NODES: Dict[str, Dict[str, Any]] = {
    "D6-4D": {"type": "room", "condition-basic": "D6-5D"},
    "D6-2A": {"type": "room", "condition-basic": "D6-3A & hit-crystal-switch & bracelet:2"},
}
CONDITION_EDITS = {
    ("kill-flower", "condition-basic"): ("(bow-wow & !rooster)", "bow-wow"),
}
# logic.yml spells one vanilla content differently from items.yml.
CONTENT_ALIASES = {"powders-capacity": "powder-capacity"}
# The companions: LASR's followers become item locations holding their companion (kept, see lasr_settings), and the
# companions items the logic counts.
COMPANIONS = {"moblin-cave": "bow-wow", "rooster-statue": "rooster"}
COMPANION_ITEMS = {"bow-wow": {"type": "important", "quantity": 1, "item-key": "None"},
                   "rooster": {"type": "important", "quantity": 1, "item-key": "None"}}
ITEM_DEFS.update(COMPANION_ITEMS)


def _fixed_logic() -> Dict[str, Dict[str, Any]]:
    logic = copy.deepcopy(RAW_LOGIC)
    for (node, key), (old, new) in CONDITION_EDITS.items():
        assert old in logic[node][key], (node, key)
        logic[node][key] = logic[node][key].replace(old, new)
    for node, definition in EXTRA_NODES.items():
        assert node not in logic, node
        logic[node] = dict(definition)
    for node, content in COMPANIONS.items():
        assert logic[node]["type"] == "follower" and logic[node]["content"] == content, node
        logic[node].update(type="item", subtype="companion")
    for definition in logic.values():
        if "content" in definition:
            definition["content"] = CONTENT_ALIASES.get(definition["content"], definition["content"])
    return logic


LOGIC: Dict[str, Dict[str, Any]] = _fixed_logic()

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


def goal_logic(instruments_required: int, logic: Dict[str, Dict[str, Any]] = LOGIC) -> Dict[str, Dict[str, Any]]:
    """The logic with the Wind Fish's egg opening at this many instruments."""
    if instruments_required >= len(INSTRUMENTS):
        return logic
    parts = [part.strip() for part in logic["egg"]["condition-basic"].split("&")]
    assert set(INSTRUMENTS) <= set(parts), parts
    parts = [part for part in parts if part not in INSTRUMENTS] + [f"{INSTRUMENTS_TOKEN}:{instruments_required}"]
    return {**logic, "egg": {**logic["egg"], "condition-basic": " & ".join(parts)}}


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
