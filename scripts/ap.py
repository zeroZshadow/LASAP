"""Runs an Archipelago entry point (Generate.py, MultiServer.py, ...) from vendor/Archipelago in .venv-ap.

python scripts/ap.py Generate.py --player_files_path <dir> --outputpath <dir>
The venv has no pip, so ModuleUpdate's dependency check is skipped, also in processes the Launcher starts.
LASAP_AP points at another Archipelago checkout. The scripts that import Archipelago start with setup().
"""
import os
import runpy
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AP = os.environ.get("LASAP_AP", os.path.join(ROOT, "vendor", "Archipelago"))


def setup() -> None:
    """Archipelago as the working folder and import root, without ModuleUpdate's dependency check."""
    os.environ["SKIP_REQUIREMENTS_UPDATE"] = "1"
    os.chdir(AP)
    sys.path.insert(0, AP)
    import ModuleUpdate
    ModuleUpdate.update_ran = True


if __name__ == "__main__":
    setup()
    script = sys.argv[1]
    sys.argv = [os.path.join(AP, script)] + sys.argv[2:]
    runpy.run_path(sys.argv[0], run_name="__main__")
