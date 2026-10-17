"""The jukebox: /music's track names and the client's MUSIC lines."""
import ast
import asyncio
import re
import unittest
from pathlib import Path

from .. import music

ROOT = Path(__file__).resolve().parents[3]
PATCHER_MUSIC = ROOT / "patcher" / "RandomizerCore" / "Randomizers" / "music.py"
TRACK_NAMES = ROOT / "las-ap-exl" / "source" / "program" / "AP" / "ap_tracks.hpp"
ENTRY = re.compile(r'\{\s*"(BGM_\w+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
GAME_MUSIC = re.compile(r'GAME_MUSIC\s*=\s*\{\s*nullptr\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')


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
        entries = [(i, c_string(n), c_string(t)) for i, n, t in ENTRY.findall(source)]
        self.assertEqual(tuple(i for i, _, _ in entries), music.TRACKS)
        game_music = GAME_MUSIC.search(source)
        self.assertIsNotNone(game_music)
        names = [n for _, n, _ in entries] + [c_string(game_music.group(1))]
        self.assertEqual(len(names), len(set(names)))
        for _, name, info in entries:
            self.check_text(name, info)
        self.check_text(c_string(game_music.group(1)), c_string(game_music.group(2)))

    def test_tracks_match_patcher(self) -> None:
        tree = ast.parse(PATCHER_MUSIC.read_text(encoding="utf-8"))
        tracks = next(ast.literal_eval(node.value) for node in tree.body if isinstance(node, ast.Assign)
                      and any(getattr(t, "id", None) == "BGM_TRACKS" for t in node.targets))
        self.assertEqual(music.TRACKS, tracks)

    def test_resolve(self) -> None:
        self.assertEqual(music.resolve("BGM_FAIRY")[0], "BGM_FAIRY")
        self.assertEqual(music.resolve("fairy")[0], "BGM_FAIRY")
        self.assertEqual(music.resolve("field_normal")[0], "BGM_FIELD_NORMAL")  # also part of FIELD_NORMAL_INTRO
        self.assertEqual(music.resolve("tail")[0], "BGM_DUNGEON_LV1_TAIL")
        chosen, matches = music.resolve("lv7")
        self.assertIsNone(chosen)
        self.assertEqual(matches, ["BGM_DUNGEON_LV7_TOWER", "BGM_DUNGEON_LV7_BOSS"])
        self.assertEqual(music.resolve("bgm_marine_sing")[0], "BGM_MARINE_SING")  # not shuffled: as typed
        self.assertEqual(music.resolve("nothing"), (None, []))


class TestMusicLines(unittest.TestCase):
    def test_lines(self) -> None:
        from ..client import LASContext

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            sent = []

            def capture(line: str) -> None:
                sent.append(line)
            ctx.send_game = capture
            ctx.set_music("BGM_FAIRY")
            ctx.set_music("BGM_FAIRY")
            ctx.handle_game_line("HELLO 2 abc")
            ctx.set_music(None)
            ctx.handle_game_line("HELLO 2 abc")
            ctx.set_music("BGM_NOPE")
            ctx.handle_game_line("LOG music: unknown track BGM_NOPE")
            self.assertIsNone(ctx.music)
            await asyncio.sleep(0)
            return [line for line in sent if line.startswith("MUSIC")]
        self.assertEqual(asyncio.run(run()), ["MUSIC BGM_FAIRY", "MUSIC BGM_FAIRY", "MUSIC off", "MUSIC off",
                                              "MUSIC BGM_NOPE"])
