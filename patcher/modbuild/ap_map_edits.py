"""The map tracker's rows and texts in the mod, the same in every seed (ap_map.py; DESIGN.md, Map tracker)."""
import ap_map_icons
from RandomizerCore.Randomizers.chests import CHEST_ROOMS
from RandomizerCore.Randomizers.data import DUNGEON_MAP_ICONS
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

from ap_map import (CHEST_ACTOR, DOT_LABEL, DUNGEON_MARKERS, ENTRANCE_OFFSET, FLAG_PREFIX, LOOKS, load_positions,
                    overworld_groups)


def door_groups(files) -> list:
    """[(x, z, y, dungeon)]: per dungeon door, its place next to the game's marker of the door, and the dungeon whose
    door it is."""
    rows = {str(row["mNameLabel"]): row for row in map(parseStruct, files.readFile("UiFieldMapIcons.gsheet")["values"])}
    dungeon_of = {label: dungeon for dungeon, (label, _) in DUNGEON_MAP_ICONS.items()}
    return [(rows[label]["mPosX"] + ENTRANCE_OFFSET, rows[label]["mPosZ"], rows[label]["mPosY"], dungeon_of[label])
            for label in DUNGEON_MARKERS.values()]


def add(files) -> None:
    """The icons' rows and texts, in every mod."""
    places = [(x, z, y) for x, z, y, _ in overworld_groups(load_positions()) + door_groups(files)]
    _field_icons(files, places)
    _text(files, len(places))


def _field_icons(files, places: list) -> None:
    """Four rows per group at its place (x, z, y), at a group's size: the module gives a group of one check its own."""
    sheet = files.readFile("UiFieldMapIcons.gsheet")
    # Parsed up front: appending to the sheet invalidates references to its rows.
    rows = [parseStruct(row) for row in sheet["values"]]
    next_id = max(row["mId"] for row in rows) + 1
    for g, (x, z, y) in enumerate(places):
        for look, mark in enumerate(LOOKS):
            name = f"{FLAG_PREFIX}{mark}{g}"
            row = dict(rows[0])
            row.update({"mId": next_id + len(LOOKS) * g + look, "mNameLabel": name,
                        "mType": ap_map_icons.frame(look, False),
                        "mFirstShowFlagName": name, "mPosX": float(x), "mPosY": float(y), "mPosZ": float(z),
                        "mArg00": ""})
            sheet["values"].append(dictToStruct(row))
    files.writeFile("UiFieldMapIcons.gsheet", sheet)


def dungeon_chests(files, location_ids: dict, placeholder_key: str) -> list:
    """[(location, level, x, z)]: the chests of the dungeon locations, found by the placeholder with the location's
    index; in each room of the location (Eagle's Tower before and after its collapse). For ap_map.lines."""
    positions = load_positions()
    chests = []
    for loc, room_name in CHEST_ROOMS.items():
        if loc not in location_ids or not room_name.startswith("Lv"):
            continue
        local_id = int(location_ids[loc])
        rooms = {room_name} | {room for room, *_ in positions["dungeon"].get(loc, ())}
        for room in sorted(rooms):
            for actor in files.readFile(f"{room}.leb").actors:
                key = actor.parameters[1]
                if (actor.type == CHEST_ACTOR and isinstance(key, bytes) and key == placeholder_key.encode()
                        and actor.parameters[2] == local_id):
                    chests.append((loc, room.split("_")[0], actor.posX, actor.posZ))
    return chests


def _text(files, groups: int) -> None:
    """Place.msbt entries for the icons' names and the dungeon dots' name, whose texts the module gives."""
    for relative_path, msbt in files.getAllTextFiles("Place.msbt"):
        for g in range(groups):
            for mark in LOOKS:
                msbt.addEntry(f"{FLAG_PREFIX}{mark}{g}", "")
        msbt.addEntry(DOT_LABEL, "")
        files.writeTextFile(relative_path, "Place.msbt", msbt)
