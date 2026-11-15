"""Universal Tracker's map tab (DESIGN.md, Universal Tracker map): the data behind tracker/*.json (scripts/ut_map.py
writes them) and the door entrances a route goes through."""
import re
from typing import Any, Dict, Iterable, List, Mapping, Optional

from .items import LASR_TO_ITEM
from .lasr import DOORS, DUNGEON_NAMES, DUNGEON_NUMBERS, DUNGEONS, INSTRUMENTS, door_entrance
from .tracker import LEVEL_KEY

OVERWORLD_MAP = "Koholint Island"


def dungeon_label(number: int) -> str:
    """A dungeon's name on the map tab, its maps and its item section: "D1 - Tail Cave"."""
    return f"D{number} - {DUNGEON_NAMES[number]}"


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
