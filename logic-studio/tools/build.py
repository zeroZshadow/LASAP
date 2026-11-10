"""Writes Logic Studio's data for Link's Awakening from the RomFS: each check's place in checks.yml, the places file,
the level pictures and doors.yml, where logic-studio/game.yml says. Prints what the extractor found odd, and fails
when a check's index is not the one its actor gives.

Run with .venv-ap: python logic-studio/tools/build.py [--check]. Needs "romfs" in scripts/local_paths.json.
"""
import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import checks  # noqa: E402
import doors  # noqa: E402
import extract  # noqa: E402
import model_bounds  # noqa: E402  (on the path through extract)
import render  # noqa: E402

MANIFEST = HERE.parent / "game.yml"
SHOWN = ("overworld", "interior", "dungeon")  # levels Logic Studio draws; the Chamber Dungeon's panels and the ending are not
ROOM_FIELDS = ("name", "x", "z", "w", "h", "side", "floor")
# What the report lists, as the build prints it
FINDINGS = {"target_not_found": "Teleport target not found", "no_companion": "Entrance with no companion",
            "flow_targets_not_found": "Flowchart jump target not found", "warp_points_not_found": "Warp point not found",
            "flow_jumps_from_arguments": "Flowchart jump decided by its caller", "checks_level_only": "Check known only by its level",
            "checks_no_area": "Check off every walkable tile", "rooms_without_grid": "Levels with rooms that have no grid",
            "actors_not_followed": "Moves not followed"}


def paths():
    manifest = yaml.safe_load(MANIFEST.read_text(encoding="utf-8"))
    return tuple((MANIFEST.parent / manifest[key]).resolve() for key in ("checks", "places", "maps", "doors"))


def room_versions():
    """Room -> the first of the rooms the manifest lists as one with it."""
    manifest = yaml.safe_load(MANIFEST.read_text(encoding="utf-8"))
    return {room: rooms[0] for rooms in manifest.get("room_versions") or () for room in rooms}


def same_marks(marks):
    """Locks that share a save flag are one lock: a mark's `same` when the flag is on more than one of a level's marks. Every id is unique."""
    used = Counter(m["flag"] for m in marks if m["flag"] is not None)
    for m in marks:
        flag = m.pop("flag")
        if used[flag] > 1:
            m["same"] = f"flag-{flag}"
    ids = Counter(m["id"] for m in marks)
    if max(ids.values()) > 1:
        raise SystemExit(f"Two door marks with one id: {[key for key, n in ids.items() if n > 1]}")


def same_doors(teleports, levels, versions):
    """Doors at one place in the versions of a room are one door: they share a `same`."""
    rooms = {room["name"]: room for rs in levels.values() for room in rs}
    places = defaultdict(list)
    for t in teleports:
        if t["door"] and t["room"] in versions:
            places[(versions[t["room"]], t["x"] - rooms[t["room"]]["x"], t["z"] - rooms[t["room"]]["z"])].append(t)
    for (room, x, z), found in places.items():
        if len(found) > 1:
            for t in found:
                t["same"] = f"{room}-door-{x:g}-{z:g}"


def add_same(index, levels):
    """What the versions of a room share, on the extracted data: the doors' `same` and the checks' `also`."""
    versions = room_versions()
    same_doors(index["teleports"], levels, versions)
    checks.add_versions(index["checks"], levels, versions)


def item_pool():
    """items.yml's items, by LASR name."""
    items = yaml.safe_load((HERE.parents[1] / "apworld" / "las" / "data" / "items.yml").read_text(encoding="utf-8"))["Item_Pool"]
    return items


def lines(value):
    """JSON with every entry of a top-level list or map on a line of its own, so a change shows as the lines it touches."""
    out = ["{"]
    keys = list(value)
    for n, key in enumerate(keys):
        item = value[key]
        end = "," if n < len(keys) - 1 else ""
        if isinstance(item, list) and item:
            out.append(f"{json.dumps(key)}: [")
            out += [json.dumps(entry, ensure_ascii=False, separators=(",", ":")) + ("," if i < len(item) - 1 else "") for i, entry in enumerate(item)]
            out.append("]" + end)
        elif isinstance(item, dict) and item:
            out.append(f"{json.dumps(key)}: {{")
            out += [f"{json.dumps(str(k))}:{json.dumps(v, ensure_ascii=False, separators=(',', ':'))}" + ("," if i < len(item) - 1 else "") for i, (k, v) in enumerate(item.items())]
            out.append("}" + end)
        else:
            out.append(f"{json.dumps(key)}: {json.dumps(item, separators=(',', ':'))}{end}")
    return "\n".join(out + ["}"]) + "\n"


def write_if_changed(path, data):
    if not path.exists() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def side_terrain(rooms):
    """What a level's side-view room models show on the plane Link plays on."""
    folder = Path(extract.lookup("romfs")) / "region_common" / "map"
    placed = [(folder / f"{room['name']}.bfres", room["origin"]) for room in rooms if room["side"] and "origin" in room]
    return [face for file, origin in placed if file.exists() for face in render.terrain(model_bounds.shapes(str(file)), origin)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="print the totals and findings, write nothing")
    check = parser.parse_args().check
    checks_file, places_file, maps, doors_file = paths()
    entries = checks.read(checks_file)
    levels, index, report = extract.build({name: fields for name, (fields, _) in entries.items()})
    items = item_pool()
    keys = {items[fields['content']]['item-key'] for fields, _ in entries.values() if 'index' in fields and fields.get('content') in items}
    add_same(index, levels)
    entries, differ = checks.merged(entries, index.pop("checks"), levels, keys)
    kinds = {level["name"]: level["kind"] for level in index["levels"]}
    layout, pictures, marks, files = {}, {}, {}, {}
    for name, rooms in levels.items():
        if kinds[name] not in SHOWN:
            continue
        drawn = [r for r in rooms if r.get("flags")]
        layout[name] = [{k: r[k] for k in ROOM_FIELDS if k in r} for r in drawn]
        opened, found = render.doors(rooms) if name != extract.FIELD else (set(), [])
        if found:
            same_marks(found)
            marks[name] = found
        if render.top_rooms(rooms):
            img, (x, z) = render.level_picture(rooms, opened)
            files[f"{name}.png"] = render.png(img)
            pictures[name] = {"file": f"{name}.png", "x": x, "z": z}
        terrain = side_terrain(drawn)  # one room's model can hold its neighbours' terrain, so a level's models are read together
        for room in drawn:
            if room["side"]:
                modelled = render.has_terrain(room, terrain)
                img = render.side_picture(room, wall=render.SIDE_BACK if modelled else render.SIDE_WALL)  # the model says what is solid
                if modelled:
                    render.draw_terrain(img, room, terrain)
                files[f"{room['name']}.png"] = render.png(img)
                pictures[room["name"]] = {"file": f"{room['name']}.png"}
    cells = [re.fullmatch(r"Field_(\d+)([A-Z])", room["name"]) for room in layout[extract.FIELD]]
    overworld = {"level": extract.FIELD, "columns": sorted({m[2] for m in cells if m}), "rows": sorted({m[1] for m in cells if m})}
    places = {"tile": render.TILE, "px": render.PX, "side_tiles": render.SIDE_TILES, "overworld": overworld, "colours": render.COLOURS,
              "doors": {letter: name for letter, name in render.DOORS.values()}, **index, "layout": layout, "pictures": pictures, "marks": marks}
    print(json.dumps(report["totals"]))
    for key, title in FINDINGS.items():
        found = report.get(key)
        if found:
            print(f"{title}: {len(found)}")
            for entry in (found.items() if isinstance(found, dict) else found):
                print(f"  {entry}")
    if differ:
        raise SystemExit("Index not the one its actor gives (check, index, actor's): " + "; ".join(map(str, differ)))
    if check:
        return
    write_if_changed(checks_file, checks.text(entries).encode("utf-8"))
    write_if_changed(places_file, lines(places).encode("utf-8"))
    for file, data in files.items():
        write_if_changed(maps / file, data)
    for stale in maps.glob("*.png"):
        if stale.name not in files:
            stale.unlink()
    doors.write(index["teleports"], doors_file)
    print(f"Wrote {checks_file.name}, {places_file.name}, {len(files)} pictures and {doors_file.name}")


if __name__ == "__main__":
    main()
