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
        slot_data = world.fill_slot_data()
        logic = slot_data["logic"]
        # The client knows the slot's locations from the server, and the rest from the slot data.
        tracker = LogicTracker(logic["preset"], logic["tokens"], logic["vanilla"], world.shuffled,
                               slot_data["dungeon_entrances"])
        # locked locations' items count once received, like the pool's
        locked = [multiworld.get_location(LOCATION_NAMES[loc], 1) for loc in logic["locked"]]
        pool = [item for item in multiworld.itempool + world.get_pre_fill_items() if item.player == 1]
        pool += [location.item for location in locked]
        unlocked = [location for location in multiworld.get_locations() if location not in locked]
        rng = random.Random(seed)
        for sample in range(self.samples):
            share = rng.choice((0.0, 0.15, 0.3, 0.5, 0.7, 1.0))
            chosen = [item for item in pool if rng.random() < share]
            state = CollectionState(multiworld)
            for item in chosen:
                state.collect(item, prevent_sweep=True)
            state.sweep_for_advancements(unlocked)
            expected = {loc for loc in world.shuffled
                        if multiworld.get_location(LOCATION_NAMES[loc], 1).can_reach(state)}
            # the client counts the slot data's starting items and the items received
            got = tracker.in_logic(logic["starting"] + [item.name for item in chosen])
            with self.subTest(options=options, seed=seed, sample=sample, share=share):
                self.assertEqual(sorted(got - expected), [], "in logic for the tracker only")
                self.assertEqual(sorted(expected - got), [], "in logic for Archipelago only")

    def test_option_sets(self) -> None:
        for index, options in enumerate(OPTION_SETS):
            self.check(options, seed=2000 + index)

    def test_starting_items_count(self) -> None:
        """The slot data's starting items count: Marin's song needs the Ocarina, a starting item by default."""
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        multiworld = setup_multiworld(world_type, STEPS, 3000, {})
        world = multiworld.worlds[1]
        # Generate applies the starting gear (start_inventory_from_pool) after the steps the test runs
        multiworld.push_precollected(world.create_item("Ocarina"))
        logic = world.fill_slot_data()["logic"]
        self.assertIn("Ocarina", logic["starting"])
        tracker = LogicTracker(logic["preset"], logic["tokens"], logic["vanilla"], world.shuffled)
        self.assertIn("marin", tracker.in_logic(logic["starting"]))
        self.assertNotIn("marin", tracker.in_logic([]))
