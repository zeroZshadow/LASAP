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
    """The client's MUSIC lines to the game, and the game's MUSIC lines: its report on connecting and a choice made
    in the game."""

    def run_lines(self, steps) -> list:
        from ..client import LASContext

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            sent = []
            ctx.send_game = sent.append
            steps(ctx)
            await asyncio.sleep(0)
            return [line for line in sent if line.startswith("MUSIC")]
        return asyncio.run(run())

    def test_client_choice(self) -> None:
        def steps(ctx) -> None:
            ctx.set_music("BGM_FAIRY")
            ctx.set_music("BGM_FAIRY")
            ctx.set_music(None)
            ctx.set_music("BGM_NOPE")
            ctx.handle_game_line("LOG music: unknown track BGM_NOPE")
            self.assertIsNone(ctx.music)
        self.assertEqual(self.run_lines(steps), ["MUSIC BGM_FAIRY", "MUSIC off", "MUSIC BGM_NOPE"])

    def test_game_choice(self) -> None:
        def steps(ctx) -> None:
            ctx.handle_game_line("HELLO 2 abc")
            ctx.handle_game_line("MUSIC off")
            ctx.handle_game_line("MUSIC BGM_FAIRY")  # chosen in the game: not sent back
            self.assertEqual(ctx.music, "BGM_FAIRY")
            ctx.set_music(None)  # /music off
            ctx.handle_game_line("MUSIC BGM_HOUSE")
            ctx.handle_game_line("MUSIC off")
            self.assertIsNone(ctx.music)
            ctx.handle_game_line("MUSIC music=- zonebgm=BGM_MEVE bgm=-")  # DBG music's reply: not a choice
            self.assertIsNone(ctx.music)
        self.assertEqual(self.run_lines(steps), ["MUSIC off"])

    def test_reconnect(self) -> None:
        def steps(ctx) -> None:
            # /music while the game was off: the game reports none, the client sends its choice.
            ctx.set_music("BGM_FAIRY")
            ctx.handle_game_line("HELLO 2 abc")
            ctx.handle_game_line("MUSIC off")
            self.assertEqual(ctx.music, "BGM_FAIRY")
            # The game reports a track (chosen while the client was away): the client takes it and sends nothing.
            ctx.handle_game_line("HELLO 2 abc")
            ctx.handle_game_line("MUSIC BGM_HOUSE")
            self.assertEqual(ctx.music, "BGM_HOUSE")
            # Both have none.
            ctx.set_music(None)
            ctx.handle_game_line("HELLO 2 abc")
            ctx.handle_game_line("MUSIC off")
            # /music after HELLO, before the report: the client's choice is newer and the report is ignored.
            ctx.handle_game_line("HELLO 2 abc")
            ctx.set_music("BGM_TAL_TAL")
            ctx.handle_game_line("MUSIC BGM_HOUSE")
            self.assertEqual(ctx.music, "BGM_TAL_TAL")
            # After the report, MUSIC off is a choice made in the game.
            ctx.handle_game_line("MUSIC off")
            self.assertIsNone(ctx.music)
        self.assertEqual(self.run_lines(steps), ["MUSIC BGM_FAIRY", "MUSIC BGM_FAIRY", "MUSIC off",
                                                 "MUSIC BGM_TAL_TAL"])
