"""Starts the LAS client from vendor/Archipelago without the Launcher: python scripts/las_client.py [client args]
Ghosts stay off unless --ghost-relay names a relay, so test runs never join the public one. The client's worker
processes (the tracker's images) import this script, so it starts the client only as the main script."""
import os
import sys

if __name__ == "__main__":
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    AP = os.path.join(ROOT, "vendor", "Archipelago")
    os.chdir(AP)
    sys.path.insert(0, AP)
    import ModuleUpdate
    ModuleUpdate.update_ran = True
    from worlds.las.client import launch

    args = sys.argv[1:]
    if not any(arg == "--ghost-relay" or arg.startswith("--ghost-relay=") for arg in args):
        args = ["--ghost-relay", "", *args]
    launch(*args)
