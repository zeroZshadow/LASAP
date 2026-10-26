"""Builds patcher/assets: the files the mod changes the same way for every seed, as patches against the RomFS
(patcher/ap_assets.py). Run with .venv-lasr after changing ap_ui.py, ap_model.py, ap_chest.py or LASR's title screen
or chest textures, and commit the result:
  python scripts/build_assets.py [--romfs <RomFS folder>]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
import las_ap_patch  # noqa: E402,F401  (LASR's paths and the Qt stand-in)
import ap_assets  # noqa: E402
from local_paths import local_path  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", type=Path)
    args = parser.parse_args()
    args.romfs = args.romfs or local_path("romfs", "LASAP_ROMFS")
    built = ap_assets.build(args.romfs)
    ap_assets.write(args.romfs, built)
    for path in sorted(built):
        patch = ap_assets.ASSET_DIR / (Path(path).name + ".bsdiff4")
        print(f"{path}: {len(built[path])} bytes, patch {patch.stat().st_size} bytes")


if __name__ == "__main__":
    main()
