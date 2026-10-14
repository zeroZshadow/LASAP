"""Builds the release: las.apworld with the patcher inside, and the player options template.

Run with .venv-lasr after scripts/build_exl.sh (it also needs .venv-ap and vendor/Archipelago for "Build APWorlds"):
  python scripts/build_release.py
Writes build/release/LASAP-<world version>/: las.apworld and "Links Awakening Switch.yaml".
The world carries the patcher as las/patcher/: its sources, LASR's data, the assets, the game module, and the pure
Python libraries it needs in lib/. The client runs it in its own process when no patcher command is set
(apworld/las/install.py).
"""
import hashlib
import importlib
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCHER = ROOT / "patcher"
AP = ROOT / "vendor" / "Archipelago"
AP_PYTHON = ROOT / ".venv-ap" / "Scripts" / "python.exe"
BUILD = ROOT / "build" / "release"
GAME = "Links Awakening Switch"
PREFIX = "las/patcher/"  # install.py PATCHER_PREFIX
# Patcher files and folders, relative to patcher/
PATCHER_FILES = ("las_ap_patch.py", "ap_assets.py", "ap_ghost.py", "ap_goal.py", "ap_map.py", "ap_map_icons.py",
                 "ap_model.py", "ap_ui.py", "lasr_qt.py", "map_positions.json", "version.txt", "LICENSE-LASR.txt",
                 "RandomizerCore", "RandomizerUI/Resources/Info", "RandomizerUI/Resources/Menu",
                 "RandomizerUI/Resources/Theme", "assets", "exl/subsdk9", "exl/main.npdm")
# Libraries for lib/: import name -> distribution name (for its license)
LIBRARIES = {"evfl": "evfl", "lms": "pylibms", "appdirs": "appdirs"}
MODULE_LICENSE = ROOT / "las-ap-exl" / "LICENSE"


def patcher_files() -> dict:
    """Archive name -> file for the patcher and its libraries."""
    files = {}
    for entry in PATCHER_FILES:
        path = PATCHER / entry
        if not path.exists():
            sys.exit(f"patcher/{entry} is missing" + (" (scripts/build_exl.sh)" if entry.startswith("exl") else ""))
        for file in ([path] if path.is_file() else sorted(path.rglob("*"))):
            if file.is_file() and "__pycache__" not in file.parts:
                files[PREFIX + file.relative_to(PATCHER).as_posix()] = file
    files[PREFIX + "licenses/LICENSE-las-ap-exl.txt"] = MODULE_LICENSE
    site = Path(str(importlib.import_module("yaml").__file__)).parent.parent
    for name, dist in LIBRARIES.items():
        module = Path(str(importlib.import_module(name).__file__))
        if module.name == "__init__.py":
            for file in sorted(module.parent.rglob("*")):
                if file.is_file() and "__pycache__" not in file.parts:
                    files[PREFIX + "lib/" + file.relative_to(module.parent.parent).as_posix()] = file
        else:
            files[PREFIX + "lib/" + module.name] = module
        licenses = [p for info in site.glob(f"{dist}-*.dist-info") for p in info.rglob("LICENSE*") if p.is_file()]
        if not licenses:
            sys.exit(f"no license file found for {dist}")
        files[PREFIX + f"lib/licenses/{dist}.txt"] = licenses[0]
    return files


def run_ap(code: str) -> None:
    env = dict(os.environ, SKIP_REQUIREMENTS_UPDATE="1")
    prelude = "import sys; sys.path.insert(0, '.'); import ModuleUpdate; ModuleUpdate.update_ran = True\n"
    subprocess.run([str(AP_PYTHON), "-c", prelude + code], cwd=AP, env=env, check=True)


def main() -> None:
    files = patcher_files()
    digest = hashlib.sha256()
    for name in sorted(files):
        digest.update(name.encode() + b"\0" + files[name].read_bytes())
    build_id = digest.hexdigest()[:16]

    manifest = json.loads((ROOT / "apworld" / "las" / "archipelago.json").read_text(encoding="utf-8"))
    release = BUILD / f"LASAP-{manifest['world_version']}"
    shutil.rmtree(release, ignore_errors=True)
    release.mkdir(parents=True)
    run_ap(f"import runpy; sys.argv = ['Launcher.py', 'Build APWorlds', '--', {GAME!r}, '--skip_open_folder']; "
           "runpy.run_path('Launcher.py', run_name='__main__')")
    apworld = release / "las.apworld"
    shutil.copyfile(AP / "build" / "apworlds" / "las.apworld", apworld)
    with zipfile.ZipFile(apworld, "a", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in sorted(files):
            zf.write(files[name], name)
        zf.writestr(PREFIX + "build.txt", build_id + "\n")

    templates = BUILD / "templates"
    shutil.rmtree(templates, ignore_errors=True)
    run_ap(f"import Options; Options.generate_yaml_templates({str(templates)!r}, False)")
    shutil.copyfile(templates / f"{GAME}.yaml", release / f"{GAME}.yaml")
    size = apworld.stat().st_size / 1e6
    print(f"Release written to {release} (las.apworld {size:.1f} MB, patcher build {build_id})")


if __name__ == "__main__":
    main()
