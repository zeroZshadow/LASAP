"""The warp points' item model: a faceted stone medallion with the warp pads' glyph engraved in both faces, glowing
blue as an open warp point's.

The glyph is the one the game's warp holes and Manbo's platforms show (the effect texture warpmanbo_00 in
effect/Object.ptcl, carved into MI_WarpHole_01 in map/Field.bntx): a spiral, four rays on the diagonals and four dots.
The asset build (scripts/build_assets.py) builds it from the Seashell Sensor model in the RomFS (ItemShellRader.bfres),
as the Archipelago model (ap_model.py): its first shape, whose material glows through its emission map, becomes the
medallion, a polygon with chamfered edges, flat-shaded; the second shape is emptied. The engraving is in the textures,
which map the front face onto the whole texture (the back face mirrored, so it reads the same from behind): the normal
map gives the groove's walls, the albedo darkens them, and the emission map lights the groove: its floor hottest along
each stroke's middle, its light reaching up the walls and onto the face around it. The model inside keeps its name.
"""
import math
import struct
from pathlib import Path
from typing import Callable, List, Sequence, Tuple

import ap_bntx
from ap_model import bc1, set_material_params, vertex, write_shapes

SOURCE = "ItemShellRader.bfres"
FILE_NAME = "ItemWarpMedallion.bfres"
MODEL_NAME = "ShellRader"
MATERIAL = "MI_ShellRader"  # shape 0's; its emission map is the mask texture

# Sizes in model units. The medallion is about as wide as the Archipelago model, whose item-get pose it takes.
SIDES = 12
APOTHEM = 0.48       # centre to the middle of a side; a flat side at the bottom
THICKNESS = 0.16
CHAMFER = 0.045      # each edge cut at 45 degrees
TEXTURE_RADIUS = 0.5  # the texture spans the face from -0.5 to 0.5
# The glyph (as the effect texture draws it, scaled to the face): a spiral of half circles from the centre outwards,
# each half circle SPIRAL_STEP / 2 wider than the last, rays from RAY_BASE to RAY_TIP on the diagonals, dots on the axes
SPIRAL_STEP = 0.06
SPIRAL_ARCS = 6
STROKE = 0.045
RAY_BASE, RAY_TIP, RAY_HALF_WIDTH = 0.235, 0.34, 0.036
DOT_DISTANCE, DOT_RADIUS = 0.28, 0.03
# The engraving: the glyph is its floor, DEPTH below the face, with walls sloping out to WALL beyond the glyph
DEPTH = 0.04
WALL = 0.02
# Texels across the face per texture, within the source's room for textures (single mip levels): the glow's gradient
# sharpest; the second normal map, which the material's single layer does not read, a single block
TEXTURE_SIZES = {"_mask01": 512, "_alb": 256, "_nml": 256, "_smt": 128, "_nml2": 4}

# The stone: old and weathered, as the chamber stones (ItemPanelDungeonPiece.bfres) and the cave rocks
# (ObjCaveRock.bfres) are. Its colour lies between the warp hole's olive (MI_WarpHole_01_alb) and the cave rocks' grey,
# from STONE_LIGHT to STONE_DARK in soft broad patches, lighter at the top and darker at the bottom and towards the rim,
# with faint pale streaks (VEINS) and the pits darker; worn where the face turns into the chamfers (WORN). The detail is
# in its relief: it rolls (EROSION, the height of its swells) and is pitted in clusters (PIT_DEPTH deep).
STONE_LIGHT = (128, 123, 98)
STONE_DARK = (62, 64, 54)
VEINS = 0.12
WORN = (104, 100, 80)
DIRT = (44, 42, 34)
WALLS = (60, 58, 46)
EROSION = 0.006
PIT_DEPTH = 0.007
FLOOR = (10, 30, 70)
# The glow: the emission map's colour times the material's emissive_intensity, as the game's glowing models have it
# (its emissive colour is not read with an emission map; rupees take 2, the Angler's lamp 3, the Armos' eyes 7).
# The groove's floor glows GLOW_CORE along the middle of a stroke, GLOW_EDGE at its edges; the light reaches up the
# walls (WALL_GLOW at their foot) and onto the face around the groove (SPILL, fading over SPILL_WIDTH).
GLOW_CORE = (150, 225, 255)
GLOW_EDGE = (20, 90, 255)
WALL_GLOW = 0.7
SPILL = 0.12
SPILL_WIDTH = 0.02
GLOW_INTENSITY = 3.0
STONE_SMOOTHNESS = 40   # of 255
PIT_SMOOTHNESS = 15
WALL_SMOOTHNESS = 60
FLOOR_SMOOTHNESS = 150

Point = Tuple[float, float]
Colour = Tuple[float, ...]


# --- the glyph's outline

def spiral_arcs() -> List[Tuple[Point, float, float]]:
    """The spiral's half circles from the centre out: (centre, radius, start angle); each runs half a turn clockwise.
    Their ends lie on the x axis, alternately right and left of the centre."""
    ends = [0.0] + [(k + 1) // 2 * SPIRAL_STEP * (1 if k % 2 else -1) for k in range(1, SPIRAL_ARCS + 1)]
    return [(((a + b) / 2, 0.0), abs(b - a) / 2, math.pi if b > a else 0.0) for a, b in zip(ends, ends[1:])]


def rays() -> List[Tuple[Point, Point, Point]]:
    """The four rays: (tip, base corner, base corner), counter-clockwise."""
    out = []
    for angle in (45, 135, 225, 315):
        d = (math.cos(math.radians(angle)), math.sin(math.radians(angle)))
        p = (-d[1], d[0])
        base = (RAY_BASE * d[0], RAY_BASE * d[1])
        out.append(((RAY_TIP * d[0], RAY_TIP * d[1]),
                    (base[0] + RAY_HALF_WIDTH * p[0], base[1] + RAY_HALF_WIDTH * p[1]),
                    (base[0] - RAY_HALF_WIDTH * p[0], base[1] - RAY_HALF_WIDTH * p[1])))
    return out


def dots() -> List[Point]:
    return [(DOT_DISTANCE * math.cos(math.radians(a)), DOT_DISTANCE * math.sin(math.radians(a))) for a in (0, 90, 180, 270)]


def _segment_distance(p: Point, a: Point, b: Point) -> float:
    ab = (b[0] - a[0], b[1] - a[1])
    t = max(0.0, min(1.0, ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / (ab[0] ** 2 + ab[1] ** 2)))
    return math.hypot(p[0] - a[0] - t * ab[0], p[1] - a[1] - t * ab[1])


def glyph_distance(p: Point) -> float:
    """How far p lies outside the glyph's outline, negative inside."""
    best = math.inf
    for (cx, cy), r, start in spiral_arcs():
        angle = math.atan2(p[1] - cy, p[0] - cx)
        if (start - angle) % (2 * math.pi) <= math.pi:  # within the arc, which runs from start down to start - pi
            d = abs(math.hypot(p[0] - cx, p[1] - cy) - r)
        else:
            d = min(math.hypot(p[0] - cx - r * math.cos(a), p[1] - cy - r * math.sin(a)) for a in (start, start - math.pi))
        best = min(best, d - STROKE / 2)
    for tri in rays():
        inside = all((tri[(i + 1) % 3][0] - tri[i][0]) * (p[1] - tri[i][1])
                     - (tri[(i + 1) % 3][1] - tri[i][1]) * (p[0] - tri[i][0]) >= 0 for i in range(3))
        edge = min(_segment_distance(p, tri[i], tri[(i + 1) % 3]) for i in range(3))
        best = min(best, -edge if inside else edge)
    for c in dots():
        best = min(best, math.hypot(p[0] - c[0], p[1] - c[1]) - DOT_RADIUS)
    return best


# --- geometry

def _corners(apothem: float) -> List[Point]:
    """The polygon's corners, counter-clockwise, its sides' middles at multiples of 360 / SIDES degrees (one at the
    bottom)."""
    r = apothem / math.cos(math.pi / SIDES)
    return [(r * math.cos((2 * k + 1) * math.pi / SIDES), r * math.sin((2 * k + 1) * math.pi / SIDES))
            for k in range(SIDES)]


def _face_uv(x: float, y: float, back: bool) -> Point:
    """The texture's coordinates of a point: the front seen from the front; the back mirrored, so its engraving reads
    the same from behind."""
    return 0.5 + (-x if back else x) / (2 * TEXTURE_RADIUS), 0.5 - y / (2 * TEXTURE_RADIUS)


def medallion_mesh() -> Tuple[List[bytes], List[int]]:
    """Front cap, per side a front chamfer, a side and a back chamfer, back cap; each face its own vertices, as the
    model is flat-shaded. Triangles counter-clockwise seen from outside."""
    vertices: List[bytes] = []
    indices: List[int] = []
    inner, outer = _corners(APOTHEM - CHAMFER), _corners(APOTHEM)
    front, back = THICKNESS / 2, -THICKNESS / 2

    def add(p: Sequence[float], normal: Sequence[float], tangent: Sequence[float], back_uv: bool) -> int:
        vertices.append(vertex(tuple(p), tuple(normal), tuple(tangent), _face_uv(p[0], p[1], back_uv)))
        return len(vertices) - 1

    for z, side in ((front, 1.0), (back, -1.0)):
        centre = add((0.0, 0.0, z), (0.0, 0.0, side), (side, 0.0, 0.0), side < 0)
        ring = [add((x, y, z), (0.0, 0.0, side), (side, 0.0, 0.0), side < 0) for x, y in inner]
        for k in range(SIDES):
            a, b = ring[k], ring[(k + 1) % SIDES]
            indices += [centre, a, b] if side > 0 else [centre, b, a]
    s = math.sqrt(0.5)
    for k in range(SIDES):
        m = (2 * k + 2) * math.pi / SIDES  # the side's middle, between corners k and k + 1
        out = (math.cos(m), math.sin(m))
        tangent = (-out[1], out[0], 0.0)
        bands = (  # (lower ring corners and z, upper ring corners and z, normal, back mapping), from front to back
            ((inner, front), (outer, front - CHAMFER), (s * out[0], s * out[1], s), False),
            ((outer, front - CHAMFER), (outer, back + CHAMFER), (out[0], out[1], 0.0), False),
            ((outer, back + CHAMFER), (inner, back), (s * out[0], s * out[1], -s), True),
        )
        for (ring_a, za), (ring_b, zb), normal, back_uv in bands:
            a0 = add((*ring_a[k], za), normal, tangent, back_uv)
            a1 = add((*ring_a[(k + 1) % SIDES], za), normal, tangent, back_uv)
            b0 = add((*ring_b[k], zb), normal, tangent, back_uv)
            b1 = add((*ring_b[(k + 1) % SIDES], zb), normal, tangent, back_uv)
            indices += [a0, b0, b1, a0, b1, a1]
    return vertices, indices


def _wound(indices: List[int], front_ccw: bool) -> List[int]:
    """Triangles given counter-clockwise seen from outside, wound as the source model winds its outward faces."""
    if front_ccw:
        return indices
    return [i for k in range(0, len(indices), 3) for i in (indices[k], indices[k + 2], indices[k + 1])]


def _check_outward(vertices: List[bytes], indices: List[int]) -> None:
    """Every triangle (counter-clockwise) faces the way its vertices' normals point."""
    for k in range(0, len(indices), 3):
        ps = [struct.unpack_from("<3e", vertices[i]) for i in indices[k:k + 3]]
        n = [0, 0, 0]
        for i in indices[k:k + 3]:
            v = struct.unpack_from("<I", vertices[i], 8)[0]
            n = [n[c] + ((((v >> (10 * c)) & 0x3FF) ^ 0x200) - 0x200) for c in range(3)]
        u = [ps[1][m] - ps[0][m] for m in range(3)]
        w = [ps[2][m] - ps[0][m] for m in range(3)]
        face = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        if sum(face[m] * n[m] for m in range(3)) <= 0:
            raise AssertionError(f"triangle {k // 3} faces inwards")


def _rebuild_geometry(data: bytearray) -> None:
    corner = APOTHEM / math.cos(math.pi / SIDES)
    extent = (corner, corner, THICKNESS / 2)
    bounds = ((0.0, 0.0, 0.0, *extent), math.hypot(*extent))

    def make(shape: int, vertex_budget: int, index_budget: int, front_ccw: bool):
        if shape:
            return None
        vertices, indices = medallion_mesh()
        _check_outward(vertices, indices)
        return vertices, _wound(indices, front_ccw), bounds
    write_shapes(data, SOURCE, make)


# --- textures

def _lattice(i: int, j: int) -> float:
    h = (i * 374761393 + j * 668265263) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return (h >> 8) / (1 << 23) - 1.0


def _octave(x: float, y: float) -> float:
    """Smooth value noise in [-1, 1] on a hashed lattice of unit cells."""
    i, j = math.floor(x), math.floor(y)
    fx, fy = x - i, y - j
    sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    top = _lattice(i, j) + sx * (_lattice(i + 1, j) - _lattice(i, j))
    bottom = _lattice(i, j + 1) + sx * (_lattice(i + 1, j + 1) - _lattice(i, j + 1))
    return top + sy * (bottom - top)


def _fbm(x: float, y: float, frequency: float, octaves: int, seed: float) -> float:
    """Octaves of value noise from `frequency` up, each twice as fine and half as strong, in about [-1, 1]."""
    total, weight, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        f = frequency * 2 ** o
        total += weight * _octave(x * f + seed + 3.7 * o, y * f - seed + 1.9 * o)
        norm += weight
        weight /= 2
    return total / norm


def pits(x: float, y: float) -> float:
    """How deep the stone is pitted at a point (0 to 1): small pits, clustered in patches."""
    clusters = _clamp01(0.3 + 1.8 * _fbm(x, y, 4.0, 2, 3.1))
    grain = _clamp01((_fbm(x, y, 80.0, 2, 11.0) - 0.15) * 3.5)
    return grain * clusters


def stone_height(x: float, y: float) -> float:
    """The weathered face's height: gentle swells and its pits."""
    return EROSION * _fbm(x, y, 9.0, 4, 5.3) - PIT_DEPTH * pits(x, y)


def stone_colour(x: float, y: float, rim: float) -> Colour:
    """The stone's albedo at a point; rim: how far into the worn edge (0 on the face, 1 on the chamfers)."""
    below = _clamp01(0.5 - 0.5 * y / APOTHEM)  # 0 at the top, 1 at the bottom
    age = _clamp01(0.2 + 0.8 * _fbm(x, y, 2.2, 3, 7.7) + 0.3 * below + 0.35 * (math.hypot(x, y) / APOTHEM) ** 2)
    colour = _mix(STONE_LIGHT, STONE_DARK, age)
    vein = _clamp01(1.0 - abs(_fbm(x, y, 5.0, 3, 17.0)) * 10)  # thin lines where a noise crosses zero
    colour = tuple(c * (1.0 + VEINS * vein + 0.05 * _fbm(x, y, 30.0, 2, 2.2)) for c in colour)
    colour = _mix(colour, WORN, rim)
    pit = pits(x, y)  # darker in the pits, and dirtier
    return _mix(tuple(c * (1.0 - 0.3 * pit) for c in colour), DIRT, 0.15 * pit)


def _mix(a: Sequence[float], b: Sequence[float], t: float) -> Colour:
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def _clamp01(v: float) -> float:
    return max(0.0, min(1.0, v))


class Face:
    """The face's texels (rows from the top, at the texel's centre): where they lie and how far outside the glyph."""

    def __init__(self, size: int):
        self.size = size
        self.texel = 2 * TEXTURE_RADIUS / size
        self.points = [[(((tx + 0.5) / size - 0.5) * 2 * TEXTURE_RADIUS, (0.5 - (ty + 0.5) / size) * 2 * TEXTURE_RADIUS)
                        for tx in range(size)] for ty in range(size)]
        reach = RAY_TIP + WALL + SPILL_WIDTH + 2 * self.texel
        self.signed = [[glyph_distance(p) if math.hypot(*p) < reach else math.inf for p in row] for row in self.points]
        self.distance = [[max(0.0, d) for d in row] for row in self.signed]
        self.heights = None  # stone_height per texel, made when first read

    def floor(self, tx: int, ty: int) -> float:
        """How much of the texel is the groove's floor (the glyph)."""
        return _clamp01(0.5 - self.signed[ty][tx] / self.texel)

    def glow(self, tx: int, ty: int) -> Colour:
        """The emission map's texel: the floor's glow, hottest along a stroke's middle, and its light up the walls and
        on the face around the groove."""
        d = self.signed[ty][tx]
        core = _clamp01(-d / (STROKE / 2)) ** 0.7
        floor = _mix(GLOW_EDGE, GLOW_CORE, core)
        if d < WALL:
            around = WALL_GLOW * (1.0 - _clamp01(d / WALL)) ** 2 + SPILL
        else:
            around = SPILL * (1.0 - _clamp01((d - WALL) / SPILL_WIDTH))
        return _mix(tuple(c * around for c in GLOW_EDGE), floor, self.floor(tx, ty))

    def height(self, tx: int, ty: int) -> float:
        """The engraving, and the weathered stone outside it."""
        tx, ty = max(0, min(self.size - 1, tx)), max(0, min(self.size - 1, ty))
        if self.heights is None:
            self.heights = [[stone_height(x, y) for x, y in row] for row in self.points]
        cut = _clamp01(1.0 - self.distance[ty][tx] / WALL)
        return -DEPTH * cut + (1.0 - cut) * self.heights[ty][tx]

    def wall(self, tx: int, ty: int) -> float:
        """How much of the texel is the groove's wall."""
        return _clamp01(1.0 - self.distance[ty][tx] / WALL) * (1.0 - self.floor(tx, ty))


def albedo(face: Face) -> List[List[Colour]]:
    rows = []
    edge = APOTHEM - CHAMFER
    for ty, row_points in enumerate(face.points):
        row = []
        for tx, (x, y) in enumerate(row_points):
            # the chamfers and sides read beyond the front face's edge: worn smooth there
            out = max(abs(x * math.cos(2 * math.pi * k / SIDES) + y * math.sin(2 * math.pi * k / SIDES))
                      for k in range(SIDES // 2))
            colour = stone_colour(x, y, _clamp01((out - edge + 0.015) / 0.015))
            colour = _mix(colour, WALLS, face.wall(tx, ty))
            row.append(_mix(colour, FLOOR, face.floor(tx, ty)))
        rows.append(row)
    return rows


def normals(face: Face) -> List[List[Tuple[float, float]]]:
    """Tangent-space x and y of the face's normal (x along +u, y up the face), from the engraving's slopes."""
    step = 2 * face.texel
    rows = []
    for ty in range(face.size):
        row = []
        for tx in range(face.size):
            dx = (face.height(tx + 1, ty) - face.height(tx - 1, ty)) / step
            dy = (face.height(tx, ty - 1) - face.height(tx, ty + 1)) / step  # rows run down the face
            length = math.sqrt(dx * dx + dy * dy + 1.0)
            row.append((-dx / length, -dy / length))
        rows.append(row)
    return rows


def encode_bc1(rows: List[List[Colour]]) -> bytes:
    """BC1 blocks in row order, four colours each: the ends are the block's darkest and lightest texels."""
    out = bytearray()
    for by in range(0, len(rows), 4):
        for bx in range(0, len(rows[0]), 4):
            texels = [rows[by + k // 4][bx + k % 4] for k in range(16)]
            c0, c1 = ap_bntx.to565(max(texels, key=sum)), ap_bntx.to565(min(texels, key=sum))
            if c0 == c1:
                out += struct.pack("<HHI", c0, c1, 0)
                continue
            if c0 < c1:
                c0, c1 = c1, c0
            e0, e1 = ap_bntx.from565(c0), ap_bntx.from565(c1)
            palette = [e0, e1, _mix(e0, e1, 1 / 3), _mix(e0, e1, 2 / 3)]
            bits = 0
            for k, t in enumerate(texels):
                bits |= min(range(4), key=lambda i: sum((a - b) ** 2 for a, b in zip(t, palette[i]))) << (2 * k)
            out += struct.pack("<HHI", c0, c1, bits)
    return bytes(out)


def encode_bc4(values: Callable[[int, int], float], size: int, signed: bool) -> List[bytes]:
    """BC4 blocks in row order of one channel, values(tx, ty) in [0, 1] (unsigned) or [-1, 1] (signed): the ends are
    the block's extremes, eight levels between them."""
    blocks = []
    scale, low = (127, -127) if signed else (255, 0)
    for by in range(0, size, 4):
        for bx in range(0, size, 4):
            texels = [max(low, min(scale, round(values(bx + k % 4, by + k // 4) * scale))) for k in range(16)]
            e0, e1 = max(texels), min(texels)
            ends = bytes(((e0 & 0xFF), (e1 & 0xFF)))
            if e0 == e1:
                blocks.append(ends + bytes(6))
                continue
            palette = [e0, e1] + [((7 - i) * e0 + i * e1) / 7 for i in range(1, 7)]
            bits = 0
            for k, t in enumerate(texels):
                bits |= min(range(8), key=lambda i: abs(t - palette[i])) << (3 * k)
            blocks.append(ends + bits.to_bytes(6, "little"))
    return blocks


def _rebuild_textures(data: bytes) -> bytearray:
    bntx, at, size = ap_bntx.open_bfres(data)
    faces = {}
    for texture in bntx.textures:
        w, h = texture.width, texture.height
        suffix = texture.name[len("MI_ShellRader"):]
        if suffix not in TEXTURE_SIZES:
            raise ValueError(f"unexpected texture {texture.name} in {SOURCE}")
        w = h = TEXTURE_SIZES[suffix]
        if suffix == "_nml2":
            texture.replace(0x1a06, w, h, [bc1((128, 128, 255)) * (w // 4 * h // 4)])
            continue
        face = faces.get(w) or faces.setdefault(w, Face(w))
        if suffix == "_alb":
            texture.replace(0x1a06, w, h, [encode_bc1(albedo(face))])
        elif suffix == "_mask01":  # the emission map
            glow = [[face.glow(tx, ty) for tx in range(w)] for ty in range(h)]
            texture.replace(0x1a06, w, h, [encode_bc1(glow)])
        elif suffix == "_smt":
            def smoothness(tx: int, ty: int) -> float:
                level = _mix((STONE_SMOOTHNESS,), (PIT_SMOOTHNESS,), pits(*face.points[ty][tx]))
                level = _mix(level, (WALL_SMOOTHNESS,), face.wall(tx, ty))
                return _mix(level, (FLOOR_SMOOTHNESS,), face.floor(tx, ty))[0] / 255
            texture.replace(0x1d01, w, h, [b"".join(encode_bc4(smoothness, w, signed=False))])
        elif suffix == "_nml":  # BC5, signed: x then y
            n = normals(face)
            xs = encode_bc4(lambda tx, ty: n[ty][tx][0], w, signed=True)
            ys = encode_bc4(lambda tx, ty: n[ty][tx][1], w, signed=True)
            texture.replace(0x1e02, w, h, [b"".join(x + y for x, y in zip(xs, ys))])
    return ap_bntx.replace_bntx(data, bntx.pack(), at, size)


MATERIAL_PARAMS = {"emissive_intensity": (GLOW_INTENSITY,), "fresnel_effect_intensity": (0.0,)}


def build(source_path: str) -> bytes:
    """ItemWarpMedallion.bfres, from the RomFS's ItemShellRader.bfres."""
    data = _rebuild_textures(Path(source_path).read_bytes())
    _rebuild_geometry(data)
    set_material_params(data, SOURCE, lambda material: MATERIAL_PARAMS if material == MATERIAL else {})
    return bytes(data)
