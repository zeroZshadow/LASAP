"""Shuffled dungeons: each dungeon's overworld door leads into the dungeon the patch maps it to, and that dungeon's
ways out lead out of that door (DESIGN.md, Shuffled dungeons).

The module leads the level jumps (`jumps`; the seed file's jumps): a jump from the overworld to a dungeon's entrance
goes to the entrance of the dungeon behind that door, and a jump from a dungeon to the outside of its own door (its
exit, its instrument's warp, the Fairy Queen's warps and her room's loading zone) goes to the outside of the door that
leads into it. The rooms and flowcharts keep their vanilla targets. The module also swaps the overworld map's dungeon
markers (`markers`; the seed file's markers)."""
import re
from typing import List, Mapping, Optional

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
    """The jumps' lines of the overworld doors that lead elsewhere (Entrances): entrances maps such a door to the inside
    door it leads to, pairs to the one it led to. A door goes by the spot its partner lands on, so the overworld's jump
    through the door goes to the new inside door's spot, and that inside's jump out to the door's own spot. Tarin's
    house's jump out is from any level: the warp menu and the Save & Quit after a game over lead outside its door too.
    A jump out onto a covered door (OPENING_FLAGS) also names the flag that opens it."""
    door_of = {inside: door for door, inside in pairs.items()}
    lines = []
    for door, inside in entrances.items():
        lines.append(f"{FIELD} {level_of(pairs[door])} {pairs[door]} {level_of(inside)} {inside}")
        out_from = ANY_LEVEL if door_of[inside] == HOME else level_of(inside)
        opening = f" {OPENING_FLAGS[door]}" if door in OPENING_FLAGS else ""
        lines.append(f"{out_from} {FIELD} {door_of[inside]} {FIELD} {door}{opening}")
    return lines


def start_jumps(start: Optional[str]) -> List[str]:
    """The jumps' line of a random start (Random start): the jump to the spot outside Tarin's door, from any level (the
    house's exit, the warp menu, the Save & Quit after a game over), goes to the spot outside the start door instead,
    with the flag that opens it if something covers it."""
    if not start or start == HOME:
        return []
    opening = f" {OPENING_FLAGS[start]}" if start in OPENING_FLAGS else ""
    return [f"{ANY_LEVEL} {FIELD} {HOME} {FIELD} {start}{opening}"]


def markers(entrances: Mapping[str, str]) -> List[str]:
    """The markers' lines: per door that leads into another dungeon, its map marker's label, then the label and show
    flag of the dungeon's, which the module gives the door's marker: it names the dungeon behind the door and shows
    once that dungeon is entered."""
    icons = ap_assets.data()["dungeon_map_icons"]
    return [f"{icons[door][0]} {' '.join(icons[dungeon])}"
            for door, dungeon in entrances.items() if door != dungeon]
