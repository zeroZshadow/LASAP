"""Builds LASGhostRelay.exe, the ghost relay as one Windows program (scripts/relay_exe.py), with PyInstaller.

Needs .venv-relay with PyInstaller (README, Building a release). build_release.py runs it when that venv exists;
on its own:
  python scripts/build_relay.py [<out dir>]     (default: build/release/LASAP-<world version>)
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENV_PYTHON = ROOT / ".venv-relay" / "Scripts" / "python.exe"
if not VENV_PYTHON.exists():  # a worktree uses the main checkout's venv (TESTING.md, "Working in a worktree")
    VENV_PYTHON = Path(subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                      capture_output=True, text=True).stdout.strip()).parent / ".venv-relay" / "Scripts" / "python.exe"
NAME = "LASGhostRelay"


def available() -> bool:
    return VENV_PYTHON.exists()


def build(out: Path) -> Path:
    work = ROOT / "build" / "relay"
    shutil.rmtree(work, ignore_errors=True)
    subprocess.run([str(VENV_PYTHON), "-m", "PyInstaller", "--onefile", "--console", "--noconfirm", "--log-level", "WARN",
                    "--name", NAME, "--paths", str(ROOT / "apworld" / "las"), "--distpath", str(out),
                    "--workpath", str(work / "work"), "--specpath", str(work), str(ROOT / "scripts" / "relay_exe.py")],
                   check=True)
    shutil.rmtree(work, ignore_errors=True)
    return out / f"{NAME}.exe"


def main() -> None:
    if not available():
        sys.exit(f"No PyInstaller venv at {VENV_PYTHON} (README, Building a release).")
    if len(sys.argv) > 1:
        out = Path(sys.argv[1])
    else:
        manifest = json.loads((ROOT / "apworld" / "las" / "archipelago.json").read_text(encoding="utf-8"))
        out = ROOT / "build" / "release" / f"LASAP-{manifest['world_version']}"
    exe = build(out)
    print(f"Ghost relay written to {exe} ({exe.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
