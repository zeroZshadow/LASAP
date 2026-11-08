"""Archipelago's Python for the game's scripts Logic Studio starts: the main checkout's .venv-ap, also from a worktree."""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def python():
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    main = Path(common).parent if common else ROOT
    return main / ".venv-ap" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def relaunch():
    """Runs the calling script again in .venv-ap, with its stdin and stdout, unless it runs there already or there is none."""
    venv = python()
    if venv.exists() and Path(sys.prefix).resolve() != venv.parent.parent.resolve():
        sys.exit(subprocess.call([str(venv), "-B", str(Path(sys.argv[0]).resolve()), *sys.argv[1:]]))
