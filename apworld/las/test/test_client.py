"""The client's handling of game lines that reach the server."""
import asyncio
import unittest

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
