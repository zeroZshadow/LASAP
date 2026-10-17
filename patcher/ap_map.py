"""Map tracker (DESIGN.md, Map tracker): the pause map shows where the slot's unchecked locations are.

Overworld: four UiFieldMapIcons rows per spot holding checks (a group of nearby locations), one per look: in logic,
partly in logic, out of logic and collected, with the marker frames of ap_map_icons.py (smaller for a single check).
Each dungeon also gets a group with all of its checks, next to the game's marker of its entrance. The map shows a row's
icon while the event flag in the row is set and names it with Place:<label>; flag and label are APMap<mark><group>,
which the module answers from ap_map.txt (ap_map.cpp): the group's look shows, named by group_name().

Dungeons: the map shows chests already (the module gives it the compass) and the module shows a chest open once its
location is checked; every room with other checks gets a UiDungeonIcon row with a chest icon, which the module shows
open once the room's checks are done.

Positions come from map_positions.json (scripts/map_positions.py).
"""
import json
import math
import re
from pathlib import Path

import ap_map_icons
from RandomizerCore.Randomizers.chests import CHEST_ROOMS
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

HERE = Path(__file__).resolve().parent
POSITIONS = HERE / "map_positions.json"
FILE_NAME = "ap_map.txt"  # next to config.ini; AP::MAP_PATH in the module
FLAG_PREFIX = "APMap"  # AP::MAP_FLAG_PREFIX in the module
# The mark after the prefix per look: in logic, partly in logic, out of logic, collected (AP::MAP_LOOK_MARKS)
LOOKS = ("", "P", "X", "C")
# Checks closer than this (world units; a screen is 15 x 12) share an icon.
GROUP_DISTANCE = 3.0
CHEST_ACTOR = 0xF7  # UiDungeonIcon mActorID that shows a chest icon
# The game's UiFieldMapIcons row marking each dungeon's entrance, by the dungeon's location prefix
DUNGEON_MARKERS = {"D1": "Lv1Dungeon_map", "D2": "Lv2Dungeon_map", "D3": "Lv3Dungeon_map", "D4": "Lv4Dungeon_map",
                   "D5": "Lv5Dungeon_map", "D6": "Lv6Dungeon_map", "D7": "Lv7Dungeon_map", "D8": "Lv8Dungeon_map",
                   "D0": "ClothesDungeon_map"}
ENTRANCE_OFFSET = 2.5  # x distance from the game's dungeon marker, so both show
NAME_WIDTH = 30  # the name of an icon of several checks fits the hover box's line after its count ("22/22 ")


def load_positions() -> dict:
    return json.loads(POSITIONS.read_text(encoding="utf-8"))


def overworld_groups(positions: dict, location_ids: dict) -> list:
    """[(x, z, y, [lasr location, ...])]: the slot's overworld checks, each joined to the first spot near it (by x
    and z)."""
    groups = []
    for loc in sorted(location_ids, key=lambda loc: int(location_ids[loc])):
        if loc not in positions["overworld"]:
            continue
        x, z, y = positions["overworld"][loc]
        for group in groups:
            if math.dist((group[0], group[1]), (x, z)) < GROUP_DISTANCE:
                group[3].append(loc)
                break
        else:
            groups.append((x, z, y, [loc]))
    return groups


def dungeon_groups(positions: dict, location_ids: dict) -> list:
    """[(room, x, z, [lasr location, ...])]: the slot's dungeon checks that are not chests, one icon per room."""
    rooms = {}
    for loc in sorted(location_ids, key=lambda loc: int(location_ids[loc])):
        if loc in positions["dungeon"] and loc not in CHEST_ROOMS:
            room, x, z = positions["dungeon"][loc]
            rooms.setdefault(room, (room, x, z, []))[3].append(loc)
    return list(rooms.values())


def entrance_groups(files, location_ids: dict) -> list:
    """[(x, z, y, [lasr location, ...])]: per dungeon, all of the slot's checks in it, next to its entrance."""
    rows = {str(row["mNameLabel"]): row for row in map(parseStruct, files.readFile("UiFieldMapIcons.gsheet")["values"])}
    groups = []
    for prefix, label in DUNGEON_MARKERS.items():
        locs = sorted((loc for loc in location_ids if loc.startswith(prefix + "-")), key=lambda l: int(location_ids[l]))
        if locs:
            row = rows[label]
            groups.append((row["mPosX"] + ENTRANCE_OFFSET, row["mPosZ"], row["mPosY"], locs))
    return groups


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


def shown(location_ids: dict, settings: dict) -> dict:
    """The locations that get icons: the slot's AP locations, but not the trade quest's when it is off. Those stay
    checks holding their trade item, as the logic needs it, but the player turned the trade quest off, and PopTracker
    hides them too."""
    if settings.get("Trade Quest", True):
        return location_ids
    from RandomizerCore.randomizer_data import TRADE_GIFT_LOCATIONS
    return {loc: i for loc, i in location_ids.items() if loc not in TRADE_GIFT_LOCATIONS}


def add(files, config_dir: Path, location_ids: dict, placeholder_key: str) -> None:
    positions = load_positions()
    overworld = overworld_groups(positions, location_ids) + entrance_groups(files, location_ids)
    dungeon = dungeon_groups(positions, location_ids)
    _field_icons(files, overworld)
    _dungeon_icons(files, dungeon)
    names = [group_name(positions, locs) for *_, locs in overworld]
    _text(files, names)
    lines = [f"C {level} {x:.3f} {z:.3f} {local_id}"
             for level, x, z, local_id in _dungeon_chests(files, location_ids, placeholder_key)]
    for g, (*_, locs) in enumerate(overworld):
        lines.append(f"O {g} " + " ".join(str(location_ids[loc]) for loc in locs))
    for g, (_, x, z, locs) in enumerate(dungeon):
        lines.append(f"D {g} {x:.3f} {z:.3f} " + " ".join(str(location_ids[loc]) for loc in locs))
    lines += [f"G {g} {name}" for g, name in enumerate(names)]
    (config_dir / FILE_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def _field_icons(files, groups: list) -> None:
    sheet = files.readFile("UiFieldMapIcons.gsheet")
    # Parsed up front: appending to the sheet invalidates references to its rows.
    rows = [parseStruct(row) for row in sheet["values"]]
    next_id = max(row["mId"] for row in rows) + 1
    for g, (x, z, y, locs) in enumerate(groups):
        for look, mark in enumerate(LOOKS):
            name = f"{FLAG_PREFIX}{mark}{g}"
            row = dict(rows[0])
            row.update({"mId": next_id + len(LOOKS) * g + look, "mNameLabel": name,
                        "mType": ap_map_icons.frame(look, len(locs) == 1),
                        "mFirstShowFlagName": name, "mPosX": float(x), "mPosY": float(y), "mPosZ": float(z),
                        "mArg00": ""})
            sheet["values"].append(dictToStruct(row))
    files.writeFile("UiFieldMapIcons.gsheet", sheet)


def _dungeon_icons(files, groups: list) -> None:
    sheet = files.readFile("UiDungeonIcon.gsheet")
    rows = [parseStruct(row) for row in sheet["values"]]
    next_id = max(row["mId"] for row in rows) + 1
    for g, (room, x, z, _) in enumerate(groups):
        row = dict(rows[0])
        row.update({"mId": next_id + g, "mLeaName": room, "mActorID": CHEST_ACTOR, "posX": float(x),
                    "posZ": float(z)})
        sheet["values"].append(dictToStruct(row))
    files.writeFile("UiDungeonIcon.gsheet", sheet)


def _dungeon_chests(files, location_ids: dict, placeholder_key: str) -> list:
    """[(level, x, z, local id)]: the chests of the slot's dungeon locations, found by the placeholder (or a model
    variant of it) with the location's index."""
    chests = []
    for loc, room_name in CHEST_ROOMS.items():
        if loc not in location_ids or not room_name.startswith("Lv"):
            continue
        local_id = int(location_ids[loc])
        for actor in files.readFile(f"{room_name}.leb").actors:
            key = actor.parameters[1]
            if (actor.type == CHEST_ACTOR and isinstance(key, bytes) and key.startswith(placeholder_key.encode())
                    and actor.parameters[2] == local_id):
                chests.append((room_name.split("_")[0], actor.posX, actor.posZ, local_id))
    return chests


def _text(files, names: list) -> None:
    """Place.msbt entries for the icons' names; the module adds the group's count to them."""
    for relative_path, msbt in files.getAllTextFiles("Place.msbt"):
        for g, name in enumerate(names):
            for mark in LOOKS:
                msbt.addEntry(f"{FLAG_PREFIX}{mark}{g}", name)
        files.writeTextFile(relative_path, "Place.msbt", msbt)
