import os
import tempfile
import unittest
from unittest import mock
from pathlib import Path

from .. import checklist, install
from ..emulators import TITLE_ID

ROMFS = Path(os.environ.get("LASAP_ROMFS", r"D:\Games\Switch\LASRomFS"))


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
        with mock.patch.object(checklist, "romfs_problem", return_value=None):
            result = checklist.check("", {"emulator": str(self.ryujinx), "romfs_folder": str(self.romfs.parent),
                                          "patcher_command": "patcher"})
        self.assertEqual(result.checks["emulator"].level, checklist.OK)
        self.assertEqual(result.checks["romfs_folder"].level, checklist.OK)  # the folder above romfs is fine too
        self.assertEqual(result.target.mods,
                         self.ryujinx.parent / "portable" / "sdcard" / "atmosphere" / "contents" / TITLE_ID)

    def test_incomplete_romfs(self) -> None:
        options = {"romfs_folder": str(self.romfs)}
        result = checklist.check("", options)  # region_common/event/PlayerStart.bfevfl alone
        self.assertEqual(result.checks["romfs_folder"].level, checklist.ERROR)
        self.assertIn("The RomFS dump is incomplete: it lacks region_common/audio/ and ",
                      result.checks["romfs_folder"].text)

        for folder in ("audio", "datasheets", "event", "level", "ui"):
            (self.romfs / "region_common" / folder).mkdir(exist_ok=True)
            (self.romfs / "region_common" / folder / "file").write_bytes(b"")
        for region, language in (("regionCN", "CNch"), ("regionEU", "EUen"), ("regionJP", "JPja"), ("regionKR", "KRkr"),
                                 ("regionTW", "TWch"), ("regionUS", "USen")):
            (self.romfs / region / language / "message").mkdir(parents=True)
            for name in ("Hint", "Npc", "Place", "Scenario", "SubEvent", "System", "Talker", "Telephone", "UI"):
                (self.romfs / region / language / "message" / f"{name}.msbt").write_bytes(b"")
        (self.romfs / "regionCN" / "CNch" / "message" / "Place.msbt").unlink()
        result = checklist.check("", options)
        self.assertIn("it lacks regionCN/CNch/message/Place.msbt and ", result.checks["romfs_folder"].text)

        (self.romfs / "regionCN" / "CNch" / "message" / "Place.msbt").write_bytes(b"")
        for source in install._romfs_sources:  # filled by the first check
            (self.romfs / source).parent.mkdir(parents=True, exist_ok=True)
            (self.romfs / source).write_bytes(b"not 1.0.0")
        result = checklist.check("", options)
        self.assertIn("The RomFS dump is not of game version 1.0.0", result.checks["romfs_folder"].text)

    @unittest.skipUnless((ROMFS / "region_common").is_dir(), "RomFS dump not found (LASAP_ROMFS)")
    def test_romfs_1_0_0(self) -> None:
        self.assertEqual(checklist.check("", {"romfs_folder": str(ROMFS)}).checks["romfs_folder"].level, checklist.OK)

    def test_problems(self) -> None:
        unknown = self.root / "emu.exe"
        unknown.write_bytes(b"")
        with mock.patch.object(checklist, "builtin_patcher_available", return_value=False):
            result = checklist.check("", {"emulator": str(unknown), "romfs_folder": str(self.root)})
        self.assertEqual(result.checks["emulator"].level, checklist.ERROR)
        self.assertIn("SD card folder", result.checks["emulator"].text)
        self.assertEqual(result.checks["romfs_folder"].level, checklist.ERROR)
        self.assertEqual(result.checks["patcher_command"].level, checklist.ERROR)
        with mock.patch.object(checklist, "builtin_patcher_available", return_value=True):
            result = checklist.check("", {})
        self.assertEqual(result.checks["patcher_command"].level, checklist.OK)

        result = checklist.check("", {"emulator": str(self.ryujinx.parent)})
        self.assertIn("This is a folder", result.checks["emulator"].text)

        result = checklist.check("", {"sd_card_folder": str(self.root / "missing")})
        self.assertEqual(result.checks["emulator"].level, checklist.ERROR)

        eden = self.root / "eden" / "eden.exe"
        (eden.parent / "user").mkdir(parents=True)
        eden.write_bytes(b"")
        result = checklist.check("", {"emulator": str(eden)})
        self.assertIn("Start Eden once", result.checks["emulator"].text)
        self.assertIn("Dump RomFS", result.checks["romfs_folder"].text)
