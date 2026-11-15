"""The client's in-logic locations (tracker.py) match Archipelago's reachability for the same items."""
import random
import unittest

from BaseClasses import CollectionState

from . import OPTION_SETS, generate, generate_world
from ..lasr import DOORS, ENTRANCE_POOL
from ..locations import LOCATION_NAMES
from ..tracker import LogicTracker, door_pool, found_entrance, tracked_seashells


class TestTracker(unittest.TestCase):
    samples = 12

    def check(self, options: dict, seed: int) -> None:
        multiworld = generate(options, seed)
        world = multiworld.worlds[1]
        slot_data = world.fill_slot_data()
        logic = slot_data["logic"]
        # The client knows the slot's locations from the server, and the rest from the slot data.
        tracker = LogicTracker(logic["preset"], logic["tokens"], logic["vanilla"], world.shuffled,
                               slot_data["dungeon_entrances"], logic["optional"], slot_data["instruments_required"],
                               tracked_seashells(slot_data, True), slot_data["entrances"], slot_data["start"])
        # locked locations' items count once received, like the pool's
        locked = [multiworld.get_location(LOCATION_NAMES[loc], 1) for loc in world.locked]
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

    def test_entrances(self) -> None:
        self.check({"entrances": "everything"}, seed=2200)
        self.check({"entrances": "everything", "random_start": True, "shuffled_dungeons": True}, seed=2201)
        self.check({"entrances": "everything", "shuffled_dungeons": "everything"}, seed=2202)

    def test_seashell_hunt(self) -> None:
        """The optional locations wait for an egg that takes seashells; the Mansion's rewards above the count are
        checks before it."""
        self.check({"goal": "seashell_hunt", "seashells_required": 20, "dampe": False}, seed=2100)
        self.check({"goal": "seashell_hunt", "instruments_required": 4, "seashells_required": 35,
                    "seashell_mansion": 50}, seed=2101)
        self.check({"goal": "seashell_hunt", "seashells_required": 0}, seed=2102)

    def test_starting_items_count(self) -> None:
        """The slot data's starting items count: Marin's song needs the Ocarina, a starting item by default."""
        multiworld = generate(seed=3000)
        world = multiworld.worlds[1]
        # Generate applies the starting gear (start_inventory_from_pool) after the steps the test runs
        multiworld.push_precollected(world.create_item("Ocarina"))
        logic = world.fill_slot_data()["logic"]
        self.assertIn("Ocarina", logic["starting"])
        tracker = LogicTracker(logic["preset"], logic["tokens"], logic["vanilla"], world.shuffled)
        self.assertIn("marin", tracker.in_logic(logic["starting"]))
        self.assertNotIn("marin", tracker.in_logic([]))


class TestFoundEntrances(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.world = generate_world({"entrances": "everything"}, 2300)

    def test_a_jump_finds_its_door(self) -> None:
        """Through a door: the jump to the spot inside its own inside door leads to another one's. Out of an inside
        door: the jump to the spot outside its own overworld door leads to another one's."""
        door, other = ENTRANCE_POOL[:2]
        inside = DOORS["pairs"][other]
        self.assertEqual(found_entrance(DOORS["pairs"][door], inside), (door, inside))
        self.assertEqual(found_entrance(other, door), (door, inside))
        self.assertEqual(found_entrance(door, door), (door, DOORS["pairs"][door]))  # a door that leads where it did
        self.assertEqual(found_entrance("Field_11C", door), (door, "MarinTarinHouse_01A"))  # the house's exit led out of door
        dungeon = DOORS["groups"]["dungeon"][0]
        self.assertIsNone(found_entrance(DOORS["pairs"][dungeon], inside))
        # Shuffled dungeons everything: a dungeon's door is a door of the pool
        pool = door_pool({dungeon: inside})
        self.assertEqual(found_entrance(DOORS["pairs"][dungeon], inside, pool), (dungeon, inside))
        self.assertEqual(door_pool({door: inside}), ENTRANCE_POOL)

    def test_doors_not_gone_through_lead_nowhere(self) -> None:
        world = self.world
        tracker = world._tracker(world.shuffled, world.entrances, world.goal_instruments, world.goal_seashells)
        items = [item.name for item in world.multiworld.itempool + world.get_pre_fill_items()]
        every = tracker.in_logic(items)
        self.assertEqual(tracker.in_logic(items, found_entrances=world.entrances), every)
        none = tracker.in_logic(items, found_entrances=())
        self.assertLess(len(none), len(every))
        self.assertTrue(none)  # Mabe and what its own zones reach

    def test_a_tracker_led_elsewhere_sweeps_as_a_new_one(self) -> None:
        """The world's tries share a sweep (LASWorld._tracker): one led to other doors answers as one made for them."""
        world = self.world
        doors = list(world.entrances)
        swapped = {**world.entrances, doors[0]: world.entrances[doors[1]], doors[1]: world.entrances[doors[0]]}
        args = (world.options.logic.current_key, world.settings_tokens, [], world.shuffled, world.dungeon_entrances, (),
                world.goal_instruments, world.goal_seashells)
        items = [item.name for item in world.multiworld.itempool + world.get_pre_fill_items()][:40]
        led = LogicTracker(*args, world.entrances, world.start).lead(swapped)
        self.assertEqual(led.in_logic(items), LogicTracker(*args, swapped, world.start).in_logic(items))
        with self.assertRaises(ValueError):
            led.lead({doors[0]: world.entrances[doors[0]]})
