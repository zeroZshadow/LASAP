"""A count of instruments or seashells left to chance stays hidden in the trackers until the player reads the sign at
the egg's stairs (DESIGN.md, Goal): the option knows whether the YAML picked the number, the slot data carries it, and
the client and Universal Tracker take every instrument or seashell until the sign is read."""
import argparse
import asyncio
import glob
import os
import tempfile
import unittest

import yaml

from Generate import handle_option, roll_settings
from BaseClasses import PlandoOptions
from Options import generate_yaml_templates
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister

from . import test_goal
from .test_logic import STEPS
from .test_universal_tracker import regenerate
from ..items import GAME
from ..lasr import INSTRUMENTS
from ..options import InstrumentsRequired, SeashellsRequired
from ..tracker import SIGN_KEY

COUNTS = {"instruments_required": InstrumentsRequired, "seashells_required": SeashellsRequired}


class TestKnown(unittest.TestCase):
    def test_known(self) -> None:
        for option_type in COUNTS.values():
            for value, number in ((3, 3), ("3", 3), (0, 0), ("high", option_type.range_end), ("low", 0),
                                  ("default", option_type.default)):
                with self.subTest(option=option_type.__name__, value=value):
                    option = option_type.from_any(value)
                    self.assertEqual(option.value, number)
                    self.assertTrue(option.known)

    def test_hidden(self) -> None:
        for option_type in COUNTS.values():
            every = range(option_type.range_end + 1)
            for value, numbers in (("random", every), ("random-low", every), ("random-high", every),
                                   ("random-range-2-6", range(2, 7)), ("random-range-low-2-6", range(2, 7))):
                with self.subTest(option=option_type.__name__, value=value):
                    option = option_type.from_any(value)
                    self.assertIn(option.value, numbers)
                    self.assertFalse(option.known)

    def test_options_creator_offers_random(self) -> None:
        """The Options Creator shows its Random toggle only for options that support weighting."""
        for option_type in COUNTS.values():
            self.assertTrue(option_type.supports_weighting)

    def test_generate_weights_first(self) -> None:
        """Generate weights the YAML value before from_any: a "random..." pick is hidden, a number picked by weight
        is known."""
        for name, option_type in COUNTS.items():
            for weights, known in (({}, True), ({name: 5}, True), ({name: {4: 50, 8: 50}}, True),
                                   ({name: "random"}, False), ({name: {"random-range-3-5": 10}}, False)):
                with self.subTest(weights=weights):
                    ret = argparse.Namespace(game=GAME, name="Tester")
                    handle_option(ret, weights, name, option_type, PlandoOptions.bosses)
                    self.assertEqual(getattr(ret, name).known, known)

    def test_template(self) -> None:
        """The template YAML weights the default alone, so its number is known."""
        with tempfile.TemporaryDirectory() as folder:
            generate_yaml_templates(folder)
            path, = [p for p in glob.glob(os.path.join(folder, "*.yaml")) if "Links Awakening Switch" in p]
            with open(path, encoding="utf-8-sig") as f:
                weights = yaml.safe_load(f)
        settings = roll_settings(weights)
        for name, option_type in COUNTS.items():
            self.assertEqual(getattr(settings, name).value, option_type.default)
            self.assertTrue(getattr(settings, name).known)


class TestSlotData(unittest.TestCase):
    def test_slot_data(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        for value, known in ((3, True), ("random-range-2-6", False)):
            with self.subTest(value=value):
                world = setup_multiworld(world_type, STEPS, 70, {"instruments_required": value}).worlds[1]
                slot_data = world.fill_slot_data()
                self.assertEqual(slot_data["instruments_known"], known)
                self.assertTrue(slot_data["seashells_known"])
                self.assertEqual(world.goal_instruments, slot_data["instruments_required"])

    def test_seashell_hunt(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        for value, known in ((12, True), ("random-range-5-20", False)):
            with self.subTest(value=value):
                world = setup_multiworld(world_type, STEPS, 71, {"goal": "seashell_hunt", "instruments_required":
                                                                 "random", "seashells_required": value}).worlds[1]
                slot_data = world.fill_slot_data()
                self.assertEqual(slot_data["seashells_known"], known)
                self.assertTrue(slot_data["instruments_known"])
                self.assertEqual(world.goal_seashells, slot_data["seashells_required"])


class TestClient(unittest.TestCase):
    SLOT_DATA = {"seed_hash": "abc", "instruments_required": 3, "instruments_known": False,
                 "logic": {"preset": "basic", "tokens": {}, "vanilla": [], "optional": [], "starting": []}}
    HUNT = {"seed_hash": "abc", "instruments_required": 0, "instruments_known": True, "seashells_required": 12,
            "seashells_known": False, "options": {"goal": 1},
            "logic": {"preset": "basic", "tokens": {}, "vanilla": [], "optional": [], "starting": []}}

    def run_lines(self, slot_data: dict, lines) -> tuple:
        from ..client import LASContext

        async def run():
            ctx = LASContext(None, None, "127.0.0.1", 0)
            sent = []

            async def send_msgs(msgs):
                sent.extend(msgs)
            ctx.send_msgs = send_msgs
            ctx.server = object()
            ctx.slot = 1
            ctx.team = 0
            seen = [ctx.logic_tracker_goal]
            ctx.on_package("Connected", {"slot_data": slot_data})
            seen.append(ctx.logic_tracker_goal)
            for line in lines:
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
                seen.append(ctx.logic_tracker_goal)
            gets = [msg for msg in sent if msg["cmd"] == "Get"]
            self.assertEqual(gets, [{"cmd": "Get", "keys": ["las_sign_0_1", "las_doors_0_1"]}])
            return [msg for msg in sent if msg["cmd"] == "Set" and msg["key"].startswith("las_sign")], seen
        return asyncio.run(run())

    def test_hidden_until_the_sign(self) -> None:
        sent, seen = self.run_lines(self.SLOT_DATA, ["HELLO 2 abc", "SIGN"])
        self.assertEqual([goal[0] for goal in seen], [8, 8, 8, 3])
        self.assertEqual(sent, [{"cmd": "Set", "key": SIGN_KEY.format(team=0, player=1), "default": False,
                                 "want_reply": False, "operations": [{"operation": "replace", "value": True}]}])
        self.assertEqual(sent[0]["key"], "las_sign_0_1")

    def test_seashells_hidden_until_the_sign(self) -> None:
        sent, seen = self.run_lines(self.HUNT, ["HELLO 2 abc", "SIGN"])
        self.assertEqual(seen, [(8, None), (0, 50), (0, 50), (0, 12)])
        self.assertEqual(len(sent), 1)
        _, seen = self.run_lines({**self.HUNT, "seashells_known": True}, ["HELLO 2 abc"])
        self.assertEqual(seen[1:], [(0, 12), (0, 12)])

    def test_another_seed_reads_it_again(self) -> None:
        sent, seen = self.run_lines(self.SLOT_DATA, ["HELLO 2 abc", "SIGN", "HELLO 2 def"])
        self.assertEqual(len(sent), 1)
        sent, seen = self.run_lines(self.SLOT_DATA, ["HELLO 2 def", "SIGN"])
        self.assertEqual((sent, seen[-1][0]), ([], 8))  # the game runs another seed than the slot

    def test_read_in_an_earlier_session(self) -> None:
        """The key in data storage counts, as the game's save may have lost the read."""
        from ..client import LASContext

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            ctx.slot, ctx.team, ctx.slot_seed_hash, ctx.slot_data = 1, 0, "abc", self.SLOT_DATA
            ctx.make_logic_tracker()
            seen = []
            for value in (None, True):
                ctx.on_package("Retrieved", {"keys": {"las_sign_0_1": value}})
                seen.append(ctx.logic_tracker_goal[0])
            return seen
        self.assertEqual(asyncio.run(run()), [8, 3])

    def test_known_and_old_slot_data(self) -> None:
        old = {k: v for k, v in self.SLOT_DATA.items() if k != "instruments_known"}
        for slot_data in ({**self.SLOT_DATA, "instruments_known": True}, old):
            with self.subTest(slot_data=slot_data):
                _, seen = self.run_lines(slot_data, ["HELLO 2 abc"])
                self.assertEqual([goal[0] for goal in seen[1:]], [3, 3])


class TestUniversalTracker(unittest.TestCase):
    def test_keys(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        self.assertEqual(world_type.found_entrances_datastorage_key,
                         ("las_sign_{team}_{player}", "las_doors_{team}_{player}"))

    def test_hidden_until_the_sign(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        for value, known in (("random-range-1-6", False), (3, True)):
            with self.subTest(value=value):
                world = setup_multiworld(world_type, STEPS, 80, {"instruments_required": value}).worlds[1]
                slot_data = world.fill_slot_data()
                regen = regenerate(slot_data, 81)
                again = regen.worlds[1]
                required = slot_data["instruments_required"]
                self.assertEqual(again.goal_instruments, required if known else 8)
                beatable = test_goal.TestInstrumentsRequired.beatable
                self.assertEqual(beatable(self, regen, INSTRUMENTS[:required]), known)
                again.reconnect_found_entrances("las_sign_0_1", None)
                self.assertEqual(again.goal_instruments, required if known else 8)
                again.reconnect_found_entrances("las_sign_0_1", True)
                self.assertEqual(again.goal_instruments, required)
                self.assertTrue(beatable(self, regen, INSTRUMENTS[:required]))
                self.assertFalse(beatable(self, regen, INSTRUMENTS[:required - 1]))
                self.assertEqual(again.compiler.node_expr("egg"), world.compiler.node_expr("egg"))
                for loc in world.shuffled:
                    self.assertEqual(again.compiler.node_expr(loc), world.compiler.node_expr(loc), loc)

    def test_seashells_hidden_until_the_sign(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        world = setup_multiworld(world_type, STEPS, 82, {"goal": "seashell_hunt",
                                                         "seashells_required": "random-range-5-20"}).worlds[1]
        slot_data = world.fill_slot_data()
        again = regenerate(slot_data, 83).worlds[1]
        self.assertEqual(again.goal_seashells, SeashellsRequired.range_end)
        again.reconnect_found_entrances("las_sign_0_1", True)
        self.assertEqual(again.goal_seashells, slot_data["seashells_required"])
        self.assertEqual(again.compiler.node_expr("egg"), world.compiler.node_expr("egg"))

    def test_shown_with_deferral_off(self) -> None:
        """With Universal Tracker's enforce_deferred_entrances off, a count left to chance shows at once."""
        world_type = AutoWorldRegister.world_types[GAME]
        world = setup_multiworld(world_type, STEPS, 84, {"goal": "seashell_hunt",
                                                         "seashells_required": "random-range-5-20"}).worlds[1]
        again = regenerate(world.fill_slot_data(), 85, "off").worlds[1]
        self.assertEqual(again.goal_seashells, world.goal_seashells)
