"""Cross-checks the compiled AP rules against LASR's own reachability evaluation."""
import copy
import os
import random
import sys
import types
import unittest
from collections import Counter

from BaseClasses import CollectionState
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister

from .lasr_eval import LASREvaluator
from ..items import ITEM_TABLE, LASR_TO_ITEM
from ..lasr import ITEM_DEFS, ITEM_LOCATIONS, LOGIC, dungeon_logic, goal_logic
from ..locations import LOCATION_NAMES
from ..logic import make_rule, parse

STEPS = ("generate_early", "create_regions", "create_items", "set_rules", "connect_entrances", "generate_basic")
LASR_ROOT = os.path.join(os.path.dirname(os.path.realpath(__file__)), *[os.pardir] * 3, "reference", "DEV-BUILD-LASR")

OPTION_SETS = [
    {},
    {"logic": "advanced"},
    {"logic": "glitched"},
    {"logic": "hell"},
    {"open_mabe": False, "open_kanalet": False, "open_mamu": False, "completed_bridge": False,
     "classic_d2": True, "stealing": "never", "shuffled_bombs": False, "shuffled_powder": False},
    {"logic": "hell", "owl_statues": "all", "blue_rupees": True, "trade_quest": True, "dampe": True,
     "rapids": True, "fishing": True, "seashell_mansion": 50, "free_book": False, "fast_fishing": False,
     "fast_stalfos": False, "stealing": "always"},
    {"chests": False, "free_gifts": False, "miscellaneous": False, "boss_drops": False, "instruments": "vanilla",
     "starting_instruments": 3, "seashell_mansion": 0, "dungeon_items": "start_with"},
    {"shuffled_dungeons": True},
    {"logic": "hell", "shuffled_dungeons": True, "owl_statues": "dungeons"},
]


def lasr_reachable(world, lasr_items: Counter) -> set:
    """Item locations LASR's evaluator reaches with the given items and the world's settings and dungeon entrances;
    the world's optional locations only once it reaches the goal. LASR's logic has the doors in the dungeons'
    conditions (dungeon_logic), the world's in its entrances."""
    logic = goal_logic(world.goal_instruments, dungeon_logic(LOGIC, world.dungeon_entrances), world.goal_seashells)
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
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        multiworld = setup_multiworld(world_type, STEPS, seed, options)
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

    def test_goal_matches_lasr(self) -> None:
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        multiworld = setup_multiworld(world_type, STEPS, 7, {"instruments": "anywhere"})
        state = multiworld.get_all_state()
        self.assertTrue(multiworld.can_beat_game(state))
        for instrument in ("full-moon-cello", "thunder-drum"):
            state = CollectionState(multiworld)
            for item in multiworld.itempool + multiworld.worlds[1].get_pre_fill_items():
                if item.name != LASR_TO_ITEM[instrument]:
                    state.collect(item, prevent_sweep=True)
            state.sweep_for_advancements()
            self.assertFalse(multiworld.can_beat_game(state), instrument)


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


@unittest.skipUnless(os.path.isdir(os.path.join(LASR_ROOT, "RandomizerCore")), "LASR reference checkout not found")
class TestEvaluatorMatchesLASR(unittest.TestCase):
    """The vendored evaluator gives the same answers as LASR's ItemShuffler.canReachLocation."""

    def test_against_lasr_source(self) -> None:
        stubs = {}
        qtcore = types.ModuleType("PySide6.QtCore")
        qtcore.QThread = type("QThread", (), {"__init__": lambda self, parent=None: None})
        qtcore.Signal = lambda *args: None
        stubs["PySide6"] = types.ModuleType("PySide6")
        stubs["PySide6.QtCore"] = qtcore
        paths = types.ModuleType("RandomizerCore.Paths.randomizer_paths")
        paths.IS_RUNNING_FROM_SOURCE = False
        stubs["RandomizerCore.Paths.randomizer_paths"] = paths
        saved = {name: sys.modules.get(name) for name in stubs}
        sys.modules.update(stubs)
        sys.path.insert(0, os.path.abspath(LASR_ROOT))
        try:
            from RandomizerCore.shuffler import ItemShuffler
        finally:
            sys.path.pop(0)
            for name, module in saved.items():
                if module is None:
                    sys.modules.pop(name, None)
                else:
                    sys.modules[name] = module

        rng = random.Random(5)
        pool = [k for k, v in ITEM_DEFS.items() for _ in range(v["quantity"])]
        for preset in ("basic", "hell"):
            shuffler = ItemShuffler({"Seed": "1", "Preset": preset}, {}, copy.deepcopy(LOGIC))
            placements = {k: None for k, v in LOGIC.items() if v["type"] in ("item", "follower")}
            placements["moblin-cave"] = "bow-wow"
            placements["rooster-statue"] = "rooster"
            placements["force-junk"] = set()
            shuffler.placements = placements
            for _ in range(4):
                access = Counter(item for item in pool if rng.random() < 0.6)
                access.update({"open-mabe": 1, "free-book": 1, "shuffled-bombs": 1, "can-steal": 1})
                reached = LASREvaluator(preset, LOGIC, placements).reachable(dict(access))
                for loc in rng.sample(ITEM_LOCATIONS, 25):
                    self.assertEqual(shuffler.canReachLocation(loc, dict(access)), loc in reached, (preset, loc))


class TestIndirectConditions(unittest.TestCase):
    """Spheres are the same whether AP uses the registered indirect conditions or rechecks every entrance."""

    def test_option_sets(self) -> None:
        from Fill import distribute_items_restrictive
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        for index, options in enumerate(OPTION_SETS):
            multiworld = setup_multiworld(world_type, seed=2000 + index, options=options)
            distribute_items_restrictive(multiworld)
            world = multiworld.worlds[1]
            explicit = list(multiworld.get_spheres())
            world.explicit_indirect_conditions = False
            implicit = list(multiworld.get_spheres())
            with self.subTest(options=options):
                self.assertEqual(explicit, implicit)


class TestRefills(unittest.TestCase):
    """Powder, bombs and arrows refill from bushes Link breaks and from the Ammo Ghost (lasr.REFILL_ALTERNATIVES), for
    the item he holds; a refill never pays for itself. Without stealing, as stealing refills bombs and arrows at the
    shop with no item."""

    def state(self, ammo_ghost: bool, items):
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        world = setup_multiworld(world_type, STEPS, 90, {"ammo_ghost": ammo_ghost, "stealing": "never"}).worlds[1]
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
    (lasr.CONDITION_EDITS)."""

    def test_order(self) -> None:
        world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
        world = setup_multiworld(world_type, STEPS, 91, {}).worlds[1]
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
