"""Sets up the multiplayer ghosts' model and rows (patcher/ap_player_ghost.py) in the sandbox mod, as the patcher
does, or takes them out again; for a mod deployed from an older patcher output.

install: writes romfs/region_common/actor/PlayerGhost.bfres from the asset patch (patcher/assets) and the RomFS
dump's Playerlink.bfres, and adds the rows APGhost to the deployed Npc.gsheet and Facial.gsheet (from the dump's when
the mod has none), keeping each sheet as <sheet>.before-puppet.
uninstall: removes the model and puts the kept sheets back.

Run with .venv-lasr:
  python scripts/ghost_puppet.py install|uninstall [--romfs <RomFS folder>] [--contents <mod contents>]
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "patcher"))
import ap_assets  # noqa: E402
import ap_player_ghost  # noqa: E402
from RandomizerCore.Tools.oead_tools import readSheet, writeSheet  # noqa: E402
from local_paths import local_path  # noqa: E402

CONTENTS = Path("ryujinx/sdcard/atmosphere/contents/01006BB00C6F0000")  # in the sandbox folder


# Each sheet and how its APGhost row is added
SHEETS = {"Npc.gsheet": ap_player_ghost.add_row, "Facial.gsheet": ap_player_ghost.add_facial_row}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("install", "uninstall"))
    parser.add_argument("--romfs", type=Path)
    parser.add_argument("--contents", type=Path)
    args = parser.parse_args()
    args.romfs = args.romfs or local_path("romfs", "LASAP_ROMFS")
    args.contents = args.contents or local_path("sandbox") / CONTENTS
    model_path = args.contents / "romfs" / "region_common" / "actor" / "PlayerGhost.bfres"
    sheets = args.contents / "romfs" / "region_common" / "datasheets"
    if args.action == "install":
        model_path.parent.mkdir(parents=True, exist_ok=True)
        model_path.write_bytes(ap_assets.load(args.romfs, ap_assets.PLAYER_GHOST))
    else:
        model_path.unlink(missing_ok=True)
    for name, add_row in SHEETS.items():
        sheet_path = sheets / name
        kept = sheet_path.with_name(name + ".before-puppet")
        if args.action == "install":
            if not kept.exists():  # empty: the mod had no sheet of its own
                kept.parent.mkdir(parents=True, exist_ok=True)
                kept.write_bytes(sheet_path.read_bytes() if sheet_path.exists() else b"")
            sheet = readSheet(str(kept if kept.stat().st_size else args.romfs / "region_common" / "datasheets" / name))
            add_row(sheet)
            writeSheet(str(sheet_path), sheet)
        elif kept.exists():
            if kept.stat().st_size:
                sheet_path.write_bytes(kept.read_bytes())
            else:
                sheet_path.unlink(missing_ok=True)
            kept.unlink()
    print(f"installed {model_path.name} and the APGhost rows" if args.action == "install" else "uninstalled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
