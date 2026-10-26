import importlib.util
import os
from pathlib import Path

from test.bases import WorldTestBase

LOCAL_PATHS = Path(os.path.realpath(__file__)).parents[3] / "scripts" / "local_paths.py"


class LASTestBase(WorldTestBase):
    game = "Links Awakening Switch"


def romfs() -> Path:
    """The RomFS dump the tests read: LASAP_ROMFS, else "romfs" in scripts/local_paths.json. Not configured, a folder
    that does not exist, so the tests that need the dump skip."""
    value = ""
    if LOCAL_PATHS.exists():
        spec = importlib.util.spec_from_file_location("local_paths", LOCAL_PATHS)
        local_paths = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(local_paths)
        value = local_paths.lookup("romfs", "LASAP_ROMFS")
    return Path(value) if value else LOCAL_PATHS.with_name("<no RomFS configured>")
