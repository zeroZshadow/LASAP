"""The Jukebox page's track list (music_tracks.hpp) and the Ballad's looping stream (patcher/ap_ballad.py)."""
import ast
import os
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ROMFS = Path(os.environ.get("LASAP_ROMFS", r"D:\Games\Switch\LASRomFS"))
ENSEMBLE = ROMFS / "region_common" / "audio" / "stream" / "BGM_ENSEMBLE_INST.bfstm"
PATCHER_MUSIC = ROOT / "patcher" / "RandomizerCore" / "Randomizers" / "music.py"
TRACK_NAMES = ROOT / "las-ap-exl" / "source" / "program" / "music" / "music_tracks.hpp"
# tracks the Jukebox plays that the music randomizer does not shuffle, after the patcher's
JUKEBOX_ONLY = ("BGM_ENSEMBLE_INST",)
ENTRY = re.compile(r'\{\s*"(BGM_\w+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\d+)\s*\}')


def patcher_tracks() -> tuple:
    """The looping BGM tracks, as the patcher's music randomizer shuffles them (BGM_TRACKS)."""
    tree = ast.parse(PATCHER_MUSIC.read_text(encoding="utf-8"))
    return next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                and any(getattr(t, "id", None) == "BGM_TRACKS" for t in node.targets))


def c_string(text: str) -> str:
    return text.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")


class TestTracks(unittest.TestCase):
    def check_text(self, name: str, info: str) -> None:
        self.assertTrue(name.isascii() and info.isascii(), name)
        self.assertLessEqual(len(name), 24, name)
        self.assertLessEqual(len(info.replace("\n", "")), 80, name)
        lines = info.split("\n")
        self.assertLessEqual(len(lines), 2, name)
        if len(lines) > 1:
            self.assertTrue(all(0 < len(line) <= 40 for line in lines), name)

    def test_track_names(self) -> None:
        source = TRACK_NAMES.read_text(encoding="utf-8")
        entries = [(i, c_string(n), c_string(t)) for i, n, t, _ in ENTRY.findall(source)]
        # each track's play-through (scripts/track_lengths.py): at least a few seconds, at most ten minutes
        self.assertTrue(all(3000 <= int(ms) <= 600000 for *_, ms in ENTRY.findall(source)))
        self.assertEqual(tuple(i for i, _, _ in entries), patcher_tracks() + JUKEBOX_ONLY)
        names = [n for _, n, _ in entries]
        self.assertEqual(len(names), len(set(names)))
        for _, name, info in entries:
            self.check_text(name, info)


@unittest.skipUnless(ENSEMBLE.exists(), "RomFS dump not found (LASAP_ROMFS)")
class TestBalladLoop(unittest.TestCase):
    """The Jukebox's copy of the ensemble loops from its start to the loop end and holds the original's samples."""

    @classmethod
    def setUpClass(cls) -> None:
        saved = list(sys.path)
        sys.path.insert(0, str(ROOT / "patcher"))
        try:
            import ap_ballad
        finally:
            sys.path[:] = saved
        cls.ap_ballad = ap_ballad
        cls.source = ENSEMBLE.read_bytes()
        cls.looped = ap_ballad.loop_stream(cls.source, ap_ballad.LOOP_END)

    @classmethod
    def tearDownClass(cls) -> None:
        sys.modules.pop("ap_ballad", None)

    @staticmethod
    def sections(data: bytes) -> dict:
        found = {}
        for i in range(struct.unpack_from("<H", data, 0x10)[0]):
            kind, offset, size = struct.unpack_from("<HxxiI", data, 0x14 + 12 * i)
            found[kind] = (offset, size)
        return found

    def test_name(self) -> None:
        # the module swaps the path in place
        self.assertEqual(len(self.ap_ballad.LOOPED), len(self.ap_ballad.SOURCE))

    def test_header(self) -> None:
        data = self.looped
        self.assertEqual(struct.unpack_from("<I", data, 0xc)[0], len(data))
        sections = self.sections(data)
        self.assertEqual(sections[0x4002][0] + sections[0x4002][1], len(data))
        stream = 0x48 + 0x18
        loop, = struct.unpack_from("<B", data, stream + 1)
        start, end, blocks = struct.unpack_from("<III", data, stream + 8)
        self.assertEqual((loop, start, end), (1, 0, self.ap_ballad.LOOP_END))
        self.assertEqual(blocks, (end + 14335) // 14336)

    def test_samples(self) -> None:
        """Every block's ADPCM bytes are the original's, the last one's as far as its samples reach."""
        old, new = self.source, self.looped
        old_data = self.sections(old)[0x4002][0] + 0x20
        new_data = self.sections(new)[0x4002][0] + 0x20
        stream = 0x48 + 0x18
        blocks, size = struct.unpack_from("<II", new, stream + 0x10)
        last_size, _, last_padded = struct.unpack_from("<III", new, stream + 0x1c)
        for b in range(blocks):
            for c in range(8):
                length = size if b < blocks - 1 else last_size
                at_new = new_data + b * 8 * size + c * (size if b < blocks - 1 else last_padded)
                at_old = old_data + (b * 8 + c) * size
                self.assertEqual(new[at_new:at_new + length], old[at_old:at_old + length], (b, c))
