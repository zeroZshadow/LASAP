"""This machine's folders and files, which stay out of the repo: scripts/local_paths.json (gitignored; the keys are in
scripts/local_paths.example.json, TESTING.md, Requirements). The environment variable of a key (ENV) comes first.

python local_paths.py <key>   prints the value, for shell scripts
"""
import json
import os
import subprocess
import sys
from pathlib import Path

FILE = Path(__file__).resolve().with_name("local_paths.json")
ENV = {"romfs": "LASAP_ROMFS", "re": "LASAP_RE"}  # key -> the environment variable that comes before it


def lookup(key: str) -> str:
    """The value of key (its environment variable first), or "" when neither has it."""
    env = ENV.get(key, "")
    if env and os.environ.get(env):
        return os.environ[env]
    try:
        return str(json.loads(FILE.read_text(encoding="utf-8")).get(key, ""))
    except (OSError, ValueError):
        return ""


def local_path(key: str) -> Path:
    """The path key names (its environment variable first); stops with a message naming the key when it is not set."""
    value = lookup(key)
    if not value:
        env = ENV.get(key)
        raise SystemExit(f'{FILE} needs "{key}"' + (f" (or {env})" if env else "") + " (TESTING.md, Requirements)")
    return Path(value)


def venv_python(name: str) -> Path:
    """The Python of the main checkout's venv `name` (.venv-ap, ...), which a worktree uses too (TESTING.md, "Working
    in a worktree")."""
    root = FILE.parent.parent
    common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=root,
                            capture_output=True, text=True).stdout.strip()
    main = Path(common).parent if common else root
    return main / name / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


if __name__ == "__main__":
    print(local_path(sys.argv[1]).as_posix())
