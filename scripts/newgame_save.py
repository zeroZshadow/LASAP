"""Builds the new-game snapshot (TESTING.md, "New-game snapshot"), the one save every test starts from: the sandbox's
game save emptied, the game booted and file 2 created (tests/newgame_create.txt), then the sandbox's save folder
copied to the sandbox folder's save-newgame. Needs the sandbox lock (LASAP_SANDBOX_OWNER) and a mod installed.

python newgame_save.py
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import emu

TITLE = bytes.fromhex("00006f0cb06b0001")  # 01006bb00c6f0000 as a save's ExtraData holds it (little endian)
SAVES = Path(emu.SANDBOX) / "ryujinx" / "bis" / "user" / "save"
SNAPSHOT = Path(emu.SANDBOX) / "save-newgame"
HERE = Path(__file__).resolve().parent


def game_save() -> Path:
    """The game's save folder in the sandbox: the one whose ExtraData0 names its title."""
    for folder in sorted(SAVES.iterdir()):
        extra = folder / "ExtraData0"
        if extra.is_file() and extra.read_bytes()[:8] == TITLE:
            return folder
    sys.exit(f"no save of the game in {SAVES}: boot it once in the sandbox")


def main() -> None:
    if emu.INSTANCE != 0:
        sys.exit("works on the sandbox only (LASAP_INSTANCE unset)")
    emu.check()
    emu.stop(quiet=True)
    folder = game_save()
    # Ryujinx keeps a save's files in 0 (committed) and 1 (working copy): both emptied is a save with no files.
    for copy in ("0", "1"):
        shutil.rmtree(folder / copy, ignore_errors=True)
        (folder / copy).mkdir()
    shutil.rmtree(Path(emu.SANDBOX) / "ryujinx" / "sdcard" / "lasap", ignore_errors=True)
    emu.start()
    try:
        code = subprocess.run([sys.executable, str(HERE / "e2e.py"), "run", str(HERE / "tests" / "newgame_create.txt")],
                              env=dict(os.environ, PYTHONUNBUFFERED="1")).returncode
    finally:
        emu.stop()
    if code != 0:
        sys.exit("newgame_create failed: the snapshot is unchanged")
    files = sorted(p.name for p in (folder / "0").iterdir())
    if not any(name.startswith("savedata1") for name in files):
        sys.exit(f"no file 2 in the save ({', '.join(files) or 'empty'}): the snapshot is unchanged")
    shutil.rmtree(SNAPSHOT, ignore_errors=True)
    shutil.copytree(SAVES, SNAPSHOT)
    print(f"{SNAPSHOT}: file 2 only ({', '.join(files)})")


if __name__ == "__main__":
    main()
