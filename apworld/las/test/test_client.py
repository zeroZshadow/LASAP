"""The client's handling of game lines that reach the server, and of the game's connection."""
import asyncio
import socket
import unittest
from unittest import mock

from NetUtils import HintStatus

from . import client_context
from ..items import ITEM_TABLE, LASR_TO_ITEM
from ..lasr import DUNGEONS, LOGIC

SWAP = {**{d: d for d in DUNGEONS}, "tail-cave": "turtle-rock", "turtle-rock": "tail-cave"}


class TestTrackerVersion(unittest.TestCase):
    def test_versions(self) -> None:
        from ..client import TRACKER_MIN_VERSION, tracker_version
        self.assertEqual(tracker_version("v0.3.8"), (0, 3, 8))
        self.assertEqual(tracker_version("v0.2.24.1"), (0, 2, 24))
        self.assertEqual(tracker_version("v0.4.0-beta2"), (0, 4, 0))
        self.assertIsNone(tracker_version(None))
        self.assertIsNone(tracker_version("tracker"))
        self.assertLess(tracker_version("v0.3.7"), TRACKER_MIN_VERSION)
        self.assertGreaterEqual(tracker_version("v0.3.10"), TRACKER_MIN_VERSION)


class TestTrackerItems(unittest.TestCase):
    def test_starting_items_reach_universal_tracker(self) -> None:
        """Universal Tracker takes the slot's starting items, which the server sends only at items_handling 0b111,
        before the items received."""
        from .. import client
        if not client.TRACKER_LOADED:
            self.skipTest("Universal Tracker not installed")
        from NetUtils import NetworkItem

        async def run() -> list:
            ctx, _, _ = client_context(
                slot_data={"logic": {"starting": ["Stone Beak (Face Shrine)", "Compass (Face Shrine)"]}},
                items_received=[NetworkItem(ITEM_TABLE["Hookshot"].code, 1234, 1, 1)])
            return [item.item for item in ctx.tracker_items_received]

        self.assertEqual(asyncio.run(run()), [ITEM_TABLE[name].code for name in
                                              ("Stone Beak (Face Shrine)", "Compass (Face Shrine)", "Hookshot")])


class TestHintLines(unittest.TestCase):
    def test_create_hints(self) -> None:
        async def run() -> list:
            ctx, sent, _ = client_context(server=object(), slot=1)
            for line in ("HINT 2 1279393921", "HINT 1 1279393846", "HINT 2 1279393921"):
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
            return sent
        self.assertEqual(asyncio.run(run()), [
            {"cmd": "CreateHints", "player": 2, "locations": [1279393921], "status": HintStatus.HINT_PRIORITY},
            {"cmd": "CreateHints", "player": 1, "locations": [1279393846], "status": HintStatus.HINT_PRIORITY},
        ])


class TestChecksBySeed(unittest.TestCase):
    def test_checks_wait_for_the_slot_of_the_game_seed(self) -> None:
        """Checks and the goal the game reports before the client knows the slot go only to the slot of the seed the
        game runs; a new game connection forgets those of the last one."""
        from ..client import LOCATION_BASE, PROTOCOL_VERSION

        async def run() -> list:
            ctx, sent, _ = client_context(server=object(), slot=1,
                                          missing_locations={LOCATION_BASE + 5, LOCATION_BASE + 6})
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


class TestDoorLines(unittest.TestCase):
    def test_doors_gone_through_go_to_data_storage(self) -> None:
        """A DOOR line finds a shuffled door and the inside door it leads to, which go to data storage once, and only
        for the slot of the seed the game runs; other jumps find nothing. The last line's spot is the route's start,
        until the game says HELLO again."""
        from ..client import PROTOCOL_VERSION
        from ..lasr import DOORS, ENTRANCE_POOL

        door, other = ENTRANCE_POOL[:2]
        inside = DOORS["pairs"][other]

        async def run() -> list:
            ctx, sent, _ = client_context(server=object(), slot=1, team=0, slot_seed_hash="aaaa")
            for line in (f"HELLO {PROTOCOL_VERSION} bbbb", f"DOOR {DOORS['pairs'][door]} {inside}",
                         f"HELLO {PROTOCOL_VERSION} aaaa", f"DOOR {DOORS['pairs'][door]} {inside}",
                         f"DOOR {other} {door}", "DOOR Lv01TailCave_08D Lv01TailCave_08D"):
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
            exits = [ctx.game_exit]
            ctx.handle_game_line(f"HELLO {PROTOCOL_VERSION} aaaa")
            return sent, exits + [ctx.game_exit]
        sent, exits = asyncio.run(run())
        self.assertEqual(exits, ["Lv01TailCave_08D", None])
        self.assertEqual(sent, [{"cmd": "Set", "key": "las_entrances_0_1", "default": {},
                                               "want_reply": False,
                                               "operations": [{"operation": "update", "value": {door: inside}}]}])


class TestDungeonDoors(unittest.TestCase):
    def test_found_doors(self) -> None:
        """The client's found doors: those leading into the dungeons the game reports entered and those data storage
        has; every door with dungeons not shuffled."""
        from ..client import PROTOCOL_VERSION

        async def run() -> list:
            ctx, _, _ = client_context(slot=1, team=0,
                                       slot_data={"dungeon_entrances": SWAP, "options": {"shuffled_dungeons": 1}})
            found = [ctx.found_doors]
            ctx.handle_game_line(f"HELLO {PROTOCOL_VERSION} aaaa")
            ctx.handle_game_line("DUNGEON 8")
            found.append(ctx.found_doors)
            ctx.on_package("Retrieved", {"keys": {"las_sign_0_1": None, "las_doors_0_1": {"key-cavern": "key-cavern"}}})
            found.append(ctx.found_doors)
            ctx.slot_data = {"dungeon_entrances": SWAP, "options": {"shuffled_dungeons": 0}}
            found.append(ctx.found_doors)
            await asyncio.sleep(0)
            return found
        self.assertEqual(asyncio.run(run()), [set(), {"tail-cave"}, {"tail-cave", "key-cavern"}, None])

    def test_sends_doors(self) -> None:
        """The client sends the locations behind reachable doors (DOORS) before those in logic (INLOGIC)."""
        from ..ids import LOCATION_BASE, LOCATION_OFFSETS

        d1 = [k for k in LOGIC if k.startswith("D1-") and LOGIC[k]["type"] == "item"]
        d8 = [k for k in LOGIC if k.startswith("D8-") and LOGIC[k]["type"] == "item"]
        tokens = {"open-mabe": True, "free-book": True, "shuffled-bombs": True, "can-steal": True}

        async def run() -> list:
            ctx, _, lines = client_context(
                game=True, slot=1, team=0, server_locations={LOCATION_BASE + LOCATION_OFFSETS[loc] for loc in d1 + d8},
                slot_data={"dungeon_entrances": SWAP, "logic": {
                    "preset": "basic", "tokens": tokens, "vanilla": [], "optional": [],
                    "starting": [LASR_TO_ITEM["tail-key"]]}})
            ctx.make_logic_tracker()
            ctx.send_logic()
            ctx.send_logic()  # nothing changed: nothing sent
            return lines
        doors = " ".join(str(local_id) for local_id in sorted(LOCATION_OFFSETS[loc] for loc in d8))
        self.assertEqual(asyncio.run(run()), ["DOORS CLEAR", f"DOORS {doors}", "INLOGIC CLEAR"])

    def test_publishes_found_doors(self) -> None:
        """A dungeon the game reports entered (DUNGEON) finds the door that leads into it: the client adds it to
        las_doors_<team>_<slot> once per connection, for the slot of the seed the game runs only. Turtle Rock is behind
        Tail Cave's door."""
        from ..client import PROTOCOL_VERSION

        async def run() -> list:
            ctx, sent, _ = client_context(server=object(), slot=1, team=0, slot_data={"dungeon_entrances": SWAP},
                                          slot_seed_hash="aaaa")
            # another seed's game: not its doors; then the game of the slot's seed, SYNC sending its dungeons again
            for line in (f"HELLO {PROTOCOL_VERSION} bbbb", "DUNGEON 8", f"HELLO {PROTOCOL_VERSION} aaaa",
                         "DUNGEON 8", "DUNGEON 3", "DUNGEON 8", "DUNGEON 3", "DUNGEON 9"):
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
            sent.append("reconnected")
            ctx.doors_sent.clear()  # a new connection to the server sends them again
            await ctx.send_doors()
            return [msg if isinstance(msg, str) else msg["operations"] for msg in sent
                    if isinstance(msg, str) or (msg["cmd"] == "Set" and msg["key"] == "las_doors_0_1")]
        self.assertEqual(asyncio.run(run()), [
            [{"operation": "update", "value": {"tail-cave": "turtle-rock"}}],
            [{"operation": "update", "value": {"key-cavern": "key-cavern"}}],
            "reconnected",
            [{"operation": "update", "value": {"tail-cave": "turtle-rock", "key-cavern": "key-cavern"}}],
        ])


class TestGhostNamesFromGame(unittest.TestCase):
    def test_dpad_toggle_keeps_the_setting(self) -> None:
        """GHOSTNAMES from the game (the d-pad's up) sets the client's ghost_names, as /ghostnames does."""
        from ..client import LASContext

        async def run() -> list:
            ctx, _, _ = client_context()
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
                with mock.patch.object(LASContext, "option", return_value="Ryujinx.exe"), \
                        mock.patch("subprocess.Popen", side_effect=AssertionError("started an emulator")):
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
                ctx, _, _ = client_context(report=lambda *args, **kwargs: None)
                with mock.patch.object(LASContext, "option", side_effect=lambda key: options.get(key, "")), \
                        mock.patch.object(LASContext, "set_option",
                                          side_effect=lambda key, value, detect=True: options.update({key: value})):
                    await ctx.install_patch()
                    self.assertNotIn("romfs_folder", options)
                    dump = exe.parent / "user" / "dump" / TITLE_ID / "romfs" / "region_common" / "event"
                    dump.mkdir(parents=True)
                    (dump / "PlayerStart.bfevfl").write_bytes(b"")
                    await ctx.install_patch()
                    self.assertEqual(options["romfs_folder"], str(dump.parent.parent))
            asyncio.run(run())


class TestSwitchInstall(unittest.TestCase):
    def test_play_installs_on_the_sd_card(self) -> None:
        """For a Switch, Play copies the mod to its SD card and starts no emulator, even with one set; the install
        goes through its steps and says what to do next."""
        import tempfile
        from pathlib import Path
        from .. import client, checklist
        from ..client import LASContext

        with tempfile.TemporaryDirectory() as tmp:
            sd = Path(tmp) / "sd"
            (sd / "atmosphere").mkdir(parents=True)
            options = {"platform": "switch", "emulator": "Ryujinx.exe", "sd_card_folder": str(sd),
                       "game_host": "192.168.1.42"}
            ready = checklist.Checklist(platform=checklist.SWITCH, target=client.emulators.target("", str(sd), ""))
            ready.checks.update({key: checklist.Check(checklist.OK, "") for key in ready.required})
            ready.checks["installed"] = checklist.Check(checklist.INFO, "No mod installed yet.")
            steps = []

            async def run() -> LASContext:
                ctx, _, _ = client_context()

                def install(*args) -> None:
                    steps.append(list(ctx.install_steps))
                    args[-1]()  # patched: copying

                with mock.patch.object(LASContext, "option", side_effect=lambda key: options.get(key, "")), \
                        mock.patch.object(LASContext, "check_setup", return_value=ready), \
                        mock.patch.object(client, "patch_and_install", side_effect=install), \
                        mock.patch("subprocess.Popen", side_effect=AssertionError("started an emulator")):
                    ctx.patch_file = str(Path(tmp) / "x.aplas")
                    await ctx.install_patch(start=True)
                    await asyncio.sleep(0)
                    self.assertIn("Put the SD card back in the Switch", ctx.last_report.text)
                    # pressed again with this seed's mod on the card: nothing to copy, and it says so
                    ready.checks["installed"] = checklist.Check(checklist.OK, "")
                    await ctx.install_patch(start=True)
                return ctx

            ctx = asyncio.run(run())
            self.assertEqual(steps, [["Patch the game", f"Copy the mod to {sd}"]])
            self.assertFalse(ctx.patching)
            self.assertIn("installed already", ctx.last_report.text)
