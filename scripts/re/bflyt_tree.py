"""Prints a layout's pane tree from a RomFS dump: kind, name, translate, scale, size, and for parts the layout they use.

Run with .venv-lasr (needs oead):
  python bflyt_tree.py <layout name, e.g. Dg_DgnMap> [--arc Game.arc] [--romfs <RomFS folder>]
  python bflyt_tree.py --list [--arc Game.arc]     layouts and animations in the archive
"""
import argparse
import struct
import sys
from pathlib import Path

import oead

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from local_paths import local_path  # noqa: E402

PANES = {b"pan1", b"pic1", b"txt1", b"wnd1", b"bnd1", b"prt1", b"ali1", b"scr1"}


def tree(data: bytes):
    """Yields (depth, kind, name, translate, scale, size, part layout) in file order."""
    off = struct.unpack_from("<H", data, 6)[0]
    count = struct.unpack_from("<H", data, 0x10)[0]
    depth = 0
    for _ in range(count):
        magic, size = data[off:off + 4], struct.unpack_from("<I", data, off + 4)[0]
        if magic in (b"pas1", b"grs1"):
            depth += 1
        elif magic in (b"pae1", b"gre1"):
            depth -= 1
        elif magic in PANES:
            name = data[off + 0xc:off + 0x24].split(b"\0")[0].decode()
            tx, ty, _tz, _rx, _ry, _rz, sx, sy, w, h = struct.unpack_from("<10f", data, off + 0x2c)
            part = ""
            if magic == b"prt1":
                # after the pane header (0x54): property count u32, magnify x/y f32, properties (0x28 each), name
                props = struct.unpack_from("<I", data, off + 0x54)[0]
                at = off + 0x60 + 0x28 * props
                part = data[at:data.index(b"\0", at)].decode(errors="replace")
            yield depth, magic.decode(), name, (tx, ty), (sx, sy), (w, h), part
        off += size


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("layout", nargs="?")
    parser.add_argument("--arc", default="Game.arc")
    parser.add_argument("--romfs", type=Path)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    args.romfs = args.romfs or local_path("romfs", "LASAP_ROMFS")
    sarc = oead.Sarc((args.romfs / "region_common" / "ui" / args.arc).read_bytes())
    if args.list or not args.layout:
        for f in sorted(f.name for f in sarc.get_files() if f.name.endswith((".bflyt", ".bflan"))):
            print(f)
        return
    data = bytes(sarc.get_file(f"blyt/{args.layout}.bflyt").data)
    for depth, kind, name, t, s, size, part in tree(data):
        scale = "" if s == (1.0, 1.0) else f" s=({s[0]:g},{s[1]:g})"
        print(f"{'  ' * depth}{kind} {name} t=({t[0]:g},{t[1]:g}){scale} size=({size[0]:g},{size[1]:g})"
              + (f" -> {part}" if part else ""))


if __name__ == "__main__":
    main()
