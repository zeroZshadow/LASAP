"""Map tracker (DESIGN.md, Map tracker): the pause map shows where the slot's unchecked locations are.

Overworld: one UiFieldMapIcons row per spot holding checks (a group of nearby locations). The map shows a row's icon
while the event flag in the row is set and names it with Place:<label>; flag and label are APMap<group>, which the
module answers from ap_map.txt (ap_map.cpp): shown while a location of the group is unchecked, named by those
locations. Dungeons: the map shows chests already (the module gives it the compass); every room with other checks
gets a UiDungeonIcon row with a chest icon, which the module shows open once the room's checks are done.

Positions come from map_positions.json (scripts/map_positions.py).
"""
import json
import math
from pathlib import Path

from RandomizerCore.Randomizers.chests import CHEST_ROOMS
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

HERE = Path(__file__).resolve().parent
POSITIONS = HERE / "map_positions.json"
FILE_NAME = "ap_map.txt"  # next to config.ini; AP::MAP_PATH in the module
FLAG_PREFIX = "APMap"  # AP::MAP_FLAG_PREFIX in the module
# Gm_MapMarker frame of the icons: 0 facility, 1 shop, 2 telephone, 3 dungeon, 5 heart piece, 6 seashell
ICON_TYPE = 6
ICON_Y = 10.0  # the map places icons by x and z
# Checks closer than this (world units; a screen is 15 x 12) share an icon.
GROUP_DISTANCE = 3.0
CHEST_ACTOR = 0xF7  # UiDungeonIcon mActorID that shows a chest icon


def load_positions() -> dict:
    return json.loads(POSITIONS.read_text(encoding="utf-8"))


def overworld_groups(positions: dict, location_ids: dict) -> list:
    """[(x, z, [lasr location, ...])]: the slot's overworld checks, each joined to the first spot near it."""
    groups = []
    for loc in sorted(location_ids, key=lambda loc: int(location_ids[loc])):
        if loc not in positions["overworld"]:
            continue
        x, z = positions["overworld"][loc]
        for group in groups:
            if math.dist((group[0], group[1]), (x, z)) < GROUP_DISTANCE:
                group[2].append(loc)
                break
        else:
            groups.append((x, z, [loc]))
    return groups


def dungeon_groups(positions: dict, location_ids: dict) -> list:
    """[(room, x, z, [lasr location, ...])]: the slot's dungeon checks that are not chests, one icon per room."""
    rooms = {}
    for loc in sorted(location_ids, key=lambda loc: int(location_ids[loc])):
        if loc in positions["dungeon"] and loc not in CHEST_ROOMS:
            room, x, z = positions["dungeon"][loc]
            rooms.setdefault(room, (room, x, z, []))[3].append(loc)
    return list(rooms.values())


def short_name(name: str) -> str:
    """The location's name without its region ("Mabe Village - Tarin": "Tarin"); the map names the regions."""
    return name.split(" - ", 1)[-1]


def add(files, config_dir: Path, location_ids: dict) -> None:
    positions = load_positions()
    overworld = overworld_groups(positions, location_ids)
    dungeon = dungeon_groups(positions, location_ids)
    _field_icons(files, overworld)
    _dungeon_icons(files, dungeon)
    _text(files, overworld, positions["names"])
    lines = []
    for g, (_, _, locs) in enumerate(overworld):
        lines.append(f"O {g} " + " ".join(str(location_ids[loc]) for loc in locs))
    for g, (_, x, z, locs) in enumerate(dungeon):
        lines.append(f"D {g} {x:.3f} {z:.3f} " + " ".join(str(location_ids[loc]) for loc in locs))
    for _, _, locs in overworld:
        lines += [f"N {location_ids[loc]} {short_name(positions['names'][loc])}" for loc in locs]
    (config_dir / FILE_NAME).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def _field_icons(files, groups: list) -> None:
    sheet = files.readFile("UiFieldMapIcons.gsheet")
    # Parsed up front: appending to the sheet invalidates references to its rows.
    rows = [parseStruct(row) for row in sheet["values"]]
    next_id = max(row["mId"] for row in rows) + 1
    for g, (x, z, _) in enumerate(groups):
        row = dict(rows[0])
        row.update({"mId": next_id + g, "mNameLabel": f"{FLAG_PREFIX}{g}", "mType": ICON_TYPE,
                    "mFirstShowFlagName": f"{FLAG_PREFIX}{g}", "mPosX": float(x), "mPosY": ICON_Y,
                    "mPosZ": float(z), "mArg00": ""})
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


def _text(files, groups: list, names: dict) -> None:
    """Place.msbt entries for the icons' names; the module replaces them with the unchecked locations."""
    for relative_path, msbt in files.getAllTextFiles("Place.msbt"):
        for g, (_, _, locs) in enumerate(groups):
            msbt.addEntry(f"{FLAG_PREFIX}{g}", short_name(names[locs[0]]))
        files.writeTextFile(relative_path, "Place.msbt", msbt)
