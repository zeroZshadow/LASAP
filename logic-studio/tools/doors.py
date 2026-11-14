"""doors.yml from the teleports the extractor finds. The world cannot read the RomFS, so the table lives in the repo: names,
and the groups an entrance shuffle draws its doors from."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "patcher" / "modbuild"))
from RandomizerCore.Randomizers.data import DUNGEON_ENTRANCES  # noqa: E402

FIELD = "Field"
HEAD = ("# Where the game's doors lead, by name, and the groups an entrance shuffle draws from. Generated: python logic-studio/tools/build.py",
        "# A zone in logic.yml lists its doors; a door no zone lists is not in logic.")
# The pairs with an overworld side, by kind; a pair goes by its overworld name. Pairs inside the game's levels, one-way
# doors and the fixed doors are in none.
# Overworld doors no shuffle moves. Catfish's Maw's dives: Link comes up out of the water at the other end, which he
# leaves only with the flippers; another door's inside led there crashes the game without them. Dampe's hut and the
# Rapids Ride's hut are never shuffled.
FIXED = {"Field_01G": "the Wind Fish's Egg", "Field_04H": "Dampe's hut", "Field_04P": "the Rapids Ride's hut",
         "Field_14J_a": "Catfish's Maw's dive", "Field_14J_c": "Catfish's Maw's dive"}
GROUPS = {
    "dungeon": "a dungeon's own door, the one shuffled dungeons move",
    "dungeon-other": "another overworld door into a dungeon's level",
    "connector": "a door into a level with more than one overworld door",
    "single": "the one overworld door into its level",
}


def dungeon_doors():
    """Each dungeon's own overworld door, as LASR's dungeon shuffle and the patcher's entrance jumps name it."""
    return {field_room + suffix for _, _, field_room, suffix, _ in DUNGEON_ENTRANCES.values()}


def group_of(door, inside_level, field_doors, dungeons):
    if door in dungeons:
        return "dungeon"
    if inside_level.startswith("Lv"):
        return "dungeon-other"
    return "connector" if field_doors[inside_level] > 1 else "single"


def table(teleports, dungeons=None):
    """A door's name is the spot its partner lands on, so names come in pairs; a door with no partner goes by its room.
    The overworld's door comes first in a pair; two doors inside go by their ids."""
    dungeons = dungeon_doors() if dungeons is None else dungeons
    by_id = {t["id"]: t for t in teleports}
    pairs, one_way, named, inside = {}, {}, {}, {}
    for t in teleports:
        if not t["door"]:
            continue
        name = by_id[t["companion"]]["to"] if t["companion"] else t["room"]
        if name in named:
            raise ValueError(f"Two doors named {name}: {named[name]} and {t['id']}")
        named[name] = t["id"]
        if not t["companion"]:
            one_way[t["room"]] = t["to"]
        elif t["level"] == FIELD or (by_id[t["companion"]]["level"] != FIELD and t["id"] < t["companion"]):
            pairs[by_id[t["companion"]]["to"]] = t["to"]
            if t["level"] == FIELD and t["to_level"] != FIELD:
                inside[by_id[t["companion"]]["to"]] = t["to_level"]
    field_doors = {}
    for level in inside.values():
        field_doors[level] = field_doors.get(level, 0) + 1
    groups = {group: [] for group in GROUPS}
    for door in sorted(set(inside) - set(FIXED)):
        groups[group_of(door, inside[door], field_doors, dungeons)].append(door)
    return {"pairs": dict(sorted(pairs.items())), "one-way": dict(sorted(one_way.items())), "groups": groups}


def text(found):
    lines = list(HEAD) + ["pairs:"] + [f"  {a}: {b}" for a, b in found["pairs"].items()] + ["one-way:"] + [f"  {a}: {b}" for a, b in found["one-way"].items()]
    lines += ["groups:  # the pairs with an overworld side, by their overworld name; pairs inside the levels and the doors that stay are in none. Stay: "
              + ", ".join(f"{door} ({what})" for door, what in FIXED.items())]
    for group, names in found["groups"].items():
        lines += [f"  {group}:  # {GROUPS[group]}"] + [f"    - {name}" for name in names]
    return "\n".join(lines) + "\n"


def write(teleports, file):
    file.write_bytes(text(table(teleports)).encode("utf-8"))
