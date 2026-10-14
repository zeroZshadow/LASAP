"""Sets up the multiplayer ghosts' model and Npc row (patcher/ap_player_ghost.py) in the sandbox mod, as the patcher
does, or takes them out again; for a mod deployed from an older patcher output.

install: writes romfs/region_common/actor/PlayerGhost.bfres from the asset patch (patcher/assets) and the RomFS
dump's Playerlink.bfres, and adds the row APGhost to the deployed Npc.gsheet (from the dump's when the mod has none),
keeping the sheet as Npc.gsheet.before-puppet.
uninstall: removes the model and puts the kept Npc.gsheet back.

Run with .venv-lasr:
  python scripts/ghost_puppet.py install|uninstall [--romfs D:/Games/Switch/LASRomFS] [--contents <mod contents>]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
import ap_assets  # noqa: E402
import ap_player_ghost  # noqa: E402
from RandomizerCore.Tools.oead_tools import readSheet, writeSheet  # noqa: E402

CONTENTS = Path("D:/DEV/LASAP-sandbox/ryujinx/sdcard/atmosphere/contents/01006BB00C6F0000")


def add_ghost_row(src: Path, dst: Path) -> None:
    sheet = readSheet(str(src))
    ap_player_ghost.add_row(sheet)
    writeSheet(str(dst), sheet)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("install", "uninstall"))
    parser.add_argument("--romfs", type=Path, default=Path("D:/Games/Switch/LASRomFS"))
    parser.add_argument("--contents", type=Path, default=CONTENTS)
    args = parser.parse_args()
    model_path = args.contents / "romfs" / "region_common" / "actor" / "PlayerGhost.bfres"
    sheet_path = args.contents / "romfs" / "region_common" / "datasheets" / "Npc.gsheet"
    kept = sheet_path.with_name("Npc.gsheet.before-puppet")
    if args.action == "install":
        model_path.parent.mkdir(parents=True, exist_ok=True)
        model_path.write_bytes(ap_assets.load(args.romfs, ap_assets.PLAYER_GHOST))
        if not kept.exists():  # empty: the mod had no Npc.gsheet of its own
            kept.parent.mkdir(parents=True, exist_ok=True)
            kept.write_bytes(sheet_path.read_bytes() if sheet_path.exists() else b"")
        source = kept if kept.stat().st_size else args.romfs / "region_common" / "datasheets" / "Npc.gsheet"
        add_ghost_row(source, sheet_path)
        print("installed", model_path.name, "and the APGhost row")
    else:
        model_path.unlink(missing_ok=True)
        if kept.exists():
            if kept.stat().st_size:
                sheet_path.write_bytes(kept.read_bytes())
            else:
                sheet_path.unlink(missing_ok=True)
            kept.unlink()
        print("uninstalled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
