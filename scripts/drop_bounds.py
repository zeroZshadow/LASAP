"""Writes the drop actor's model bounds into the module (las-ap-exl/source/program/ap/drop_bounds.hpp; DESIGN.md, Drop
actor, Model): per model a drop can show, every items.yml item's, the Archipelago model and the warp medallion, its
bind-pose centre and radius (model_bounds.py); and per item id of Items.gsheet, its vanilla model (its npcKey row's)
and how high that row's collision holds the actor's root above the floor (ap_drop.floor_height). The same in every
seed. Run in .venv-lasr after a change of those models or sheets:

python scripts/drop_bounds.py [--romfs <RomFS folder>]
"""
import argparse
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher"))
sys.path.insert(0, str(ROOT / "patcher" / "modbuild"))
import ap_assets  # noqa: E402
from ap_drop import floor_height  # noqa: E402
import ap_model  # noqa: E402
import ap_warp_model  # noqa: E402
import model_bounds  # noqa: E402
from las_ap_patch import load_yaml  # noqa: E402
from local_paths import local_path  # noqa: E402
from RandomizerCore.Tools import oead_tools  # noqa: E402

OUT = ROOT / "las-ap-exl" / "source" / "program" / "ap" / "drop_bounds.hpp"
HEADER = """#pragma once
// Written by scripts/drop_bounds.py: per model a drop can show (model file, model name), its bind-pose centre and
// radius in model units; per item id, its vanilla model and how high its collision holds its root above the floor
// (DESIGN.md, Drop actor, Model).
#include <string_view>

namespace AP::DropBounds {
    struct Bounds {
        std::string_view file, name;
        float x, y, z, radius;
    };
    struct Item {
        int id;
        std::string_view file, name;
        float floor;
    };
    constexpr Bounds kBounds[] = {
"""


def item_lines(romfs: Path) -> list:
    """kItems: per Items.gsheet row whose npcKey row has a model, the item id, that model and its floor height."""
    sheets = romfs / "region_common" / "datasheets"
    rows = {row["symbol"]: row for row in map(oead_tools.parseStruct,
                                              oead_tools.readSheet(str(sheets / "Npc.gsheet"))["values"])}
    lines = []
    for item in map(oead_tools.parseStruct, oead_tools.readSheet(str(sheets / "Items.gsheet"))["values"]):
        row = rows.get(item["npcKey"])
        if row is None or not row["graphics"]["path"] or not row["graphics"]["model"]:
            continue
        graphics = row["graphics"]
        lines.append(f'        {{{item["itemID"]}, "{graphics["path"]}", "{graphics["model"]}", '
                     f'{floor_height(row["collision"]):.4f}f}},\n')
    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", type=Path, default=None)
    romfs = parser.parse_args().romfs or local_path("romfs")
    actors = romfs / ap_assets.ACTOR
    models = sorted({(item["model-path"], item["model-name"]) for item in load_yaml("items.yml")["Item_Pool"].values()
                     if item.get("model-path") and item.get("model-name") and not item["item-key"].startswith("$")})
    lines = []
    with tempfile.TemporaryDirectory() as built:
        own = {ap_model.FILE_NAME: (ap_assets.MODEL, ap_model.MODEL_NAME),
               ap_warp_model.FILE_NAME: (ap_assets.WARP_MODEL, ap_warp_model.MODEL_NAME)}
        for asset, _ in own.values():
            ap_assets.install(romfs, Path(built), asset)
        for path, name in [(file, name) for file, (_, name) in own.items()] + models:
            file = Path(built) / own[path][0] if path in own else actors / path
            try:
                b = model_bounds.bounds(str(file), name)
            except (SystemExit, Exception) as e:  # the reader stops on layouts it does not know
                print(f"{path}:{name}: not measured ({e})")
                continue
            x, y, z = b["center"]
            lines.append(f'        {{"{path}", "{name}", {x:.4f}f, {y:.4f}f, {z:.4f}f, {b["radius"]:.4f}f}},\n')
    items = item_lines(romfs)
    OUT.write_text(HEADER + "".join(lines) + "    };\n    constexpr Item kItems[] = {\n" + "".join(items) + "    };\n}\n",
                   encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(lines)} models, {len(items)} items")


if __name__ == "__main__":
    main()
