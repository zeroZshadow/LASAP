"""The Jukebox page's track list (music_tracks.hpp)."""
import ast
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATCHER_MUSIC = ROOT / "patcher" / "RandomizerCore" / "Randomizers" / "music.py"
TRACK_NAMES = ROOT / "las-ap-exl" / "source" / "program" / "music" / "music_tracks.hpp"
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
        self.assertEqual(tuple(i for i, _, _ in entries), patcher_tracks())
        names = [n for _, n, _ in entries]
        self.assertEqual(len(names), len(set(names)))
        for _, name, info in entries:
            self.check_text(name, info)
