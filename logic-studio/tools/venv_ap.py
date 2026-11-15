"""Archipelago's Python for the game's scripts Logic Studio starts: the main checkout's .venv-ap, also from a worktree."""
import subprocess
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parents[2] / "scripts"))
from local_paths import venv_python  # noqa: E402


def python():
    return venv_python(".venv-ap")


def relaunch():
    """Runs the calling script again in .venv-ap, with its stdin and stdout, unless it runs there already or there is none."""
    venv = python()
    if venv.exists() and Path(sys.prefix).resolve() != venv.parent.parent.resolve():
        sys.exit(subprocess.call([str(venv), "-B", str(Path(sys.argv[0]).resolve()), *sys.argv[1:]]))
