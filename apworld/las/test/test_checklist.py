import tempfile
import unittest
from pathlib import Path

from .. import checklist
from ..emulators import TITLE_ID


class TestChecklist(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.romfs = self.root / "dump" / "romfs"
        (self.romfs / "region_common" / "event").mkdir(parents=True)
        (self.romfs / "region_common" / "event" / "PlayerStart.bfevfl").write_bytes(b"")
        self.ryujinx = self.root / "Ryujinx" / "Ryujinx.exe"
        (self.ryujinx.parent / "portable" / "sdcard").mkdir(parents=True)
        self.ryujinx.write_bytes(b"")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_empty(self) -> None:
        result = checklist.check("", {})
        self.assertFalse(result.ready)
        for key in ("patch_file", "emulator", "romfs_folder"):
            self.assertEqual(result.checks[key].level, checklist.TODO, key)
        self.assertIn(".aplas", result.problems())

    def test_ryujinx_ready_except_patch(self) -> None:
        result = checklist.check("", {"emulator": str(self.ryujinx), "romfs_folder": str(self.romfs.parent),
                                      "patcher_command": "patcher"})
        self.assertEqual(result.checks["emulator"].level, checklist.OK)
        self.assertEqual(result.checks["romfs_folder"].level, checklist.OK)  # the folder above romfs is fine too
        self.assertEqual(result.target.mods,
                         self.ryujinx.parent / "portable" / "sdcard" / "atmosphere" / "contents" / TITLE_ID)

    def test_problems(self) -> None:
        unknown = self.root / "emu.exe"
        unknown.write_bytes(b"")
        result = checklist.check("", {"emulator": str(unknown), "romfs_folder": str(self.root)})
        self.assertEqual(result.checks["emulator"].level, checklist.ERROR)
        self.assertIn("SD card folder", result.checks["emulator"].text)
        self.assertEqual(result.checks["romfs_folder"].level, checklist.ERROR)
        self.assertEqual(result.checks["patcher_command"].level, checklist.ERROR)

        result = checklist.check("", {"sd_card_folder": str(self.root / "missing")})
        self.assertEqual(result.checks["emulator"].level, checklist.ERROR)

        eden = self.root / "eden" / "eden.exe"
        (eden.parent / "user").mkdir(parents=True)
        eden.write_bytes(b"")
        result = checklist.check("", {"emulator": str(eden)})
        self.assertIn("Start Eden once", result.checks["emulator"].text)
        self.assertIn("Dump RomFS", result.checks["romfs_folder"].text)
