"""Collision meshes of a BFRES model's embedded .cidb (scripts/re/CRANE.md, cidb): the triangles Link walks on and
against.

python cidb.py <file.bfres> ... prints each mesh entry's collision kind and its vertex and triangle counts.
"""
import struct
import sys

# Collision kinds whose filter word and Link's controller capsule collide both ways (CRANE.md, cidb)
LINK_KINDS = {0, 1, 2, 3, 4, 5, 7, 8, 0xa, 0xb, 0x10, 0x11}


def meshes(data: bytes):
    """[{kind, verts, tris}]: the triangle-mesh entries of the .cidb in a file's bytes, in model space."""
    at = data.find(b"CIDB") - 4
    if at < 0:
        return []
    c = data[at:]
    u32 = lambda o: struct.unpack_from("<I", c, o)[0]  # noqa: E731
    out = []
    for i in range(u32(0x10)):
        o = u32(0x14 + 4 * i) + 8
        m = o + 0x28
        if c[m:m + 8] != b"NXS\x01MESH":
            continue
        flags, nv, nt = struct.unpack_from("<3I", c, m + 0x10)
        p = m + 0x1c
        verts = [struct.unpack_from("<3f", c, p + 12 * k) for k in range(nv)]
        p += 12 * nv
        width = 1 if flags & 4 else 2 if flags & 8 else 4
        fmt = "<3" + {1: "B", 2: "H", 4: "I"}[width]
        tris = [struct.unpack_from(fmt, c, p + 3 * width * k) for k in range(nt)]
        out.append({"kind": u32(o + 8) >> 16, "verts": verts, "tris": tris})
    return out


def link_triangles(data: bytes, origin=(0.0, 0.0, 0.0)):
    """The triangles of the meshes Link collides with, moved to origin."""
    ox, oy, oz = origin
    out = []
    for mesh in meshes(data):
        if mesh["kind"] in LINK_KINDS:
            v = [(x + ox, y + oy, z + oz) for x, y, z in mesh["verts"]]
            out += [(v[a], v[b], v[c]) for a, b, c in mesh["tris"]]
    return out


if __name__ == "__main__":
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            for mesh in meshes(f.read()):
                print(f"{path}: kind {mesh['kind']:#x}, {len(mesh['verts'])} vertices, {len(mesh['tris'])} triangles")
