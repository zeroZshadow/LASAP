"""Writes patcher/map_positions.json: where each check shows on the in-game map (the map tracker, DESIGN.md).

Run with .venv-ap: python scripts/map_positions.py [<PopTracker pack folder>] [--romfs D:/Games/Switch/LASRomFS]

Overworld checks get world coordinates (x, z):
- an item actor in an overworld room (LASR's *_ROOMS tables): the actor's position;
- a check inside a building or cave: the door into it on the overworld (its AreaLevelOpen actor);
- anything else (NPCs, shops, games): the LASR PopTracker pack's pin (github.com/Br00ty/lasr_brooty), converted to
  world coordinates by an affine fit through the checks with an actor position.
Dungeon checks get their room and level coordinates: the item actor or owl statue, else the room's upper left.
"""
import argparse
import ast
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher"))
sys.path.insert(0, str(ROOT / "vendor" / "Archipelago"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from RandomizerCore.Tools.leb import Room  # noqa: E402
from worlds.las.locations import display_name  # noqa: E402
import poptracker_map  # noqa: E402

OUT = ROOT / "patcher" / "map_positions.json"
IDS = json.loads((ROOT / "apworld" / "las" / "data" / "ids.json").read_text(encoding="utf-8"))
RANDOMIZERS = ROOT / "patcher" / "RandomizerCore" / "Randomizers"
ROOM_W, ROOM_H = 15.0, 12.0  # a room (screen) in world units; room <row><col> spans x 15·col.., z 12·(row - 1)..

# Actors that hold a check, by name prefix or by their first parameter (rocks, bushes and holes hiding an item).
ITEM_ACTORS = ("ObjTreasureBox", "ItemHeartPiece", "ItemSeashell", "ItemGoldenLeaf", "ItemSmallKey", "ItemInstrument",
               "ItemHeartContainer")
ITEM_PARAMS = (b"Seashell", b"GoldenLeaf", b"HeartPiece", b"SmallKey")

# Dungeon checks without a room table. Owl statues: the room in the owl's event entry (DungeonOwlStatue.bfevfl
# examine_<dungeon><room>); bosses: the dungeon's boss room (UiDungeonArea.gsheet mIsBoss).
DUNGEON_ROOMS = {
    "D1-owl-statue-spinies": "Lv01TailCave_04B", "D1-owl-statue-3-of-a-kind": "Lv01TailCave_05F",
    "D1-owl-statue-long-hallway": "Lv01TailCave_04C",
    "D2-owl-statue-first-switch": "Lv02BottleGrotto_07E", "D2-owl-statue-push-puzzle": "Lv02BottleGrotto_06G",
    "D2-owl-statue-past-hinox": "Lv02BottleGrotto_04C",
    "D3-owl-statue-basement-north": "Lv03KeyCavern_03G", "D3-owl-statue-arrow": "Lv03KeyCavern_02D",
    "D3-owl-statue-northwest": "Lv03KeyCavern_01A",
    "D4-owl-statue": "Lv04AnglersTunnel_05E",
    "D5-owl-statue-triple-stalfos": "Lv05CatfishsMaw_06F", "D5-owl-statue-before-slime-eel": "Lv05CatfishsMaw_03C",
    "D6-owl-statue-ledge": "Lv06FaceShrine_04D", "D6-owl-statue-southeast": "Lv06FaceShrine_08G",
    "D6-owl-statue-canal": "Lv06FaceShrine_03G",
    "D7-owl-statue-ball": "Lv07EagleTower_02D", "D7-owl-statue-kirbys": "Lv07EagleTower_05D",
    "D7-owl-statue-hookshot-chest": "Lv07EagleTower_04B",
    "D8-owl-statue-above-smasher": "Lv08TurtleRock_06H", "D8-owl-statue-below-gibdos": "Lv08TurtleRock_04G",
    "D8-owl-statue-eye-statue": "Lv08TurtleRock_04C",
    "D0-owl-statue-nine-switches": "Lv10ClothesDungeon_06C", "D0-owl-statue-first-switches": "Lv10ClothesDungeon_07D",
    "D0-owl-statue-before-mini-boss": "Lv10ClothesDungeon_05F",
    "D1-moldorm": "Lv01TailCave_04G", "D2-genie": "Lv02BottleGrotto_04G", "D3-slime-eye": "Lv03KeyCavern_07G",
    "D4-angler": "Lv04AnglersTunnel_04B", "D5-slime-eel": "Lv05CatfishsMaw_02D", "D6-facade": "Lv06FaceShrine_04E",
    "D7-eagle": "Lv07EagleTower_03G", "D8-hothead": "Lv08TurtleRock_02D",
    "D5-master-stalfos": "Lv05CatfishsMaw_01B",  # his last fight
    "D0-fairy-1": "Lv10ClothesDungeon_04C", "D0-fairy-2": "Lv10ClothesDungeon_04C",
    **{f"D0-rupee-{i}": "Lv10ClothesDungeon_08D" for i in range(1, 29)},
}
# Actors that mark a dungeon check in its room, by name prefix.
DUNGEON_ACTORS = ITEM_ACTORS + ("ObjDungeonOwlStatue", "ObjDungeonOw", "NpcFairyQueen", "EnemyMasterS")


def room_tables() -> dict:
    """LASR location -> room, from the *_ROOMS dicts in LASR's randomizers."""
    rooms = {}
    for path in sorted(RANDOMIZERS.glob("*.py")):
        for node in ast.parse(path.read_text(encoding="utf-8")).body:
            if (isinstance(node, ast.Assign) and isinstance(node.value, ast.Dict)
                    and isinstance(node.targets[0], ast.Name) and node.targets[0].id.endswith("_ROOMS")
                    and not node.targets[0].id.startswith("PANEL")):
                try:
                    table = ast.literal_eval(node.value)
                except ValueError:
                    continue
                for loc, room in table.items():
                    if isinstance(room, str):
                        rooms.setdefault(loc, room)
    return rooms


class Level:
    def __init__(self, romfs: Path):
        self.root = romfs / "region_common" / "level"
        self.cache = {}

    def room(self, name: str) -> Room:
        if name not in self.cache:
            self.cache[name] = Room((self.root / name.split("_")[0] / f"{name}.leb").read_bytes())
        return self.cache[name]

    def field_doors(self) -> dict:
        """Level -> positions of the overworld doors (AreaLevelOpen) into it."""
        doors = {}
        for path in sorted((self.root / "Field").glob("Field_*.leb")):
            for actor in self.room(path.stem).actors:
                if actor.name.startswith(b"AreaLevelOpen") and isinstance(actor.parameters[0], bytes):
                    doors.setdefault(actor.parameters[0].decode(), []).append((actor.posX, actor.posZ))
        return doors


def item_actors(room: Room, prefixes=ITEM_ACTORS) -> list:
    found = []
    for actor in room.actors:
        param = actor.parameters[0] if actor.parameters else None
        if actor.name.startswith(tuple(p.encode() for p in prefixes)) or (
                isinstance(param, bytes) and param.startswith(ITEM_PARAMS)):
            found.append((actor.posX, actor.posZ))
    return found


def room_rect(room: str):
    m = re.search(r"_(\d\d)([A-P])$", room)
    row, col = int(m.group(1)), ord(m.group(2)) - 65
    return col * ROOM_W, (row - 1) * ROOM_H


def nearest(points, target):
    if target is None:
        return points[0]
    return min(points, key=lambda p: math.hypot(p[0] - target[0], p[1] - target[1]))


def clamp_to_room(point, room: str):
    x0, z0 = room_rect(room)
    if point is None:
        return x0 + ROOM_W / 2, z0 + ROOM_H / 2
    return min(max(point[0], x0 + 1.5), x0 + ROOM_W - 1.5), min(max(point[1], z0 + 1.5), z0 + ROOM_H - 1.5)


def solve3(rows, values):
    """Least squares for value = a·x + b·y + c."""
    m = [[sum(r[i] * r[j] for r in rows) for j in range(3)] for i in range(3)]
    v = [sum(r[i] * y for r, y in zip(rows, values)) for i in range(3)]
    for i in range(3):
        p = max(range(i, 3), key=lambda k: abs(m[k][i]))
        m[i], m[p], v[i], v[p] = m[p], m[i], v[p], v[i]
        for k in range(i + 1, 3):
            f = m[k][i] / m[i][i]
            m[k] = [a - f * b for a, b in zip(m[k], m[i])]
            v[k] -= f * v[i]
    x = [0.0] * 3
    for i in reversed(range(3)):
        x[i] = (v[i] - sum(m[i][j] * x[j] for j in range(i + 1, 3))) / m[i][i]
    return x


def pack_pins(pack: Path) -> dict:
    """LASR overworld location -> (x, y) of its pin on the pack's overworld map."""
    located = {}

    def walk(locations, parent):
        for loc in locations:
            loc_id = f"{parent}/{loc['name']}" if parent else loc["name"]
            for m in loc.get("map_locations", []):
                if m["map"] == "map":
                    located.setdefault(loc_id, (m["x"], m["y"]))
            walk(loc.get("children", []), loc_id)

    walk(poptracker_map.load(pack / "locations" / "overworld.json"), "")
    pins = {}
    for lasr, codes in poptracker_map.location_codes(poptracker_map.pack_sections(pack)).items():
        for code in codes:
            loc_id = code[1:].rpartition("/")[0]
            if loc_id in located:
                pins[lasr] = located[loc_id]
                break
    return pins


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pack", nargs="?", default="D:/DEV/lasr_brooty", type=Path)
    parser.add_argument("--romfs", default="D:/Games/Switch/LASRomFS", type=Path)
    args = parser.parse_args()

    level = Level(args.romfs)
    rooms = {**room_tables(), **DUNGEON_ROOMS}
    doors = level.field_doors()
    pins = pack_pins(args.pack)

    overworld, dungeon, exact = {}, {}, {}
    for lasr in IDS["locations"]:
        room = rooms.get(lasr)
        if re.match(r"D\d-", lasr):
            actors = item_actors(level.room(room), DUNGEON_ACTORS)
            if len(actors) == 1:
                x, z = actors[0]
            else:  # the room's upper left, clear of the boss icon in its middle
                x0, z0 = room_rect(room)
                x, z = x0 + ROOM_W / 4, z0 + ROOM_H / 4
            dungeon[lasr] = [room, round(x, 2), round(z, 2)]
        elif room and room.startswith("Field_"):
            actors = item_actors(level.room(room))
            if len(actors) == 1:
                exact[lasr] = actors[0]
    # The pack's pins in world coordinates
    anchors = [(pins[loc], pos) for loc, pos in exact.items() if loc in pins]
    rows = [(p[0], p[1], 1.0) for p, _ in anchors]
    fx = solve3(rows, [w[0] for _, w in anchors])
    fz = solve3(rows, [w[1] for _, w in anchors])
    fitted = {loc: (fx[0] * p[0] + fx[1] * p[1] + fx[2], fz[0] * p[0] + fz[1] * p[1] + fz[2]) for loc, p in pins.items()}
    errors = [math.dist(fitted[loc], exact[loc]) for loc in exact if loc in fitted]
    print(f"pin fit: {len(anchors)} anchors, mean error {sum(errors) / len(errors):.1f}, max {max(errors):.1f}")

    sources = {}
    for lasr in IDS["locations"]:
        if re.match(r"D\d-", lasr):
            continue
        room = rooms.get(lasr)
        pin = fitted.get(lasr)
        if lasr in exact:
            pos, source = exact[lasr], "actor"
        elif room and room.startswith("Field_"):
            pos, source = clamp_to_room(pin, room), "room"
        elif room and room.split("_")[0] in doors:
            pos, source = nearest(doors[room.split("_")[0]], pin), "door"
        elif pin:
            pos, source = pin, "pin"
        else:
            raise SystemExit(f"{lasr}: no position")
        overworld[lasr] = [round(pos[0], 2), round(pos[1], 2)]
        sources[source] = sources.get(source, 0) + 1
    print("overworld:", sources, "dungeon:", len(dungeon))

    names = {lasr: display_name(lasr) for lasr in IDS["locations"]}
    OUT.write_text(json.dumps({"overworld": overworld, "dungeon": dungeon, "names": names}, indent=1,
                              ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
