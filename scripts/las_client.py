"""Starts the LAS client from vendor/Archipelago without the Launcher: python scripts/las_client.py [client args]
Ghosts stay off unless --ghost-relay names a relay, so test runs never join the public one."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AP = os.path.join(ROOT, "vendor", "Archipelago")
os.chdir(AP)
sys.path.insert(0, AP)
import ModuleUpdate  # noqa: E402
ModuleUpdate.update_ran = True
from worlds.las.client import launch  # noqa: E402

args = sys.argv[1:]
if not any(arg == "--ghost-relay" or arg.startswith("--ghost-relay=") for arg in args):
    args = ["--ghost-relay", "", *args]
launch(*args)
