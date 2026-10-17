"""Shuffled dungeons: the Color Dungeon's other ways out lead out of the door that leads into it.

LASR's EntranceRandomizer moves each dungeon's door and exit, the map's dungeon markers, the instrument warps and the
Fairy Queen's warp after her gifts (FreeGiftsRandomizer, Event37). The Fairy Queen's room also has stairs out, and her
flow other warps; in the game all of them go to the Color Dungeon's own door (Field_08H)."""
import re
from typing import Mapping, Optional

from RandomizerCore.Randomizers.data import DUNGEON_ENTRANCES
from RandomizerCore.Tools import event_tools

FAIRY_ROOM = "Lv10ClothesDungeon_04C"
FAIRY_FLOW = "FairyQueen.bfevfl"
FAIRY_WARPS = ("Event22", "Event26", "Event37", "Event42")  # GameControl RequestLevelJump to Field_08H


def color_dungeon_exit(entrances: Mapping[str, str]) -> Optional[str]:
    """The locator outside the door that leads into the Color Dungeon, None for its own."""
    door = next(door for door, dungeon in entrances.items() if dungeon == "color-dungeon")
    if door == "color-dungeon":
        return None
    _, _, field_room, suffix, _ = DUNGEON_ENTRANCES[door]
    return field_room + suffix


def add(files, entrances: Mapping[str, str]) -> None:
    locator = color_dungeon_exit(entrances)
    if locator is None:
        return
    level = re.match(r"(.+)_\d\d[A-Z]", locator).group(1)
    room = files.readFile(f"{FAIRY_ROOM}.leb")
    room.setLoadingZoneTarget(locator, 0)
    files.writeFile(f"{FAIRY_ROOM}.leb", room)
    flow = files.readFile(FAIRY_FLOW)
    for name in FAIRY_WARPS:
        params = event_tools.findEvent(flow.flowchart, name).data.params.data
        params["level"] = level
        params["locator"] = locator
    files.writeFile(FAIRY_FLOW, flow)
