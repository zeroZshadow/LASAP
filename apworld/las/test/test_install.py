import sys
import tempfile
import unittest
from pathlib import Path

from ..install import CONFIG_PATH, TITLE_ID, InstallError, installed_seed, mod_dir, patch_and_install, split_command

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

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def run_install(self, seed: str, force: bool = False) -> bool:
        self.patch.write_text(seed)
        return patch_and_install(self.command, self.patch, seed, self.romfs, self.sd, mod_dir(self.sd, ""),
                                 self.lines.append, force)

    def test_installs_once_per_seed(self) -> None:
        mods = self.sd / "atmosphere" / "contents" / TITLE_ID
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

    def test_mod_folder(self) -> None:
        self.assertEqual(mod_dir(self.sd, "D:/emu/load/X/Archipelago"), Path("D:/emu/load/X/Archipelago"))

    def test_errors(self) -> None:
        self.command = ""
        with self.assertRaises(InstallError):
            self.run_install("aaaa0001")
        self.command = f'"{sys.executable}" -c "raise SystemExit(3)"'
        with self.assertRaises(InstallError):
            self.run_install("aaaa0001")
        self.assertIsNone(installed_seed(self.sd, mod_dir(self.sd, "")))

    def test_split_command(self) -> None:
        self.assertEqual(split_command('"C:/Program Files/py.exe" D:\\a\\b.py'),
                         ["C:/Program Files/py.exe", "D:\\a\\b.py"] if sys.platform == "win32"
                         else ["C:/Program Files/py.exe", "D:ab.py"])
