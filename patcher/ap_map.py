"""Map tracker (DESIGN.md, Map tracker): the pause map shows where the slot's unchecked locations are.

Overworld: four UiFieldMapIcons rows per spot holding checks (a group of nearby locations), one per look: in logic,
partly in logic, out of logic and collected. The map shows a row's icon while the event flag in the row is set and
names it with Place:<label>; flag and label are APMap<mark><group>, which the module answers from ap_map.txt
(ap_map.cpp): the group's look shows, named by its unchecked locations. Dungeons: the map shows chests already (the module gives it the compass) and the module shows a chest
open once its location is checked; every room with other checks gets a UiDungeonIcon row with a chest icon, which the
module shows open once the room's checks are done.

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
# (mark after the prefix, Gm_MapMarker frame) per look: in logic, partly in logic, out of logic, collected. The marks
# are AP::MAP_LOOK_MARKS. Frames: 0 facility, 1 shop, 2 telephone, 3 dungeon, 5 heart piece, 6 seashell.
LOOKS = (("", 6), ("P", 1), ("X", 5), ("C", 0))
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


def add(files, config_dir: Path, location_ids: dict, placeholder_key: str) -> None:
    positions = load_positions()
    overworld = overworld_groups(positions, location_ids)
    dungeon = dungeon_groups(positions, location_ids)
    _field_icons(files, overworld)
    _dungeon_icons(files, dungeon)
    _text(files, overworld, positions["names"])
    lines = [f"C {level} {x:.3f} {z:.3f} {local_id}"
             for level, x, z, local_id in _dungeon_chests(files, location_ids, placeholder_key)]
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
        for look, (mark, icon_type) in enumerate(LOOKS):
            name = f"{FLAG_PREFIX}{mark}{g}"
            row = dict(rows[0])
            row.update({"mId": next_id + len(LOOKS) * g + look, "mNameLabel": name, "mType": icon_type,
                        "mFirstShowFlagName": name, "mPosX": float(x), "mPosY": ICON_Y, "mPosZ": float(z),
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


def _text(files, groups: list, names: dict) -> None:
    """Place.msbt entries for the icons' names; the module replaces them with the unchecked locations."""
    for relative_path, msbt in files.getAllTextFiles("Place.msbt"):
        for g, (_, _, locs) in enumerate(groups):
            for mark, _ in LOOKS:
                msbt.addEntry(f"{FLAG_PREFIX}{mark}{g}", short_name(names[locs[0]]))
        files.writeTextFile(relative_path, "Place.msbt", msbt)
