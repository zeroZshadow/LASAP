"""Builds patcher/assets: the mod's RomFS, the same for every seed, as patches against the RomFS, and what the seed's
files take from the build (patcher/ap_assets.py). The mod is built from a patch with the locations the world gives
every seed (make_test_patch.make) by LASR's mod generator with the patcher's edits (patcher/modbuild/mod.py), the
files only this build makes (the title screen and notice look, the Archipelago model and the map icons) and the
files the patcher makes itself (ap_assets.MADE: the ghosts' model and the chest textures). Run with .venv-lasr after
changing what the mod's files come from, and commit the result:
  python scripts/build_assets.py [--romfs <RomFS folder>] [--check]
--check builds and compares with the committed assets instead of writing them.
"""
import argparse
import json
import sys
import tempfile
from pathlib import Path
from typing import Dict

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher" / "modbuild"))
import mod  # noqa: E402  (LASR and the patcher on the path)
import ap_assets  # noqa: E402
import las_ap_patch  # noqa: E402
import make_test_patch  # noqa: E402
from local_paths import local_path  # noqa: E402


def own_files(romfs: Path) -> Dict[str, bytes]:
    """The files of the mod only this build makes (they need oead's SARC, quicktex and Pillow), and those the patcher
    makes itself."""
    import ap_map_icons
    import ap_model
    import ap_ui
    from RandomizerCore.Tools.bntx_tools import createRandomizerTitleScreenArchive
    from RandomizerCore.Tools.oead_tools import SARC

    built = {}
    sarc = SARC(str(romfs / ap_assets.UI))
    createRandomizerTitleScreenArchive(sarc)  # LASR's title screen logo
    built[ap_assets.UI] = ap_ui.build(bytes(sarc.repack()))
    built[ap_assets.MODEL] = ap_model.build(str(romfs / ap_assets.SOURCES[ap_assets.MODEL]))
    for path, make in ap_assets.MADE.items():  # the patcher makes these itself, the same way
        built[path] = make((romfs / ap_assets.SOURCES[path]).read_bytes())
    built[ap_assets.GAME] = ap_map_icons.build((romfs / ap_assets.GAME).read_bytes())
    return built


def build(romfs: Path) -> tuple:
    """The mod's files that differ from the RomFS's, by RomFS path, and the build's data (ap_assets.data)."""
    patch = make_test_patch.make()
    with tempfile.TemporaryDirectory() as tmp:
        data = mod.build(romfs, Path(tmp), patch)
        out = Path(tmp) / las_ap_patch.GAME_DIR / "romfs"
        built = {path.relative_to(out).as_posix(): path.read_bytes() for path in out.rglob("*") if path.is_file()}
    built.update(own_files(romfs))
    data["shape"] = las_ap_patch.patch_shape(patch)
    # LASR writes back some files it reads unchanged; the game reads those from its own RomFS
    return {path: content for path, content in built.items()
            if not (romfs / path).is_file() or (romfs / path).read_bytes() != content}, json.loads(json.dumps(data))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    args.romfs = args.romfs or local_path("romfs")
    built, data = build(args.romfs)
    if args.check:
        files = ap_assets.manifest()
        differ = sorted(path for path in set(built) | set(files)
                        if path not in built or path not in files
                        or ap_assets._sha256(built[path]) != files[path]["sha256"])
        for path in differ:
            print(f"differs: {path}")
        if data != ap_assets.data():
            print(f"differs: {ap_assets.DATA.name}")
            differ.append(ap_assets.DATA.name)
        print(f"{len(built)} files, {len(differ)} differ from the committed assets")
        return 1 if differ else 0
    ap_assets.write(args.romfs, built, data)
    size = sum(ap_assets.patch_path(path).stat().st_size for path in built if path not in ap_assets.MADE)
    print(f"{len(built)} files, {size} bytes of patches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
