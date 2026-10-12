"""Builds the release: las.apworld with a frozen Windows patcher inside, and the player options template.

Run with .venv-release (patcher/requirements.txt plus pyinstaller), after scripts/build_exl.sh:
  python scripts/build_release.py
Writes build/release/LASAP-<world version>/: las.apworld and "Links Awakening Switch.yaml".
The patcher is a PyInstaller folder with LASR's data and the game module, zipped into the apworld as
las/bin/patcher-windows-x64.zip; the client unpacks it when no patcher command is set (apworld/las/install.py).
"""
import json
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

import PyInstaller.__main__

ROOT = Path(__file__).resolve().parent.parent
PATCHER = ROOT / "patcher"
AP = ROOT / "vendor" / "Archipelago"
AP_PYTHON = ROOT / ".venv-ap" / "Scripts" / "python.exe"
BUILD = ROOT / "build" / "release"
GAME = "Links Awakening Switch"
NAME = "las_ap_patcher"
BUNDLE = "patcher-windows-x64.zip"  # install.py BUNDLED_PATCHERS
RESOURCES = Path("RandomizerUI", "Resources")
# Resources LASR reads; textures/chest/bfres is its cache of generated chest models.
RESOURCE_PARTS = ("Info", "Menu", "Theme", "randomizer.png", "textures/chest/TreasureBoxJunk.dds",
                  "textures/chest/TreasureBoxKey.dds", "textures/chest/TreasureBoxLifeUpgrade.dds")
LICENSES = {
    "LICENSE-LASR.txt": PATCHER / "LICENSE-LASR.txt",
    "LICENSE-RandomizerCore.txt": PATCHER / "RandomizerCore" / "LICENSE.txt",
    "LICENSE-bntx_editor.txt": PATCHER / "RandomizerCore" / "Tools" / "bntx_editor" / "LICENSE.txt",
    "LICENSE-las-ap-exl.txt": ROOT / "las-ap-exl" / "LICENSE",
}


def data_arg(source: Path, dest: Path) -> list:
    return ["--add-data", f"{source}{os.pathsep}{dest.as_posix()}"]


def build_patcher() -> Path:
    for part in ("subsdk9", "main.npdm"):
        if not (PATCHER / "exl" / part).is_file():
            sys.exit(f"patcher/exl/{part} is missing; build the module first (scripts/build_exl.sh)")
    args = [str(PATCHER / "las_ap_patch.py"), "--name", NAME, "--onedir", "--console", "--noconfirm", "--clean",
            "--distpath", str(BUILD / "dist"), "--workpath", str(BUILD / "work"), "--specpath", str(BUILD),
            "--paths", str(PATCHER), "--exclude-module", "PySide6", "--exclude-module", "tkinter",
            "--exclude-module", "numpy", "--collect-all", "lms"]  # PyLibMS loads its title presets by name
    args += data_arg(PATCHER / "RandomizerCore" / "Data", Path("RandomizerCore", "Data"))
    for part in RESOURCE_PARTS:
        source = PATCHER / RESOURCES / part
        args += data_arg(source, RESOURCES / part if source.is_dir() else (RESOURCES / part).parent)
    args += data_arg(PATCHER / "exl", Path("exl"))
    args += data_arg(PATCHER / "version.txt", Path("."))
    PyInstaller.__main__.run(args)
    folder = BUILD / "dist" / NAME
    licenses = folder / "licenses"
    licenses.mkdir(exist_ok=True)
    for name, source in LICENSES.items():
        shutil.copyfile(source, licenses / name)
    return folder


def zip_folder(folder: Path, target: Path) -> None:
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for file in sorted(folder.rglob("*")):
            if file.is_file():
                zf.write(file, Path(folder.name, file.relative_to(folder)))


def run_ap(code: str) -> None:
    env = dict(os.environ, SKIP_REQUIREMENTS_UPDATE="1")
    prelude = "import sys; sys.path.insert(0, '.'); import ModuleUpdate; ModuleUpdate.update_ran = True\n"
    subprocess.run([str(AP_PYTHON), "-c", prelude + code], cwd=AP, env=env, check=True)


def main() -> None:
    BUILD.mkdir(parents=True, exist_ok=True)
    folder = build_patcher()
    bundle = BUILD / BUNDLE
    zip_folder(folder, bundle)

    manifest = json.loads((ROOT / "apworld" / "las" / "archipelago.json").read_text(encoding="utf-8"))
    release = BUILD / f"LASAP-{manifest['world_version']}"
    shutil.rmtree(release, ignore_errors=True)
    release.mkdir()
    run_ap(f"import runpy; sys.argv = ['Launcher.py', 'Build APWorlds', '--', {GAME!r}, '--skip_open_folder']; "
           "runpy.run_path('Launcher.py', run_name='__main__')")
    apworld = release / "las.apworld"
    shutil.copyfile(AP / "build" / "apworlds" / "las.apworld", apworld)
    with zipfile.ZipFile(apworld, "a", zipfile.ZIP_STORED) as zf:  # the bundle is compressed already
        zf.write(bundle, f"las/bin/{BUNDLE}")

    templates = BUILD / "templates"
    shutil.rmtree(templates, ignore_errors=True)
    run_ap(f"import Options; Options.generate_yaml_templates({str(templates)!r}, False)")
    shutil.copyfile(templates / f"{GAME}.yaml", release / f"{GAME}.yaml")
    size = apworld.stat().st_size / 1e6
    print(f"Release written to {release} (las.apworld {size:.1f} MB)")


if __name__ == "__main__":
    main()
