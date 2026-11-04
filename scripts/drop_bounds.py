"""Writes the drop actor's model bounds into the module (las-ap-exl/source/program/ap/drop_bounds.hpp; DESIGN.md, Drop
actor, Model): per model a drop can show, every items.yml item's and the Archipelago model, its bind-pose centre and
radius (model_bounds.py), the same in every seed. Run in .venv-lasr after a change of those models:

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
import ap_model  # noqa: E402
import model_bounds  # noqa: E402
from las_ap_patch import load_yaml  # noqa: E402
from local_paths import local_path  # noqa: E402

OUT = ROOT / "las-ap-exl" / "source" / "program" / "ap" / "drop_bounds.hpp"
HEADER = """#pragma once
// Written by scripts/drop_bounds.py: per model a drop can show (model file, model name), its bind-pose centre and
// radius in model units (DESIGN.md, Drop actor, Model).
#include <string_view>

namespace AP::DropBounds {
    struct Bounds {
        std::string_view file, name;
        float x, y, z, radius;
    };
    constexpr Bounds kBounds[] = {
"""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--romfs", type=Path, default=None)
    romfs = parser.parse_args().romfs or local_path("romfs")
    actors = romfs / ap_assets.ACTOR
    models = sorted({(item["model-path"], item["model-name"]) for item in load_yaml("items.yml")["Item_Pool"].values()
                     if item.get("model-path") and item.get("model-name") and not item["item-key"].startswith("$")})
    lines = []
    with tempfile.TemporaryDirectory() as built:
        ap_assets.install(romfs, Path(built), ap_assets.MODEL)
        for path, name in [(ap_model.FILE_NAME, ap_model.MODEL_NAME), *models]:
            file = Path(built) / ap_assets.MODEL if path == ap_model.FILE_NAME else actors / path
            try:
                b = model_bounds.bounds(str(file), name)
            except (SystemExit, Exception) as e:  # the reader stops on layouts it does not know
                print(f"{path}:{name}: not measured ({e})")
                continue
            x, y, z = b["center"]
            lines.append(f'        {{"{path}", "{name}", {x:.4f}f, {y:.4f}f, {z:.4f}f, {b["radius"]:.4f}f}},\n')
    OUT.write_text(HEADER + "".join(lines) + "    };\n}\n", encoding="utf-8", newline="\n")
    print(f"{OUT}: {len(lines)} models")


if __name__ == "__main__":
    main()
