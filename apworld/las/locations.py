import json
import pkgutil
from typing import Dict, Set

from BaseClasses import Location

from .ids import LOCATION_BASE, LOCATION_OFFSETS
from .items import GAME
from .lasr import CATEGORIES, DUNGEON_NAMES, ITEM_LOCATIONS, location_dungeon


class LASLocation(Location):
    game = GAME


# LASR name -> AP name: "<region or dungeon> - <label>", the label as the PopTracker pack names the check (its section,
# or for a dungeon its room on the dungeon map; scripts/poptracker_map.py)
_NAMES: Dict[str, str] = json.loads(pkgutil.get_data(__name__, "data/location_names.json").decode("utf-8"))


def display_name(lasr_location: str) -> str:
    return _NAMES[lasr_location]


LOCATION_NAMES: Dict[str, str] = {k: display_name(k) for k in ITEM_LOCATIONS}  # LASR name -> AP name
LASR_LOCATIONS: Dict[str, str] = {v: k for k, v in LOCATION_NAMES.items()}  # AP name -> LASR name
assert len(LASR_LOCATIONS) == len(LOCATION_NAMES), "duplicate location display names"

LOCATION_NAME_TO_ID: Dict[str, int] = {LOCATION_NAMES[k]: LOCATION_BASE + LOCATION_OFFSETS[k] for k in ITEM_LOCATIONS}

# LASR category of each categorized location (locations.yml); the rest are always shuffled.
LOCATION_CATEGORY: Dict[str, str] = {loc: category for category, locs in CATEGORIES.items() for loc in locs}

_GROUP_NAMES = {
    "Chest_Locations": "Overworld Chests",
    "Fishing_Rewards": "Fishing",
    "Rapids_Rewards": "Rapids Ride",
    "Dampe_Rewards": "Dampe",
    "Free_Gifts": "Free Gifts",
    "Trade_Gifts": "Trade Quest",
    "Boss_Locations": "Boss Drops",
    "Misc_Items": "Miscellaneous",
    "Mansion": "Seashell Mansion",
    "Shop_Items": "Shop",
    "Trendy_Rewards": "Trendy Game",
    "Heart_Pieces": "Heart Pieces",
    "Seashells": "Seashells",
    "Golden_Leaves": "Golden Leaves",
    "Dungeon_Owl_Statues": "Dungeon Owl Statues",
    "Overworld_Owl_Statues": "Overworld Owl Statues",
    "Blue_Rupees": "Blue Rupees",
}
LOCATION_NAME_GROUPS: Dict[str, Set[str]] = {
    group: {LOCATION_NAMES[loc] for loc in CATEGORIES[category]} for category, group in _GROUP_NAMES.items()
}
for _number, _dungeon in DUNGEON_NAMES.items():
    LOCATION_NAME_GROUPS[_dungeon] = {name for loc, name in LOCATION_NAMES.items() if location_dungeon(loc) == _number}
LOCATION_NAME_GROUPS["Instruments"] = {LOCATION_NAMES[f"D{i}-instrument"] for i in range(1, 9)}
