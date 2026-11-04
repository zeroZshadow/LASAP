"""The instruments or seashells the Wind Fish's egg needs: the logic (goal, instruments_required,
seashells_required) and the egg's event, whose instrument checks the module answers (patcher/ap_goal.py)."""
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
        for required in (0, 1, 30, 50):
            options = {"goal": "seashell_hunt", "seashells_required": required, "instruments_required": 8}
            multiworld = setup_multiworld(world_type, STEPS, 60 + required, options)
            with self.subTest(required=required):
                slot_data = multiworld.worlds[1].fill_slot_data()
                self.assertEqual((slot_data["instruments_required"], slot_data["seashells_required"]), (0, required))
                settings = multiworld.worlds[1].patch_data()["settings"]
                self.assertEqual((settings["Instruments Required"], settings["Seashell Hunt"],
                                  settings["Seashells Required"]), (0, True, required))
                # the instruments are not needed
                self.assertTrue(self.beatable(multiworld, required, ()))
                if required:
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
        """The goal alone decides the hunt: the instruments goal takes no seashells, whatever Seashells Required."""
        world_type = AutoWorldRegister.world_types[GAME]
        for required in (0, 40):
            world = setup_multiworld(world_type, STEPS, 71, {"seashells_required": required}).worlds[1]
            with self.subTest(required=required):
                self.assertEqual(world.fill_slot_data()["seashells_required"], 0)
                settings = world.patch_data()["settings"]
                self.assertEqual((settings["Seashell Hunt"], settings["Seashells Required"]), (False, 0))
                self.assertTrue(self.beatable(world.multiworld, 0))
                self.assertFalse(self.beatable(world.multiworld, 0, INSTRUMENTS[:7]))

    def test_mansion_follows_its_option(self) -> None:
        """The goal changes only what the egg takes: the Mansion's rewards are optional above seashell_mansion alone."""
        world_type = AutoWorldRegister.world_types[GAME]
        for goal in ("seashell_hunt", "instruments"):
            for required, mansion, optional in ((35, 50, set()), (15, 30, {"40-seashell-reward", "50-seashell-reward"})):
                options = {"goal": goal, "seashells_required": required, "seashell_mansion": mansion}
                world = setup_multiworld(world_type, STEPS, 72, options).worlds[1]
                with self.subTest(goal=goal, required=required, mansion=mansion):
                    got = {loc for loc in world.optional if loc.endswith("-seashell-reward")}
                    self.assertEqual(got, optional)

    def test_fills(self) -> None:
        """Seeds with the hunt fill and are beatable, also with all 50 seashells and few other locations."""
        from Fill import distribute_items_restrictive
        world_type = AutoWorldRegister.world_types[GAME]
        option_sets = ({"goal": "seashell_hunt"},
                       {"goal": "seashell_hunt", "seashells_required": 50, "chests": False, "free_gifts": False},
                       {"goal": "seashell_hunt", "seashells_required": 0},
                       {"goal": "seashell_hunt", "instruments_required": 4, "seashell_mansion": 50,
                        "seashells": False})
        for seed, options in enumerate(option_sets):
            multiworld = setup_multiworld(world_type, (*STEPS, "pre_fill"), 80 + seed, options)
            distribute_items_restrictive(multiworld)
            with self.subTest(options=options):
                self.assertTrue(multiworld.can_beat_game())

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
    """The module answers the egg's HasAllInstruments with the goal (ap_egg.cpp): the egg's event asks it at its four
    switches, no other event asks it, and its answer alone decides whether the egg opens."""

    @classmethod
    def setUpClass(cls) -> None:
        saved = list(sys.path)
        sys.path.insert(0, str(PATCHER))
        sys.path.insert(0, str(PATCHER / "modbuild"))
        try:
            import evfl
            import ap_goal
            from RandomizerCore.Tools import leb
        finally:
            sys.path[:] = saved
        cls.evfl, cls.ap_goal, cls.leb = evfl, ap_goal, leb

    @classmethod
    def tearDownClass(cls) -> None:
        for name in list(sys.modules):
            if name.split(".")[0] in ("ap_goal", "RandomizerCore", "lms"):
                del sys.modules[name]

    def read(self):
        flow = self.evfl.EventFlow()
        flow.read(EGG_EVENT.read_bytes())
        return flow

    def visited(self, chart, answer: bool) -> set:
        """The events of the orchestra that run with this answer to the query; subflows are not followed."""
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
                self.assertEqual(data.actor_query.v.v, self.ap_goal.ALL_QUERY, event.name)
                case = data.cases.get(int(answer))
                stack.append(case.v if case is not None else None)
        return seen

    def test_answer_opens_the_egg(self) -> None:
        chart = self.read().flowchart
        switches = [e.name for e in chart.events if getattr(e.data, "actor_query", None) is not None
                    and e.data.actor_query.v.v == self.ap_goal.ALL_QUERY]
        self.assertEqual(len(switches), self.ap_goal.SWITCHES)
        for answer in (False, True):
            opens = any(getattr(e.data, "entry_point_name", None) == "oepn" for e in chart.events
                        if e.name in self.visited(chart, answer))
            self.assertEqual(opens, answer)

    def test_only_the_egg_asks(self) -> None:
        asking = []
        for path in sorted(EGG_EVENT.parent.glob("*.bfevfl")):
            flow = self.evfl.EventFlow()
            flow.read(path.read_bytes())
            if any(getattr(e.data, "actor_query", None) is not None and e.data.actor_query.v.v == self.ap_goal.ALL_QUERY
                   for e in flow.flowchart.events):
                asking.append(path.name)
        self.assertEqual(asking, [self.ap_goal.EVENT_FILE])

    def test_door_leads_to_ending(self) -> None:
        """With seashells the module leads the egg's door where the stairs after the Nightmare lead; the room keeps
        the door, and without seashells nothing is led."""
        room = self.leb.Room((ROMFS / "region_common" / "level" / "Field" / "Field_01G.leb").read_bytes())
        doors = [a.parameters[:2] for a in room.actors if a.type == 0x190 and a.parameters[0] == b"Lv09WindFishsEgg"]
        self.assertEqual(doors, [[b"Lv09WindFishsEgg", self.ap_goal.EGG_LOCATOR.encode()]])
        for seashells in (0, 3):
            self.assertEqual(self.ap_goal.jumps(seashells),
                             ["Field Lv09WindFishsEgg Lv09WindFishsEgg_08D Ending Lv09WindFishsEgg_05H"])
        self.assertEqual(self.ap_goal.jumps(None), [])

    def test_settings_seashells(self) -> None:
        """The patch's Seashell Hunt decides the hunt; a patch without it is a hunt when it takes seashells."""
        for settings, seashells in (({"Seashell Hunt": True, "Seashells Required": 0}, 0),
                                    ({"Seashell Hunt": True, "Seashells Required": 20}, 20),
                                    ({"Seashell Hunt": False, "Seashells Required": 0}, None),
                                    ({"Seashells Required": 20}, 20), ({"Seashells Required": 0}, None), ({}, None)):
            with self.subTest(settings=settings):
                self.assertEqual(self.ap_goal.settings_seashells(settings), seashells)
