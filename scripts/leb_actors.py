"""Lists the actors of a level room with their positions, to find teleport targets for tests.

Run with .venv-lasr: python leb_actors.py <room, e.g. Field_15F> [name regex] [--romfs D:/Games/Switch/LASRomFS]
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
from RandomizerCore.Tools.leb import Room  # noqa: E402

DEFAULT_ROMFS = "D:/Games/Switch/LASRomFS"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("room")
    parser.add_argument("pattern", nargs="?", default="")
    parser.add_argument("--romfs", default=DEFAULT_ROMFS)
    args = parser.parse_args()

    area = args.room.split("_")[0]
    path = Path(args.romfs) / "region_common" / "level" / area / f"{args.room}.leb"
    if not path.exists():  # rooms the mod does not change are only in the base RomFS
        path = Path(DEFAULT_ROMFS) / "region_common" / "level" / area / f"{args.room}.leb"
    room = Room(path.read_bytes())
    rx = re.compile(args.pattern, re.I)
    for actor in room.actors:
        name = actor.name.decode("utf-8", "replace")
        if rx.search(name):
            params = [p.decode("utf-8", "replace") if isinstance(p, bytes) else p for p in actor.parameters]
            params = [p for p in params if p not in ("", 0)]
            print(f"{name:48} {actor.posX:8.2f} {actor.posY:6.2f} {actor.posZ:8.2f}  {params}")


if __name__ == "__main__":
    main()
