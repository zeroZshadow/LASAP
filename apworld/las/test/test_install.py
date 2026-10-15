import ast
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from .. import emulators, install
from ..emulators import TITLE_ID
from ..host_settings import PLAYER_DEFAULTS, PLAYER_SETTINGS
from ..install import CONFIG_PATH, InstallError, installed_seed, patch_and_install, split_command

# Stands in for las_ap_patch.py: writes the patcher's output layout for the seed named in the patch file.
FAKE_PATCHER = """
import sys
from pathlib import Path
patch, out = Path(sys.argv[1]), Path(sys.argv[sys.argv.index("--out") + 1])
game = out / "atmosphere" / "contents" / "01006BB00C6F0000"
for part in ("romfs", "exefs"):
    (game / part).mkdir(parents=True)
    (game / part / "file").write_text(patch.read_text())
(out / "config" / "lasr-exl").mkdir(parents=True)
(out / "config" / "lasr-exl" / "config.ini").write_text("[archipelago]\\nseed_hash = " + patch.read_text() + "\\n")
(out / "config" / "lasr-exl" / "ap_names.txt").write_text("1 Bow|")
print("Mod written")
"""


# Stands in for las_ap_patch.py as the built-in patcher imports it: main(argv), with state in a module global that
# a fresh import resets.
FAKE_BUILTIN = """
from pathlib import Path
RUNS = []


def main(argv):
    RUNS.append(argv)
    patch, out = Path(argv[0]), Path(argv[argv.index("--out") + 1])
    game = out / "atmosphere" / "contents" / "01006BB00C6F0000"
    for part in ("romfs", "exefs"):
        (game / part).mkdir(parents=True)
        (game / part / "file").write_text(patch.read_text())
    (out / "config" / "lasr-exl").mkdir(parents=True)
    (out / "config" / "lasr-exl" / "config.ini").write_text("[archipelago]\\nseed_hash = " + patch.read_text() + "\\n")
    print(f"Mod written, run {len(RUNS)}")
    return 0
"""


class TestInstall(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.sd = root / "sd card"
        self.sd.mkdir()
        self.romfs = root / "romfs"
        script = root / "fake patcher.py"
        script.write_text(FAKE_PATCHER)
        self.command = f'"{sys.executable}" "{script}"'
        self.patch = root / "seed.aplas"
        self.lines = []
        self.mods = self.sd / "atmosphere" / "contents" / TITLE_ID

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_install(self, seed: str, force: bool = False) -> bool:
        self.patch.write_text(seed)
        return patch_and_install(self.command, self.patch, seed, self.romfs, self.sd, self.mods, self.lines.append,
                                 force)

    def test_installs_once_per_seed(self) -> None:
        mods = self.mods
        self.assertIsNone(installed_seed(self.sd, mods))
        self.assertTrue(self.run_install("aaaa0001"))
        self.assertIn("Mod written", self.lines)
        self.assertEqual(installed_seed(self.sd, mods), "aaaa0001")
        self.assertFalse(self.run_install("aaaa0001"))
        self.assertTrue(self.run_install("aaaa0001", force=True))

        (mods / "romfs" / "stale").write_text("old seed")
        self.assertTrue(self.run_install("bbbb0002"))
        self.assertEqual(installed_seed(self.sd, mods), "bbbb0002")
        self.assertEqual((mods / "exefs" / "file").read_text(), "bbbb0002")
        self.assertFalse((mods / "romfs" / "stale").exists())
        self.assertTrue((self.sd / CONFIG_PATH).exists())
        self.assertTrue((self.sd / CONFIG_PATH).with_name("ap_names.txt").exists())

    def test_removes_stale_map(self) -> None:
        # The map tracker's file of a seed with the option does not stay for a seed without it.
        self.assertTrue(self.run_install("aaaa0001"))
        stale = (self.sd / CONFIG_PATH).with_name("ap_map.txt")
        stale.write_text("O 0 1")
        self.assertTrue(self.run_install("bbbb0002"))
        self.assertFalse(stale.exists())

    def test_errors(self) -> None:
        self.command = ""
        with self.assertRaises(InstallError):
            self.run_install("aaaa0001")
        self.command = f'"{sys.executable}" -c "raise SystemExit(3)"'
        with self.assertRaisesRegex(InstallError, r"exit code 3"):
            self.run_install("aaaa0001")
        self.assertIsNone(installed_seed(self.sd, self.mods))
        # the patcher prints why it failed last; the error names that
        self.command = f'"{sys.executable}" -c "print(\'Patching\'); print(\'The RomFS dump is old.\'); exit(1)"'
        with self.assertRaisesRegex(InstallError, r"^The RomFS dump is old\.$"):
            self.run_install("aaaa0001")

    def test_builtin_patcher(self) -> None:
        """With no command, the patcher runs in this process, its modules imported fresh for each run."""
        root = Path(self.tmp.name) / "patcher"
        root.mkdir()
        (root / "las_ap_patch.py").write_text(FAKE_BUILTIN)
        self.command = ""
        saved_path = list(sys.path)
        with mock.patch.object(install, "builtin_patcher_dir", return_value=root):
            self.assertTrue(self.run_install("aaaa0001"))
            self.assertTrue(self.run_install("bbbb0002", force=True))
        self.assertEqual((self.mods / "exefs" / "file").read_text(), "bbbb0002")
        self.assertEqual(self.lines.count("Mod written, run 1"), 2)
        self.assertEqual(sys.path, saved_path)
        self.assertNotIn("las_ap_patch", sys.modules)

    def test_builtin_patcher_unpacks_once(self) -> None:
        """The packaged world's patcher is unpacked once per build; a new build replaces the old one."""
        import Utils
        root = Path(self.tmp.name)

        def world(build: str) -> Path:
            path = root / f"las {build}.apworld"
            with zipfile.ZipFile(path, "w") as zf:
                zf.writestr(install.PATCHER_BUILD, build)
                zf.writestr(install.PATCHER_PREFIX + "RandomizerCore/Data/items.yml", build)
            return path

        with mock.patch.object(Utils.cache_path, "cached_path", str(root / "cache"), create=True):
            with mock.patch.object(install, "_world_archive", return_value=world("1")):
                self.assertTrue(install.builtin_patcher_available())
                first = install.builtin_patcher_dir(self.lines.append)
                self.assertEqual(install.builtin_patcher_dir(self.lines.append), first)
            self.assertEqual((first / "RandomizerCore" / "Data" / "items.yml").read_text(), "1")
            self.assertEqual(sum("Unpacking" in line for line in self.lines), 1)
            with mock.patch.object(install, "_world_archive", return_value=world("2")):
                second = install.builtin_patcher_dir(self.lines.append)
            self.assertEqual((second / "RandomizerCore" / "Data" / "items.yml").read_text(), "2")
            self.assertFalse(first.exists())

    def test_newer_world_reinstalls(self) -> None:
        """A mod the built-in patcher of another world version wrote is outdated; Play reinstalls it."""
        root = Path(self.tmp.name) / "patcher"
        root.mkdir()
        (root / "las_ap_patch.py").write_text(FAKE_BUILTIN)
        self.command = ""
        with mock.patch.object(install, "builtin_patcher_dir", return_value=root):
            with mock.patch.object(install, "builtin_patcher_build", return_value="1"):
                self.assertTrue(self.run_install("aaaa0001"))
                self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", ""), install.CURRENT)
                self.assertFalse(self.run_install("aaaa0001"))
            with mock.patch.object(install, "builtin_patcher_build", return_value="2"):
                self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", ""), install.OUTDATED)
                self.assertEqual(install.mod_state(self.sd, self.mods, "bbbb0002", ""), install.OTHER_SEED)
                self.assertTrue(self.run_install("aaaa0001"))
                self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", ""), install.CURRENT)

    def test_player_settings_reinstall(self) -> None:
        """The patcher gets the player settings; a mod patched with other settings is reinstalled."""
        root = Path(self.tmp.name) / "patcher"
        root.mkdir()
        (root / "las_ap_patch.py").write_text(FAKE_BUILTIN)
        self.command = ""
        music = {"Music": "Shuffled"}
        with mock.patch.object(install, "builtin_patcher_dir", return_value=root):
            self.assertTrue(self.run_install("aaaa0001"))
            # an install without the settings file counts as the defaults
            (self.sd / install.SETTINGS_PATH).unlink()
            self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", "", dict(PLAYER_DEFAULTS)),
                             install.CURRENT)
            self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", "", music), install.CHANGED)
            # settings that are no player settings any more, from an older install, do not count
            (self.sd / install.SETTINGS_PATH).write_text('{"Damage": "OHKO", "Music": "Vanilla"}')
            self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", "", dict(PLAYER_DEFAULTS)),
                             install.CURRENT)
            self.patch.write_text("aaaa0001")
            self.assertTrue(patch_and_install("", self.patch, "aaaa0001", self.romfs, self.sd, self.mods,
                                              self.lines.append, settings=music))
            self.assertIn("Mod written, run 1", self.lines)
            self.assertEqual(install.mod_state(self.sd, self.mods, "aaaa0001", "", music), install.CURRENT)
            self.assertFalse(patch_and_install("", self.patch, "aaaa0001", self.romfs, self.sd, self.mods,
                                               self.lines.append, settings=music))

    def test_player_settings_arguments(self) -> None:
        """The settings reach the patcher command as a JSON object."""
        script = Path(self.tmp.name) / "args patcher.py"
        script.write_text(FAKE_PATCHER + "\nprint('ARGS ' + repr(sys.argv[sys.argv.index('--settings') + 1]))\n")
        self.command = f'"{sys.executable}" "{script}"'
        self.patch.write_text("aaaa0001")
        patch_and_install(self.command, self.patch, "aaaa0001", self.romfs, self.sd, self.mods, self.lines.append,
                          settings={"Music": "Removed", "Blur Removal": True})
        self.assertIn("ARGS '{\"Music\": \"Removed\", \"Blur Removal\": true}'", self.lines)

    def test_player_settings_match_patcher(self) -> None:
        """The client's player settings are the patcher's, with the same defaults and choices."""
        source = install.SOURCE_PATCHER / "las_ap_patch.py"
        if not source.is_file():
            self.skipTest("no patcher source")
        tree = ast.parse(source.read_text(encoding="utf-8"))
        values = {node.targets[0].id: ast.literal_eval(node.value) for node in tree.body
                  if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
                  and node.targets[0].id in ("PLAYER_SETTINGS", "PLAYER_CHOICES")}
        self.assertEqual(values["PLAYER_SETTINGS"], PLAYER_DEFAULTS)
        self.assertEqual(values["PLAYER_CHOICES"], {s.lasr: s.choices for s in PLAYER_SETTINGS if s.choices})
        from ..host_settings import LASSettings
        for setting in PLAYER_SETTINGS:
            self.assertEqual(getattr(LASSettings, setting.key), setting.default, setting.key)

    def test_patcher_modules_ship(self) -> None:
        """Every top-level patcher module is in the release (build_release.py's PATCHER_FILES) and is unloaded after an
        in-process run (install.PATCHER_MODULES)."""
        modules = sorted(install.SOURCE_PATCHER.glob("*.py"))
        if not modules:
            self.skipTest("no patcher source")
        tree = ast.parse((install.SOURCE_PATCHER.parent / "scripts" / "build_release.py").read_text(encoding="utf-8"))
        files = next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                     and isinstance(node.targets[0], ast.Name) and node.targets[0].id == "PATCHER_FILES")
        for module in modules:
            with self.subTest(module=module.name):
                self.assertIn(module.name, files)
                self.assertIn(module.stem, install.PATCHER_MODULES)

    def test_split_command(self) -> None:
        self.assertEqual(split_command('"C:/Program Files/py.exe" D:\\a\\b.py'),
                         ["C:/Program Files/py.exe", "D:\\a\\b.py"] if sys.platform == "win32"
                         else ["C:/Program Files/py.exe", "D:ab.py"])


class TestEmulators(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_ryujinx_portable(self) -> None:
        exe = self.root / "Ryujinx" / "Ryujinx.exe"
        (exe.parent / "portable").mkdir(parents=True)
        target = emulators.target(str(exe), "", "")
        self.assertEqual(target.sd_card, exe.parent / "portable" / "sdcard")
        self.assertEqual(target.mods, exe.parent / "portable" / "sdcard" / "atmosphere" / "contents" / TITLE_ID)
        self.assertEqual(emulators.start_args(str(exe), "game.nsp", target.data_dir), [str(exe), "game.nsp"])

    def test_ryujinx_custom_data_folder(self) -> None:
        exe = self.root / "Ryujinx.exe"
        data = self.root / "data"
        target = emulators.target(str(exe), str(data / "sdcard"), "")
        self.assertEqual(target.mods, data / "sdcard" / "atmosphere" / "contents" / TITLE_ID)
        self.assertEqual(emulators.start_args(str(exe), "game.nsp", target.data_dir),
                         [str(exe), "--root-data-dir", str(data), "game.nsp"])

    def test_yuzu_based(self) -> None:
        exe = self.root / "eden" / "eden.exe"
        (exe.parent / "user").mkdir(parents=True)
        target = emulators.target(str(exe), "", "")
        self.assertEqual(target.kind, "yuzu")
        self.assertEqual(target.sd_card, exe.parent / "user" / "sdmc")
        self.assertEqual(target.mods, exe.parent / "user" / "load" / TITLE_ID / "Archipelago")
        self.assertEqual(emulators.start_args(str(exe), "game.nsp", target.data_dir), [str(exe), "-g", "game.nsp"])

    def test_without_emulator(self) -> None:
        self.assertIsNone(emulators.target("", "", ""))
        target = emulators.target("", str(self.root / "sd"), str(self.root / "mods"))
        self.assertEqual((target.sd_card, target.mods), (self.root / "sd", self.root / "mods"))
