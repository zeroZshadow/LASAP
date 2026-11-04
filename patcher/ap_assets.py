"""The mod's RomFS, the same for every seed (DESIGN.md, Assets): every file the mod changes or adds, built once from a
RomFS dump by scripts/build_assets.py (LASR's mod generator and the patcher's edits, patcher/modbuild) and stored as a
binary patch (bsdiff4) against the RomFS file it comes from, or against nothing for a file of its own. The patcher only
applies these patches to the player's dump.

assets/assets.json maps each file's RomFS path to its source file and both files' SHA-256; its patch is
assets/files/<path>.bsdiff4. assets/build.json holds what the seed's files take from the build (`data`): LASR's tables,
the mod's Items.gsheet rows, the map tracker's places, the messages Randomize Text shuffles, and the shape of the
patch the mod is built for (las_ap_patch.patch_shape)."""
import hashlib
import json
from pathlib import Path
from typing import Dict, Optional

import bsdiff4

ASSET_DIR = Path(__file__).resolve().parent / "assets"
MANIFEST = ASSET_DIR / "assets.json"
DATA = ASSET_DIR / "build.json"
PATCHES = ASSET_DIR / "files"
UI = "region_common/ui/StartUp.arc"
GAME = "region_common/ui/Game.arc"  # ap_map_icons.ARCHIVE
ACTOR = "region_common/actor"
MODEL = f"{ACTOR}/ItemArchipelago.bfres"  # ap_model.FILE_NAME
PLAYER_GHOST = f"{ACTOR}/PlayerGhost.bfres"  # ap_player_ghost.FILE_NAME
CHEST = f"{ACTOR}/ObjTreasureBox.bfres"
LASR_CHESTS = tuple(f"{ACTOR}/ObjTreasureBox{kind}.bfres" for kind in ("Junk", "Key", "LifeUpgrade"))
SEASHELL_CHEST = f"{ACTOR}/ObjTreasureBoxSeashell.bfres"  # ap_chest.FILE_NAME
CHEST_TEXTURES = LASR_CHESTS + (SEASHELL_CHEST,)
BALLAD_LOOP = "region_common/audio/stream/BGM_ENSEMBLE_LOOP.bfstm"  # ap_ballad.LOOPED
# The files the mod adds that come from another RomFS file; the others it adds come from nothing
SOURCES = {MODEL: f"{ACTOR}/ItemShellRader.bfres", PLAYER_GHOST: f"{ACTOR}/Playerlink.bfres",
           **{path: CHEST for path in CHEST_TEXTURES},
           BALLAD_LOOP: "region_common/audio/stream/BGM_ENSEMBLE_INST.bfstm"}

_cache: Dict[Path, dict] = {}


class AssetError(Exception):
    pass


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _json(path: Path) -> dict:
    if path not in _cache:
        _cache[path] = json.loads(path.read_text(encoding="utf-8"))
    return _cache[path]


def manifest() -> dict:
    return _json(MANIFEST)


def data() -> dict:
    return _json(DATA)


def patch_path(path: str) -> Path:
    return PATCHES / f"{path}.bsdiff4"


def write(romfs: Path, built: Dict[str, bytes], build_data: dict) -> None:
    """Stores the mod's files as patches against the RomFS files they come from, and the build's data."""
    for old in PATCHES.rglob("*.bsdiff4"):
        old.unlink()
    files = {}
    for path, content in sorted(built.items()):
        source = SOURCES.get(path, path)
        old = (romfs / source).read_bytes() if (romfs / source).is_file() else None
        target = patch_path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(bsdiff4.diff(old or b"", content))
        files[path] = {"source": source if old is not None else None,
                       "source_sha256": _sha256(old) if old is not None else None, "sha256": _sha256(content)}
    MANIFEST.write_text(json.dumps(files, indent=1) + "\n", encoding="utf-8")
    DATA.write_text(json.dumps(build_data, indent=1) + "\n", encoding="utf-8")
    _cache.clear()


def _source(romfs: Path, entry: Dict[str, Optional[str]]) -> bytes:
    """The RomFS file an asset comes from; AssetError unless it is the file of game version 1.0.0."""
    if entry["source"] is None:
        return b""
    try:
        source = (romfs / entry["source"]).read_bytes()
    except OSError:
        source = b""
    if _sha256(source) != entry["source_sha256"]:
        raise AssetError(f"The RomFS dump is not of game version 1.0.0 ({entry['source']} differs). Turn the "
                         f"game's update off in the emulator and dump the RomFS again (see the setup guide).")
    return source


def load(romfs: Path, path: str) -> bytes:
    """One asset, patched from the player's RomFS dump; checked against the build's checksums."""
    entry = manifest()[path]
    content = bsdiff4.patch(_source(romfs, entry), patch_path(path).read_bytes())
    if _sha256(content) != entry["sha256"]:
        raise AssetError(f"Patching {path} gave a different file than the release build.")
    return content


def install(romfs: Path, out_romfs: Path, path: str) -> None:
    target = out_romfs / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(load(romfs, path))


def install_all(romfs: Path, out_romfs: Path) -> None:
    """The mod's RomFS: every asset."""
    for path in manifest():
        install(romfs, out_romfs, path)
