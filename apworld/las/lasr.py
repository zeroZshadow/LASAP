"""LASR (LAS Randomizer) data: logic, item pool and location categories, loaded from the bundled YAML files."""
import pkgutil
from typing import Any, Dict, Iterable, List, Optional

import yaml

from .entrances import shuffle_pool, shuffled_doors
from .logic import ALWAYS, SPEC, START, house_exits, lower_routes

_Loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)


def _load_yaml(name: str) -> Any:
    return yaml.load(pkgutil.get_data(__name__, f"data/{name}"), Loader=_Loader)


WRITTEN_LOGIC: Dict[str, Dict[str, Any]] = _load_yaml("logic.yml")  # with its routes, as logic.yml writes them
DOORS: Dict[str, Any] = _load_yaml("doors.yml")

# Dungeon entrance shuffle. A dungeon's door is its pair in doors.yml's `dungeon` group; the dungeon is the one its
# inside's level holds (Lv01 Tail Cave, ..., Lv08 Turtle Rock, Lv10 the Color Dungeon). The region just inside the door
# (dungeon_region, named by data/logic_spec.yml) is the way in through it: it leads on to the zone of the dungeon's
# inside door. Shuffled dungeons pair a dungeon's door with another dungeon's inside (slot_doors), as entrance shuffle
# does the other doors, so the way out leads where the way in came from.
DUNGEONS = tuple(SPEC["dungeons"])


def _inside_dungeon(door: str) -> str:
    level = int(DOORS["pairs"][door][2:4])
    return DUNGEONS[(level % 10 - 1) % len(DUNGEONS)]


DUNGEON_DOORS: Dict[str, str] = {_inside_dungeon(door): door for door in DOORS["groups"]["dungeon"]}
if sorted(DUNGEON_DOORS) != sorted(DUNGEONS):
    raise ValueError(f"doors.yml's dungeon group needs one door per dungeon: {sorted(DUNGEON_DOORS)}")


def _without_conditions(node: Dict[str, Any]) -> Dict[str, Any]:
    return {k: v for k, v in node.items() if not k.startswith("condition-")}


def dungeon_region(dungeon: str) -> str:
    """The region just inside a dungeon's door."""
    return SPEC["dungeons"][dungeon]


def slot_doors(entrances: Optional[Dict[str, str]] = None,
               dungeon_entrances: Optional[Dict[str, str]] = None) -> Dict[str, Any]:
    """doors.yml with a slot's doors: the overworld doors leading as its entrances say, and each dungeon's door into the
    dungeon its dungeon_entrances say."""
    pairs = dict(entrances or {})
    for door, dungeon in (dungeon_entrances or {}).items():
        if door != dungeon:
            pairs[DUNGEON_DOORS[door]] = DOORS["pairs"][DUNGEON_DOORS[dungeon]]
    return shuffled_doors(DOORS, pairs) if pairs else DOORS


def _with_dungeon_regions(written: Dict[str, Dict[str, Any]], *extra: str) -> Dict[str, Dict[str, Any]]:
    nodes = dict(written)
    for name in extra:
        nodes.setdefault(name, {"type": "region"})
    return nodes


_ENTERED = {DOORS["pairs"][DUNGEON_DOORS[dungeon]]: dungeon_region(dungeon) for dungeon in DUNGEONS}


def plain_logic(written: Dict[str, Dict[str, Any]], doors: Dict[str, Any],
                house_exit: Optional[str] = None) -> Dict[str, Dict[str, Any]]:
    """The routes lowered, each dungeon's door leading into the region just inside it."""
    return lower_routes(_with_dungeon_regions(written, *_ENTERED.values()), doors, house_exit, _ENTERED)


LOGIC: Dict[str, Dict[str, Any]] = plain_logic(WRITTEN_LOGIC, DOORS)


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


def dungeon_name(dungeon: str) -> str:
    """The game's name of a dungeon (DUNGEONS name): Tail Cave, ..., Color Dungeon."""
    return DUNGEON_NAMES[(DUNGEONS.index(dungeon) + 1) % len(DUNGEONS)]


def door_node(dungeon: str) -> str:
    """The logic node of a dungeon's overworld door (door_logic)."""
    return f"{dungeon}-door"


def door_entrance(dungeon: str) -> str:
    """The AP entrance through a dungeon's door, into the dungeon it leads into."""
    return f"{dungeon_name(dungeon)} Entrance"


def door_region(door: str) -> str:
    """The region of a shuffled overworld door or of its inside door (Entrances): the door's way in, the spot the way
    out of its partner lands on, and the way on to its zone. A rule's name has no `_` (data/logic_spec.yml's
    grammar)."""
    return f"{door.replace('_', '-')}-door"


# Entrances: the AP entrance of the house's way out, and the region it leaves from (door_logic's house_entrance)
HOUSE_EXIT = "Tarin's House Exit"
HOUSE_EXIT_REGION = "house-exit"


def door_logic(written: Dict[str, Dict[str, Any]], doors: Dict[str, Any], house_exit: Optional[str] = None,
               open_doors: Iterable[str] = (), house_entrance: bool = False) -> Dict[str, Dict[str, Any]]:
    """The routes lowered with each dungeon's door a region of its own (door_node), holding the door's way in, and the
    regions just inside the doors (dungeon_region) entered only through a door's entrance, which the world connects to
    the dungeon the door leads into: they keep no conditions. The ways out of the dungeons lead as `doors` pairs them.
    Each of `open_doors` (Entrances: ENTRANCE_POOL) and the inside door `doors` pairs it with is a region of its own as
    well (door_region), with the door's way in, which leads on to the door's zone: the world connects the doors'
    entrances to each other's regions as the slot's entrances pair them, so the rules do not depend on where the doors
    lead. A way that drops onto such a door's spot leads to its zone: only its partner's way out leads through it.
    With house_entrance the start's doors lead into a region of their own (HOUSE_EXIT_REGION), whose entrance
    (HOUSE_EXIT) the world connects to the zone the house's exit leads to (START_DOORS, HOME_ZONE)."""
    through = {DUNGEON_DOORS[dungeon]: door_node(dungeon) for dungeon in DUNGEONS}
    for dungeon in DUNGEONS:
        assert door_node(dungeon) not in written, dungeon
    nodes = _with_dungeon_regions(written, *_ENTERED.values(), *through.values())
    zones = {door: zone for zone, node in written.items() for door in (node.get("doors") or {})}
    for door in open_doors:
        for side in (door, doors["pairs"][door]):
            through[side] = door_region(side)
            assert through[side] not in written, through[side]
            nodes[through[side]] = {"type": "region", "one-way": {zones[side]: ALWAYS}}
    if house_entrance:
        assert HOUSE_EXIT_REGION not in written
        for door in written[START].get("doors") or {}:
            through[door] = HOUSE_EXIT_REGION
        nodes[HOUSE_EXIT_REGION] = {"type": "region"}
    lowered = lower_routes(nodes, doors, house_exit, _ENTERED, through)
    for dungeon in DUNGEONS:
        lowered[dungeon_region(dungeon)] = _without_conditions(lowered[dungeon_region(dungeon)])
    return lowered


DOOR_LOGIC = door_logic(WRITTEN_LOGIC, DOORS)


def entrance_logic(entrances: Optional[Dict[str, str]], dungeon_entrances: Optional[Dict[str, str]] = None,
                   start: Optional[str] = None) -> Dict[str, Dict[str, Any]]:
    """DOOR_LOGIC with a slot's doors (slot_doors): its overworld entrances (Entrances) and its dungeons',
    and with the house's exit leading out of the `start` door (Random start, START_DOORS)."""
    doors = slot_doors(entrances, dungeon_entrances)
    if doors is DOORS and not start:
        return DOOR_LOGIC
    return door_logic(WRITTEN_LOGIC, doors, START_DOORS[start] if start else None)


# Entrances: the overworld doors the shuffle moves (entrances.shuffle_pool), as door_logic's open_doors
ENTRANCE_POOL: List[str] = shuffle_pool(WRITTEN_LOGIC, DOORS)

# Random start: {overworld door: its zone} for each door the house's exit may lead out of (logic.house_exits)
START_DOORS: Dict[str, str] = house_exits(WRITTEN_LOGIC, DOORS)


def _home_zone() -> str:
    (door,) = WRITTEN_LOGIC[START]["doors"]
    leads = {**DOORS["pairs"], **{other: one for one, other in DOORS["pairs"].items()}}
    return next(zone for zone, node in WRITTEN_LOGIC.items() if leads[door] in (node.get("doors") or {}))


# The zone the house's exit leads to without Random start: that of the door outside Tarin's
HOME_ZONE = _home_zone()


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
