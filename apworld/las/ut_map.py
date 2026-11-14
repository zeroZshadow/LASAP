"""Universal Tracker's map tab (DESIGN.md, Universal Tracker map): this world's tracker/maps.json, tracker/locations.json
and tracker/items.json, which scripts/ut_map.py writes, with the images the client builds from the player's RomFS dump
(patcher/ap_tracker_map.py) in the zip las_options ut_pack_path names: a section per location, named after it, and per
shuffled overworld door one named after its entrance. UT colours a door by its entrance: gone through (connected,
Deferred) or not, reachable or not. Left of the map the items of ITEM_SECTIONS, right of it each dungeon's of
DUNGEON_SECTIONS, those the slot's item pool has, with the game's icons."""
import re
from typing import Any, Dict

from .items import LASR_TO_ITEM
from .lasr import DUNGEON_NAMES
from .tracker import LEVEL_KEY

OVERWORLD_MAP = "Koholint Island"


def dungeon_label(number: int) -> str:
    """A dungeon's name on the map tab, its maps and its item section: "D1 - Tail Cave"."""
    return f"D{number} - {DUNGEON_NAMES[number]}"


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
               "medicine"), 6),
    ("Songs", ("song-ballad", "song-mambo", "song-soul"), 6),
    ("Instruments", ("full-moon-cello", "conch-horn", "sea-lilys-bell", "surf-harp", "wind-marimba", "coral-triangle",
                     "evening-calm-organ", "thunder-drum"), 4),
    ("Keys", ("tail-key", "slime-key", "angler-key", "face-key", "bird-key"), 6),
    ("Trade", ("yoshi-doll", "ribbon", "dog-food", "bananas", "stick", "honeycomb", "pineapple", "hibiscus", "letter",
               "broom", "fishing-hook", "necklace", "mermaid-scale", "magnifying-lens"), 6),
)
# Right of the map, a section per dungeon: its small keys, nightmare key and stone beak. The pack has no icons for maps
# and compasses.
DUNGEON_SECTIONS = tuple((dungeon_label(n), (f"key-D{n}", f"nightmare-key-D{n}", f"map-D{n}", f"compass-D{n}",
                                              f"stone-beak-D{n}"), 5) for n in (*range(1, 9), 0))
ITEM_SIZE, DUNGEON_ITEM_SIZE = (40, 40), (32, 32)


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
