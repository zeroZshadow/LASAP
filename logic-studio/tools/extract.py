"""Reads the RomFS: every level's tiles and actors, every teleport, each check's place, and what was odd (the report).

build.py writes Logic Studio's data from it.
"""
import ast
import json
import math
import re
import sys
from collections import Counter, defaultdict, deque
from pathlib import Path


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / "patcher" / "modbuild"), str(ROOT / "scripts")]
from RandomizerCore.Tools import event_tools  # noqa: E402
from RandomizerCore.Tools.leb import Grid, Room  # noqa: E402
from RandomizerCore.Tools.oead_tools import readSheet  # noqa: E402
from local_paths import lookup  # noqa: E402

TILE = 1.5
FIELD = "Field"
ACTOR_KEY = re.compile(r"[-_]?[0-9A-Fa-f]{16}$")
# Actors that stand on a tile and stop Link until something removes them
SOLID = ("ObjGrass", "ObjRock", "ObjFieldCrac", "ObjGrave", "ObjCactus", "ObjTree", "ObjSignboard", "ObjFieldOwlS", "ObjPanelSign",
         "ObjWoodenBox", "ObjTreasureBox", "ObjPot", "ObjVase", "ObjBlock", "ObjLockBlock")
# Actors left out of the level files: meshes, lights and camera setup that no view draws
SKIP_ACTORS = ("MapStatic", "MapKinematic", "MapFixture", "TagCamera", "TagChangeEnv", "TagLight", "TagTailorPla", "TagHolocaust", "TagSwitchLight", "TagActivateC",
               "AreaCameraOf", "AreaAmbience", "AreaWarterFlow", "AreaBeltConv")
ITEM_ACTORS = ("ObjTreasureBox", "ItemHeartPiece", "ItemSeashell", "ItemSecretSe", "ItemGoldenLeaf", "ItemSmallKey", "ItemHeartContainer")
# Checks that stand on one actor, where the rooms LASR and DOOR_LEVELS give do not say (DOOR_LEVELS names the door the map marks)
CHECK_ACTORS = {"ciao-ciao": ("DogHouse_01A", "NpcRibbonBow"), "dream-shrine-left": ("DreamShrine_01A", "ItemOcarina"), "bay-fisherman": ("FishingBoat_01A", "NpcMarthasBa")}
ITEM_PARAMS = ("Seashell", "GoldenLeaf", "HeartPiece", "SmallKey")
# Overworld checks found by what they hold: actors whose first parameter is the check's index, and actors of which the overworld has one
INDEXED_ITEMS = {"ItemSecretSe": "seashell", "ItemHeartPiece": "heart-piece"}
SINGLE_ITEMS = {"ItemSlimeKey": "slime-key", "ItemSleepyMu": "mushroom"}
# A teleport's kind by actor: where its target's level and locator sit in the parameters
TELEPORTS = {"AreaLevelOpen": (0, 1), "AreaHoleLowe": (0, 1), "AreaUpstairs": (None, 0), "AreaDownstairs": (None, 0),
             "PanelDownsta": (None, 0), "PanelWallDow": (None, 0), "ObjDungeonWa": (None, 0)}
INSTRUMENTS = ("ItemFullMoon", "ItemConchHorn", "ItemSeaLilys", "ItemSurfHarp", "ItemWindMari", "ItemCoralTri", "ItemEveningC", "ItemThunderD")
FLOW_JUMPS = {("GameControl", "RequestLevelJump"), ("GameControl", "RequestWarp"), ("GameControl", "RequestRestartWarp"),
              ("GameControl", "RequestLevelJumpInCurrentLevel"), ("WarpUI", "JumpToSelectedLocation")}
# Actors that move Link by running a flow, by room and kind: the flow holding the jump
EVENT_JUMPS = {("BedRoom_01A", "ObjBed2"): "Bed"}
# Kinds that may move Link, seen in the room files, that the extractor does not follow
NOT_FOLLOWED = ("AreaEagleTower", "AreaReturnAb", "ObjLv9Stair", "ObjColorHole", "AreaBasement", "TagAreaEvent")
COMPANION_REACH = 6.0  # world units: how far from a landing a teleport back may be
F_DEEP, F_SOLID, F_NORTH, F_EAST, F_SOUTH = 1 << 0, 1 << 1, 1 << 3, 1 << 5, 1 << 7
F_WEST = 1 << 9
DIRS = ((1, 0, F_EAST, F_WEST), (-1, 0, F_WEST, F_EAST), (0, 1, F_SOUTH, F_NORTH), (0, -1, F_NORTH, F_SOUTH))


def text(value):
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else value


def level_kind(name):
    if name == FIELD:
        return "overworld"
    if name.startswith("Panel"):
        return "panel"
    if name.startswith("Ending"):
        return "ending"
    return "dungeon" if re.match(r"Lv\d\d", name) else "interior"


def read_grid(room):
    """(info, tile bytes, chain bytes) of a room's grid; room shapes vary, so not through leb.Grid's fixed sizes."""
    for entry in room.fixed_hash.entries:
        if entry.name == b"grid":
            parts = {e.name: e.data for e in entry.data.entries}
            return Grid.InfoBlock(parts[b"info"]), parts.get(b"data", b""), parts.get(b"chain", b"")
    return None, b"", b""


def tile_record(data, offset):
    flags = int.from_bytes(data[offset:offset + 4], "little")
    chain = int.from_bytes(data[offset + 8:offset + 12], "little")
    return flags, round(Grid.TileData(data[offset:offset + 16]).elevation, 3), (-1 if chain == 0xFFFFFFFF else chain)


def read_levels(level_dir):
    levels = {}
    for folder in sorted(p for p in level_dir.iterdir() if p.is_dir()):
        rooms = []
        for path in sorted(folder.glob("*.leb")):
            room = Room(path.read_bytes())
            info, data, chain = read_grid(room)
            entry = {"name": path.stem, "actors": []}
            if info:
                records = [tile_record(data, i * 16) for i in range(info.room_height * info.room_width)]
                entry.update(x=info.x_coord, z=info.z_coord, w=info.room_width, h=info.room_height, side=info.room_height == 2,
                             flags=[r[0] for r in records], elevation=[r[1] for r in records])
                if any(r[2] >= 0 for r in records):
                    entry["chain"] = [r[2] for r in records]
                if chain:
                    entry["links"] = [tile_record(chain, i * 16) for i in range(len(chain) // 16)]
            for actor in room.actors:
                kind = ACTOR_KEY.sub("", text(actor.name))
                if kind == "MapStatic":
                    entry["origin"] = actor.posX, actor.posY, actor.posZ  # where the room's model stands
                entry["actors"].append({"kind": kind, "x": round(actor.posX, 3), "y": round(actor.posY, 3), "z": round(actor.posZ, 3), "params": [text(p) for p in actor.parameters],
                                        "switches": [list(s) for s in actor.switches if s[0] != 4]})
            rooms.append(entry)
        levels[folder.name] = rooms
    return levels


def level_tiles(rooms):
    """(tile x, tile z) -> (room index, tile index, flags) for a level's top-down rooms."""
    tiles = {}
    for r, room in enumerate(rooms):
        if "flags" not in room or room["side"]:
            continue
        ox, oz = round(room["x"] / TILE), round(room["z"] / TILE)
        for i, flags in enumerate(room["flags"]):
            tiles[(ox + i % room["w"], oz + i // room["w"])] = (r, i, flags)
    return tiles


def flood_tiles(rooms, blocked, stopped_by=F_SOLID | F_DEEP):
    """Numbers the tiles by the area Link can walk: `stopped_by` tiles, one-sided walls and `blocked` stop him; shallow water does not."""
    tiles = level_tiles(rooms)

    def free(p):
        t = tiles.get(p)
        return t is not None and not t[2] & stopped_by and p not in blocked

    area = {}
    count = 0
    for start in tiles:
        if start in area or not free(start):
            continue
        count += 1
        area[start] = count
        queue = deque([start])
        while queue:
            p = queue.popleft()
            for dx, dz, leave, enter in DIRS:
                n = (p[0] + dx, p[1] + dz)
                if n in area or not free(n) or tiles[p][2] & leave or tiles[n][2] & enter:
                    continue
                area[n] = count
                queue.append(n)
    return area, count, tiles


def walkable_areas(rooms):
    """Numbers the areas Link can walk without removing anything, solid actors stopping him too. A side-view room counts as
    one area. Writes room["areas"]; 0 is not walkable."""
    blocked = {(int(a["x"] // TILE), int(a["z"] // TILE)) for room in rooms if not room.get("side") for a in room["actors"] if a["kind"].startswith(SOLID)}
    area, count, tiles = flood_tiles(rooms, blocked)
    for room in rooms:
        if "flags" in room and not room["side"]:
            room["areas"] = [0] * len(room["flags"])
        elif room.get("side"):
            count += 1
            room["side_area"] = count
    for p, a in area.items():
        r, i, _ = tiles[p]
        rooms[r]["areas"][i] = a
    return area, count


def area_near(area, x, z, reach=2):
    """The walkable area at a point, or the nearest around it: a door stands on a wall."""
    cx, cz = int(x // TILE), int(z // TILE)
    best = None
    for dx in range(-reach, reach + 1):
        for dz in range(-reach, reach + 1):
            a = area.get((cx + dx, cz + dz))
            if a and (best is None or dx * dx + dz * dz < best[0]):
                best = (dx * dx + dz * dz, a)
    return best[1] if best else None


def read_teleports(levels):
    """Every teleport actor, and the player starts they land on."""
    starts, teleports = defaultdict(list), []
    for level, rooms in levels.items():
        for room in rooms:
            for a in room["actors"]:
                kind, params = a["kind"], a["params"]
                if kind == "TagPlayerStart":
                    starts[(level, params[0])].append((room["name"], a["x"], a["y"], a["z"]))
                elif kind in TELEPORTS:
                    lvl_at, loc_at = TELEPORTS[kind]
                    target_level = params[lvl_at] if lvl_at is not None and params[lvl_at] else level
                    teleports.append({"id": f"{room['name']}/{kind}/{len(teleports)}", "kind": kind, "level": level, "room": room["name"], "x": a["x"], "y": a["y"], "z": a["z"],
                                      "to_level": target_level, "to": params[loc_at], "gate": a["switches"], "event": next((p for p in params[loc_at + 1:] if isinstance(p, str) and p), "")})
    return starts, teleports


def landing_of(level, locator, starts, room_index):
    """Where a locator lands; a locator with no start falls back to the room it names."""
    found = starts.get((level, locator))
    if found:
        room, x, y, z = found[0]
        return {"room": room, "x": x, "y": y, "z": z}
    for name in (locator, locator.rsplit("_", 1)[0]):
        room = room_index.get(name)
        if room and "flags" in room:
            return {"room": name, "x": room["x"] + room["w"] * TILE / 2, "y": 0, "z": room["z"] + room["h"] * TILE / 2, "approx": True}
    return None


def pair_companions(teleports, starts, room_index, report):
    """A teleport's companion is the one that leaves where it lands and lands where it leaves: mutual nearest."""
    by_level = defaultdict(list)
    for t in teleports:
        by_level[t["level"]].append(t)
        t["landing"] = landing_of(t["to_level"], t["to"], starts, room_index)
        if level_kind(t["level"]) != "panel" and (not t["landing"] or t["landing"].get("approx")):
            report["target_not_found"].append({"id": t["id"], "to_level": t["to_level"], "to": t["to"], "landing": t["landing"]["room"] if t["landing"] else None})

    def apart(option, land):
        vertical = land["y"] - option["y"] if room_index[land["room"]].get("side") else land["z"] - option["z"]
        return math.hypot(option["x"] - land["x"], vertical)

    best = {}
    for t in teleports:
        if t["landing"]:
            options = [o for o in by_level[t["to_level"]] if o["to_level"] == t["level"] and o is not t]
            if options:
                o = min(options, key=lambda c: apart(c, t["landing"]))
                if apart(o, t["landing"]) <= COMPANION_REACH:
                    best[t["id"]] = o["id"]
    for t in teleports:
        other = best.get(t["id"])
        t["companion"] = other if other and best.get(other) == t["id"] else None


def eval_constants(path, names):
    """Module-level dicts of a tracked script, without importing it (its imports need other setup)."""
    found = {}
    for node in ast.parse(path.read_text(encoding="utf-8")).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id in names:
            found[node.targets[0].id] = eval(compile(ast.Expression(node.value), str(path), "eval"), {})
    return found


def room_tables():
    """LASR location -> room, from the *_ROOMS dicts in LASR's randomizers."""
    rooms = {}
    for path in sorted((ROOT / "patcher" / "modbuild" / "RandomizerCore" / "Randomizers").glob("*.py")):
        for node in ast.parse(path.read_text(encoding="utf-8")).body:
            if (isinstance(node, ast.Assign) and isinstance(node.value, ast.Dict) and isinstance(node.targets[0], ast.Name)
                    and node.targets[0].id.endswith("_ROOMS") and not node.targets[0].id.startswith("PANEL")):
                try:
                    table = ast.literal_eval(node.value)
                except ValueError:
                    continue
                for location, room in table.items():
                    if isinstance(room, str):
                        rooms.setdefault(location, room)
    return rooms


def field_room(x, z):
    return f"Field_{int(z // 12) + 1:02d}{chr(65 + int(x // 15))}"


def field_items(rooms):
    """Overworld item actors by (what they hold, index), with their room. A hidden shell is a bush, rock or tree with a parameter Seashell<index>."""
    found = {}
    for room in rooms:
        for a in room["actors"]:
            first = a["params"][0] if a["params"] else None
            hidden = next(filter(None, (re.fullmatch(r"Seashell(\d+)", p) for p in a["params"] if isinstance(p, str))), None)
            if a["kind"] in INDEXED_ITEMS:
                found[(INDEXED_ITEMS[a["kind"]], first)] = (room["name"], a)
            elif a["kind"] in SINGLE_ITEMS:
                found[(SINGLE_ITEMS[a["kind"]], None)] = (room["name"], a)
            elif hidden:
                found[("seashell", int(hidden[1]))] = (room["name"], a)
    return found


def place_checks(levels, room_index, report, facts):
    """Each check's real place. map_positions.json puts interior checks at their door, so those take their room from LASR's
    tables or map_positions' level table, and their position from the item actor there. Its overworld points are what the game's
    map shows, some of them pins or a neighbour's actor, so an overworld check with its own actor stands on that."""
    positions = json.loads((ROOT / "patcher" / "map_positions.json").read_text(encoding="utf-8"))
    tables = room_tables()
    door_levels = eval_constants(ROOT / "scripts" / "map_positions.py", {"DOOR_LEVELS"})["DOOR_LEVELS"]
    field = field_items(levels[FIELD])
    checks = {}
    for name, places in positions["dungeon"].items():
        room, x, z = places[0]
        checks[name] = {"level": room.rsplit("_", 1)[0], "room": room, "x": x, "z": z, "exact": True}
    for name, (x, z, _) in positions["overworld"].items():
        if name in CHECK_ACTORS:
            room, kind = CHECK_ACTORS[name]
            actor = next(a for a in room_index[room]["actors"] if a["kind"] == kind)
            checks[name] = {"level": room.rsplit("_", 1)[0], "room": room, "x": actor["x"], "y": actor["y"], "z": actor["z"], "exact": True}
            continue
        room = tables.get(name)
        level = door_levels.get(name)
        if room and not room.startswith(FIELD) and room in room_index:
            data = room_index[room]
            items = [a for a in data["actors"] if a["kind"].startswith(ITEM_ACTORS) or (a["params"] and isinstance(a["params"][0], str) and a["params"][0].startswith(ITEM_PARAMS))]
            if items:
                checks[name] = {"level": room.rsplit("_", 1)[0], "room": room, "x": items[0]["x"], "y": items[0]["y"], "z": items[0]["z"], "exact": len(items) == 1}
            else:
                checks[name] = {"level": room.rsplit("_", 1)[0], "room": room, "x": data.get("x", 0) + 7.5, "z": data.get("z", 0) + 6, "exact": False}
        elif level and level in levels:
            rooms = [r for r in levels[level] if "flags" in r]
            first = rooms[0]
            checks[name] = {"level": level, "room": first["name"] if len(rooms) == 1 else None, "x": first["x"] + first["w"] * TILE / 2, "z": first["z"] + first["h"] * TILE / 2, "exact": False}
            if len(rooms) > 1:
                report["checks_level_only"].append(name)
        else:
            node = facts.get(name, {})
            own = field.get((node.get("content"), node.get("index")))
            if own:
                checks[name] = {"level": FIELD, "room": own[0], "x": own[1]["x"], "z": own[1]["z"], "exact": True}
            else:
                checks[name] = {"level": FIELD, "room": field_room(x, z), "x": x, "z": z, "exact": True}
    return checks


def read_flow_jumps(event_dir, starts, report):
    jumps = []
    for path in sorted(event_dir.glob("*.bfevfl")):
        chart = event_tools.readFlow(str(path)).flowchart
        for event in chart.events:
            data = event.data
            if not hasattr(data, "actor_action") or not hasattr(data, "actor"):
                continue
            pair = (data.actor.v.identifier.name, data.actor_action.v.v)
            if pair not in FLOW_JUMPS:
                continue
            raw = dict(data.params.data) if data.params else {}
            params = {k: getattr(v, "v", v) for k, v in raw.items()}
            kinds = {k: type(v).__name__ for k, v in raw.items()}
            level, locator = params.get("level"), params.get("locator")
            literal = isinstance(locator, str) and bool(locator) and "Argument" not in kinds.get("locator", "") and "Argument" not in kinds.get("level", "")
            entry = {"flow": path.stem, "event": event.name, "action": pair[1], "level": level if isinstance(level, str) else None, "to": locator if isinstance(locator, str) else None, "literal": literal}
            if literal:
                found = next(((k, v[0]) for k, v in starts.items() if k[1] == locator and (not level or k[0] == level)), None)
                if found:
                    entry["level"] = found[0][0]
                    entry["landing"] = {"room": found[1][0], "x": found[1][1], "y": found[1][2], "z": found[1][3]}
                else:
                    report["flow_targets_not_found"].append(entry)
            jumps.append(entry)
    return jumps


def pair_flow_jumps(teleports, jumps):
    """A teleport with no companion may be undone by a script: a literal jump that lands beside it."""
    for t in teleports:
        t["back_jump"] = None
    for i, jump in enumerate(jumps):
        land = jump.get("landing")
        if not land or level_kind(jump["level"]) == "panel":
            continue
        for t in teleports:
            if not t["companion"] and t["to"] and t["room"] == land["room"] and t["to_level"] != t["level"] and math.hypot(t["x"] - land["x"], t["z"] - land["z"]) <= COMPANION_REACH:
                t["back_jump"] = i
                jump["back"] = t["id"]


def global_flags(sheet_dir, gates):
    """The names of the global flags (switch type 1) these gates read."""
    used = {index for gate in gates for kind, index in gate if kind == 1}
    return {row["index"]: row["symbol"] for row in map(dict, readSheet(str(sheet_dir / "GlobalFlags.gsheet"))["values"]) if row["index"] in used}


def warp_points(sheet_dir, starts, report):
    rows = []
    for row in readSheet(str(sheet_dir / "WarpPoint.gsheet"))["values"]:
        row = dict(row)
        locator = row["mLocator"]
        landing = (starts.get((FIELD, locator)) or [None])[0]
        if not landing:
            report["warp_points_not_found"].append(locator)
        rows.append({"index": row["mIndex"], "locator": locator, "label": row["mNameLabel"], "room": landing[0] if landing else None, "x": landing[1] if landing else None, "z": landing[3] if landing else None})
    return rows


def floors(sheet_dir):
    return {dict(row)["mLeaName"]: dict(row)["mFloor"] for row in readSheet(str(sheet_dir / "UiDungeonArea.gsheet"))["values"]}


def other_teleports(levels, points, starts, jumps):
    """Warps and moves that are not entrances: warp holes, Mambo points, ocarina spots, instrument warps, rapids, the module's warp menu."""
    found = []

    def landing_at(level, locator):
        landing = (starts.get((level, locator)) or [None])[0] if locator else None
        return {"room": landing[0], "x": landing[1], "y": landing[2], "z": landing[3]} if landing else None

    def add(kind, level, room, actor, to_level=None, to=None, note=""):
        found.append({"kind": kind, "level": level, "room": room["name"], "x": actor["x"], "y": actor["y"], "z": actor["z"], "to_level": to_level, "to": to, "note": note,
                      "gate": actor["switches"], "landing": landing_at(to_level, to)})

    by_index = {p["index"]: p for p in points}
    for level, rooms in levels.items():
        if level_kind(level) == "panel":
            continue
        for room in rooms:
            for a in room["actors"]:
                k, ps = a["kind"], a["params"]
                if k == "AreaWarpHole":
                    add(k, level, room, a, note="Warp menu")
                elif k == "ObjWarpManbo":
                    point = by_index.get(ps[0])
                    add(k, level, room, a, FIELD, point["locator"] if point else None, f"WarpPoint {ps[0]}")
                elif k in INSTRUMENTS and ps and ps[0] == FIELD:
                    add(k, level, room, a, FIELD, ps[1])
                elif k in ("AreaRapidFlow", "TagRapidFlow"):
                    add(k, level, room, a)
                elif (room["name"], k) in EVENT_JUMPS:
                    jump = next(j for j in jumps if j["flow"] == EVENT_JUMPS[room["name"], k] and j["literal"])
                    add(k, level, room, a, jump["level"], jump["to"], f"{jump['flow']}/{jump['event']}")
                    found[-1]["back"] = jump.get("back")
    home = eval_constants(ROOT / "patcher" / "modbuild" / "mod.py", {"WARP_HOME"}).get("WARP_HOME")
    if home:
        found.append({"kind": "WARP_HOME", "level": None, "room": None, "x": None, "y": None, "z": None, "to_level": home["level"], "to": home["locator"], "note": "Module", "gate": [],
                      "landing": landing_at(home["level"], home["locator"])})
    return found


def interior_groups(teleports, levels):
    """Per level other than the overworld: its ways out, grouped by whether Link can get between them by walking and taking
    stairs once whatever an item removes is gone, and again when he can also cross deep water and pits (`across`)."""
    result = {}
    by_level = defaultdict(list)
    for t in teleports:
        by_level[t["level"]].append(t)
    for level, rooms in levels.items():
        if level_kind(level) in ("overworld", "panel", "ending"):
            continue
        sides = {r["name"] for r in rooms if r.get("side")}

        def grouped(stopped_by):
            open_area = flood_tiles(rooms, frozenset(), stopped_by)[0]
            parent = {}

            def find(a):
                parent.setdefault(a, a)
                while parent[a] != a:
                    parent[a] = parent[parent[a]]
                    a = parent[a]
                return a

            def spot(room, x, z):
                return room if room in sides else area_near(open_area, x, z)

            for t in by_level[level]:
                land = t["landing"]
                if t["to_level"] == level and land and not t["warp"]:  # a warp pad needs activating: it joins nothing by walking
                    here, there = spot(t["room"], t["x"], t["z"]), spot(land["room"], land["x"], land["z"])
                    if here and there:
                        parent[find(here)] = find(there)
            groups = defaultdict(list)
            for t in by_level[level]:
                if t["entrance"]:
                    here = spot(t["room"], t["x"], t["z"])
                    groups[find(here) if here else t["id"]].append(t["id"])
            return list(groups.values())

        result[level] = {"out": sum(1 for t in by_level[level] if t["entrance"]), "groups": grouped(F_SOLID | F_DEEP), "across": grouped(F_SOLID), "has_side": bool(sides)}
    return result


def build(facts):
    """(levels, index, report): every level's rooms with tiles and actors, the index of places, and the report. `facts`
    are the checks file's: an overworld item check is found by what it holds (content, index).
    """
    romfs_value = lookup("romfs")
    if not romfs_value:
        raise SystemExit('scripts/local_paths.json needs "romfs"')
    common = Path(romfs_value) / "region_common"
    if not (common / "level" / FIELD).is_dir():
        raise SystemExit(f"No RomFS at {common}")
    report: dict = {"target_not_found": [], "flow_targets_not_found": [], "warp_points_not_found": [], "checks_level_only": [], "checks_no_area": []}
    levels = read_levels(common / "level")
    room_index = {room["name"]: room for rooms in levels.values() for room in rooms}
    room_floors = floors(common / "datasheets")
    areas = {}
    level_index = []
    for name, rooms in levels.items():
        area, count = walkable_areas(rooms)
        areas[name] = area
        for room in rooms:
            room["floor"] = room_floors.get(room["name"])
        level_index.append({"name": name, "kind": level_kind(name), "rooms": len(rooms), "top": sum(1 for r in rooms if "flags" in r and not r["side"]), "side": sum(1 for r in rooms if r.get("side")), "areas": count})

    def area_at(level, room, x, z, reach=2):
        data = room_index.get(room)
        if data and data.get("side"):
            return data["side_area"]
        return area_near(areas.get(level, {}), x, z, reach)

    starts, teleports = read_teleports(levels)
    pair_companions(teleports, starts, room_index, report)
    for t in teleports:
        t["from_area"] = area_at(t["level"], t["room"], t["x"], t["z"])
        land = t["landing"]
        t["to_area"] = area_at(t["to_level"], land["room"], land["x"], land["z"]) if land else None
        t["entrance"] = (t["level"] == FIELD) != (t["to_level"] == FIELD) and level_kind(t["level"]) != "panel" and level_kind(t["to_level"]) != "panel"
        t["warp"] = t["kind"] == "ObjDungeonWa"
        t["door"] = t["entrance"] or ((bool(t["companion"]) or t["warp"]) and level_kind(t["level"]) != "panel" and level_kind(t["to_level"]) != "panel")

    entrances = [t for t in teleports if t["entrance"]]
    report["no_companion"] = [{"id": t["id"], "room": t["room"], "kind": t["kind"], "to": t["to"]} for t in entrances if not t["companion"]]
    jumps = read_flow_jumps(common / "event", starts, report)
    pair_flow_jumps(teleports, jumps)
    points = warp_points(common / "datasheets", starts, report)
    warps = other_teleports(levels, points, starts, jumps)
    flags = global_flags(common / "datasheets", [x["gate"] for x in teleports + warps])
    checks = place_checks(levels, room_index, report, facts)
    for name, entry in checks.items():
        entry["area"] = area_at(entry["level"], entry["room"], entry["x"], entry["z"])
        if entry["area"] is None:
            report["checks_no_area"].append(name)

    report["totals"] = {"levels": len(levels), "rooms": sum(len(r) for r in levels.values()), "side_rooms": sum(i["side"] for i in level_index),
                        "rooms_without_grid": sum(1 for rs in levels.values() for r in rs if "flags" not in r),
                        "teleports": len(teleports), "entrances": len(entrances), "companion_pairs": sum(1 for t in entrances if t["companion"]) // 2,
                        "by_actor": dict(Counter(t["kind"] for t in teleports)), "warps": dict(Counter(w["kind"] for w in warps)),
                        "flow_jumps": dict(Counter(j["action"] for j in jumps)), "flow_jumps_literal": sum(1 for j in jumps if j["literal"]),
                        "checks": len(checks), "checks_exact": sum(1 for c in checks.values() if c["exact"])}
    report["rooms_without_grid"] = sorted({r["name"].rsplit("_", 1)[0] for rs in levels.values() for r in rs if "flags" not in r})
    report["flow_jumps_from_arguments"] = sorted({f"{j['flow']}/{j['event']}" for j in jumps if not j["literal"]})
    report["actors_not_followed"] = dict(Counter(a["kind"] for rs in levels.values() for r in rs for a in r["actors"] if a["kind"] in NOT_FOLLOWED))
    report["panel_levels"] = sorted(n for n in levels if level_kind(n) == "panel")
    for rooms in levels.values():
        for room in rooms:
            room["actors"] = [a for a in room["actors"] if not a["kind"].startswith(SKIP_ACTORS)]
    for t in teleports:
        t["drop"] = t["kind"] == "AreaHoleLowe"  # a hole Link falls through, one way
    for w in warps:
        w["on_map"] = w["kind"] not in ("TagRapidFlow", "AreaRapidFlow")  # the rapids' current carries Link along, no warp spot
    index = {"levels": level_index, "teleports": teleports, "warps": warps, "warp_points": points, "flow_jumps": jumps, "flags": flags, "checks": checks,
             "interiors": interior_groups(teleports, levels),
             "rooms": {r["name"]: {"level": n, "x": r["x"] + r["w"] * TILE / 2, "z": r["z"] + r["h"] * TILE / 2} for n, rs in levels.items() if level_kind(n) != "panel" for r in rs if "flags" in r and not r["side"]}}
    return levels, index, report
