"""Bind-pose bounds of BFRES models, for sizing and centring NPC models shown as items (ap_held_model.cpp, g_HeldModelFixes)
and the drop actor's models (ap_drop.write_drops).

python model_bounds.py <file.bfres>[:<model>] ... [--ref <file.bfres>[:<model>]] [--shapes]

Each vertex goes to model space: smooth-skinned ones (skin count 2 or more) are stored there, rigid ones (1) in their
bone's space and static ones (0) in the shape's bone's space, which the skeleton's bind pose (FSKL, Euler XYZ) maps
to model space. Prints per model the box, its centre, the radius around the root (the origin) and around the centre.
With --ref, also the fix that gives each model the reference's radius around its centre and puts that centre on the
root: scale = ref radius / radius, offset = the centre (model units, what the root bone moves by).
"""
import math
import struct
import sys
from pathlib import Path


class Reader:
    def __init__(self, data: bytes) -> None:
        self.d = data

    def u8(self, o): return self.d[o]
    def u16(self, o): return struct.unpack_from("<H", self.d, o)[0]
    def s16(self, o): return struct.unpack_from("<h", self.d, o)[0]
    def u32(self, o): return struct.unpack_from("<I", self.d, o)[0]
    def u64(self, o): return struct.unpack_from("<Q", self.d, o)[0]
    def f32(self, o, n): return struct.unpack_from(f"<{n}f", self.d, o)

    def string(self, o):
        n = self.u16(o)
        return self.d[o + 2:o + 2 + n].decode()


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def srt(scale, rot, pos):
    """Bone local matrix, rotation as Euler XYZ (R = Rz * Ry * Rx)."""
    sx, sy, sz = scale
    cx, cy, cz = (math.cos(a) for a in rot[:3])
    nx, ny, nz = (math.sin(a) for a in rot[:3])
    r = [[cy * cz, nx * ny * cz - cx * nz, cx * ny * cz + nx * nz],
         [cy * nz, nx * ny * nz + cx * cz, cx * ny * nz - nx * cz],
         [-ny, nx * cy, cx * cy]]
    return [[r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, pos[0]],
            [r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, pos[1]],
            [r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, pos[2]],
            [0, 0, 0, 1]]


def apply(m, v):
    return tuple(m[i][0] * v[0] + m[i][1] * v[1] + m[i][2] * v[2] + m[i][3] for i in range(3))


def half(h):
    return struct.unpack("<e", struct.pack("<H", h))[0]


# Attribute formats: high byte the component layout, low byte the type (1 unorm, 2 snorm, 3 uint, 4 sint, 5 float)
LAYOUTS = {0x02: (1, 1), 0x09: (2, 1), 0x0B: (4, 1), 0x0A: (1, 2), 0x12: (2, 2), 0x15: (4, 2), 0x14: (1, 4), 0x16: (1, 4),
           0x17: (2, 4), 0x18: (3, 4), 0x19: (4, 4)}


def read_attr(r: Reader, at: int, fmt: int):
    count, size = LAYOUTS[fmt >> 8]
    kind = fmt & 0xFF
    out = []
    for c in range(count):
        o = at + c * size
        if size == 4:
            out.append(r.f32(o, 1)[0] if kind == 5 else r.u32(o))
        elif size == 2:
            out.append(half(r.u16(o)) if kind == 5 else r.u16(o))
        else:
            out.append(r.u8(o))
    return out


def models(r: Reader):
    """(name, offset) of each FMDL: the file header's model array (+0x28), 0x78 bytes per model (BFRES 9)."""
    first = r.u64(0x28)
    out = []
    o = first
    while r.d[o:o + 4] == b"FMDL":
        out.append((r.string(r.u64(o + 8)), o))
        o += 0x78
    return out


def bounds(path: str, want: str = "", per_shape: bool = False):
    r = Reader(Path(path).read_bytes())
    found = models(r)
    name, model = next(((n, o) for n, o in found if not want or n == want), (None, None))
    if model is None:
        raise SystemExit(f"{path}: no model {want!r} (has {[n for n, _ in found]})")
    skeleton = r.u64(model + 0x18)
    if r.d[skeleton:skeleton + 4] != b"FSKL":
        raise SystemExit(f"{path}: no FSKL at the model's skeleton pointer")
    maya = r.u32(skeleton + 4) & 0x300 == 0x200  # Maya scaling: a parent's scale does not reach its children
    bones_at = r.u64(skeleton + 0x10)
    matrix_to_bone = r.u64(skeleton + 0x18)
    bone_count, smooth_count, rigid_count = r.u16(skeleton + 0x38), r.u16(skeleton + 0x3A), r.u16(skeleton + 0x3C)
    # Bones (BFRES 9: 0x60 each): name, user data, 16 bytes, index, parent, ..., flags +0x34, scale +0x38,
    # rotation +0x44, translation +0x54
    bones = []
    for b in range(bone_count):
        at = bones_at + b * 0x60
        if r.u16(at + 0x28) != b:
            raise SystemExit(f"{path}: bone {b} has index {r.u16(at + 0x28)}: unexpected bone layout")
        if r.u32(at + 0x34) & 0x7000 != 0x1000:
            raise SystemExit(f"{path}: bone {b} rotation mode {r.u32(at + 0x34) & 0x7000:#x}, only Euler XYZ is read")
        bones.append({"name": r.string(r.u64(at)), "parent": r.s16(at + 0x2A), "scale": r.f32(at + 0x38, 3),
                      "local": srt(r.f32(at + 0x38, 3), r.f32(at + 0x44, 4), r.f32(at + 0x54, 3))})
    world = []
    for b in bones:
        if b["parent"] < 0:
            world.append(b["local"])
            continue
        parent = world[b["parent"]]
        if maya:  # the child's own transform without the parent's scale: Wp * T * S(1/sp) * R * S
            ps = bones[b["parent"]]["scale"]
            undo = [[1 / ps[0], 0, 0, 0], [0, 1 / ps[1], 0, 0], [0, 0, 1 / ps[2], 0], [0, 0, 0, 1]]
            t = [[1, 0, 0, b["local"][0][3]], [0, 1, 0, b["local"][1][3]], [0, 0, 1, b["local"][2][3]], [0, 0, 0, 1]]
            rs = [row[:3] + [0] for row in b["local"][:3]] + [[0, 0, 0, 1]]
            world.append(mat_mul(parent, mat_mul(t, mat_mul(undo, rs))))
        else:
            world.append(mat_mul(parent, b["local"]))
    skin_bone = [r.u16(matrix_to_bone + 2 * i) for i in range(smooth_count + rigid_count)]

    shapes = r.u64(model + 0x28)
    buffer_block = r.u64(r.u64(0xB0) + 8)
    lo, hi, points = [math.inf] * 3, [-math.inf] * 3, []
    for s in range(r.u16(model + 0x6A)):
        shape = shapes + s * 0x60
        shape_name = r.string(r.u64(shape + 8))
        fvtx = r.u64(shape + 0x10)
        skin = r.u8(shape + 0x5A)
        shape_bone = r.u16(shape + 0x54)
        attrs = {}
        for a in range(r.u8(fvtx + 0x4C)):
            at = r.u64(fvtx + 8) + 16 * a
            attrs[r.string(r.u64(at))] = (r.u32(at + 8), r.u16(at + 12), r.u16(at + 14))
        sizes = r.u64(fvtx + 0x30)
        strides = r.u64(fvtx + 0x38)
        buffer_count = r.u8(fvtx + 0x4D)
        offsets, o = [], buffer_block + r.u32(fvtx + 0x48)
        for k in range(buffer_count):
            offsets.append(o)
            o += r.u32(sizes + 16 * k)
            o = (o + 7) & ~7
        fmt, off, buf = attrs["_p0"]
        stride = r.u32(strides + 16 * buf)
        ifmt = attrs.get("_i0")
        if skin == 1 and ifmt is None:
            raise SystemExit(f"{path}: shape {shape_name} is rigid-skinned without bone indices (_i0)")
        slo, shi = [math.inf] * 3, [-math.inf] * 3
        for v in range(r.u32(fvtx + 0x50)):
            p = read_attr(r, offsets[buf] + v * stride + off, fmt)[:3]
            if skin == 0:
                p = apply(world[shape_bone], p)
            elif skin == 1:
                ib = read_attr(r, offsets[ifmt[2]] + v * r.u32(strides + 16 * ifmt[2]) + ifmt[1], ifmt[0])[0]
                p = apply(world[skin_bone[ib]], p)
            points.append(p)
            for i in range(3):
                slo[i], shi[i] = min(slo[i], p[i]), max(shi[i], p[i])
                lo[i], hi[i] = min(lo[i], p[i]), max(hi[i], p[i])
        if per_shape:
            print(f"    shape {shape_name:<28} skin={skin} bone={bones[shape_bone]['name']:<16} "
                  f"min=({', '.join(f'{x:.3f}' for x in slo)}) max=({', '.join(f'{x:.3f}' for x in shi)})")
    center = tuple((lo[i] + hi[i]) / 2 for i in range(3))
    root_radius = max(math.dist(p, (0, 0, 0)) for p in points)
    radius = max(math.dist(p, center) for p in points)
    return {"model": name, "min": lo, "max": hi, "center": center, "root_radius": root_radius, "radius": radius}


def main() -> None:
    args = sys.argv[1:]
    per_shape = "--shapes" in args
    args = [a for a in args if a != "--shapes"]
    ref = None
    if "--ref" in args:
        i = args.index("--ref")
        ref = args[i + 1]
        del args[i:i + 2]
    def split(spec):
        """<file>[:<model>]; a drive letter's colon is part of the file."""
        head, sep, model = spec.rpartition(":")
        if not sep or "/" in model or "\\" in model or len(head) < 2:
            return spec, ""
        return head, model
    ref_bounds = None
    if ref:
        ref_bounds = bounds(*split(ref), per_shape)
        args = [ref] + args
    for spec in args:
        b = bounds(*split(spec), per_shape)
        print(f"{Path(split(spec)[0]).name}:{b['model']}  min=({', '.join(f'{x:.3f}' for x in b['min'])}) "
              f"max=({', '.join(f'{x:.3f}' for x in b['max'])})  centre=({', '.join(f'{x:.3f}' for x in b['center'])}) "
              f"radius root={b['root_radius']:.3f} centre={b['radius']:.3f}")
        if ref_bounds:
            scale = ref_bounds["radius"] / b["radius"]
            print(f"    fix: scale={scale:.3f} offset=({', '.join(f'{x:.3f}' for x in b['center'])})")


if __name__ == "__main__":
    main()
