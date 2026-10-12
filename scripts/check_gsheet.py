"""Checks the pure-Python datasheet module (patcher/RandomizerCore/Tools/gsheet.py) against oead: both must parse the
same values and write the same bytes, for every datasheet of the RomFS and any extra .gsheet files given.

Run with .venv-lasr (patcher/requirements-assets.txt has oead):
  python scripts/check_gsheet.py [--romfs D:/Games/Switch/LASRomFS] [file.gsheet ...]
"""
import argparse
import sys
from pathlib import Path

import oead

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
from RandomizerCore.Tools import gsheet  # noqa: E402


def plain(value):
    if isinstance(value, (oead.gsheet.Struct, dict)):
        return {k: plain(v) for k, v in value.items()}
    if isinstance(value, (str, bool, int, float)) or value is None:
        return value
    return [plain(v) for v in value]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--romfs", type=Path, default=Path("D:/Games/Switch/LASRomFS"))
    args = parser.parse_args()
    files = sorted((args.romfs / "region_common" / "datasheets").glob("*.gsheet")) + args.files
    failed = 0
    for path in files:
        data = path.read_bytes()
        theirs = oead.gsheet.parse(oead.Bytes(data))
        ours = gsheet.parse(data)
        values = plain(list(theirs.values)) == plain(ours.values)
        binary = bytes(theirs.to_binary()) == ours.to_binary()
        if not (values and binary):
            failed += 1
            print(f"{path.name}: values {'same' if values else 'DIFFER'}, binary {'same' if binary else 'DIFFERS'}")
    print(f"{len(files) - failed}/{len(files)} datasheets match oead")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
