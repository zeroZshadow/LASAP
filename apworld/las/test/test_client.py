"""The client's handling of game lines that reach the server, and of the game's connection."""
import asyncio
import socket
import unittest
from unittest import mock

from NetUtils import HintStatus
from Utils import Version


class TestHintLines(unittest.TestCase):
    def run_lines(self, server_version: Version, lines) -> list:
        from ..client import LASContext

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            sent = []

            async def send_msgs(msgs):
                sent.extend(msgs)
            ctx.send_msgs = send_msgs
            ctx.server = object()
            ctx.slot = 1
            ctx.server_version = server_version
            for line in lines:
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
            return sent
        return asyncio.run(run())

    def test_create_hints(self) -> None:
        sent = self.run_lines(Version(0, 6, 8), ["HINT 2 1279393921", "HINT 1 1279393846", "HINT 2 1279393921"])
        self.assertEqual(sent, [
            {"cmd": "CreateHints", "player": 2, "locations": [1279393921], "status": HintStatus.HINT_PRIORITY},
            {"cmd": "CreateHints", "player": 1, "locations": [1279393846], "status": HintStatus.HINT_PRIORITY},
        ])

    def test_old_server_scouts_own_locations(self) -> None:
        sent = self.run_lines(Version(0, 6, 1), ["HINT 2 1279393921", "HINT 1 1279393846"])
        self.assertEqual(sent, [{"cmd": "LocationScouts", "locations": [1279393846], "create_as_hint": 2}])


class TestChecksBySeed(unittest.TestCase):
    def test_checks_wait_for_the_slot_of_the_game_seed(self) -> None:
        """Checks and the goal the game reports before the client knows the slot go only to the slot of the seed the
        game runs; a new game connection forgets those of the last one."""
        from ..client import LASContext, LOCATION_BASE, PROTOCOL_VERSION

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            sent = []

            async def send_msgs(msgs):
                sent.extend(msgs)
            ctx.send_msgs = send_msgs
            ctx.server = object()
            ctx.slot = 1
            ctx.missing_locations = {LOCATION_BASE + 5, LOCATION_BASE + 6}
            for line in (f"HELLO {PROTOCOL_VERSION} aaaa", "CHECKS 5 6", "GOAL"):
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
            ctx.slot_seed_hash = "bbbb"  # logged into the slot of another seed
            await ctx.flush_checks()
            sent.append("slot aaaa")
            ctx.slot_seed_hash = "aaaa"
            await ctx.flush_checks()
            sent.append("game bbbb")
            ctx.handle_game_line(f"HELLO {PROTOCOL_VERSION} bbbb")
            ctx.slot_seed_hash = "bbbb"
            await ctx.flush_checks()
            return sent
        sent = asyncio.run(run())
        self.assertEqual([m if isinstance(m, str) else m["cmd"] for m in sent],
                         ["slot aaaa", "LocationChecks", "StatusUpdate", "game bbbb"])
        self.assertEqual(sorted(sent[1]["locations"]), [LOCATION_BASE + 5, LOCATION_BASE + 6])


class TestGhostNamesFromGame(unittest.TestCase):
    def test_dpad_toggle_keeps_the_setting(self) -> None:
        """GHOSTNAMES from the game (the d-pad's up) sets the client's ghost_names, as /ghostnames does."""
        from ..client import LASContext

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            with mock.patch.object(LASContext, "set_ghost_names") as set_names:
                ctx.handle_game_line("GHOSTNAMES 0")
                ctx.handle_game_line("GHOSTNAMES 1")
            return set_names.call_args_list
        self.assertEqual(asyncio.run(run()), [mock.call(False), mock.call(True)])


class TestGameConnection(unittest.TestCase):
    def test_game_busy_with_another_client(self) -> None:
        """A port that takes the connection but sends no HELLO is a game serving another client: the client says so
        and starts no second emulator."""
        from .. import client
        from ..client import LASContext

        async def run() -> LASContext:
            with socket.socket() as busy:
                busy.bind(("127.0.0.1", 0))
                busy.listen(1)  # never accepts, as the game while it serves another client
                ctx = LASContext(None, None, "127.0.0.1", busy.getsockname()[1])
                loop = asyncio.create_task(ctx.game_loop())
                reports = []
                ctx.report = lambda level, message, replace=False: reports.append(message)
                with mock.patch.object(LASContext, "option", return_value="Ryujinx.exe"),                         mock.patch("subprocess.Popen", side_effect=AssertionError("started an emulator")):
                    await ctx.start_emulator()
                ctx.exit_event.set()
                loop.cancel()
            self.assertTrue(ctx.game_busy)
            self.assertEqual(ctx.game_status.text, client.BUSY_STATUS)
            self.assertTrue(any("Another client" in r for r in reports), reports)
            return ctx

        with mock.patch.object(client, "HELLO_TIMEOUT", 0.2):
            asyncio.run(run())


class TestBadGameLines(unittest.TestCase):
    def test_bad_lines_are_skipped(self) -> None:
        """Lines the client cannot handle (bad fields, a line past the reader's limit) are logged and skipped: the
        connection goes on and later lines are handled."""
        from ..client import LASContext, PROTOCOL_VERSION

        async def run() -> tuple:
            async def game(reader, writer):
                writer.write(f"HELLO {PROTOCOL_VERSION} aaaa\n".encode())
                writer.write(b"RECV x\nRECV\nCHECKS 5 y\nHINT a b\n" + b"x" * 100_000 + b"\nRECV 3\n")
                await writer.drain()
                await asyncio.sleep(5)

            server = await asyncio.start_server(game, "127.0.0.1", 0)
            ctx = LASContext(None, None, "127.0.0.1", server.sockets[0].getsockname()[1])
            ctx.send_game = lambda line: None
            loop = asyncio.create_task(ctx.game_loop())
            for _ in range(100):
                await asyncio.sleep(0.02)
                if ctx.game_recv == 3:
                    break
            result = (ctx.game_recv, loop.done(), ctx.game_writer is not None)
            ctx.exit_event.set()
            loop.cancel()
            server.close()
            return result

        with self.assertLogs("Client", level="WARNING") as logs:
            self.assertEqual(asyncio.run(run()), (3, False, True))
        self.assertTrue(any("Ignored a line from the game: RECV x" in line for line in logs.output), logs.output)


class TestDetection(unittest.TestCase):
    def test_play_finds_a_dump_made_after_start(self) -> None:
        """A yuzu-based emulator's RomFS dump made while the client runs is found when Play checks the setup."""
        import tempfile
        from pathlib import Path
        from ..client import LASContext
        from ..emulators import TITLE_ID

        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "eden" / "eden.exe"
            (exe.parent / "user" / "sdmc").mkdir(parents=True)
            exe.write_bytes(b"")
            options = {"emulator": str(exe)}

            async def run() -> None:
                ctx = LASContext(None, None, "127.0.0.1", 0)
                with mock.patch.object(LASContext, "option", side_effect=lambda key: options.get(key, "")), \
                        mock.patch.object(LASContext, "set_option",
                                          side_effect=lambda key, value, detect=True: options.update({key: value})):
                    ctx.report = lambda *args, **kwargs: None
                    await ctx.install_patch()
                    self.assertNotIn("romfs_folder", options)
                    dump = exe.parent / "user" / "dump" / TITLE_ID / "romfs" / "region_common" / "event"
                    dump.mkdir(parents=True)
                    (dump / "PlayerStart.bfevfl").write_bytes(b"")
                    await ctx.install_patch()
                    self.assertEqual(options["romfs_folder"], str(dump.parent.parent))
            asyncio.run(run())
