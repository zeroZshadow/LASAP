"""The jukebox: /music's track names and the client's MUSIC lines."""
import ast
import asyncio
import unittest
from pathlib import Path

from .. import music

PATCHER_MUSIC = Path(__file__).resolve().parents[3] / "patcher" / "RandomizerCore" / "Randomizers" / "music.py"


class TestTracks(unittest.TestCase):
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
