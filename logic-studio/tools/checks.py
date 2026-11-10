"""checks.yml: every check, one line each. Its place (level, room, x, y, z, exact) is the game tooling's to write; its
facts (content, index) are kept by hand. A check's index is checked against the actor at its place where one carries it.
"""
import re

import yaml

PLACE = ("level", "room", "x", "y", "z", "exact", "also")
FACTS = ("content", "index")
HEAD = ("# Every check: where it is in the game (level, room, x, y, z, exact; written by python logic-studio/tools/build.py)",
        "# and what it holds (content, and index where the game numbers it; kept by hand). Export writes these into logic.yml.")
LINE = re.compile(r"^(?P<name>[^\s#][^:]*): (?P<value>\{.*\})(?P<comment>\s+#.*)?$")
# Item actors whose first parameter is the item's index, and the parameter that hides a shell in a rock, bush, tree or pot
NUMBERED = ("ItemHeartPiece", "ItemSecretSe", "ItemGoldenLeaf", "AreaShellDro")
HIDDEN_SHELL = re.compile(r"Seashell(\d+)")


def read(path):
    """{check: (fields, trailing comment)} in the file's order."""
    found = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = LINE.match(line)
        if match:
            found[match["name"]] = (yaml.safe_load(match["value"]), match["comment"] or "")
    return found


def scalar(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, list):
        return "[" + ", ".join(scalar(part) for part in value) + "]"
    text = str(value)
    plain = yaml.safe_load(f"k: {text}") == {"k": text} and not any(c in text for c in ",{}[]:#")
    return text if plain else '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def text(entries):
    out: list = list(HEAD)
    for name, (fields, comment) in entries.items():
        ordered = [key for key in PLACE + FACTS if key in fields] + [key for key in fields if key not in PLACE + FACTS]
        body = ", ".join(key + ": " + scalar(fields[key]) for key in ordered)
        out.append(name + ": {" + body + "}" + comment)
    written = "\n".join(out) + "\n"
    assert yaml.safe_load(written) == {name: fields for name, (fields, _) in entries.items()}, "a check's line does not read back"
    return written


def actor_index(actor, indexed_keys):
    """The index an actor gives what it holds, or None: an item actor's first parameter, a chest's third parameter when
    its item is one the game numbers, or a hidden shell's number."""
    params = actor["params"]
    if actor["kind"] in NUMBERED and params and isinstance(params[0], int):
        return params[0]
    if actor["kind"] == "ObjTreasureBox" and len(params) > 2 and params[1] in indexed_keys and isinstance(params[2], int):
        return params[2]
    for param in params:
        match = HIDDEN_SHELL.fullmatch(param) if isinstance(param, str) else None
        if match:
            return int(match[1])
    return None


def add_versions(places, levels, versions):
    """Each check's other places: the same actor at the same spot in the other versions of its room. `versions` is room -> the first of its versions."""
    rooms = {room["name"]: room for rs in levels.values() for room in rs}
    for place in places.values():
        room = rooms.get(place.get("room"))
        if not room or not place.get("exact") or place["room"] not in versions:
            continue
        kinds = {a["kind"] for a in room["actors"] if (a["x"], a["z"]) == (place["x"], place["z"])}
        for other, first in versions.items():
            if first == versions[place["room"]] and other != place["room"]:
                x, z = place["x"] + rooms[other]["x"] - room["x"], place["z"] + rooms[other]["z"] - room["z"]
                if any(a["kind"] in kinds and (a["x"], a["z"]) == (x, z) for a in rooms[other]["actors"]):
                    place.setdefault("also", []).append([other, x, z])


def merged(entries, places, levels, indexed_keys):
    """The checks with the extractor's places, and [(check, its index, the actor's)] where the two differ."""
    rooms = {room["name"]: room for rs in levels.values() for room in rs}
    out, differ = {}, []
    for name in list(entries) + [n for n in places if n not in entries]:
        fields, comment = entries.get(name, ({}, ""))
        facts = {key: value for key, value in fields.items() if key not in PLACE}
        place = places.get(name, {})
        out[name] = ({**{key: place[key] for key in PLACE if key in place}, **facts}, comment)
        room = rooms.get(place.get("room"))
        if room and place.get("exact") and "index" in facts:
            here = [a for a in room["actors"] if (a["x"], a["z"]) == (place["x"], place["z"])]
            given = {actor_index(a, indexed_keys) for a in here} - {None}
            if given and facts["index"] not in given:
                differ.append((name, facts["index"], sorted(given)))
    return out, differ
