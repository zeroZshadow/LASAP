"""The Archipelago item model: six balls in a ring, in the colors of the Archipelago logo.

The asset build (ap_assets.py) builds it from the Seashell Sensor model in the RomFS (ItemShellRader.bfres): the first
shape's vertex and index buffers are overwritten with six spheres, the other shapes are emptied, the textures are
replaced with flat ones (the albedo holds one stripe per ball color, the smoothness map makes them glossy), the
material's rim light is white and kept to the balls' edges, and its glow is turned off. The model inside keeps its name.
"""
import colorsys
import io
import math
import struct
from typing import TYPE_CHECKING, Callable

if TYPE_CHECKING:
    from RandomizerCore.Tools.bntx_editor import bfres

SOURCE = "ItemShellRader.bfres"
FILE_NAME = "ItemArchipelago.bfres"
MODEL_NAME = "ShellRader"

# Logo colors (data/icon.png in Archipelago), from the top ball clockwise as seen from the front.
LOGO_COLORS = [(201, 118, 130), (117, 194, 117), (202, 148, 194), (217, 160, 125), (118, 126, 189), (238, 227, 145)]
# The game's item models have saturated colors; the logo's pastels look washed out next to them.
SATURATION = 1.8


def _saturated(color):
    h, s, v = colorsys.rgb_to_hsv(*(c / 255 for c in color))
    return tuple(round(c * 255) for c in colorsys.hsv_to_rgb(h, min(1.0, s * SATURATION), v))


COLORS = [_saturated(c) for c in LOGO_COLORS]
# Material: a softer, white rim light than the sensor's pink one (intensity 0.7, edge 4), and no orange glow. The
# sensor's rim has power 1, which lightens the balls up to their centers; power 3 keeps it to their edges.
MATERIAL_PARAMS = {
    "fresnel_effect_intensity": (0.3,),
    "fresnel_effect_edge_brightness": (2.0,),
    "fresnel_effect_power": (3.0,),
    "fresnel_effect_color": (0.6, 0.6, 0.6, 1.0),
    "emissive": (0.0, 0.0, 0.0),
}
SMOOTHNESS = 200  # of 255: glossy, with a small highlight like the bomb's
# Item models are centered on their origin, which the item-get puts in the middle of its glow (see ItemRupeeBlue).
# The size matches the heart container's.
RING_RADIUS = 0.322
BALL_RADIUS = 0.203
CENTER_Y = 0.0

# Vertex layout of the source model's shapes (stride 24): position half4, normal 10-10-10-2 snorm,
# tangent and binormal 8-8-8-8 snorm, uv unorm16x2.
VERTEX_FORMAT = {"_p0": (0x1505, 0), "_n0": (0x0E02, 8), "_t0": (0x0B02, 12), "_b0": (0x0B02, 16), "_u0": (0x1201, 20)}
STRIDE = 24


class _Reader:
    def __init__(self, data: bytearray) -> None:
        self.d = data

    def u8(self, o): return self.d[o]
    def u16(self, o): return struct.unpack_from("<H", self.d, o)[0]
    def u32(self, o): return struct.unpack_from("<I", self.d, o)[0]
    def u64(self, o): return struct.unpack_from("<Q", self.d, o)[0]

    def string(self, o):
        n = self.u16(o)
        return self.d[o + 2:o + 2 + n].decode()


def _snorm8(v: float) -> int:
    return max(-127, min(127, round(v * 127))) & 0xFF


def _vertex(pos, normal, tangent, uv) -> bytes:
    n = [max(-511, min(511, round(c * 511))) & 0x3FF for c in normal]
    binormal = (normal[1] * tangent[2] - normal[2] * tangent[1], normal[2] * tangent[0] - normal[0] * tangent[2],
                normal[0] * tangent[1] - normal[1] * tangent[0])
    return (struct.pack("<4e", *pos, 1.0) + struct.pack("<I", n[0] | n[1] << 10 | n[2] << 20)
            + bytes(_snorm8(c) for c in tangent) + b"\x7f" + bytes(_snorm8(c) for c in binormal) + b"\x7f"
            + struct.pack("<2H", round(uv[0] * 65535), round(uv[1] * 65535)))


def _sphere_grid(vertex_budget: int, index_budget: int):
    """The finest (longitudes, latitudes) grid for six spheres that fits the buffers."""
    best = None
    for lon in range(4, 33):
        for lat in range(3, 17):
            if 6 * (lon + 1) * (lat + 1) <= vertex_budget and 6 * lon * lat * 6 <= index_budget:
                if best is None or lon * lat > best[0] * best[1]:
                    best = (lon, lat)
    if best is None:
        raise ValueError("the source model is too small for the Archipelago model")
    return best


def _balls(lon: int, lat: int, front_ccw: bool):
    vertices, indices = [], []
    for ball, angle in enumerate(90 - 60 * i for i in range(6)):
        cx = RING_RADIUS * math.cos(math.radians(angle))
        cy = CENTER_Y + RING_RADIUS * math.sin(math.radians(angle))
        uv = ((ball + 0.5) / 6, 0.5)
        first = len(vertices)
        for i in range(lat + 1):
            theta = math.pi * i / lat
            for j in range(lon + 1):
                phi = 2 * math.pi * j / lon
                n = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
                t = (-math.sin(phi), 0.0, math.cos(phi))
                p = (cx + BALL_RADIUS * n[0], cy + BALL_RADIUS * n[1], BALL_RADIUS * n[2])
                vertices.append(_vertex(p, n, t, uv))
        for i in range(lat):
            for j in range(lon):
                a = first + i * (lon + 1) + j
                b, c, d = a + lon + 1, a + lon + 2, a + 1
                # (a, b, c) winds clockwise seen from outside with this parametrization
                for tri in ((a, b, c), (a, c, d)):
                    indices.extend(tri[::-1] if front_ccw else tri)
    return vertices, indices


def _front_ccw(r: _Reader, base: int, vbuf: int, ibuf: int, icount: int) -> bool:
    """Whether the source mesh's outward faces wind counter-clockwise (face normal along the vertex normals)."""
    def pos(i):
        return struct.unpack_from("<3e", r.d, base + vbuf + i * STRIDE)

    def normal(i):
        v = r.u32(base + vbuf + i * STRIDE + 8)
        return [((v >> s & 0x3FF) ^ 0x200) - 0x200 for s in (0, 10, 20)]

    score = 0
    for k in range(0, icount - 2, 3):
        ia, ib, ic = (struct.unpack_from("<H", r.d, base + ibuf + 2 * (k + m))[0] for m in range(3))
        pa, pb, pc = pos(ia), pos(ib), pos(ic)
        u = [pb[m] - pa[m] for m in range(3)]
        v = [pc[m] - pa[m] for m in range(3)]
        face = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        n = [sum(c) for c in zip(normal(ia), normal(ib), normal(ic))]
        score += 1 if sum(face[m] * n[m] for m in range(3)) > 0 else -1
    return score > 0


def _rebuild_geometry(data: bytearray) -> None:
    r = _Reader(data)
    model = r.u64(0x28)
    base = r.u64(r.u64(0xB0) + 8)  # buffer data block
    shapes = r.u64(model + 0x28)
    indices: list = []
    shape_count = r.u16(model + 0x6A)
    for s in range(shape_count):
        shape = shapes + s * 0x60
        fvtx = r.u64(shape + 0x10)
        meshes = r.u64(shape + 0x18)
        mesh_count = r.u8(shape + 0x5B)
        vcount, voffset = r.u32(fvtx + 0x50), r.u32(fvtx + 0x48)
        if s == 0:
            attributes = {r.string(r.u64(r.u64(fvtx + 8) + 16 * a)): (r.u32(r.u64(fvtx + 8) + 16 * a + 8),
                                                                      r.u16(r.u64(fvtx + 8) + 16 * a + 12))
                          for a in range(r.u8(fvtx + 0x4C))}
            if attributes != VERTEX_FORMAT or r.u32(r.u64(fvtx + 0x38)) != STRIDE:
                raise ValueError(f"unexpected vertex format in {SOURCE}: {attributes}")
            first_mesh = meshes
            lon, lat = _sphere_grid(vcount, min(r.u32(meshes + 0x20 + 12 + 0x38 * m) for m in range(mesh_count)))
            ccw = _front_ccw(r, base, voffset, r.u32(first_mesh + 0x20), r.u32(first_mesh + 0x2C))
            vertices, indices = _balls(lon, lat, ccw)
            vertices += [vertices[0]] * (vcount - len(vertices))
            data[base + voffset:base + voffset + vcount * STRIDE] = b"".join(vertices)
        for m in range(mesh_count):
            mesh = meshes + 0x38 * m
            ioffset, icount = r.u32(mesh + 0x20), r.u32(mesh + 0x2C)
            if r.u32(mesh + 0x24) != 3 or r.u32(mesh + 0x28) != 1:
                raise ValueError(f"unexpected index format in {SOURCE}")
            # Shape 0 gets the balls (the rest degenerate); other shapes are emptied.
            values = (indices + [0] * (icount - len(indices))) if s == 0 else [0] * icount
            struct.pack_into(f"<{icount}H", data, base + ioffset, *values[:icount])
        if s == 0:
            extent = RING_RADIUS + BALL_RADIUS
            bounds = (0.0, CENTER_Y, 0.0, extent, extent, BALL_RADIUS)
            boxes, radii = r.u64(shape + 0x38), r.u64(shape + 0x40)
            for b in range((radii - boxes) // 24):
                struct.pack_into("<6f", data, boxes + 24 * b, *bounds)
            struct.pack_into("<f", data, radii, math.hypot(extent, extent))


def _rebuild_materials(data: bytearray) -> None:
    r = _Reader(data)
    model = r.u64(0x28)
    materials = r.u64(model + 0x38)
    for m in range(r.u16(model + 0x6C)):
        material = materials + 0xA8 * m
        params, values = r.u64(material + 0x50), r.u64(material + 0x60)
        for p in range(r.u16(material + 0x9E)):
            entry = params + 32 * p
            name = r.string(r.u64(entry + 8))
            if name in MATERIAL_PARAMS:
                size, offset = r.u8(entry + 0x11), r.u16(entry + 0x12)
                value = MATERIAL_PARAMS[name]
                if size != 4 * len(value):
                    raise ValueError(f"unexpected size of {name} in {SOURCE}")
                struct.pack_into(f"<{len(value)}f", data, values + offset, *value)


def _dds(width: int, height: int, fourcc: bytes, block: Callable[[int, int], bytes], block_size: int) -> io.BytesIO:
    blocks = b"".join(block(bx, by) for by in range(height // 4) for bx in range(width // 4))
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x81007, height, width, len(blocks), 0, 1)
    struct.pack_into("<2I4s", header, 76, 32, 4, fourcc)
    struct.pack_into("<I", header, 108, 0x1000)
    assert len(blocks) == (width // 4) * (height // 4) * block_size
    return io.BytesIO(bytes(header) + blocks)


def _bc1(color) -> bytes:
    c = (color[0] >> 3) << 11 | (color[1] >> 2) << 5 | color[2] >> 3
    return struct.pack("<HHI", c, c, 0)


def _textures(width: int, height: int) -> dict:
    """Replacement DDS per texture name suffix: (fourcc, block function, block size, sRGB)."""
    stripes = width // 4
    return {
        "_alb": (b"DXT1", lambda bx, by: _bc1(COLORS[min(5, bx * 6 // stripes)]), 8, True),
        "_nml": (b"BC5S", lambda bx, by: bytes(16), 16, False),        # flat: x = y = 0
        "_mask01": (b"DXT1", lambda bx, by: _bc1((0, 0, 0)), 8, True),
        "_nml2": (b"DXT1", lambda bx, by: _bc1((128, 128, 255)), 8, True),
        "_smt": (b"BC4U", lambda bx, by: bytes((SMOOTHNESS, SMOOTHNESS, 0, 0, 0, 0, 0, 0)), 8, False),
    }


def _rebuild_textures(model: "bfres.File") -> None:
    from RandomizerCore.Tools.bntx_editor import bntx_editor
    if model.bfresTexture.pos is None:
        raise ValueError(f"no textures in {SOURCE}")
    start, size = model.bfresTexture.pos, model.bfresTexture.size
    editor = bntx_editor.BNTXEditor()
    editor.openFile(bytes(model.rawData[start:start + size]))
    for index, texture in enumerate(editor.bntx.textures):
        suffix = texture.name[len("MI_ShellRader"):]
        fourcc, block, block_size, srgb = _textures(texture.width, texture.height)[suffix]
        dds = _dds(texture.width, texture.height, fourcc, block, block_size)
        new = editor.bntx.replace(texture, texture.tileMode, srgb, False, False, False, dds)
        if not new:
            raise ValueError(f"cannot replace {texture.name}")
        editor.bntx.textures[index] = new
    data = editor.save()
    if len(data) > size:
        raise ValueError("the rebuilt textures do not fit the model file")
    raw = bytearray(model.rawData)
    raw[start:start + len(data)] = data
    model.rawData = raw


def build(source_path: str) -> bytes:
    """ItemArchipelago.bfres, from the RomFS's ItemShellRader.bfres."""
    from RandomizerCore.Tools.bntx_editor import bfres  # the asset build only: the release has no BNTX editor
    model = bfres.File()
    model.readFromFile(source_path)
    _rebuild_textures(model)
    data = bytearray(model.rawData)
    _rebuild_geometry(data)
    _rebuild_materials(data)
    return bytes(data)
