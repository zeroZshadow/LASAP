"""Runs an Archipelago entry point (Generate.py, MultiServer.py, ...) from vendor/Archipelago in .venv-ap.

python scripts/ap.py Generate.py --player_files_path <dir> --outputpath <dir>
The venv has no pip, so ModuleUpdate's dependency check is skipped.
"""
import os
import runpy
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AP = os.path.join(ROOT, "vendor", "Archipelago")
os.chdir(AP)
sys.path.insert(0, AP)
import ModuleUpdate  # noqa: E402
ModuleUpdate.update_ran = True
script = sys.argv[1]
sys.argv = [os.path.join(AP, script)] + sys.argv[2:]
runpy.run_path(sys.argv[0], run_name="__main__")
