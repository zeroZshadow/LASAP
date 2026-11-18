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
- An overworld drop's pin (lasr.DROP_NAMES): its hole (places.json's drop teleport), placed as the doors are. Its
  section is the world's way down it (lasr.drop_route), which the client names after the drop (lasr.door_label).
- A dungeon's doors from the overworld (doors.yml's dungeon and dungeon-other groups): a pin on its map where Link
  lands inside (places.json). Its sections: the inside door's entrance (Shuffled dungeons everything); on a dungeon's
  own door every dungeon's door entrance, which the client hides on the maps of the dungeons it is not known to lead
  into (LASContext.load_map); on another door the world's way from the zone outside it to the zone inside
  (lasr.dungeon_route), which the client names after the door (lasr.door_label).
- The goal's pin: its event location (GOAL_LOCATION) at the egg's door on the overworld, placed as the doors are.
  The client colours it by whether the goal is in logic (TrackerMixin.updateTracker).
- The warp points' places on the island (maps.json's "warps", by their items: lasr.WARP_POINTS), where a route's
  warps start and land (TrackerMixin.route_points): Logic Studio's places.json warp points, placed as the doors are.
- A dungeon map's stair letters (maps.json's "stairs", which the client draws over the map: ut_gui.StairLetters):
  the stairs of a side-view passage (places.json layout's side rooms side by side), or of a stair that leads to
  another top-view room, share a letter, A, B, ... from the map's top.
- zone_places.json: each logic zone's middle and each door's and warp point's spot in its level, for the lengths of
  the walks a route takes (ut_map.find_route); not Universal Tracker's.
- items.json: the items of ut_map.ITEM_SECTIONS and DUNGEON_SECTIONS that ap_tracker_map.ITEM_ICONS has an icon for,
  under their AP names; a progressive item has one icon per level.
"""
import argparse
import functools
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
from worlds.las import GOAL_LOCATION  # noqa: E402
from worlds.las.items import LASR_TO_ITEM  # noqa: E402
from worlds.las.lasr import (DOORS, DROP_NAMES, DUNGEON_DOORS, DUNGEONS, WARP_POINTS, WRITTEN_LOGIC,  # noqa: E402
                             door_entrance, drop_route, dungeon_route)
from worlds.las.locations import display_name  # noqa: E402
from worlds.las.ut_map import DUNGEON_SECTIONS, ITEM_SECTIONS, MAPS  # noqa: E402
import ap_tracker_map  # noqa: E402
import poptracker_map  # noqa: E402
from local_paths import local_path  # noqa: E402

OUT = ROOT / "apworld" / "las" / "tracker"
STUDIO = ROOT / "logic-studio"
DUNGEON_NUMBERS = (*range(1, 9), 0)  # MAPS[1:]'s dungeons
DOOR_GROUPS = ("dungeon", "connector", "single")  # doors.yml's groups an entrance shuffle moves
DUNGEON_DOOR_GROUPS = ("dungeon", "dungeon-other")  # doors.yml's groups of doors into a dungeon
# A pack pin (x, y) -> ap_tracker_map's island in texture pixels: scale, then x and y offsets
PACK_TO_ISLAND = (1.5, -45.0, -41.0)
# Pin sizes in image pixels: the island's checks, its doors (smaller, so that a check's pin at its door shows) and a
# dungeon's checks
ISLAND_PIN, DOOR_PIN, DUNGEON_PIN = 30, 12, 11
EGG_PIN = 24  # the goal's pin at the egg, a diamond like the doors'
LETTERS = "ABCDEFGHJKLMNP"  # the stair letters, in the order they are given out (I and O read as 1 and 0)
STAIRS = ("AreaUpstairs", "AreaDownstairs")  # places.json's teleport kinds of a dungeon's stairs


@functools.cache
def places() -> dict:
    return json.loads((STUDIO / "places.json").read_text(encoding="utf-8"))


@functools.cache
def checks() -> dict:
    return yaml.safe_load((STUDIO / "checks.yml").read_text(encoding="utf-8"))


def door_spots() -> dict:
    """Overworld door name -> (x, z, height): the door on the overworld. A door goes by the spot its partner lands
    on."""
    teleports = places()["teleports"]
    by_id = {t["id"]: t for t in teleports}
    return {by_id[t["companion"]]["to"]: (t["x"], t["z"], t["y"]) for t in teleports
            if t["level"] == "Field" and t["door"] and t["companion"]}


def drop_spots() -> dict:
    """Overworld drop name (lasr.DROP_NAMES) -> (x, z, height): its hole on the overworld."""
    return {t["room"]: (t["x"], t["z"], t["y"]) for t in places()["teleports"]
            if t["level"] == "Field" and t.get("drop") and t["room"] in DROP_NAMES}


def landings() -> dict:
    """Overworld door name -> (room, x, z): where Link lands inside the door, in the level's coordinates."""
    teleports = places()["teleports"]
    by_id = {t["id"]: t for t in teleports}
    return {by_id[t["companion"]]["to"]: (t["landing"]["room"], t["landing"]["x"], t["landing"]["z"])
            for t in teleports if t["level"] == "Field" and t["door"] and t["companion"] and t["landing"]}


def dungeon_doors(dungeon_maps: dict) -> list:
    """Each door into a dungeon's level: a pin on the dungeon's map where Link lands inside."""
    spots, origins = landings(), room_origins()
    pins = []
    for door in (door for group in DUNGEON_DOOR_GROUPS for door in DOORS["groups"][group]):
        inside = DOORS["pairs"][door]
        room, x, z = spots[door]
        sections = [{"name": inside}]
        if door in DOORS["groups"]["dungeon"]:
            sections += [{"name": door_entrance(dungeon)} for dungeon in DUNGEONS]
        else:
            sections.append({"name": dungeon_route(door)})
        x, y = in_dungeon(room, x, z, origins)
        pins.append({"name": inside, "sections": sections,
                     "map_locations": [{"map": dungeon_maps[room.partition("_")[0]], "x": x, "y": y, "size": DOOR_PIN}]})
    return pins


def world_to_pin(pins: dict):
    """The affine map from overworld coordinates (x, z, height) to the pack's overworld map, which draws higher places
    further north, fitted through the overworld checks whose place is exact (checks.yml) at their height
    (patcher/map_positions.json)."""
    world = json.loads((ROOT / "patcher" / "map_positions.json").read_text(encoding="utf-8"))["overworld"]
    anchors = [(world[loc], pins[loc]) for loc, c in checks().items()
               if c.get("level") == "Field" and c.get("exact") and loc in pins and loc in world]
    rows = [(*w, 1.0) for w, _ in anchors]
    fx = poptracker_map.least_squares(rows, [p[0] for _, p in anchors])
    fy = poptracker_map.least_squares(rows, [p[1] for _, p in anchors])

    def place(*w: float):
        return (sum(a * b for a, b in zip(fx, (*w, 1.0))), sum(a * b for a, b in zip(fy, (*w, 1.0))))

    errors = [math.dist(place(*w), p) for w, p in anchors]
    print(f"door fit: {len(anchors)} anchors, mean error {sum(errors) / len(errors):.1f} px, max {max(errors):.1f}")
    return place


@functools.cache
def overworld_place(pack: Path):
    """world_to_pin through the pack's overworld pins."""
    return world_to_pin(poptracker_map.overworld_pins(poptracker_map.pack_locations(pack)))


def warp_spots(place) -> list:
    """Each warp point's place on the island, by its item: its hole or platform (places.json warps) at its height."""
    items = {index: item for item, index in WARP_POINTS.items()}
    heights = {(w["room"], w["x"], w["z"]): w["y"] for w in places()["warps"]}
    spots = []
    for point in places()["warp_points"]:
        x, y = on_island(*place(point["x"], point["z"], heights[point["room"], point["x"], point["z"]]))
        spots.append({"warp": items[point["index"]], "x": x, "y": y})
    return spots


def tiles(spec: str, width: int, height: int) -> list:
    """The tiles (row, column) of a zone in a room (zones.yml's paint): `all`, or rows=columns groups, each a comma
    list of numbers and ranges."""
    def numbers(text: str) -> list:
        found = []
        for part in text.split(","):
            low, _, high = part.partition("-")
            found += range(int(low), int(high or low) + 1)
        return found
    if spec.strip() == "all":
        return [(row, column) for row in range(height) for column in range(width)]
    found = []
    for group in spec.split():
        rows, _, columns = group.partition("=")
        found += [(row, column) for row in numbers(rows) for column in numbers(columns)]
    return found


def zone_places() -> dict:
    """tracker/zone_places.json: each logic zone's middle, the mean of its tiles (Logic Studio's zones.yml paint, in its
    level with the most) or of its doors' spots for a zone not painted, and each door's and warp point's spot (places.json: where a jump to the door lands, and the
    warp point's place), the doors by doors.yml's names and the points by their items, each as [level, x, z] in its
    level's coordinates."""
    paint = yaml.safe_load((STUDIO / "zones.yml").read_text(encoding="utf-8"))["paint"]
    tile = places()["tile"]
    sizes = {room["name"]: (room["x"], room["z"], room["w"], room["h"])
             for layout in places()["layout"].values() for room in layout}
    level_of = {room["name"]: level for level, layout in places()["layout"].items() for room in layout}
    spots: dict = {}
    for room, zones in paint.items():
        ox, oz, width, height = sizes[room]
        for zone, spec in zones.items():
            spots.setdefault(zone, {}).setdefault(level_of[room], []).extend(
                (ox + (column + 0.5) * tile, oz + (row + 0.5) * tile) for row, column in tiles(spec, width, height))
    middles = {}
    for zone, levels in sorted(spots.items()):
        level, points = max(levels.items(), key=lambda kv: len(kv[1]))
        middles[zone] = [level, round(sum(x for x, _ in points) / len(points), 2),
                         round(sum(z for _, z in points) / len(points), 2)]
    doors = set(DOORS["pairs"]) | set(DOORS["pairs"].values()) | set(DOORS["one-way"].values())
    landed = {t["to"]: t["landing"] for t in places()["teleports"] if t["to"] in doors and t["landing"]}
    door_spots = {door: [level_of[land["room"]], land["x"], land["z"]] for door, land in sorted(landed.items())}
    for zone, node in WRITTEN_LOGIC.items():  # a level of one room is one zone, which zones.yml does not paint
        at = [door_spots[door] for door in node.get("doors") or () if door in door_spots]
        if zone not in middles and at:
            level = max({spot[0] for spot in at}, key=lambda name: sum(spot[0] == name for spot in at))
            at = [spot for spot in at if spot[0] == level]
            middles[zone] = [level, round(sum(spot[1] for spot in at) / len(at), 2),
                             round(sum(spot[2] for spot in at) / len(at), 2)]
    middles = dict(sorted(middles.items()))
    items = {index: item for item, index in WARP_POINTS.items()}
    warps = {items[point["index"]]: [level_of[point["room"]], point["x"], point["z"]]
             for point in places()["warp_points"]}
    return {"zones": middles, "doors": door_spots, "warps": warps}


def on_island(x: float, y: float):
    """A pack pin's place on ap_tracker_map's island image."""
    scale, dx, dy = PACK_TO_ISLAND
    return round((x * scale + dx) / ap_tracker_map.STEP), round((y * scale + dy) / ap_tracker_map.STEP)


def room_origins() -> dict:
    """Room name -> (x, z): its top left in its level's coordinates (places.json layout)."""
    layout = places()["layout"]
    return {room["name"]: (room["x"], room["z"]) for rooms in layout.values() for room in rooms}


def in_dungeon(room: str, x: float, z: float, origins: dict):
    """A place on its dungeon's image (ap_tracker_map's grid of rooms): its room's cell, which the room's name gives
    as the image does, and its place in the room. Eagle's Tower's floors lie apart in its level's coordinates."""
    (gx, gy), (rw, rh), (uw, uh) = ap_tracker_map.GRID_ORIGIN, ap_tracker_map.ROOM_PIXELS, ap_tracker_map.ROOM_UNITS
    step = ap_tracker_map.STEP
    row, column = re.search(r"_(\d\d)([A-H])", room).groups()
    ox, oz = origins[room]
    cx, cz = (ord(column) - ord("A")) * uw + x - ox, (int(row) - 1) * uh + z - oz
    return round((gx + cx / uw * rw) / step), round((gy + cz / uh * rh) / step)


def stair_letters(level: str) -> list:
    """A dungeon level's stairs that lead to each other, each pair or group under one letter: those of a side-view
    passage (its side rooms side by side in the layout), and a stair leading to another top-view room with the stair
    it lands at (or its landing). On the dungeon's image, lettered from its top."""
    layout, origins = places()["layout"][level], room_origins()
    teleports = [t for t in places()["teleports"] if t["level"] == level and t["to_level"] == level
                 and t["kind"] in STAIRS]
    by_id = {t["id"]: t for t in teleports}
    side = {room["name"]: room for room in layout if room["side"]}
    passage = {name: name for name in side}

    def root(name: str) -> str:
        while passage[name] != name:
            name = passage[name]
        return name
    for a in side.values():
        for b in side.values():
            if a["z"] == b["z"] and abs(a["x"] + a["w"] * places()["tile"] - b["x"]) < 0.01:
                passage[root(a["name"])] = root(b["name"])
    groups: dict = {}
    for t in teleports:
        if t["room"] in side:
            continue
        to = t["landing"]["room"] if t["landing"] else t["to"]
        here = (t["room"], t["x"], t["z"])
        if to in side:
            groups.setdefault(root(to), set()).add(here)
        else:
            there = by_id.get(t["companion"])
            end = (there["room"], there["x"], there["z"]) if there else (to, t["landing"]["x"], t["landing"]["z"])
            groups.setdefault(frozenset((t["id"], there["id"] if there else to)), set()).update((here, end))
    merged: list = []  # groups that share a stair are one (Eagle's Tower's 04C: from 04G, and to and from 08G)
    for group in groups.values():
        for other in [m for m in merged if m & group]:
            merged.remove(other)
            group = group | other
        merged.append(group)
    # a room the layout has only in side-view variants (Eagle's Tower's 05E) is not on the image
    spots = [sorted((in_dungeon(room, x, z, origins) for room, x, z in group), key=lambda p: (p[1], p[0]))
             for group in merged if len(group) > 1 and all(room in origins for room, _, _ in group)]
    spots.sort(key=lambda group: (group[0][1], group[0][0]))
    if len(spots) > len(LETTERS):
        raise SystemExit(f"{level}: {len(spots)} stair groups, more than LETTERS")
    return [{"letter": letter, "x": x, "y": y} for letter, group in zip(LETTERS, spots)
            for x, y in group]


def maps_json(pack: Path) -> list:
    maps = [{"name": MAPS[0], "img": ap_tracker_map.ISLAND_IMAGE, "location_size": ISLAND_PIN,
             "warps": warp_spots(overworld_place(pack))}]
    for name, number in zip(MAPS[1:], DUNGEON_NUMBERS):
        maps.append({"name": name, "img": ap_tracker_map.dungeon_image(number), "location_size": DUNGEON_PIN,
                     "location_border_thickness": 2, "stairs": stair_letters(ap_tracker_map.LEVELS[number])})
    return maps


def egg_pin(place) -> dict:
    """The goal's pin at the egg's door on the overworld."""
    (door,) = [t for t in places()["teleports"] if t["level"] == "Field" and t["to_level"] == "Lv09WindFishsEgg"]
    x, y = on_island(*place(door["x"], door["z"], door["y"]))
    return {"name": "Wind Fish's Egg", "sections": [{"name": GOAL_LOCATION}],
            "map_locations": [{"map": MAPS[0], "x": x, "y": y, "size": EGG_PIN}]}


def locations_json(pack: Path) -> list:
    locs = poptracker_map.pack_locations(pack)
    codes = poptracker_map.location_codes(locs)
    dungeon_maps = {ap_tracker_map.LEVELS[n]: name for name, n in zip(MAPS[1:], DUNGEON_NUMBERS)}
    origins = room_origins()
    spots = {}  # (map, x, y) -> {"name": a place's name, "sections": [...]}
    unplaced = []
    for lasr in sorted(codes, key=lambda loc: poptracker_map.IDS["locations"][loc]):
        section = {"name": display_name(lasr)}
        places = []
        if re.match(r"D\d-", lasr):
            check = checks().get(lasr, {})
            if check.get("level") in dungeon_maps and "x" in check:
                places.append((dungeon_maps[check["level"]], *in_dungeon(check["room"], check["x"], check["z"], origins),
                               check["room"].rpartition("_")[2]))
        else:
            for code in codes[lasr]:
                for m, x, y in locs.pins.get(code, ()):
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
    place = overworld_place(pack)
    doors = door_spots()
    dungeon_of = {door: dungeon for dungeon, door in DUNGEON_DOORS.items()}
    entrances = []
    for door in (door for group in DOOR_GROUPS for door in DOORS["groups"][group]):
        x, y = on_island(*place(*doors[door]))
        sections = [{"name": door}] + ([{"name": door_entrance(dungeon_of[door])}] if door in dungeon_of else [])
        entrances.append({"name": door, "sections": sections,
                          "map_locations": [{"map": MAPS[0], "x": x, "y": y, "size": DOOR_PIN}]})
    drops = drop_spots()
    if set(drops) != set(DROP_NAMES):
        raise SystemExit(f"no drop teleport for {sorted(set(DROP_NAMES) - set(drops))}")
    for door in DROP_NAMES:
        x, y = on_island(*place(*drops[door]))
        entrances.append({"name": door, "sections": [{"name": drop_route(door)}],
                          "map_locations": [{"map": MAPS[0], "x": x, "y": y, "size": DOOR_PIN}]})
    pins = [{"name": spot["name"], "sections": spot["sections"], "map_locations": [{"map": m, "x": x, "y": y}]}
            for (m, x, y), spot in spots.items()]
    inside = dungeon_doors(dungeon_maps)
    print(f"{sum(len(s['sections']) for s in pins)} location sections at {len(pins)} pins, {len(entrances)} doors, "
          f"{len(inside)} dungeon doors")
    return [{"name": "Locations", "children": pins}, {"name": "Doors", "children": entrances},
            {"name": "Dungeon doors", "children": inside}, {"name": "Goal", "children": [egg_pin(place)]}]


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
    write(OUT / "maps.json", maps_json(pack))
    write(OUT / "locations.json", locations_json(pack))
    write(OUT / "items.json", items_json())
    write(OUT / "zone_places.json", zone_places())
    print(f"written to {OUT}")


if __name__ == "__main__":
    main()
