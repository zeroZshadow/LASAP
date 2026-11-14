"""Pictures of the levels for Logic Studio, and the marks it pins on them.

Each level's top-down rooms are one picture, its side-view rooms one picture each, PX pixels a tile. A door that stands
in a doorway is a mark: where it is, the rooms it joins, a letter and a name.
"""
import io
from collections import defaultdict

from PIL import Image, ImageChops, ImageDraw

TILE = 1.5  # world units a tile
PX = 16  # pixels a tile
SIDE_TILES = 8  # tiles a side-view room is drawn tall
DEEP, SOLID, NORTH, EAST, SOUTH, WEST, DIG, WATER, DOORWAY = 1, 2, 8, 32, 128, 512, 1 << 16, 1 << 18, 1 << 19
FLOOR_TYPE = DOORWAY | 1 << 20  # on floor tiles, and on the solid tiles of a flight of stairs; not on walls
STAIR_STEP = 1.2  # the most a flight climbs a tile, in world units


def hexrgb(text):
    return tuple(int(text[i:i + 2], 16) for i in (1, 3, 5))


# The picture's colours, named as Logic Studio's legend names them
COLOURS = {"Floor": "#b2be82", "Wall": "#24282c", "Wall edge": "#14181b", "Deep water": "#2e68a0", "Shallow water": "#78aa96",
           "Hole": "#4a2f5c", "Dig spot": "#cdaa6e", "Bush": "#4c9a4c", "Tree": "#3b7a3b", "Rock": "#8d857c", "Cracked rock": "#7a5c46",
           "Sign": "#b8905a", "Chest": "#e0b030", "Door": "#b5794a", "Block": "#7d4f24",
           "Orange block": "#d9822b", "Blue block": "#3f6fc0", "Crystal switch": "#f0f0f0"}
PATTERNED = ("Orange block", "Blue block", "Crystal switch")  # the legend shows these as a picture, not a colour
RGB = {name: hexrgb(value) for name, value in COLOURS.items()}
SIDE_BACK, SIDE_WALL = (40, 51, 58), (15, 20, 23)
SURFACE = 3  # pixels of floor drawn on a room model's solid
DECOR = ("grass", "kazari")  # in the name of a room model's shape that decorates
# What an actor is drawn as; walkable grass (ObjLawn) is ground
MARKED = {"ObjGrass": "Bush", "ObjTree": "Tree", "ObjCactus": "Tree", "ObjRock": "Rock", "ObjGrave": "Rock", "ObjFieldCrac": "Cracked rock",
          "ObjSignboard": "Sign", "ObjTreasureBox": "Chest", "ObjSquareBlock": "Block",
          "ObjOrangeUne": "Orange block", "ObjPurpleUne": "Blue block", "ObjCrystalSw": "Crystal switch"}
# What stands in a doorway: the letter on its mark and its name. Rotating doors let Link through going north.
DOORS = {"ObjLockDoor": ("K", "Key door"), "ObjBossDoor": ("N", "Nightmare door"), "ObjClosedDoor": ("S", "Shutter"), "ObjClosedPot": ("P", "Pot door"),
         "ObjDungeonCr": ("B", "Bomb wall"), "ObjOnewayDoo": ("↑", "Rotating door"), "ObjLockBlock": ("L", "Key block")}
# The wall piece a dungeon's room models put in a doorway, by the start of its name. Two that face each other across a
# seam with no door actor and no floor across are a passage the tiles do not show: they are solid there, while the
# rooms' collision meshes, what Link walks against, are open. It is ground, not a door: drawn as floor, with no mark.
ARCHES = ("d1_w_wall_a_08", "d2_w_wall_a_06", "d3_w_wall_a_04", "d4_w_wall_a_06", "d5_w_wall_a_02", "d6_w_wall_a_02", "d7_w_waku_a_01")
ARCH_REACH = 1.9  # how far into a room an arch's faces reach from its edge
ARCH_WIDTH = 4.5  # the widest an arch is along its edge; a longer run is a wall
# Raised floor: the grid has it solid at a wall's height of 0, while the room's collision mesh has a floor on it this far
# above the room's own. A solid tile low in the grid with the collision floor at 0 is an object's footprint (a block, a
# chest, a statue); a wall stands about 2 high in the grid. Most of it is the tops of the dungeons' low fences
# (dN_w_fence_a_01), a wall Link jumps over; it is floor only where he gets onto it: a doorway opens onto it or he
# arrives on it (stairs).
RAISED_GRID = 1.0  # the grid height below which a solid tile can be raised floor
RAISED = (0.25, 1.3)  # the collision floor's height above the room's that is raised floor
RAISED_STEP = 0.5  # the most two raised tiles differ and are one stretch of floor
ARRIVALS = ("TagPlayerStart",)  # where Link arrives in a room
RAISED_LIGHT = 0.2  # how much lighter than floor at the room's height (ground) raised floor is drawn, a world unit up


def shade(colour, k):
    return tuple(max(0, min(255, round(v * k))) for v in colour)


def top_rooms(rooms):
    return [r for r in rooms if r.get("flags") and not r["side"]]


def tiles_of(rooms):
    """(tile x, tile z) -> (room, tile index, flags) over a level's top-down rooms."""
    found = {}
    for r in rooms:
        ox, oz = round(r["x"] / TILE), round(r["z"] / TILE)
        for i, f in enumerate(r["flags"]):
            found[(ox + i % r["w"], oz + i // r["w"])] = (r, i, f)
    return found


LOCK_KINDS = ("ObjLockDoor", "ObjLockBlock", "ObjBossDoor")  # opened once for good: the save flag they share says which lock they are


def edge_arches(room, shapes):
    """[(side, middle)]: each door arch a room's model (model_bounds.shapes) stands on an edge: side N, S, W or E, and how
    far along the edge its middle is."""
    ox, _, oz = room["origin"]
    w, h = room["w"] * TILE, room["h"] * TILE
    along = defaultdict(list)
    for name, faces in shapes:
        if not name.split("__MI_")[-1].startswith(ARCHES):
            continue
        for face in faces:
            x = sum(p[0] for p in face) / 3 + ox - room["x"]
            z = sum(p[2] for p in face) / 3 + oz - room["z"]
            if 0 < x < w and -0.5 < z < ARCH_REACH:
                along["N"].append(x)
            elif 0 < x < w and h - ARCH_REACH < z < h + 0.5:
                along["S"].append(x)
            elif 0 < z < h and -0.5 < x < ARCH_REACH:
                along["W"].append(z)
            elif 0 < z < h and w - ARCH_REACH < x < w + 0.5:
                along["E"].append(z)
    found = []
    for side, values in along.items():
        values.sort()
        start = 0
        for i in range(1, len(values) + 1):
            if i == len(values) or values[i] - values[i - 1] > 1.0:  # a gap: the next arch
                if values[i - 1] - values[start] <= ARCH_WIDTH:
                    found.append((side, (values[start] + values[i - 1]) / 2))
                start = i
    return found


def doors(rooms, arches=None):
    """(tiles a door opens as {(room name, tile index)}, the marks, passage tiles). A door actor on a room's edge opens its
    tiles and the ones across the edge. A doorway with no actor is a passage, ground with no mark: two solid tiles across
    rooms that both carry the doorway bit, with no open floor beside them along the seam, or two arches facing each other
    across a seam with neither (`arches`: room name -> edge_arches)."""
    rooms = top_rooms(rooms)
    tiles = tiles_of(rooms)
    opened, marks = set(), {}

    def mark(kind, x, z, room, inward=(0, 0), flag=None):
        key = (kind, round(x, 2), round(z, 2))
        if key not in marks:
            marks[key] = {"kind": kind, "x": x, "z": z, "rooms": [], "inward": list(inward), "flag": flag}
        marks[key]["rooms"].append(room)

    for r in rooms:
        for a in r["actors"]:
            if a["kind"] not in DOORS:
                continue
            col, row = int((a["x"] - r["x"]) // TILE), int((a["z"] - r["z"]) // TILE)
            if a["kind"] == "ObjLockBlock":
                dx, dz = 0, 0
            else:
                dx, dz = (0, -1) if row == 0 else (0, 1) if row == r["h"] - 1 else (-1, 0) if col == 0 else (1, 0) if col == r["w"] - 1 else (0, 0)
            x = r["x"] + (r["w"] * TILE if dx > 0 else 0) if dx else a["x"]
            z = r["z"] + (r["h"] * TILE if dz > 0 else 0) if dz else a["z"]
            mark(a["kind"], x, z, r["name"], (-dx, -dz), next((s[1] for s in a["switches"] if s[0] == 1), None) if a["kind"] in LOCK_KINDS else None)
            if not dx and not dz:
                continue
            for sx in (-0.3, 0.3):
                for sz in (-0.3, 0.3):
                    tx, tz = int((a["x"] + sx) // TILE), int((a["z"] + sz) // TILE)
                    for key in ((tx, tz), (tx + dx, tz + dz)):
                        if key in tiles:
                            opened.add((tiles[key][0]["name"], tiles[key][1]))
    def open_across(x, z, dx, dz):
        """Floor on both sides of the seam at this tile."""
        here, there = tiles.get((x, z)), tiles.get((x + dx, z + dz))
        return here and there and not (here[2] | there[2]) & SOLID

    passages = set()
    for (x, z), (room, i, f) in tiles.items():
        for dx, dz in ((1, 0), (0, 1)):
            there = tiles.get((x + dx, z + dz))
            if not there or there[0] is room or not f & there[2] & DOORWAY or not (f | there[2]) & SOLID:
                continue
            if (room["name"], i) in opened or (there[0]["name"], there[1]) in opened:
                continue
            if open_across(x + dz, z + dx, dx, dz) or open_across(x - dz, z - dx, dx, dz):
                continue
            passages.update(((room["name"], i), (there[0]["name"], there[1])))
    by_name = {r["name"]: r for r in rooms}
    for name, found in (arches or {}).items():
        room = by_name.get(name)
        if not room:
            continue
        ox, oz = round(room["x"] / TILE), round(room["z"] / TILE)
        for side, middle in found:
            if side not in ("S", "E"):  # each seam once, from the room above or to the left
                continue
            dx, dz = (0, 1) if side == "S" else (1, 0)
            at = int(middle // TILE)
            x, z = (ox + at, oz + room["h"] - 1) if side == "S" else (ox + room["w"] - 1, oz + at)
            there = tiles.get((x + dx, z + dz))
            if not there or there[0] is room:
                continue
            other = there[0]
            ex = room["x"] + (middle if side == "S" else room["w"] * TILE)
            ez = room["z"] + (room["h"] * TILE if side == "S" else middle)
            facing = [m for s, m in arches.get(other["name"], []) if s == ("N" if side == "S" else "W") and abs((other["x"] + m if side == "S" else other["z"] + m) - (ex if side == "S" else ez)) < 0.8]
            if not facing or any(abs(m["x"] - ex) < 2.3 and abs(m["z"] - ez) < 2.3 for m in marks.values()):
                continue
            cells = [(x + dz * k, z + dx * k) for k in (-1, 0, 1)]
            if any(open_across(cx, cz, dx, dz) for cx, cz in cells):
                continue
            for cx, cz in ((ex - dz * 0.75, ez - dx * 0.75), (ex + dz * 0.75, ez + dx * 0.75)):  # the arch's two tiles each side
                tx, tz = int((cx - dx * 0.75) // TILE), int((cz - dz * 0.75) // TILE)
                if (tx, tz) in tiles and (tx + dx, tz + dz) in tiles:
                    passages.update((tiles[key][0]["name"], tiles[key][1]) for key in ((tx, tz), (tx + dx, tz + dz)))
    found = list(marks.values())
    spot = {}
    for m in found:
        spot.setdefault((round(m["x"], 2), round(m["z"], 2)), []).append(m)
    for m in found:
        letter, name = DOORS[m["kind"]]
        room = by_name[m["rooms"][0]]
        tile = min(max(int((m["z"] - room["z"]) // TILE), 0), room["h"] - 1) * room["w"] + min(max(int((m["x"] - room["x"]) // TILE), 0), room["w"] - 1)
        m.update(letter=letter, name=name, apart=len(spot[(round(m["x"], 2), round(m["z"], 2))]) > 1, x=round(m["x"], 3), z=round(m["z"], 3), id=f"{room['name']}-{tile}")
        del m["kind"]
    return opened, found, passages


def stairs(rooms):
    """{(room name, tile index)}: solid tiles the game leaves to the model to walk on, drawn as ground. They have floor flags,
    and their heights climb steadily from a walkable tile to the walkable tile across them."""
    tiles = tiles_of(rooms)
    found = set()
    for dx, dz in ((0, 1), (1, 0)):
        for (x, z), start in tiles.items():
            if start[2] & SOLID:
                continue
            run, at = [], (x + dx, z + dz)
            while at in tiles and tiles[at][2] & SOLID and tiles[at][2] & FLOOR_TYPE == FLOOR_TYPE:
                run.append(tiles[at])
                at = (at[0] + dx, at[1] + dz)
            if not run or at not in tiles or tiles[at][2] & SOLID:
                continue
            heights = [t[0]["elevation"][t[1]] for t in (start, *run, tiles[at])]
            steps = [b - a for a, b in zip(heights, heights[1:])]
            if all(0 < s <= STAIR_STEP for s in steps) or all(-STAIR_STEP <= s < 0 for s in steps):
                found.update((t[0]["name"], t[1]) for t in run)
    return found


def floor_height(triangles, x, z, below):
    """The highest upward-facing triangle under (x, z) below a height, or None."""
    best = None
    for p0, p1, p2 in triangles:
        if min(p0[0], p1[0], p2[0]) > x or max(p0[0], p1[0], p2[0]) < x or min(p0[2], p1[2], p2[2]) > z or max(p0[2], p1[2], p2[2]) < z:
            continue
        ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        if ny < 0.7 * (nx * nx + ny * ny + nz * nz) ** 0.5:
            continue
        d = (p1[2] - p2[2]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[2] - p2[2])
        if abs(d) < 1e-9:
            continue
        l1 = ((p1[2] - p2[2]) * (x - p2[0]) + (p2[0] - p1[0]) * (z - p2[2])) / d
        l2 = ((p2[2] - p0[2]) * (x - p2[0]) + (p0[0] - p2[0]) * (z - p2[2])) / d
        if min(l1, l2, 1 - l1 - l2) < 0:
            continue
        y = l1 * p0[1] + l2 * p1[1] + (1 - l1 - l2) * p2[1]
        if y <= below and (best is None or y > best):
            best = y
    return best


def raised(room, triangles):
    """{tile index: height}: a top-down room's solid tiles whose collision (triangles, cidb.link_triangles) has raised
    floor on them, and how far above the room's floor."""
    base = room["origin"][1]
    found = {}
    for i, f in enumerate(room["flags"]):
        if not f & SOLID or room["elevation"][i] >= RAISED_GRID:
            continue
        x, z = room["x"] + (i % room["w"] + 0.5) * TILE, room["z"] + (i // room["w"] + 0.5) * TILE
        y = floor_height(triangles, x, z, base + RAISED[1])
        if y is not None and y - base >= RAISED[0]:
            found[i] = round(y - base, 3)
    return found


def walkable_raised(rooms, raised_floor, opened):
    """The part of raised_floor (raised, keyed (room name, tile index)) Link gets onto: each stretch of it that touches a
    doorway (opened) or holds an arrival."""
    tiles = tiles_of(top_rooms(rooms))
    at = {key: raised_floor[(t[0]["name"], t[1])] for key, t in tiles.items() if (t[0]["name"], t[1]) in raised_floor}
    arrivals = {(int(a["x"] // TILE), int(a["z"] // TILE)) for r in top_rooms(rooms) for a in r["actors"] if a["kind"] in ARRIVALS}
    found, seen = {}, set()
    for start in at:
        if start in seen:
            continue
        stretch, todo = [], [start]
        seen.add(start)
        while todo:
            p = todo.pop()
            stretch.append(p)
            for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (p[0] + dx, p[1] + dz)
                if q in at and q not in seen and abs(at[q] - at[p]) <= RAISED_STEP:
                    seen.add(q)
                    todo.append(q)
        near = {(p[0] + dx, p[1] + dz) for p in stretch for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1))}
        if arrivals & set(stretch) or any(q in tiles and (tiles[q][0]["name"], tiles[q][1]) in opened for q in near):
            found.update(((tiles[p][0]["name"], tiles[p][1]), at[p]) for p in stretch)
    return found


def ground(f, elevation):
    if f & DEEP:
        return RGB["Deep water"] if f & WATER else RGB["Hole"]
    if f & DIG:
        return RGB["Dig spot"]
    return shade(RGB["Shallow water"] if f & WATER else RGB["Floor"], max(0.65, min(1.25, 0.8 + elevation / 30)))


def level_picture(rooms, opened, raised_floor=None, passages=()):
    """The top-down rooms as one picture, clear between rooms, and its origin in world units. raised_floor: {(room name,
    tile index): height} (raised), drawn as ground with an edge where it drops; passages: solid tiles drawn as ground."""
    rooms = top_rooms(rooms)
    raised_floor = raised_floor or {}
    tiles = tiles_of(rooms)
    x0, z0 = min(r["x"] for r in rooms), min(r["z"] for r in rooms)
    w = round((max(r["x"] + r["w"] * TILE for r in rooms) - x0) / TILE)
    h = round((max(r["z"] + r["h"] * TILE for r in rooms) - z0) / TILE)
    img = Image.new("RGBA", (w * PX, h * PX), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    edge, thick = RGB["Wall edge"], 2
    flights = stairs(rooms)
    for r in rooms:
        ox, oz = round((r["x"] - x0) / TILE), round((r["z"] - z0) / TILE)
        for i, f in enumerate(r["flags"]):
            px, py = (ox + i % r["w"]) * PX, (oz + i // r["w"]) * PX
            if (r["name"], i) in flights or (r["name"], i) in passages:
                d.rectangle([px, py, px + PX - 1, py + PX - 1], fill=ground(f, r["elevation"][i]))
                continue
            height = raised_floor.get((r["name"], i))
            if height is not None:
                d.rectangle([px, py, px + PX - 1, py + PX - 1], fill=shade(RGB["Floor"], 0.8 + RAISED_LIGHT * height))
                tx, tz = round(r["x"] / TILE) + i % r["w"], round(r["z"] / TILE) + i // r["w"]
                for (dx, dz), box in (((0, -1), [px, py, px + PX - 1, py + thick - 1]), ((0, 1), [px, py + PX - thick, px + PX - 1, py + PX - 1]),
                                      ((1, 0), [px + PX - thick, py, px + PX - 1, py + PX - 1]), ((-1, 0), [px, py, px + thick - 1, py + PX - 1])):
                    there = tiles.get((tx + dx, tz + dz))
                    if there and not there[2] & SOLID or there and raised_floor.get((there[0]["name"], there[1]), height) < height - 0.3:
                        d.rectangle(box, fill=edge)
                continue
            if f & SOLID:
                d.rectangle([px, py, px + PX - 1, py + PX - 1], fill=RGB["Door"] if (r["name"], i) in opened else RGB["Wall"])
                continue
            d.rectangle([px, py, px + PX - 1, py + PX - 1], fill=ground(f, r["elevation"][i]))
            if f & NORTH:
                d.rectangle([px, py, px + PX - 1, py + thick - 1], fill=edge)
            if f & SOUTH:
                d.rectangle([px, py + PX - thick, px + PX - 1, py + PX - 1], fill=edge)
            if f & EAST:
                d.rectangle([px + PX - thick, py, px + PX - 1, py + PX - 1], fill=edge)
            if f & WEST:
                d.rectangle([px, py, px + thick - 1, py + PX - 1], fill=edge)
        for a in r["actors"]:
            name = MARKED.get(a["kind"])
            if name and not hidden_switch(a):
                draw_mark(d, name, int((a["x"] - x0) // TILE) * PX, int((a["z"] - z0) // TILE) * PX)
    return img, (x0, z0)


def hidden_switch(actor):
    """A crystal switch with a second parameter of 1: it sets the level's switch-block state and removes itself (RE:
    ObjCrystalSwitch_Setup), so there is no switch to hit."""
    return actor["kind"] == "ObjCrystalSw" and len(actor["params"]) > 1 and actor["params"][1] == 1


def draw_mark(d, name, px, py):
    """An actor MARKED names, on the tile at pixel (px, py). Switch blocks carry a pattern, so they still differ in grey."""
    colour, line = RGB[name], shade(RGB[name], 0.6)
    if name in ("Bush", "Tree"):
        d.ellipse([px + 1, py + 1, px + PX - 2, py + PX - 2], fill=colour, outline=line)
    elif name == "Chest":
        d.rectangle([px + 2, py + 3, px + PX - 3, py + PX - 3], fill=colour, outline=(110, 80, 20))
        d.line([px + 2, py + PX // 2, px + PX - 3, py + PX // 2], fill=(110, 80, 20))
    elif name in ("Rock", "Cracked rock"):
        d.polygon([(px + PX // 2, py + 1), (px + PX - 2, py + PX // 2), (px + PX // 2, py + PX - 2), (px + 1, py + PX // 2)], fill=colour, outline=line)
    elif name == "Orange block":  # diagonal stripes
        d.rectangle([px + 1, py + 1, px + PX - 2, py + PX - 2], fill=colour, outline=shade(colour, 0.4))
        d.point([(px + x, py + y) for x in range(2, PX - 2) for y in range(2, PX - 2) if (x + y) % 4 == 0], fill=shade(colour, 0.45))
    elif name == "Blue block":  # a grid of light dots
        d.rectangle([px + 1, py + 1, px + PX - 2, py + PX - 2], fill=colour, outline=shade(colour, 0.4))
        for y in range(3, PX - 4, 4):
            for x in range(3, PX - 4, 4):
                d.rectangle([px + x, py + y, px + x + 1, py + y + 1], fill=(235, 240, 250))
    elif name == "Crystal switch":  # a light diamond around a dark one
        cx, cy, r = px + PX // 2, py + PX // 2, PX // 2 - 2
        d.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=colour, outline=(20, 20, 20))
        d.polygon([(cx, cy - r + 3), (cx + r - 3, cy), (cx, cy + r - 3), (cx - r + 3, cy)], fill=(20, 20, 20))
    else:
        d.rectangle([px + 2, py + 2, px + PX - 3, py + PX - 3], fill=colour, outline=line)


def swatch(name):
    """The legend's picture of a patterned mark: one tile of it on floor."""
    img = Image.new("RGB", (PX, PX), RGB["Floor"])
    draw_mark(ImageDraw.Draw(img), name, 0, 0)
    return img


def side_base(room):
    """The height a side-view room is drawn from: the band of SIDE_TILES its model stands in. A room stacked on another
    (Angler's Tunnel 04AU over 04AL) stands a band higher."""
    band = SIDE_TILES * TILE
    return band * int((room.get("origin") or (0, 0, 0))[1] // band)


def side_picture(room, wall=SIDE_WALL):
    """A side-view room: each column's floor at its height, and the surfaces chained above it. `wall` is what a solid column
    is drawn as."""
    h = SIDE_TILES * PX
    img = Image.new("RGB", (room["w"] * PX, h), SIDE_BACK)
    d = ImageDraw.Draw(img)
    k = PX / TILE
    base = side_base(room)

    def y_of(e):
        return round((SIDE_TILES * TILE - min(e - base, SIDE_TILES * TILE)) * k)

    for c in range(room["w"]):
        f, x = room["flags"][c], c * PX
        if f & SOLID:
            d.rectangle([x, 0, x + PX - 1, h - 1], fill=wall)
            continue
        if f & DEEP:
            d.rectangle([x, 0, x + PX - 1, h - 1], fill=RGB["Deep water"] if f & WATER else RGB["Hole"])
            continue
        e = room["elevation"][c]
        d.rectangle([x, y_of(e), x + PX - 1, y_of(e) + max(2, round(e * k)) - 1], fill=RGB["Floor"])
        nxt, guard = (room.get("chain") or [-1] * room["w"])[c], 0
        while nxt >= 0 and room.get("links") and nxt < len(room["links"]) and guard < 8:
            link = room["links"][nxt]
            d.rectangle([x, y_of(link[1]), x + PX - 1, y_of(link[1]) + max(3, round(k * 0.5)) - 1], fill=SIDE_WALL if link[0] & SOLID else RGB["Floor"])
            nxt, guard = link[2], guard + 1
    return img


def terrain(shapes, origin):
    """The faces of a room's model in front of the plane side-view rooms are played on, moved to where the room stands: a
    list of [(x, y)] * 3. Only shapes that reach the plane count, the rest being background; plants and ornaments hide nothing."""
    plane = 0.001
    ox, oy, oz = origin
    faces = []
    for name, triangles in shapes:
        if any(word in name for word in DECOR) or not any(min(p[2] for p in t) + oz < plane < max(p[2] for p in t) + oz for t in triangles):
            continue
        for a, b, c in triangles:
            if min(a[2], b[2], c[2]) + oz > -plane and (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0:
                faces.append([(p[0] + ox, p[1] + oy) for p in (a, b, c)])
    return faces


def has_terrain(room, faces):
    """Whether a model shows anything in this room. By a face's middle: a neighbour's faces can reach over the seam."""
    return any(room["x"] <= sum(x for x, _ in face) / 3 <= room["x"] + room["w"] * TILE for face in faces)


def draw_terrain(img, room, faces):
    """A room model's faces over a side-view picture, as the game's tiles draw theirs: solid, with floor on top. The model's
    art, not the game's collision."""
    k, base = PX / TILE, side_base(room)
    solid = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(solid)
    for face in faces:
        d.polygon([((x - room["x"]) * k, img.height - (y - base) * k) for x, y in face], fill=255)
    img.paste(SIDE_WALL, mask=solid)
    below = Image.new("L", img.size, 255)
    below.paste(solid, (0, SURFACE))
    img.paste(RGB["Floor"], mask=ImageChops.subtract(solid, below))


def png(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()
