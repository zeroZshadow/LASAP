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
