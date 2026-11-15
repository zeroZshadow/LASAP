"""The seed file's jumps, returns and markers for Shuffled dungeons and Entrances (DESIGN.md, Shuffled dungeons,
Entrances)."""
import re
from typing import List, Mapping

import ap_assets

FIELD = "Field"
ANY_LEVEL = "*"  # a jumps line's from level that every level matches
# The spot outside Tarin's door: where leaving the house lands, and where the warp menu (mod.WARP_HOME) and LASR's
# Save & Quit after a game over jump from any level
HOME = "Field_11C"
# The overworld door spots something covers while the door is still shut from outside, and the flag that opens it
# (scripts/re/ENTRANCES.md, Blocked landings): an inside led out onto one sets the flag as Link leaves, so he comes up
# the stairs or out of the doorway as in vanilla, and the door is open from outside too. The stairs under a rock,
# grass, the grave or the weathercock (its flag is the door's gate), Martha's statue, and the cracked walls in front
# of the landing. The Armos stairs (Field_10N, Field_11O) are left out, as the Armos clears its flag when it is made,
# and Mamu's (Field_14E), whose flag opens the maze.
OPENING_FLAGS = {
    "Field_01E": "AppearStairsFld02E", "Field_01H": "AppearStairsFld02H", "Field_05K": "AppearStairsFld05K",
    "Field_06C": "AppearStairsFld06C", "Field_08G": "AppearStairsFld08G", "Field_11K": "AppearStairsFld11K",
    "Field_10C": "WeathercockOpened", "Field_15J": "MarthaStatueOpened",
    "Field_02D": "FieldCrackedWall02D", "Field_02P_b": "FieldCrackedWall02P", "Field_09G": "FieldCrackedWall09G",
    "Field_09H": "FieldCrackedWall09H", "Field_09N": "FieldCrackedWall09N", "Field_13N_c": "FieldCrackedWall13N",
    "Field_16E": "FieldCrackedWall16E",
}


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


def door_jumps(entrances: Mapping[str, str], pairs: Mapping[str, str]) -> List[str]:
    """The jumps' lines of the overworld doors that lead elsewhere: entrances maps such a door to the inside door it
    leads to, pairs to the one it led to. Tarin's house's jump out is from any level: the warp menu and the Save & Quit
    after a game over lead outside its door too. A jump out onto a covered door also names the flag that opens it."""
    door_of = {inside: door for door, inside in pairs.items()}
    lines = []
    for door, inside in entrances.items():
        lines.append(f"{FIELD} {level_of(pairs[door])} {pairs[door]} {level_of(inside)} {inside}")
        out_from = ANY_LEVEL if door_of[inside] == HOME else level_of(inside)
        opening = f" {OPENING_FLAGS[door]}" if door in OPENING_FLAGS else ""
        lines.append(f"{out_from} {FIELD} {door_of[inside]} {FIELD} {door}{opening}")
    return lines


def returns(entrances: Mapping[str, str], pairs: Mapping[str, str]) -> List[str]:
    """The returns' lines, per spot a led overworld door lands on: "<level> <spot> <level> <locator>", the jump of the
    door at that spot, as the game names it before the jumps lead it. A Link who cannot stand where he landed (water
    without the flippers) goes back through it, to where he came from (DESIGN.md, Entrances, Water landings)."""
    door_of = {inside: door for door, inside in pairs.items()}
    lines = []
    for door, inside in entrances.items():
        lines.append(f"{level_of(inside)} {inside} {FIELD} {door_of[inside]}")
        lines.append(f"{FIELD} {door} {level_of(pairs[door])} {pairs[door]}")
    return lines


def dungeon_returns(entrances: Mapping[str, str]) -> List[str]:
    """returns' lines for the dungeon doors that lead into other dungeons: the dungeon's way out from where its door
    lands, and the door's jump in from outside it."""
    lines = []
    for door, dungeon in entrances.items():
        if door == dungeon:
            continue
        lines.append(f"{level_of(entrance(dungeon))} {entrance(dungeon)} {FIELD} {outside(dungeon)}")
        lines.append(f"{FIELD} {outside(door)} {level_of(entrance(door))} {entrance(door)}")
    return lines


def markers(entrances: Mapping[str, str]) -> List[str]:
    """The markers' lines: per door that leads into another dungeon, its map marker's label, then the label and show
    flag of the dungeon behind it."""
    icons = ap_assets.data()["dungeon_map_icons"]
    return [f"{icons[door][0]} {' '.join(icons[dungeon])}"
            for door, dungeon in entrances.items() if door != dungeon]
