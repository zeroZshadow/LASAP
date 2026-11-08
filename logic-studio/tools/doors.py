"""doors.yml from the teleports the extractor finds. The world cannot read the RomFS, so the table lives in the repo: names only."""
FIELD = "Field"
HEAD = ("# Where the game's doors lead: names only. Generated: python logic-studio/tools/build.py",
        "# A zone in logic.yml lists its doors; a door no zone lists is not in logic.")


def table(teleports):
    """A door's name is the spot its partner lands on, so names come in pairs; a door with no partner goes by its room.
    The overworld's door comes first in a pair."""
    by_id = {t["id"]: t for t in teleports}
    pairs, one_way = {}, {}
    for t in teleports:
        if not t["entrance"]:
            continue
        if not t["companion"]:
            one_way[t["room"]] = t["to"]
        elif t["level"] == FIELD:
            pairs[by_id[t["companion"]]["to"]] = t["to"]
    return {"pairs": dict(sorted(pairs.items())), "one-way": dict(sorted(one_way.items()))}


def text(found):
    lines = list(HEAD) + ["pairs:"] + [f"  {a}: {b}" for a, b in found["pairs"].items()] + ["one-way:"] + [f"  {a}: {b}" for a, b in found["one-way"].items()]
    return "\n".join(lines) + "\n"


def write(teleports, file):
    file.write_bytes(text(table(teleports)).encode("utf-8"))
