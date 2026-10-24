"""Universal Tracker regenerates a slot from its slot data (interpret_slot_data) with the same locations and rules."""
import json
import random
import unittest

from BaseClasses import CollectionState
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister, call_all

from .test_logic import OPTION_SETS, STEPS
from ..items import GAME

OPTIONS = OPTION_SETS + [
    {"small_keys": "anywhere", "nightmare_keys": "anywhere", "dungeon_items": "own_dungeon",
     "instruments": "own_dungeon", "starting_instruments": 2, "owl_statues": "dungeons", "traps": "few"},
    {"small_keys": "any_dungeon", "nightmare_keys": "start_with", "dungeon_items": "own_world"},
    {"instruments": "dungeon_rewards", "seashell_mansion": 5, "heart_pieces": False, "seashells": False,
     "golden_leaves": False, "shop": False},
]


def regenerate(slot_data: dict, seed: int):
    """What Universal Tracker does on connect: default options, another seed, the slot data passed through."""
    world_type = AutoWorldRegister.world_types[GAME]
    multiworld = setup_multiworld(world_type, (), seed)
    multiworld.generation_is_fake = True
    multiworld.re_gen_passthrough = {GAME: world_type.interpret_slot_data(json.loads(json.dumps(slot_data)))}
    for step in STEPS:
        call_all(multiworld, step)
    return multiworld


class TestUniversalTracker(unittest.TestCase):
    samples = 6

    def check(self, options: dict, seed: int) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        original = setup_multiworld(world_type, STEPS, seed, options)
        world = original.worlds[1]
        slot_data = world.fill_slot_data()
        regen = regenerate(slot_data, seed + 1)
        again = regen.worlds[1]

        self.assertEqual(again.fill_slot_data()["logic"]["tokens"], slot_data["logic"]["tokens"])
        self.assertEqual(again.starting_items, world.starting_items)
        self.assertEqual(again.optional, world.optional)
        self.assertEqual({r.name for r in regen.get_regions(1)}, {r.name for r in original.get_regions(1)})
        self.assertEqual({e.name for e in regen.get_entrances(1)}, {e.name for e in original.get_entrances(1)})

        def locations(multiworld):
            return {loc.name: (loc.address, loc.item.name if loc.item else None)
                    for loc in multiworld.get_locations(1)}
        self.assertEqual(locations(regen), locations(original))
        for name in again.node_regions:
            self.assertEqual(again.compiler.node_expr(name), world.compiler.node_expr(name), name)
        for loc in world.shuffled + world.vanilla:
            self.assertEqual(again.compiler.node_expr(loc), world.compiler.node_expr(loc), loc)

        # the same items reach the same locations; Universal Tracker gets the starting items from the server
        names = [item.name for item in original.itempool + world.get_pre_fill_items() if item.player == 1]
        starting = [item.name for item in original.precollected_items[1]]
        rng = random.Random(seed)
        for sample in range(self.samples):
            share = rng.choice((0.0, 0.15, 0.3, 0.5, 0.7, 1.0))
            chosen = starting + [name for name in names if rng.random() < share]
            reached = []
            for multiworld in (original, regen):
                state = CollectionState(multiworld)
                state.prog_items[1].clear()  # without the precollected items, as the tracker's state has none
                for name in chosen:
                    state.collect(multiworld.worlds[1].create_item(name), prevent_sweep=True)
                state.sweep_for_advancements()
                reached.append({loc.name for loc in multiworld.get_reachable_locations(state, 1)})
            with self.subTest(options=options, seed=seed, sample=sample, share=share):
                self.assertEqual(reached[1], reached[0])

    def test_regenerated_slot_matches(self) -> None:
        for index, options in enumerate(OPTIONS):
            with self.subTest(options=options):
                self.check(options, 4000 + index)

    def test_keys_of_older_seeds(self) -> None:
        """The slot data of older seeds has "keys" in place of small_keys and nightmare_keys: it sets both."""
        world_type = AutoWorldRegister.world_types[GAME]
        original = setup_multiworld(world_type, STEPS, 4100, {"small_keys": "anywhere", "nightmare_keys": "anywhere"})
        slot_data = original.worlds[1].fill_slot_data()
        options = slot_data["options"]
        options["keys"] = options.pop("small_keys")
        del options["nightmare_keys"]
        again = regenerate(slot_data, 4101).worlds[1]
        self.assertEqual(again.options.small_keys.current_key, "anywhere")
        self.assertEqual(again.options.nightmare_keys.current_key, "anywhere")

    def test_slot_data_holds_options(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        world = setup_multiworld(world_type, STEPS, 1, OPTIONS[-1]).worlds[1]
        slot_data = world.fill_slot_data()
        self.assertIs(world_type.interpret_slot_data(slot_data), slot_data)
        self.assertTrue(world_type.ut_can_gen_without_yaml)
        self.assertNotIn("start_inventory_from_pool", slot_data["options"])
        self.assertEqual(slot_data["options"]["instruments"], world.options.instruments.option_dungeon_rewards)
        self.assertEqual(slot_data["options"]["seashell_mansion"], 5)
