"""Writes Universal Tracker's map tab (DESIGN.md, Universal Tracker map): apworld/las/tracker/maps.json,
locations.json and items.json, for the images the client builds from the player's RomFS (patcher/ap_tracker_map.py).

Run with .venv-ap: python scripts/ut_map.py [<PopTracker pack folder>]
- maps.json: ut_map.MAPS with ap_tracker_map's images.
- An overworld location's pins: those of its sections in the LASR PopTracker pack (github.com/Br00ty/lasr_brooty,
  poptracker_map.location_codes), one section per AP location named after it. The pack's island is the game's at 2/3
  (PACK_TO_ISLAND, found by matching the two images), so its pins move onto ap_tracker_map's island exactly.
- A dungeon location's pin: its place in its room (checks.yml), on ap_tracker_map's grid of rooms. A dungeon check is
  only on its dungeon's map: the overworld's dungeon pins stand at the dungeon's own door, which may lead into another
  dungeon.
- A shuffled overworld door's pin: its door on the overworld (Logic Studio's places.json teleports), placed by an
  affine fit from the exact overworld checks' world coordinates (checks.yml) to their pack pins. Its section is named
  after the door's AP entrance (Entrances); a dungeon's own door also carries its dungeon's (lasr.door_entrance,
  Shuffled dungeons). UT shows what the slot has: the server's locations, the world's entrances.
- items.json: the items of ut_map.ITEM_SECTIONS and DUNGEON_SECTIONS that ap_tracker_map.ITEM_ICONS has an icon for,
  under their AP names; a progressive item has one icon per level.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "vendor" / "Archipelago"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / "patcher"))
from worlds.las.items import LASR_TO_ITEM  # noqa: E402
from worlds.las.lasr import DOORS, DUNGEON_DOORS, door_entrance  # noqa: E402
from worlds.las.locations import display_name  # noqa: E402
from worlds.las.ut_map import DUNGEON_SECTIONS, ITEM_SECTIONS, MAPS  # noqa: E402
import ap_tracker_map  # noqa: E402
import poptracker_map  # noqa: E402
from local_paths import local_path  # noqa: E402

OUT = ROOT / "apworld" / "las" / "tracker"
STUDIO = ROOT / "logic-studio"
DUNGEON_NUMBERS = (*range(1, 9), 0)  # MAPS[1:]'s dungeons
DOOR_GROUPS = ("dungeon", "connector", "single")  # doors.yml's groups an entrance shuffle moves
# A pack pin (x, y) -> ap_tracker_map's island in texture pixels: scale, then x and y offsets
PACK_TO_ISLAND = (1.5, -45.0, -41.0)
# Pin sizes in image pixels: the island's checks, its doors (smaller, so that a check's pin at its door shows) and a
# dungeon's checks
ISLAND_PIN, DOOR_PIN, DUNGEON_PIN = 30, 12, 11


def door_spots() -> dict:
    """Overworld door name -> (x, z, height): the door on the overworld. A door goes by the spot its partner lands
    on."""
    teleports = json.loads((STUDIO / "places.json").read_text(encoding="utf-8"))["teleports"]
    by_id = {t["id"]: t for t in teleports}
    return {by_id[t["companion"]]["to"]: (t["x"], t["z"], t["y"]) for t in teleports
            if t["level"] == "Field" and t["door"] and t["companion"]}


def world_to_pin(pins: dict):
    """The affine map from overworld coordinates (x, z, height) to the pack's overworld map, which draws higher places
    further north, fitted through the overworld checks whose place is exact (checks.yml) at their height
    (patcher/map_positions.json)."""
    checks = yaml.safe_load((STUDIO / "checks.yml").read_text(encoding="utf-8"))
    world = json.loads((ROOT / "patcher" / "map_positions.json").read_text(encoding="utf-8"))["overworld"]
    anchors = [(world[loc], pins[loc]) for loc, c in checks.items()
               if c.get("level") == "Field" and c.get("exact") and loc in pins and loc in world]
    rows = [(*w, 1.0) for w, _ in anchors]
    fx = poptracker_map.least_squares(rows, [p[0] for _, p in anchors])
    fy = poptracker_map.least_squares(rows, [p[1] for _, p in anchors])

    def place(*w: float):
        return (sum(a * b for a, b in zip(fx, (*w, 1.0))), sum(a * b for a, b in zip(fy, (*w, 1.0))))

    errors = [math.dist(place(*w), p) for w, p in anchors]
    print(f"door fit: {len(anchors)} anchors, mean error {sum(errors) / len(errors):.1f} px, max {max(errors):.1f}")
    return place


def on_island(x: float, y: float):
    """A pack pin's place on ap_tracker_map's island image."""
    scale, dx, dy = PACK_TO_ISLAND
    return round((x * scale + dx) / ap_tracker_map.STEP), round((y * scale + dy) / ap_tracker_map.STEP)


def in_dungeon(check: dict):
    """A check's place on its dungeon's image (ap_tracker_map's grid of rooms), from its level coordinates."""
    (gx, gy), (rw, rh), (uw, uh) = ap_tracker_map.GRID_ORIGIN, ap_tracker_map.ROOM_PIXELS, ap_tracker_map.ROOM_UNITS
    step = ap_tracker_map.STEP
    return round((gx + check["x"] / uw * rw) / step), round((gy + check["z"] / uh * rh) / step)


def maps_json() -> list:
    maps = [{"name": MAPS[0], "img": ap_tracker_map.ISLAND_IMAGE, "location_size": ISLAND_PIN}]
    for name, number in zip(MAPS[1:], DUNGEON_NUMBERS):
        maps.append({"name": name, "img": ap_tracker_map.dungeon_image(number), "location_size": DUNGEON_PIN,
                     "location_border_thickness": 2})
    return maps


def locations_json(pack: Path) -> list:
    pins = poptracker_map.section_pins(pack)
    codes = poptracker_map.location_codes(poptracker_map.pack_sections(pack))
    checks = yaml.safe_load((STUDIO / "checks.yml").read_text(encoding="utf-8"))
    dungeon_maps = {ap_tracker_map.LEVELS[n]: name for name, n in zip(MAPS[1:], DUNGEON_NUMBERS)}
    spots = {}  # (map, x, y) -> {"name": a place's name, "sections": [...]}
    unplaced = []
    for lasr in sorted(codes, key=lambda loc: poptracker_map.IDS["locations"][loc]):
        section = {"name": display_name(lasr)}
        places = []
        if re.match(r"D\d-", lasr):
            check = checks.get(lasr, {})
            if check.get("level") in dungeon_maps and "x" in check:
                places.append((dungeon_maps[check["level"]], *in_dungeon(check), check["room"].rpartition("_")[2]))
        else:
            for code in codes[lasr]:
                for m, x, y in pins.get(code, ()):
                    if m == "map":
                        places.append((MAPS[0], *on_island(x, y), code[1:].rpartition("/")[0].rpartition("/")[2]))
        if not places:
            unplaced.append(lasr)
        for m, x, y, place_name in places:
            spot = spots.setdefault((m, x, y), {"name": place_name, "sections": []})
            if section not in spot["sections"]:
                spot["sections"].append(section)
    if unplaced:
        raise SystemExit(f"no pin for {unplaced}")
    place = world_to_pin(poptracker_map.overworld_pins(pack))
    doors = door_spots()
    dungeon_of = {door: dungeon for dungeon, door in DUNGEON_DOORS.items()}
    entrances = []
    for door in (door for group in DOOR_GROUPS for door in DOORS["groups"][group]):
        x, y = on_island(*place(*doors[door]))
        sections = [{"name": door}] + ([{"name": door_entrance(dungeon_of[door])}] if door in dungeon_of else [])
        entrances.append({"name": door, "sections": sections,
                          "map_locations": [{"map": MAPS[0], "x": x, "y": y, "size": DOOR_PIN}]})
    checks = [{"name": spot["name"], "sections": spot["sections"], "map_locations": [{"map": m, "x": x, "y": y}]}
              for (m, x, y), spot in spots.items()]
    print(f"{sum(len(s['sections']) for s in checks)} location sections at {len(checks)} pins, {len(entrances)} doors")
    return [{"name": "Locations", "children": checks}, {"name": "Doors", "children": entrances}]


def items_json() -> list:
    """The panel's items as UT takes them: toggle, consumable (a count), or progressive with one icon per level."""
    items = []
    for lasr in (item for _, section, _ in ITEM_SECTIONS + DUNGEON_SECTIONS for item in section):
        icon = ap_tracker_map.ITEM_ICONS.get(re.sub(r"-D\d$", "", lasr))
        if icon is None:
            continue
        kind = poptracker_map.ITEMS.get(lasr, (None, "toggle"))[1]
        if isinstance(icon, list):
            items.append({"name": LASR_TO_ITEM[lasr], "type": "progressive",
                          "stages": [{"img": ap_tracker_map.icon_image(i)} for i in icon]})
        else:
            items.append({"name": LASR_TO_ITEM[lasr], "type": "consumable" if kind == "consumable" else "toggle",
                          "img": ap_tracker_map.icon_image(icon)})
    return items


def write(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=1, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pack", nargs="?", type=Path)
    args = parser.parse_args()
    pack = args.pack or local_path("lasr_brooty")
    write(OUT / "maps.json", maps_json())
    write(OUT / "locations.json", locations_json(pack))
    write(OUT / "items.json", items_json())
    print(f"written to {OUT}")


if __name__ == "__main__":
    main()
