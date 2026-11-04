"""Map tracker (DESIGN.md, Map tracker): the pause map shows where the slot's unchecked locations are.

Overworld: four UiFieldMapIcons rows per spot of checks (a group of nearby locations), one per look: in logic,
partly in logic, out of logic and collected, with the marker frames of ap_map_icons.py. Each dungeon door also gets a
group, next to the game's marker of the door, with the checks of the dungeon behind it. The groups are those of every
location, the same in every seed; the seed file's map gives each the slot's locations in it. The map shows a row's icon while
the event flag in the row is set and names it with Place:<label>; flag and label are APMap<mark><group>, which the
module answers from the seed file's map (map_tracker.cpp): the group's look shows, named by group_name(), at the size of one
check or of several. The module lists a group's locations by their short names (its check list).

Dungeons: the module puts a dot of the same looks on the dungeon map per spot holding checks (the map's M lines) and
hides the game's chest icons of the slot's chests (C lines); it names the hovered dot with Place:APMapDot.

Positions come from map_positions.json (scripts/map_positions.py). modbuild/ap_map_edits.py adds the rows and texts.
"""
import json
import math
import os
import re
from pathlib import Path
from typing import Mapping, Optional

import ap_assets

HERE = Path(__file__).resolve().parent
POSITIONS = HERE / "map_positions.json"
FLAG_PREFIX = "APMap"  # AP::MAP_FLAG_PREFIX in the module
# The mark after the prefix per look: in logic, partly in logic, out of logic, collected (AP::MAP_LOOK_MARKS)
LOOKS = ("", "P", "X", "C")
# Checks closer than this (world units; a screen is 15 x 12) share an icon.
GROUP_DISTANCE = 3.0
# Dungeon checks closer than this in the same room share a dot (Key Cavern's basement key beside its owl statue).
DOT_DISTANCE = 2.0
DOT_LABEL = "APMapDot"  # the Place.msbt entry the module names the hovered dungeon dot with (AP::MAP_DOT_LABEL)
CHEST_ACTOR = 0xF7  # the chest's actor type in a room's .leb
# The game's UiFieldMapIcons row marking each dungeon's entrance, by the dungeon's location prefix
DUNGEON_MARKERS = {"D1": "Lv1Dungeon_map", "D2": "Lv2Dungeon_map", "D3": "Lv3Dungeon_map", "D4": "Lv4Dungeon_map",
                   "D5": "Lv5Dungeon_map", "D6": "Lv6Dungeon_map", "D7": "Lv7Dungeon_map", "D8": "Lv8Dungeon_map",
                   "D0": "ClothesDungeon_map"}
ENTRANCE_OFFSET = 2.5  # x distance from the game's dungeon marker, so both show
# With shuffled dungeons a dungeon's icon has this name until the dungeon's marker flag is set, which the game sets
# the first time it shows the dungeon's name on entering it (LocationName.gsheet)
HIDDEN_DUNGEON = "Unknown dungeon"
NAME_WIDTH = 30  # the name of an icon of several checks fits the hover box's line after its count ("22/22 ")


def load_positions() -> dict:
    return json.loads(POSITIONS.read_text(encoding="utf-8"))


def overworld_groups(positions: dict) -> list:
    """[(x, z, y, [lasr location, ...])]: every overworld location, each joined to the first spot near it (by x and
    z), in name order."""
    groups = []
    for loc in sorted(positions["overworld"]):
        x, z, y = positions["overworld"][loc]
        for group in groups:
            if math.dist((group[0], group[1]), (x, z)) < GROUP_DISTANCE:
                group[3].append(loc)
                break
        else:
            groups.append((x, z, y, [loc]))
    return groups


def dungeon_dots(positions: dict, location_ids: dict) -> list:
    """[(room, x, z, [lasr location, ...])]: the slot's dungeon checks, each joined to the first spot near it in its
    room; a check with several places (Eagle's Tower before and after its collapse) in each."""
    dots = []
    for loc in sorted(location_ids, key=lambda loc: int(location_ids[loc])):
        for room, x, z in positions["dungeon"].get(loc, ()):
            for dot in dots:
                if dot[0] == room and math.dist((dot[1], dot[2]), (x, z)) < DOT_DISTANCE:
                    dot[3].append(loc)
                    break
            else:
                dots.append((room, x, z, [loc]))
    return dots


def door_checks(location_ids: dict, door: str, entrances: Optional[Mapping[str, str]] = None) -> list:
    """The slot's checks in the dungeon behind a door (`entrances`, door: the dungeon behind it; None: its own)."""
    behind = ap_assets.data()["dungeon_map_icons"][(entrances or {}).get(door, door)][0]
    prefix = next(prefix for prefix, label in DUNGEON_MARKERS.items() if label == behind)
    return [loc for loc in location_ids if loc.startswith(prefix + "-")]


def short_name(name: str) -> str:
    """The location's name without its region ("Mabe Village - Tarin": "Tarin"); the map names the regions."""
    return name.split(" - ", 1)[-1]


def group_name(positions: dict, locs: list) -> str:
    """An overworld icon's name: its dungeon ("D1 Tail Cave"), its check's name, or the spot of its checks
    (map_positions.json, the PopTracker pack's place)."""
    dungeon = re.match(r"(D\d)-", locs[0])
    if dungeon:
        return f"{dungeon.group(1)} {positions['names'][locs[0]].split(' - ', 1)[0]}"
    if len(locs) == 1:
        return short_name(positions["names"][locs[0]])
    return positions["spots"][locs[0]]


def dot_name(positions: dict, locs: list) -> str:
    """A dungeon dot's name: its check's name; for several, what their names start with ("Rupee Room" for Rupee Room 1
    to 28), else the first name and how many more."""
    names = [short_name(positions["names"][loc]) for loc in locs]
    if len(names) == 1:
        return names[0]
    common = os.path.commonprefix(names).rstrip(" 0123456789")
    if len(common) >= 4:
        return common
    return f"{names[0]} +{len(names) - 1}"


def hidden_until(locs: list, shuffled_dungeons: bool):
    """The flag until which an overworld icon's name is HIDDEN_DUNGEON: with shuffled dungeons, that of a dungeon's
    icon, which would tell which dungeon its door leads into."""
    dungeon = re.match(r"(D\d)-", locs[0])
    marker_flags = dict(ap_assets.data()["dungeon_map_icons"].values())  # the game's marker label -> its show flag
    return marker_flags[DUNGEON_MARKERS[dungeon.group(1)]] if dungeon and shuffled_dungeons else None


def lines(location_ids: dict, entrances: Optional[Mapping[str, str]] = None) -> list:
    """The seed file's map, which turns the tracker on. `entrances`: the patch's dungeon_entrances with shuffled
    dungeons, else None. The doors' places and the dungeon chests come from the mod's build (ap_assets.data)."""
    shuffled_dungeons = entrances is not None
    positions = load_positions()
    spots = overworld_groups(positions)
    data = ap_assets.data()
    doors = data["map_doors"]
    by_id = lambda loc: int(location_ids[loc])  # noqa: E731
    # the slot's checks of each group, in local id order; a group without any shows no icon
    overworld = [sorted((loc for loc in locs if loc in location_ids), key=by_id) for *_, locs in spots]
    overworld += [sorted(door_checks(location_ids, door, entrances), key=by_id) for *_, door in doors]
    shown = [(g, locs) for g, locs in enumerate(overworld) if locs]
    dots = dungeon_dots(positions, location_ids)
    lines = [f"C {level} {x:.3f} {z:.3f} {int(location_ids[loc])}"
             for loc, level, x, z in data["map_chests"] if loc in location_ids]
    for g, locs in shown:
        lines.append(f"O {g} " + " ".join(str(location_ids[loc]) for loc in locs))
    for d, (room, x, z, locs) in enumerate(dots):
        lines.append(f"M {d} {room} {x:.3f} {z:.3f} " + " ".join(str(location_ids[loc]) for loc in locs))
    lines += [f"G {g} {group_name(positions, locs)}" for g, locs in shown]
    lines += [f"MG {d} {dot_name(positions, locs)}" for d, (*_, locs) in enumerate(dots)]
    lines += [f"H {g} {flag} {HIDDEN_DUNGEON}" for g, locs in shown if (flag := hidden_until(locs, shuffled_dungeons))]
    lines += [f"N {location_ids[loc]} {short_name(positions['names'][loc])}"
              for loc in sorted({loc for _, locs in shown for loc in locs} | {loc for *_, locs in dots for loc in locs})]
    return lines
