"""This machine's folders and files, which stay out of the repo: scripts/local_paths.json (gitignored; the keys are in
scripts/local_paths.example.json, TESTING.md, Requirements). An environment variable named for a key comes first.

python local_paths.py <key>   prints the value, for shell scripts
"""
import json
import os
import sys
from pathlib import Path

FILE = Path(__file__).resolve().with_name("local_paths.json")


def lookup(key: str, env: str = "") -> str:
    """The value of key (env first), or "" when neither has it."""
    if env and os.environ.get(env):
        return os.environ[env]
    try:
        return str(json.loads(FILE.read_text(encoding="utf-8")).get(key, ""))
    except (OSError, ValueError):
        return ""


def local_path(key: str, env: str = "") -> Path:
    """The path key names (env first); stops with a message naming the key when it is not set."""
    value = lookup(key, env)
    if not value:
        raise SystemExit(f'{FILE} needs "{key}"' + (f" (or {env})" if env else "") + " (TESTING.md, Requirements)")
    return Path(value)


if __name__ == "__main__":
    print(local_path(sys.argv[1]).as_posix())
