"""Pictures of the levels for Logic Studio, and the marks it pins on them.

Each level's top-down rooms are one picture, its side-view rooms one picture each, PX pixels a tile. A door that stands
in a doorway is a mark: where it is, the rooms it joins, a letter and a name.
"""
import io

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
           "Sign": "#b8905a", "Chest": "#e0b030", "Door": "#b5794a", "Block": "#7d4f24"}
RGB = {name: hexrgb(value) for name, value in COLOURS.items()}
SIDE_BACK, SIDE_WALL = (40, 51, 58), (15, 20, 23)
SURFACE = 3  # pixels of floor drawn on a room model's solid
DECOR = ("grass", "kazari")  # in the name of a room model's shape that decorates
# What an actor is drawn as; walkable grass (ObjLawn) is ground
MARKED = {"ObjGrass": "Bush", "ObjTree": "Tree", "ObjCactus": "Tree", "ObjRock": "Rock", "ObjGrave": "Rock", "ObjFieldCrac": "Cracked rock",
          "ObjSignboard": "Sign", "ObjTreasureBox": "Chest", "ObjSquareBlock": "Block"}
# What stands in a doorway: the letter on its mark and its name. Rotating doors let Link through going north.
DOORS = {"ObjLockDoor": ("K", "Key door"), "ObjBossDoor": ("N", "Nightmare door"), "ObjClosedDoor": ("S", "Shutter"), "ObjClosedPot": ("P", "Pot door"),
         "ObjDungeonCr": ("B", "Bomb wall"), "ObjOnewayDoo": ("↑", "Rotating door"), "ObjLockBlock": ("L", "Key block"), "doorway": ("O", "Open door")}


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


def doors(rooms):
    """(tiles a door opens as {(room name, tile index)}, the marks). A door actor on a room's edge opens its tiles and the
    ones across the edge; a doorway with no actor is two solid tiles across rooms that both carry the doorway bit, with no open floor beside them along the seam."""
    rooms = top_rooms(rooms)
    tiles = tiles_of(rooms)
    opened, marks = set(), {}

    def mark(kind, x, z, room, inward=(0, 0)):
        key = (kind, round(x, 2), round(z, 2))
        if key not in marks:
            marks[key] = {"kind": kind, "x": x, "z": z, "rooms": [], "inward": list(inward)}
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
            mark(a["kind"], x, z, r["name"], (-dx, -dz))
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

    bare = {}
    for (x, z), (room, i, f) in tiles.items():
        for dx, dz in ((1, 0), (0, 1)):
            there = tiles.get((x + dx, z + dz))
            if not there or there[0] is room or not f & there[2] & DOORWAY or not (f | there[2]) & SOLID:
                continue
            if (room["name"], i) in opened or (there[0]["name"], there[1]) in opened:
                continue
            if open_across(x + dz, z + dx, dx, dz) or open_across(x - dz, z - dx, dx, dz):
                continue
            door = bare.setdefault((room["name"], there[0]["name"]), {"rooms": [room["name"], there[0]["name"]], "x": 0.0, "z": 0.0, "tiles": []})
            door["x"] += (x + (1 if dx else 0.5)) * TILE
            door["z"] += (z + (1 if dz else 0.5)) * TILE
            door["tiles"] += [(room["name"], i), (there[0]["name"], there[1])]
    for door in bare.values():
        n = len(door["tiles"]) / 2
        opened.update(door["tiles"])
        for room in door["rooms"]:
            mark("doorway", door["x"] / n, door["z"] / n, room)
    found = list(marks.values())
    spot = {}
    for m in found:
        spot.setdefault((round(m["x"], 2), round(m["z"], 2)), []).append(m)
    for m in found:
        letter, name = DOORS[m["kind"]]
        m.update(letter=letter, name=name, apart=len(spot[(round(m["x"], 2), round(m["z"], 2))]) > 1, x=round(m["x"], 3), z=round(m["z"], 3))
        del m["kind"]
    return opened, found


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


def ground(f, elevation):
    if f & DEEP:
        return RGB["Deep water"] if f & WATER else RGB["Hole"]
    if f & DIG:
        return RGB["Dig spot"]
    return shade(RGB["Shallow water"] if f & WATER else RGB["Floor"], max(0.65, min(1.25, 0.8 + elevation / 30)))


def level_picture(rooms, opened):
    """The top-down rooms as one picture, clear between rooms, and its origin in world units."""
    rooms = top_rooms(rooms)
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
            if (r["name"], i) in flights:
                d.rectangle([px, py, px + PX - 1, py + PX - 1], fill=ground(f, r["elevation"][i]))
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
            if not name:
                continue
            px, py = int((a["x"] - x0) // TILE) * PX, int((a["z"] - z0) // TILE) * PX
            colour, line = RGB[name], shade(RGB[name], 0.6)
            if name in ("Bush", "Tree"):
                d.ellipse([px + 1, py + 1, px + PX - 2, py + PX - 2], fill=colour, outline=line)
            elif name == "Chest":
                d.rectangle([px + 2, py + 3, px + PX - 3, py + PX - 3], fill=colour, outline=(110, 80, 20))
                d.line([px + 2, py + PX // 2, px + PX - 3, py + PX // 2], fill=(110, 80, 20))
            elif name in ("Rock", "Cracked rock"):
                d.polygon([(px + PX // 2, py + 1), (px + PX - 2, py + PX // 2), (px + PX // 2, py + PX - 2), (px + 1, py + PX // 2)], fill=colour, outline=line)
            else:
                d.rectangle([px + 2, py + 2, px + PX - 3, py + PX - 3], fill=colour, outline=line)
    return img, (x0, z0)


def side_picture(room, wall=SIDE_WALL):
    """A side-view room: each column's floor at its height, and the surfaces chained above it. `wall` is what a solid column
    is drawn as."""
    h = SIDE_TILES * PX
    img = Image.new("RGB", (room["w"] * PX, h), SIDE_BACK)
    d = ImageDraw.Draw(img)
    k = PX / TILE

    def y_of(e):
        return round((SIDE_TILES * TILE - min(e, SIDE_TILES * TILE)) * k)

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
    k = PX / TILE
    solid = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(solid)
    for face in faces:
        d.polygon([((x - room["x"]) * k, img.height - y * k) for x, y in face], fill=255)
    img.paste(SIDE_WALL, mask=solid)
    below = Image.new("L", img.size, 255)
    below.paste(solid, (0, SURFACE))
    img.paste(RGB["Floor"], mask=ImageChops.subtract(solid, below))


def png(img):
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    return buf.getvalue()
