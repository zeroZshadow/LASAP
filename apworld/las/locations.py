import re
from typing import Dict, Set

from BaseClasses import Location

from .ids import LOCATION_BASE, LOCATION_OFFSETS
from .items import GAME
from .lasr import CATEGORIES, DUNGEON_NAMES, ITEM_LOCATIONS, LOGIC, location_dungeon


class LASLocation(Location):
    game = GAME


_REGION_NAMES = {
    "marthas-bay": "Martha's Bay",
    "taltal-heights": "Tal Tal Heights",
    "taltal-mountains-east": "Tal Tal Mountains East",
    "taltal-mountains-west": "Tal Tal Mountains West",
}
_WORDS = {"ol": "Ol'", "3ofakind": "3 of a Kind", "of": "of", "a": "a", "by": "by", "to": "to", "the": "the"}


def _words(text: str) -> str:
    words = []
    for word in text.split("-"):
        if word in _WORDS and (words or word in ("ol", "3ofakind")):
            words.append(_WORDS[word])
        elif re.fullmatch(r"\d+f|d\d", word, re.IGNORECASE):
            words.append(word.upper())
        else:
            words.append(word[:1].upper() + word[1:])
    return " ".join(words)


def display_name(lasr_location: str) -> str:
    dungeon = location_dungeon(lasr_location)
    if dungeon >= 0:
        return f"{DUNGEON_NAMES[dungeon]} - {_words(lasr_location[3:])}"
    region = LOGIC[lasr_location]["spoiler-region"]
    return f"{_REGION_NAMES.get(region, _words(region))} - {_words(lasr_location)}"


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
