"""The instruments the Wind Fish's egg needs: the logic (instruments_required) and the patcher's rewrite of the egg's
event (patcher/ap_goal.py)."""
import itertools
import os
import sys
import unittest
from pathlib import Path

from BaseClasses import CollectionState
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister

from .test_logic import STEPS
from ..items import GAME, LASR_TO_ITEM
from ..lasr import INSTRUMENTS

PATCHER = Path(os.path.realpath(__file__)).parents[3] / "patcher"
ROMFS = Path(os.environ.get("LASAP_ROMFS", r"D:\Games\Switch\LASRomFS"))
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
            from RandomizerCore.Tools.text_tools import TextFile
        finally:
            sys.path[:] = saved
        cls.evfl, cls.ap_goal, cls.TextFile = evfl, ap_goal, TextFile

    @classmethod
    def tearDownClass(cls) -> None:
        for name in list(sys.modules):
            if name.split(".")[0] in ("ap_goal", "RandomizerCore", "lms"):
                del sys.modules[name]

    def read(self):
        flow = self.evfl.EventFlow()
        flow.read(EGG_EVENT.read_bytes())
        return flow

    def visited(self, chart, held: set, required: int) -> set:
        """The events of the orchestra that run with these instruments (indices into INSTRUMENTS); subflows are not
        followed."""
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
                    value = len(held) >= required
                elif query == self.ap_goal.ITEM_QUERY:
                    item_type = data.params.data["itemType"]
                    self.assertEqual(data.params.data["count"], 1)
                    value = self.ap_goal.INSTRUMENT_ITEM_TYPES.index(item_type) in held
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

        for count, wording in ((0, "with no instruments."), (1, "with 1 instrument."), (5, "with 5 instruments."),
                               (8, "with all 8 instruments.")):
            self.ap_goal._sign(Files, count)
            text = written[self.ap_goal.SIGN_FILE].getEntry(self.ap_goal.SIGN_KEY).message.text
            self.assertEqual(text, f"Mt. Tamaranch\nThe Wind Fish's Egg opens\n{wording}[1:4]")
            lines = text[:-len("[1:4]")].split("\n")
            self.assertLessEqual(len(lines), 3)
            self.assertLessEqual(max(map(len, lines)), 42)
