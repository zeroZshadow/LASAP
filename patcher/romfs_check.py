"""Whether a RomFS dump is one the patcher can patch from: game version 1.0.0, without the update, with every file the
mod's assets come from. Standard library only: the client runs it from the world's copy of the patcher to check the dump
before patching (the world's install.py)."""
import hashlib
from pathlib import Path
from typing import Dict, List, Optional, Tuple

_hashes: Dict[Tuple[Path, int, int], str] = {}  # (file, size, mtime) -> sha256, so a recheck reads nothing again


def sources(manifest: dict) -> Dict[str, str]:
    """The 1.0.0 files the assets come from, with their sha256, from assets/assets.json."""
    return {entry["source"]: entry["source_sha256"] for entry in manifest.values() if entry["source"] is not None}


def missing(romfs: Path, versioned: Dict[str, str]) -> List[str]:
    """The source files the dump lacks, as an update's RomFS alone or a copy that did not finish does."""
    return [source for source in sorted(versioned) if not (romfs / source).is_file()]


def _sha256(path: Path) -> str:
    stat = path.stat()
    key = (path.resolve(), stat.st_size, stat.st_mtime_ns)
    if key not in _hashes:
        digest = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                digest.update(block)
        _hashes[key] = digest.hexdigest()
    return _hashes[key]


def problem(romfs: Path, versioned: Dict[str, str]) -> Optional[str]:
    """What keeps the patcher from patching from the dump at `romfs`, as one message; None when nothing does.
    `versioned`: files with their 1.0.0 sha256 (sources)."""
    lacks = missing(romfs, versioned)
    if lacks:
        more = f" and {len(lacks) - 1} more" if len(lacks) > 1 else ""
        return (f"The RomFS dump is incomplete: it lacks {lacks[0]}{more}. It may be the RomFS of the game's update "
                f"alone, or a copy that did not finish. Dump the RomFS again with the game's update turned off (see "
                f"the setup guide).")
    for source, sha256 in sorted(versioned.items()):
        if _sha256(romfs / source) != sha256:
            return (f"The RomFS dump is not of game version 1.0.0 ({source} differs). Turn the game's update off in "
                    f"the emulator and dump the RomFS again (see the setup guide).")
    return None
