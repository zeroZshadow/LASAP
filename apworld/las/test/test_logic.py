"""Cross-checks the compiled AP rules against LASR's own reachability evaluation."""
import copy
import random
import unittest
from collections import Counter

from BaseClasses import CollectionState
from test.general import gen_steps

from . import OPTION_SETS, generate, generate_world
from .lasr_eval import LASREvaluator
from ..items import ITEM_TABLE, LASR_TO_ITEM
from ..lasr import LOGIC, WRITTEN_LOGIC, goal_logic, plain_logic, slot_doors
from ..locations import LOCATION_NAMES
from ..logic import make_rule, parse


def lasr_reachable(world, lasr_items: Counter) -> set:
    """Item locations LASR's evaluator reaches with the given items and the world's settings and dungeon entrances;
    the world's optional locations only once it reaches the goal. LASR's evaluator reads the routes lowered with the
    slot's doors (plain_logic), the world has the dungeon doors in its entrances."""
    doors = slot_doors(world.entrances, world.dungeon_entrances)
    logic = goal_logic(world.goal_instruments, plain_logic(WRITTEN_LOGIC, doors), world.goal_seashells)
    logic_defs = copy.deepcopy(logic)
    for loc in world.removed:
        del logic_defs[loc]
    placements = {k: None for k, v in logic_defs.items() if v["type"] in ("item", "follower")}
    for loc in world.vanilla + world.locked:
        placements[loc] = world.content(loc)
    access = dict(lasr_items)
    access.update({token: 1 for token in world.settings_tokens})
    evaluator = LASREvaluator(world.options.logic.current_key, logic_defs, placements)
    reached = set(evaluator.reachable(access))
    if "kill-nightmare" not in reached:
        reached -= set(world.optional)
    return {loc for loc in logic_defs if logic_defs[loc]["type"] == "item" and loc in reached}


class TestLogicCrossCheck(unittest.TestCase):
    samples = 20

    def check(self, options: dict, seed: int) -> None:
        multiworld = generate(options, seed)
        world = multiworld.worlds[1]
        pool = [item for item in multiworld.itempool + world.get_pre_fill_items() if item.player == 1]
        precollected = list(multiworld.precollected_items[1])
        rng = random.Random(seed)
        checked = [loc for loc in world.shuffled + world.vanilla
                   if loc in world.shuffled or world._is_progression(LOGIC[loc]["content"])]
        for sample in range(self.samples):
            share = rng.choice((0.15, 0.3, 0.5, 0.7, 0.85, 1.0))
            chosen = [item for item in pool if rng.random() < share]
            state = CollectionState(multiworld)
            for item in chosen:
                state.collect(item, prevent_sweep=True)
            state.sweep_for_advancements()
            ap = {loc for loc in checked if multiworld.get_location(LOCATION_NAMES[loc], 1).can_reach(state)}
            lasr_items = Counter(ITEM_TABLE[item.name].lasr_name for item in chosen + precollected)
            expected = lasr_reachable(world, lasr_items) & set(checked)
            with self.subTest(options=options, seed=seed, sample=sample, share=share):
                self.assertEqual(sorted(ap - expected), [], "reachable in AP only")
                self.assertEqual(sorted(expected - ap), [], "reachable in LASR only")

    def test_option_sets(self) -> None:
        for index, options in enumerate(OPTION_SETS):
            self.check(options, seed=1000 + index)


class TestParser(unittest.TestCase):
    def test_precedence(self) -> None:
        self.assertEqual(parse("a | b & !c"),
                         ("or", [("tok", "a", 1), ("and", [("tok", "b", 1), ("not", ("tok", "c", 1))])]))
        self.assertEqual(parse("!(a | b)"), ("not", ("or", [("tok", "a", 1), ("tok", "b", 1)])))
        self.assertEqual(parse("key-D2:5 & kill-zol-red[heavy]"),
                         ("and", [("tok", "key-D2", 5), ("tok", "kill-zol-red[heavy]", 1)]))

    def test_all_conditions_parse(self) -> None:
        for name, node in LOGIC.items():
            for key, value in node.items():
                if key.startswith("condition-"):
                    parse(str(value))


class TestIndirectConditions(unittest.TestCase):
    """Spheres are the same whether AP uses the registered indirect conditions or rechecks every entrance."""

    def test_option_sets(self) -> None:
        from Fill import distribute_items_restrictive
        for index, options in enumerate(OPTION_SETS):
            multiworld = generate(options, 2000 + index, gen_steps)
            distribute_items_restrictive(multiworld)
            world = multiworld.worlds[1]
            explicit = list(multiworld.get_spheres())
            world.explicit_indirect_conditions = False
            implicit = list(multiworld.get_spheres())
            with self.subTest(options=options):
                self.assertEqual(explicit, implicit)


class TestRefills(unittest.TestCase):
    """Ammo refills (logic.yml); without stealing, which refills bombs and arrows at the shop."""

    worlds = {}

    def state(self, ammo_ghost: bool, items):
        if ammo_ghost not in self.worlds:
            self.worlds[ammo_ghost] = generate_world({"ammo_ghost": ammo_ghost, "stealing": "never"}, 90)
        world = self.worlds[ammo_ghost]
        state = CollectionState(world.multiworld)
        for item in items:
            state.collect(world.create_item(LASR_TO_ITEM[item]), prevent_sweep=True)
        return world, state

    def holds(self, node: str, ammo_ghost: bool, items) -> bool:
        world, state = self.state(ammo_ghost, items)
        return make_rule(world.compiler.node_expr(node), world.player, world.node_regions)(state)

    def test_refills(self) -> None:
        for node, items, ammo_ghost, holds in (
                ("powders", ["powder"], False, False),  # powder on bushes alone does not last
                ("powders", ["powder"], True, True),
                ("powders", ["powder", "sword"], False, True),
                ("powders", ["sword"], True, False),
                ("bombs", ["bomb"], False, False),
                ("bombs", ["bomb"], True, True),
                ("bombs", ["bomb", "powder"], False, False),  # neither lasts on bushes
                ("shoot-arrows", ["bow"], False, False),
                ("shoot-arrows", ["bow"], True, True)):
            with self.subTest(node=node, items=items, ammo_ghost=ammo_ghost):
                self.assertEqual(self.holds(node, ammo_ghost, items), holds)

    def test_mad_batter(self) -> None:
        """The Mad Batter in the woods takes the bracelet and powder, which the bushes the bracelet lifts refill."""
        world, state = self.state(False, ["bracelet", "powder"])
        self.assertTrue(world.get_location(LOCATION_NAMES["mad-batter-woods"]).can_reach(state))


class TestTrendyPrizes(unittest.TestCase):
    """The Trendy Game offers prizes 2 to 6 in order, so a prize takes the instruments of the prizes before it
    (logic.yml)."""

    def test_order(self) -> None:
        world = generate_world({}, 91)
        prizes = ("trendy-prize-4", "trendy-prize-5", "trendy-prize-6")
        for instruments, reached in (
                ([], ()),
                (["surf-harp"], ()),
                (["sea-lilys-bell", "surf-harp"], ()),
                (["conch-horn"], ("trendy-prize-4",)),
                (["conch-horn", "surf-harp"], ("trendy-prize-4",)),
                (["conch-horn", "sea-lilys-bell"], ("trendy-prize-4", "trendy-prize-5")),
                (["conch-horn", "sea-lilys-bell", "surf-harp"], prizes)):
            state = CollectionState(world.multiworld)
            for item in ["sword"] + instruments:
                state.collect(world.create_item(LASR_TO_ITEM[item]), prevent_sweep=True)
            with self.subTest(instruments=instruments):
                self.assertEqual(tuple(p for p in prizes if world.get_location(LOCATION_NAMES[p]).can_reach(state)),
                                 reached)


class TestSwampSideOfWoods(unittest.TestCase):
    """The Woods North chest and the Woods West dig are on the swamp's side of the woods (logic.yml): the
    bracelet reaches them through the wasteland, and the way past Tarin takes breaking bushes and powder, the feather or
    the rooster. The dig also takes the shovel."""

    def test_ways_in(self) -> None:
        world = generate_world({"stealing": "never"}, 92)
        chest = world.get_location(LOCATION_NAMES["woods-north-chest"])
        dig = world.get_location(LOCATION_NAMES["woods-west-dig"])
        for items, chest_reached, dig_reached in (
                ([], False, False),
                (["shovel"], False, False),
                (["sword"], False, False),
                (["feather"], False, False),
                (["sword", "shovel"], False, False),
                (["bracelet"], True, False),
                (["bracelet", "shovel"], True, True),
                (["sword", "feather"], True, False),
                (["sword", "feather", "shovel"], True, True),
                (["sword", "rooster"], True, False),
                (["sword", "rooster", "shovel"], True, True)):
            state = CollectionState(world.multiworld)
            for item in items:
                state.collect(world.create_item(LASR_TO_ITEM[item]), prevent_sweep=True)
            with self.subTest(items=items):
                self.assertEqual(chest.can_reach(state), chest_reached)
                self.assertEqual(dig.can_reach(state), dig_reached)
