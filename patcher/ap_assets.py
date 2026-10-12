"""Files the mod changes the same way for every seed: the title screen and the notice look (StartUp.arc), the
Archipelago item model, and LASR's chest textures. Building them takes native packages (oead's SARC, quicktex,
Pillow), so scripts/build_assets.py builds them once from a RomFS dump and stores each as a binary patch (bsdiff4)
against the RomFS file it comes from. The patcher only applies these patches to the player's dump.

assets/assets.json maps each file's RomFS path to its patch, its source file and both files' SHA-256."""
import hashlib
import json
import tempfile
from pathlib import Path
from typing import Dict

import bsdiff4

ASSET_DIR = Path(__file__).resolve().parent / "assets"
MANIFEST = ASSET_DIR / "assets.json"
UI = "region_common/ui/StartUp.arc"
ACTOR = "region_common/actor"
MODEL = f"{ACTOR}/ItemArchipelago.bfres"  # ap_model.FILE_NAME
CHEST = f"{ACTOR}/ObjTreasureBox.bfres"
CHEST_TEXTURES = tuple(f"{ACTOR}/ObjTreasureBox{kind}.bfres" for kind in ("Junk", "Key", "LifeUpgrade"))
SOURCES = {UI: UI, MODEL: f"{ACTOR}/ItemShellRader.bfres", **{path: CHEST for path in CHEST_TEXTURES}}


class AssetError(Exception):
    pass


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build(romfs: Path) -> Dict[str, bytes]:
    """Every asset, built from the RomFS dump with LASR's and the patcher's own code (needs the native packages)."""
    import ap_model
    import ap_ui
    from RandomizerCore.Randomizers.chests import CHEST_TEXTURES as LASR_CHEST_TEXTURES
    from RandomizerCore.Tools.bntx_tools import (createChestBfresWithCustomTexturesIfMissing,
                                                 createRandomizerTitleScreenArchive)
    from RandomizerCore.Tools.oead_tools import SARC

    built = {}
    sarc = SARC(str(romfs / UI))
    createRandomizerTitleScreenArchive(sarc)  # LASR's title screen logo
    built[UI] = ap_ui.build(bytes(sarc.repack()))
    built[MODEL] = ap_model.build(str(romfs / SOURCES[MODEL]))
    with tempfile.TemporaryDirectory() as tmp:
        createChestBfresWithCustomTexturesIfMissing(str(romfs / CHEST), tmp, LASR_CHEST_TEXTURES)
        for path in CHEST_TEXTURES:
            built[path] = (Path(tmp) / Path(path).name).read_bytes()
    return built


def write(romfs: Path, built: Dict[str, bytes]) -> None:
    """Stores the assets as patches against the RomFS files they come from."""
    ASSET_DIR.mkdir(exist_ok=True)
    for old in ASSET_DIR.glob("*.bsdiff4"):
        old.unlink()
    manifest = {}
    for path, data in sorted(built.items()):
        source = (romfs / SOURCES[path]).read_bytes()
        patch = Path(path).name + ".bsdiff4"
        (ASSET_DIR / patch).write_bytes(bsdiff4.diff(source, data))
        manifest[path] = {"source": SOURCES[path], "source_sha256": _sha256(source), "patch": patch,
                          "sha256": _sha256(data)}
    MANIFEST.write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")


def load(romfs: Path, path: str) -> bytes:
    """One asset, patched from the player's RomFS dump; checked against the build's checksums."""
    entry = json.loads(MANIFEST.read_text(encoding="utf-8"))[path]
    source = (romfs / entry["source"]).read_bytes()
    if _sha256(source) != entry["source_sha256"]:
        raise AssetError(f"{entry['source']} in the RomFS dump is not the file of game version 1.0.0.")
    data = bsdiff4.patch(source, (ASSET_DIR / entry["patch"]).read_bytes())
    if _sha256(data) != entry["sha256"]:
        raise AssetError(f"Patching {path} gave a different file than the release build.")
    return data


def install(romfs: Path, out_romfs: Path, path: str) -> None:
    target = out_romfs / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(load(romfs, path))
