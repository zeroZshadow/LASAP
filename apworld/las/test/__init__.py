import importlib.util
import os
import sys
from pathlib import Path

from test.bases import WorldTestBase

ROOT = Path(os.path.realpath(__file__)).parents[3]
LOCAL_PATHS = ROOT / "scripts" / "local_paths.py"
PATCHER = ROOT / "patcher"


class LASTestBase(WorldTestBase):
    game = "Links Awakening Switch"


def patcher_import(*names):
    """Patcher modules by name, without leaving the patcher on sys.path."""
    saved = list(sys.path)
    sys.path.insert(0, str(PATCHER))
    try:
        return [__import__(name, fromlist=["_"]) for name in names]
    finally:
        sys.path[:] = saved


def forget(*prefixes) -> None:
    for name in list(sys.modules):
        if name.split(".")[0] in prefixes:
            del sys.modules[name]


def client_context(game: bool = False, **attrs):
    """A LASContext without connections, with `attrs` set: (it, the messages it sends the server, the lines it writes
    to the game, with `game` a game connected)."""
    from ..client import LASContext
    ctx = LASContext(None, None, "127.0.0.1", 0)
    sent, lines = [], []

    async def send_msgs(msgs):
        sent.extend(msgs)

    class Writer:
        def write(self, data: bytes) -> None:
            lines.append(data.decode().rstrip("\n"))

    ctx.send_msgs = send_msgs
    if game:
        ctx.game_writer = Writer()
    for key, value in attrs.items():
        setattr(ctx, key, value)
    return ctx, sent, lines


def romfs() -> Path:
    """The RomFS dump the tests read: LASAP_ROMFS, else "romfs" in scripts/local_paths.json. Not configured, a folder
    that does not exist, so the tests that need the dump skip."""
    value = ""
    if LOCAL_PATHS.exists():
        spec = importlib.util.spec_from_file_location("local_paths", LOCAL_PATHS)
        local_paths = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(local_paths)
        value = local_paths.lookup("romfs")
    return Path(value) if value else LOCAL_PATHS.with_name("<no RomFS configured>")
