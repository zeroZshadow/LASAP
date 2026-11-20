"""Whether a build of the game module is older than its source: in a checkout, scripts/build_exl.sh has not run since
las-ap-exl changed. The patcher writes config.ini for the module beside it, so an older module may read it otherwise
(DESIGN.md, Client install). The release patcher has no source beside it, so its module never is.

python module_check.py <subsdk9>: prints the problem and exits 1 (scripts/deploy_mod.sh).
"""
import sys
from pathlib import Path

SOURCE = Path(__file__).resolve().parent.parent / "las-ap-exl"
SUFFIXES = (".cpp", ".hpp", ".h", ".c")
BUILD_DIRS = {"build", "build-test", "out", "deploy"}


def newer_source(module: Path) -> str:
    """The newest las-ap-exl source file, relative to the checkout, when it is newer than module, else "" (also without
    the source or the module)."""
    if not SOURCE.is_dir() or not module.exists():
        return ""
    sources = [f for f in SOURCE.rglob("*") if f.is_file() and f.suffix in SUFFIXES
               and not BUILD_DIRS & set(f.relative_to(SOURCE).parts)]
    newest = max(sources, key=lambda f: f.stat().st_mtime, default=None)
    if newest is None or newest.stat().st_mtime <= module.stat().st_mtime:
        return ""
    return newest.relative_to(SOURCE.parent).as_posix()


def problem(module: Path) -> str:
    """What to say about module when it is older than its source, else ""."""
    newer = newer_source(module)
    if not newer:
        return ""
    path = module.resolve()
    name = path.relative_to(SOURCE.parent).as_posix() if path.is_relative_to(SOURCE.parent) else str(module)
    return f"{name} is older than {newer}: run scripts/build_exl.sh"


if __name__ == "__main__":
    message = problem(Path(sys.argv[1]))
    if message:
        print(message)
        sys.exit(1)
