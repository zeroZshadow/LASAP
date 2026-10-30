"""The instruments or seashells the Wind Fish's egg needs: the logic (goal, instruments_required,
seashells_required) and the patcher's rewrite of the egg's event (patcher/ap_goal.py)."""
import itertools
import os
import sys
import unittest
from pathlib import Path

from BaseClasses import CollectionState
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister

from . import romfs
from .test_logic import STEPS
from ..items import GAME, LASR_TO_ITEM
from ..lasr import INSTRUMENTS
from ..locations import LOCATION_NAMES

PATCHER = Path(os.path.realpath(__file__)).parents[3] / "patcher"
ROMFS = romfs()
EGG_EVENT = ROMFS / "region_common" / "event" / "WindFishsEgg.bfevfl"


class TestInstrumentsRequired(unittest.TestCase):
    def beatable(self, multiworld, instruments) -> bool:
        """Whether the goal is reached with everything but the instruments, plus these."""
        world = multiworld.worlds[1]
        state = CollectionState(multiworld)
        for item in multiworld.itempool + world.get_pre_fill_items():
            if item.name not in {LASR_TO_ITEM[i] for i in INSTRUMENTS}:
                state.collect(item, prevent_sweep=True)
        for instrument in instruments:
            state.collect(world.create_item(LASR_TO_ITEM[instrument]), prevent_sweep=True)
        state.sweep_for_advancements()
        return multiworld.can_beat_game(state)

    def test_goal_needs_that_many(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        for required in (0, 1, 3, 7, 8):
            multiworld = setup_multiworld(world_type, STEPS, 20 + required, {"instruments_required": required})
            with self.subTest(required=required):
                self.assertEqual(multiworld.worlds[1].fill_slot_data()["instruments_required"], required)
                for held in range(len(INSTRUMENTS) + 1):
                    # every instrument counts, not only some
                    for instruments in (INSTRUMENTS[:held], INSTRUMENTS[len(INSTRUMENTS) - held:]):
                        self.assertEqual(self.beatable(multiworld, instruments), held >= required, instruments)

    def test_random_count(self) -> None:
        """A random count is rolled at generation; the slot data and the patch carry the rolled number."""
        world_type = AutoWorldRegister.world_types[GAME]
        for seed in range(5):
            world = setup_multiworld(world_type, STEPS, 50 + seed, {"instruments_required": "random-range-2-6"}).worlds[1]
            required = world.options.instruments_required.value
            self.assertIn(required, range(2, 7))
            self.assertEqual(world.fill_slot_data()["instruments_required"], required)
            self.assertEqual(world.fill_slot_data()["options"]["instruments_required"], required)

    def test_starting_instruments_count(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        multiworld = setup_multiworld(world_type, STEPS, 30, {"instruments_required": 3, "starting_instruments": 2})
        other = next(i for i in INSTRUMENTS if i not in multiworld.worlds[1].starting_instruments)
        self.assertFalse(self.beatable(multiworld, ()))
        self.assertTrue(self.beatable(multiworld, (other,)))


class TestSeashellsRequired(unittest.TestCase):
    """The seashell hunt: the egg takes Seashells Required secret seashells and no instruments."""

    def beatable(self, multiworld, seashells: int, instruments=INSTRUMENTS, without=()) -> bool:
        """Whether the goal is reached with everything but the seashells, instruments and the LASR items `without`,
        plus these seashells and instruments."""
        world = multiworld.worlds[1]
        left_out = {LASR_TO_ITEM[i] for i in ("seashell", *INSTRUMENTS, *without)}
        state = CollectionState(multiworld)
        for item in multiworld.itempool + world.get_pre_fill_items():
            if item.name not in left_out:
                state.collect(item, prevent_sweep=True)
        for item in [LASR_TO_ITEM["seashell"]] * seashells + [LASR_TO_ITEM[i] for i in instruments]:
            state.collect(world.create_item(item), prevent_sweep=True)
        # locked locations (the Trendy Game's prizes) would add their seashells: only those collected here count
        locations = [loc for loc in multiworld.get_locations(1) if loc.item is None or loc.item.name not in left_out]
        state.sweep_for_advancements(locations)
        return multiworld.has_beaten_game(state)

    def test_hunt_needs_that_many(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        for required in (1, 30, 50):
            options = {"goal": "seashell_hunt", "seashells_required": required, "instruments_required": 8}
            multiworld = setup_multiworld(world_type, STEPS, 60 + required, options)
            with self.subTest(required=required):
                slot_data = multiworld.worlds[1].fill_slot_data()
                self.assertEqual((slot_data["instruments_required"], slot_data["seashells_required"]), (0, required))
                settings = multiworld.worlds[1].patch_data()["settings"]
                self.assertEqual((settings["Instruments Required"], settings["Seashells Required"]), (0, required))
                # the instruments are not needed
                self.assertTrue(self.beatable(multiworld, required, ()))
                self.assertFalse(self.beatable(multiworld, required - 1))

    def test_open_egg_is_the_goal(self) -> None:
        """With seashells the egg opens without the Ballad and its door leads to the ending: neither the ocarina, the
        Ballad nor what the Nightmare takes (the magnifying lens without Free Book) is needed."""
        world_type = AutoWorldRegister.world_types[GAME]
        without = ("ocarina", "song-ballad", "magnifying-lens")
        for goal, beatable in (("seashell_hunt", True), ("instruments", False)):
            options = {"goal": goal, "instruments_required": 2, "seashells_required": 10, "free_book": False}
            multiworld = setup_multiworld(world_type, STEPS, 74, options)
            with self.subTest(goal=goal):
                self.assertEqual(self.beatable(multiworld, 10, INSTRUMENTS[:2], without), beatable)
                self.assertTrue(self.beatable(multiworld, 10, INSTRUMENTS[:2]))

    def test_instruments_goal_takes_no_seashells(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        world = setup_multiworld(world_type, STEPS, 71, {"seashells_required": 40}).worlds[1]
        self.assertEqual(world.fill_slot_data()["seashells_required"], 0)
        self.assertEqual(world.patch_data()["settings"]["Seashells Required"], 0)
        self.assertTrue(self.beatable(world.multiworld, 0))

    def test_mansion_above_the_count_is_optional(self) -> None:
        """A reward the goal's seashells do not reach waits for the goal, so the hunt never takes more seashells."""
        world_type = AutoWorldRegister.world_types[GAME]
        for goal, optional in (("seashell_hunt", {"40-seashell-reward", "50-seashell-reward"}), ("instruments", set())):
            options = {"goal": goal, "seashells_required": 35, "seashell_mansion": 50}
            world = setup_multiworld(world_type, STEPS, 72, options).worlds[1]
            with self.subTest(goal=goal):
                mansion = {loc for loc in world.optional if loc.endswith("-seashell-reward")}
                self.assertEqual(mansion, optional)

    def test_fills(self) -> None:
        """Seeds with the hunt fill and are beatable, also with all 50 seashells and few other locations; the Mansion's
        rewards above the count hold no progression."""
        from Fill import distribute_items_restrictive
        world_type = AutoWorldRegister.world_types[GAME]
        option_sets = ({"goal": "seashell_hunt"},
                       {"goal": "seashell_hunt", "seashells_required": 50, "chests": False, "free_gifts": False},
                       {"goal": "seashell_hunt", "instruments_required": 4, "seashell_mansion": 50,
                        "seashells": False})
        for seed, options in enumerate(option_sets):
            multiworld = setup_multiworld(world_type, (*STEPS, "pre_fill"), 80 + seed, options)
            distribute_items_restrictive(multiworld)
            with self.subTest(options=options):
                self.assertTrue(multiworld.can_beat_game())
                world = multiworld.worlds[1]
                for loc in world.optional:
                    if loc.endswith("-seashell-reward"):
                        self.assertFalse(world.get_location(LOCATION_NAMES[loc]).item.advancement, loc)

    def test_random_count_is_known(self) -> None:
        """A seashell count left to chance shows at once: the trackers keep only an instrument count hidden."""
        world_type = AutoWorldRegister.world_types[GAME]
        options = {"goal": "seashell_hunt", "seashells_required": "random-range-10-20",
                   "instruments_required": "random"}
        world = setup_multiworld(world_type, STEPS, 73, options).worlds[1]
        slot_data = world.fill_slot_data()
        self.assertIn(slot_data["seashells_required"], range(10, 21))
        self.assertTrue(slot_data["instruments_known"])
        self.assertEqual(world.goal_instruments, 0)


@unittest.skipUnless(EGG_EVENT.exists(), "RomFS dump not found (LASAP_ROMFS)")
class TestEggEvent(unittest.TestCase):
    """The rewritten event takes each of the four switches' cases as the original does when all instruments means
    "at least the required number"."""

    @classmethod
    def setUpClass(cls) -> None:
        saved = list(sys.path)
        sys.path.insert(0, str(PATCHER))
        try:
            import evfl
            import ap_goal
            from RandomizerCore.Tools import leb
            from RandomizerCore.Tools.text_tools import TextFile
        finally:
            sys.path[:] = saved
        cls.evfl, cls.ap_goal, cls.leb, cls.TextFile = evfl, ap_goal, leb, TextFile

    @classmethod
    def tearDownClass(cls) -> None:
        for name in list(sys.modules):
            if name.split(".")[0] in ("ap_goal", "RandomizerCore", "lms"):
                del sys.modules[name]

    def read(self):
        flow = self.evfl.EventFlow()
        flow.read(EGG_EVENT.read_bytes())
        return flow

    def visited(self, chart, held: set, required: int, seashells: bool = True) -> set:
        """The events of the orchestra that run with these instruments (indices into INSTRUMENTS) and, by
        `seashells`, the seashells the egg needs; subflows are not followed."""
        from evfl.event import ActionEvent, ForkEvent, JoinEvent, SubFlowEvent, SwitchEvent
        entry = next(ep for ep in chart.entry_points if ep.name == "orchestra")
        seen = set()
        stack = [entry.main_event.v]
        while stack:
            event = stack.pop()
            if event is None or event.name in seen:
                continue
            seen.add(event.name)
            data = event.data
            if isinstance(data, (ActionEvent, JoinEvent, SubFlowEvent)):
                stack.append(data.nxt.v)
            elif isinstance(data, ForkEvent):
                stack.extend(fork.v for fork in data.forks)
                stack.append(data.join.v)
            elif isinstance(data, SwitchEvent):
                query = data.actor_query.v.v
                if query == self.ap_goal.ALL_QUERY:
                    value = len(held) >= required and seashells
                elif query == self.ap_goal.ITEM_QUERY:
                    item_type = data.params.data["itemType"]
                    self.assertEqual(data.params.data["count"], 1)
                    value = self.ap_goal.INSTRUMENT_ITEM_TYPES.index(item_type) in held
                elif query == self.ap_goal.FLAG_QUERY:
                    self.assertEqual(data.params.data["symbol"], self.ap_goal.SEASHELL_FLAG)
                    value = seashells
                else:
                    self.fail(f"unexpected query {query} at {event.name}")
                case = data.cases.get(int(value))
                stack.append(case.v if case is not None else None)
        return seen

    def test_rewrite_matches_count(self) -> None:
        original = self.read().flowchart
        names = {event.name for event in original.events}
        sets = [set(c) for n in range(len(INSTRUMENTS) + 1) for c in itertools.combinations(range(len(INSTRUMENTS)), n)]
        self.assertEqual(len(sets), 256)
        for required in range(len(INSTRUMENTS)):
            chart = self.read().flowchart
            self.ap_goal.rewrite(chart, required)
            self.assertFalse([e.name for e in chart.events
                              if getattr(e.data, "actor_query", None) is not None
                              and e.data.actor_query.v.v == self.ap_goal.ALL_QUERY])
            for held in sets:
                with self.subTest(required=required, held=sorted(held)):
                    self.assertEqual(self.visited(chart, held, required) & names,
                                     self.visited(original, held, required))
        # the egg opens (the entry `oepn`) exactly with enough instruments
        chart = self.read().flowchart
        self.ap_goal.rewrite(chart, 3)
        opened = [len(held) for held in sets if any(
            getattr(e.data, "entry_point_name", None) == "oepn" for e in chart.events
            if e.name in self.visited(chart, held, 3))]
        self.assertEqual(min(opened), 3)
        self.assertEqual(len(opened), sum(1 for held in sets if len(held) >= 3))

    def test_rewrite_with_seashells(self) -> None:
        """With seashells required, each switch checks the seashell flag alone and takes its case 1 with it, whatever
        the instruments."""
        original = self.read().flowchart
        names = {event.name for event in original.events}
        for required in (0, len(INSTRUMENTS)):
            chart = self.read().flowchart
            self.ap_goal.rewrite(chart, required, 30)
            queries = [e.data.actor_query.v.v for e in chart.events if getattr(e.data, "actor_query", None) is not None]
            self.assertEqual(queries.count(self.ap_goal.FLAG_QUERY), self.ap_goal.SWITCHES)
            self.assertFalse({self.ap_goal.ALL_QUERY, self.ap_goal.ITEM_QUERY} & set(queries))
            self.assertEqual({e.name for e in chart.events}, names)
            for held in (set(), set(range(len(INSTRUMENTS)))):
                for seashells in (False, True):
                    with self.subTest(required=required, held=sorted(held), seashells=seashells):
                        self.assertEqual(self.visited(chart, held, required, seashells),
                                         self.visited(original, set(), 0, seashells))

    def test_door_leads_to_ending(self) -> None:
        """With seashells the egg's door leads where the stairs after the Nightmare lead; without, it stays."""
        room_file = ROMFS / "region_common" / "level" / "Field" / f"{self.ap_goal.EGG_ROOM}.leb"
        test = self

        class Files:
            written = {}

            @staticmethod
            def readFile(name):
                return test.read() if name == test.ap_goal.EVENT_FILE else test.leb.Room(room_file.read_bytes())

            @staticmethod
            def writeFile(name, data):
                Files.written[name] = data

            @staticmethod
            def getAllTextFiles(name):
                return []

        self.ap_goal.set_goal(Files, 0, 3)
        door = self.ap_goal.egg_door(self.leb.Room(Files.written[room_file.name].repack()))
        self.assertEqual(door.parameters[:2], [b"Ending", b"Lv09WindFishsEgg_05H"])
        Files.written.clear()
        self.ap_goal.set_goal(Files, 3, 0)
        self.assertNotIn(room_file.name, Files.written)

    def test_rewrite_writes(self) -> None:
        """The rewritten flowchart writes and reads back with the same checks."""
        import io
        flow = self.read()
        self.ap_goal.rewrite(flow.flowchart, 5)
        stream = io.BytesIO()
        flow.write(stream)
        again = self.evfl.EventFlow()
        again.read(stream.getvalue())
        held = {0, 2, 4, 6, 7}
        self.assertEqual(self.visited(again.flowchart, held, 5), self.visited(flow.flowchart, held, 5))
        self.assertNotEqual(self.visited(again.flowchart, held - {7}, 5), self.visited(again.flowchart, held, 5))

    def test_sign_names_the_count(self) -> None:
        """The sign below the egg keeps its place name and adds the count, within a text box (3 lines of 42)."""
        path = ROMFS / "regionUS" / "USen" / "message"
        written = {}

        class Files:
            @staticmethod
            def getAllTextFiles(name):
                return [(path, self.TextFile(path / name))]

            @staticmethod
            def writeTextFile(relative_path, name, msbt):
                written[name] = msbt

        for count, seashells, wording in ((0, 0, "with no instruments."), (1, 0, "with 1 instrument."),
                                          (5, 0, "with 5 instruments."), (8, 0, "with all 8 instruments."),
                                          (0, 30, "with 30 secret seashells."), (0, 1, "with 1 secret seashell."),
                                          (8, 50, "with 50 secret seashells.")):
            self.ap_goal._sign(Files, count, seashells)
            text = written[self.ap_goal.SIGN_FILE].getEntry(self.ap_goal.SIGN_KEY).message.text
            self.assertEqual(text, f"Mt. Tamaranch\nThe Wind Fish's Egg opens\n{wording}[1:4]")
            lines = text[:-len("[1:4]")].split("\n")
            self.assertLessEqual(len(lines), 3)
            self.assertLessEqual(max(map(len, lines)), 42)
