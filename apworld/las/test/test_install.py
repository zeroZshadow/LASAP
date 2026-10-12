import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from .. import emulators, install
from ..emulators import TITLE_ID
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

    def test_errors(self) -> None:
        self.command = ""
        with self.assertRaises(InstallError):
            self.run_install("aaaa0001")
        self.command = f'"{sys.executable}" -c "raise SystemExit(3)"'
        with self.assertRaises(InstallError):
            self.run_install("aaaa0001")
        self.assertIsNone(installed_seed(self.sd, self.mods))

    def test_bundled_patcher(self) -> None:
        """The release world's patcher is unpacked once per version; a new version replaces the old one."""
        import Utils
        root = Path(self.tmp.name)

        def bundle(version: str) -> Path:
            data = io.BytesIO()
            with zipfile.ZipFile(data, "w") as zf:
                zf.writestr(install.BUNDLED_PATCHER_EXE.as_posix(), version)
            path = root / f"bundle {version}.zip"
            path.write_bytes(data.getvalue())
            return path

        with mock.patch.object(Utils.cache_path, "cached_path", str(root / "cache"), create=True),                 mock.patch.object(install, "bundled_patcher_resource", return_value=bundle("1")):
            first = install.bundled_patcher(self.lines.append)
            self.assertEqual(first.read_text(), "1")
            self.assertEqual(install.bundled_patcher(self.lines.append), first)
            self.assertEqual(sum("Unpacking" in line for line in self.lines), 1)
            with mock.patch.object(install, "bundled_patcher_resource", return_value=bundle("2")):
                second = install.bundled_patcher(self.lines.append)
            self.assertEqual(second.read_text(), "2")
            self.assertFalse(first.exists())
        with mock.patch.object(install, "bundled_patcher_resource", return_value=None):
            self.assertIsNone(install.bundled_patcher(self.lines.append))

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
