"""Lists the actors of a level room with their positions, to find teleport targets for tests.

Run with .venv-lasr: python leb_actors.py <room, e.g. Field_15F> [name regex] [--near <x> <z> [tiles]]
[--romfs <RomFS folder>]

--near lists the actors within that many tiles (default 2) of a point, nearest first, with their offset in tiles: a
tile with a bush, rock or pot on it is solid, so a teleport target needs one without (TESTING.md, Finding a
location).
"""
import argparse
import math
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
from RandomizerCore.Tools.leb import Room  # noqa: E402
from local_paths import local_path  # noqa: E402

TILE = 1.5


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("room")
    parser.add_argument("pattern", nargs="?", default="")
    parser.add_argument("--near", nargs="+", type=float, metavar="X Z [TILES]")
    parser.add_argument("--romfs")
    args = parser.parse_args()

    area = args.room.split("_")[0]
    path = Path(args.romfs or local_path("romfs", "LASAP_ROMFS")) / "region_common" / "level" / area / f"{args.room}.leb"
    if not path.exists():  # rooms the mod does not change are only in the base RomFS
        path = local_path("romfs", "LASAP_ROMFS") / "region_common" / "level" / area / f"{args.room}.leb"
    room = Room(path.read_bytes())
    rx = re.compile(args.pattern, re.I)
    actors = [a for a in room.actors if rx.search(a.name.decode("utf-8", "replace"))]
    if args.near:
        x, z = args.near[:2]
        reach = (args.near[2] if len(args.near) > 2 else 2) * TILE
        actors = [a for a in actors if math.hypot(a.posX - x, a.posZ - z) <= reach + 0.01]
        actors.sort(key=lambda a: math.hypot(a.posX - x, a.posZ - z))
    for actor in actors:
        name = actor.name.decode("utf-8", "replace")
        params = [p.decode("utf-8", "replace") if isinstance(p, bytes) else p for p in actor.parameters]
        params = [p for p in params if p not in ("", 0)]
        offset = f"  ({(actor.posX - x) / TILE:+.1f}, {(actor.posZ - z) / TILE:+.1f})" if args.near else ""
        print(f"{name:48} {actor.posX:8.2f} {actor.posY:6.2f} {actor.posZ:8.2f}{offset}  {params}")


if __name__ == "__main__":
    main()
