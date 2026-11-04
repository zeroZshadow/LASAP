"""Builds the release: las.apworld with the patcher inside, and the player options template.

Run with .venv-lasr after scripts/build_exl.sh (it also needs .venv-ap and vendor/Archipelago for "Build APWorlds"):
  python scripts/build_release.py
Writes build/release/LASAP-<world version>/: las.apworld, "Links Awakening Switch.yaml", the setup guide and, with
.venv-relay, LASGhostRelay.exe (build_relay.py).
The world carries the patcher as las/patcher/: its sources, LASR's data, the assets and the game module; the asset
build (patcher/modbuild) stays out. It leaves out the world's tests. The client runs it in its own process when no patcher command is set
(apworld/las/install.py).
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCHER = ROOT / "patcher"
AP = ROOT / "vendor" / "Archipelago"
AP_PYTHON = ROOT / ".venv-ap" / "Scripts" / "python.exe"
if not AP_PYTHON.exists():  # a worktree uses the main checkout's venv (TESTING.md, "Working in a worktree")
    AP_PYTHON = Path(subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                                    capture_output=True, text=True).stdout.strip()).parent / ".venv-ap" / "Scripts" / "python.exe"
BUILD = ROOT / "build" / "release"
GAME = "Links Awakening Switch"
PREFIX = "las/patcher/"  # install.py PATCHER_PREFIX
TESTS = "las/test/"  # the world's tests, which "Build APWorlds" packs and players do not need
# Patcher files and folders, relative to patcher/
PATCHER_FILES = ("las_ap_patch.py", "ap_assets.py", "ap_entrances.py", "ap_goal.py", "ap_hints.py", "ap_map.py",
                 "ap_options.py", "ap_save_name.py", "ap_shuffles.py", "seed_file.py", "romfs_check.py",
                 "map_positions.json", "version.txt", "LICENSE-LASR.txt", "data", "assets", "exl/subsdk9", "exl/main.npdm")
# What only the asset build imports (patcher/modbuild, LASR's code and its libraries); test_install.py patches with
# these blocked. The patcher itself needs PyYAML and bsdiff4, which Archipelago has.
BUILD_ONLY = ("modbuild", "RandomizerCore", "evfl", "lms", "oead")
MODULE_LICENSE = ROOT / "las-ap-exl" / "LICENSE"


def patcher_files() -> dict:
    """Archive name -> file for the patcher."""
    files = {}
    for entry in PATCHER_FILES:
        path = PATCHER / entry
        if not path.exists():
            sys.exit(f"patcher/{entry} is missing" + (" (scripts/build_exl.sh)" if entry.startswith("exl") else ""))
        for file in ([path] if path.is_file() else sorted(path.rglob("*"))):
            name = file.relative_to(PATCHER).as_posix()
            if file.is_file() and "__pycache__" not in file.parts:
                files[PREFIX + name] = file
    files[PREFIX + "licenses/LICENSE-las-ap-exl.txt"] = MODULE_LICENSE
    return files


def run_ap(code: str) -> set:
    """Runs code in vendor/Archipelago and returns the other worlds that failed to load there; their tracebacks are
    not printed, as the release needs only this world. On any other failure it prints Archipelago's output."""
    env = dict(os.environ, SKIP_REQUIREMENTS_UPDATE="1")
    prelude = "import sys; sys.path.insert(0, '.'); import ModuleUpdate; ModuleUpdate.update_ran = True\n"
    result = subprocess.run([str(AP_PYTHON), "-c", prelude + code], cwd=AP, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    output = result.stdout + result.stderr
    failed = set(re.findall(r"Could not load world WorldSource\((\w+),", output))
    if result.returncode or "las" in failed:
        print(output)
        sys.exit("Archipelago failed: " + code[:80])
    return failed


def check_module() -> None:
    """Refuses a game module older than its source: build_exl.sh has not run since the source changed."""
    module = PATCHER / "exl" / "subsdk9"
    sources = [f for f in (ROOT / "las-ap-exl").rglob("*") if f.is_file() and f.suffix in (".cpp", ".hpp", ".h", ".c")
               and not {"build", "build-test", "out", "deploy"} & set(f.relative_to(ROOT / "las-ap-exl").parts)]
    newest = max(sources, key=lambda f: f.stat().st_mtime)
    if module.exists() and newest.stat().st_mtime > module.stat().st_mtime:
        sys.exit(f"patcher/exl/subsdk9 is older than {newest.relative_to(ROOT).as_posix()}: run scripts/build_exl.sh")


def main() -> None:
    check_module()
    files = patcher_files()
    digest = hashlib.sha256()
    for name in sorted(files):
        digest.update(name.encode() + b"\0" + files[name].read_bytes())
    build_id = digest.hexdigest()[:16]

    manifest = json.loads((ROOT / "apworld" / "las" / "archipelago.json").read_text(encoding="utf-8"))
    release = BUILD / f"LASAP-{manifest['world_version']}"
    shutil.rmtree(release, ignore_errors=True)
    release.mkdir(parents=True)
    skipped = run_ap(f"import runpy; sys.argv = ['Launcher.py', 'Build APWorlds', '--', {GAME!r}, "
                     "'--skip_open_folder']; runpy.run_path('Launcher.py', run_name='__main__')")
    apworld = release / "las.apworld"
    with zipfile.ZipFile(AP / "build" / "apworlds" / "las.apworld") as built,             zipfile.ZipFile(apworld, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for info in built.infolist():
            if not info.filename.startswith(TESTS):
                zf.writestr(info, built.read(info))
        for name in sorted(files):
            zf.write(files[name], name)
        zf.write(ROOT / "LICENSE", "las/LICENSE.txt")  # the world's and the patcher's own code (README, Licenses)
        zf.writestr(PREFIX + "build.txt", build_id + "\n")

    templates = BUILD / "templates"
    shutil.rmtree(templates, ignore_errors=True)
    skipped |= run_ap(f"import Options; Options.generate_yaml_templates({str(templates)!r}, False)")
    # the template asks for the Archipelago that built it; the world works from its minimum version on
    template = (templates / f"{GAME}.yaml").read_text(encoding="utf-8-sig")
    template, count = re.subn(r"(?m)^(  version: )[0-9.]+( # Version of Archipelago required)",
                              rf"\g<1>{manifest['minimum_ap_version']}\g<2>", template)
    if count != 1:
        sys.exit("the options template has no Archipelago version to set")
    (release / f"{GAME}.yaml").write_text(template, encoding="utf-8-sig")
    shutil.copyfile(ROOT / "apworld" / "las" / "docs" / "setup_en.md", release / "Setup guide.md")
    import build_relay
    relay = build_relay.build(release) if build_relay.available() else None
    size = apworld.stat().st_size / 1e6
    if skipped:
        print(f"Skipped other worlds that do not load in vendor/Archipelago: {', '.join(sorted(skipped))}")
    print(f"Release written to {release} (las.apworld {size:.1f} MB, patcher build {build_id})")
    print(f"Ghost relay: {relay.name} ({relay.stat().st_size / 1e6:.1f} MB)" if relay else
          "No .venv-relay, so no LASGhostRelay.exe (README, Building a release).")


if __name__ == "__main__":
    main()
