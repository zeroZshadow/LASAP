import tempfile
import unittest
from unittest import mock
from pathlib import Path

from . import romfs
from .. import checklist, install
from ..emulators import TITLE_ID

ROMFS = romfs()


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

    def test_platform(self) -> None:
        """Without the platform setting, an emulator means an emulator and an SD card with the address of another
        device a Switch; otherwise the player still has to choose."""
        self.assertEqual(checklist.platform_of({}), "")
        self.assertEqual(checklist.platform_of({"emulator": "Ryujinx.exe"}), checklist.EMULATOR)
        self.assertEqual(checklist.platform_of({"sd_card_folder": "E:/", "game_host": "192.168.1.42"}),
                         checklist.SWITCH)
        self.assertEqual(checklist.platform_of({"sd_card_folder": "E:/", "game_host": "127.0.0.1"}), "")
        self.assertEqual(checklist.platform_of({"platform": "switch", "emulator": "Ryujinx.exe"}), checklist.SWITCH)
        result = checklist.check("", {})
        self.assertEqual(result.checks["platform"].level, checklist.TODO)
        self.assertIn("platform", result.required)

    def test_switch(self) -> None:
        """A Switch needs its SD card, with Atmosphère, and its address; the emulator setting is not used."""
        sd = self.root / "sd"
        sd.mkdir()
        options = {"platform": "switch", "emulator": str(self.ryujinx), "sd_card_folder": str(sd),
                   "game_host": "127.0.0.1", "romfs_folder": str(self.romfs)}
        with mock.patch.object(checklist, "romfs_problem", return_value=None), \
                mock.patch.object(checklist, "builtin_patcher_available", return_value=True):
            result = checklist.check("", options)
            self.assertEqual(result.checks["sd_card_folder"].level, checklist.TODO)
            self.assertIn("atmosphere", result.checks["sd_card_folder"].text)
            self.assertEqual(result.checks["game_host"].level, checklist.TODO)
            self.assertEqual(result.target.mods, sd / "atmosphere" / "contents" / TITLE_ID)
            self.assertNotIn("emulator", result.required)

            (sd / "atmosphere").mkdir()
            result = checklist.check("", dict(options, game_host="192.168.1.42"))
            self.assertEqual(result.checks["sd_card_folder"].level, checklist.OK)
            self.assertEqual(result.checks["game_host"].level, checklist.OK)
            self.assertEqual(result.problems(), result.checks["patch_file"].text)
            self.assertIn("Install to SD card", result.checks["installed"].text)

            self.assertTrue(result.steps_done()[3])

            # the card taken out to play: not ready to install, but the setup stays done
            result = checklist.check("", dict(options, sd_card_folder=str(self.root / "out"), game_host="192.168.1.42"))
            self.assertEqual(result.checks["sd_card_folder"].level, checklist.TODO)
            self.assertTrue(result.sd_card_out)
            self.assertTrue(result.steps_done()[3])
            self.assertFalse(result.ready)
            self.assertIn("nxdumptool", checklist.check("", {"platform": "switch"}).checks["romfs_folder"].text)
