"""The client's in-logic locations (tracker.py) match Archipelago's reachability for the same items."""
import random
import unittest

from BaseClasses import CollectionState
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister

from .test_logic import OPTION_SETS, STEPS
from ..locations import LOCATION_NAMES
from ..tracker import LogicTracker


class TestTracker(unittest.TestCase):
    samples = 12

    def check(self, options: dict, seed: int) -> None:
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        multiworld = setup_multiworld(world_type, STEPS, seed, options)
        world = multiworld.worlds[1]
        logic = world.fill_slot_data()["logic"]
        # The client knows the slot's locations from the server, and the rest from the slot data.
        tracker = LogicTracker(logic["preset"], logic["tokens"], logic["vanilla"], world.shuffled)
        pool = [item for item in multiworld.itempool + world.get_pre_fill_items() if item.player == 1]
        precollected = list(multiworld.precollected_items[1])
        rng = random.Random(seed)
        for sample in range(self.samples):
            share = rng.choice((0.0, 0.15, 0.3, 0.5, 0.7, 1.0))
            chosen = [item for item in pool if rng.random() < share]
            state = CollectionState(multiworld)
            for item in chosen:
                state.collect(item, prevent_sweep=True)
            state.sweep_for_advancements()
            expected = {loc for loc in world.shuffled
                        if multiworld.get_location(LOCATION_NAMES[loc], 1).can_reach(state)}
            got = tracker.in_logic(item.name for item in chosen + precollected)
            with self.subTest(options=options, seed=seed, sample=sample, share=share):
                self.assertEqual(sorted(got - expected), [], "in logic for the tracker only")
                self.assertEqual(sorted(expected - got), [], "in logic for Archipelago only")

    def test_option_sets(self) -> None:
        for index, options in enumerate(OPTION_SETS):
            self.check(options, seed=2000 + index)
