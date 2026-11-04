"""Shuffled dungeons: each dungeon's overworld door leads into the dungeon the patch maps it to, and that dungeon's
ways out lead out of that door (DESIGN.md, Shuffled dungeons).

The module leads the level jumps (`jumps`; the seed file's jumps): a jump from the overworld to a dungeon's entrance
goes to the entrance of the dungeon behind that door, and a jump from a dungeon to the outside of its own door (its
exit, its instrument's warp, the Fairy Queen's warps and her room's loading zone) goes to the outside of the door that
leads into it. The rooms and flowcharts keep their vanilla targets. The module also swaps the overworld map's dungeon
markers (`markers`; the seed file's markers)."""
import re
from typing import List, Mapping

import ap_assets

FIELD = "Field"


def level_of(locator: str) -> str:
    """The level of a locator named after its room (Lv01TailCave_08D, Field_03L_a)."""
    return re.match(r"(.+)_\d\d[A-Z]", locator).group(1)


def entrance(dungeon: str) -> str:
    """The locator inside a dungeon where its door leads."""
    room, suffix, _, _, _ = ap_assets.data()["dungeon_entrances"][dungeon]
    return room + suffix


def outside(door: str) -> str:
    """The locator outside a dungeon's door, where its exit leads."""
    _, _, field_room, suffix, _ = ap_assets.data()["dungeon_entrances"][door]
    return field_room + suffix


def jumps(entrances: Mapping[str, str]) -> List[str]:
    """The jumps' lines: per door that leads into another dungeon, its jump in and that dungeon's jumps out."""
    lines = []
    for door, dungeon in entrances.items():
        if door == dungeon:
            continue
        lines.append(f"{FIELD} {level_of(entrance(door))} {entrance(door)} {level_of(entrance(dungeon))} "
                     f"{entrance(dungeon)}")
        lines.append(f"{level_of(entrance(dungeon))} {FIELD} {outside(dungeon)} {FIELD} {outside(door)}")
    return lines


def markers(entrances: Mapping[str, str]) -> List[str]:
    """The markers' lines: per door that leads into another dungeon, its map marker's label, then the label and show
    flag of the dungeon's, which the module gives the door's marker: it names the dungeon behind the door and shows
    once that dungeon is entered."""
    icons = ap_assets.data()["dungeon_map_icons"]
    return [f"{icons[door][0]} {' '.join(icons[dungeon])}"
            for door, dungeon in entrances.items() if door != dungeon]
