"""Optional locations (LASWorld.optional): the activities the options leave out are checks reached only after the goal,
holding the slot's own items, so no seed needs them."""
import unittest

from BaseClasses import CollectionState
from Fill import distribute_items_restrictive
from test.general import gen_steps, setup_multiworld
from worlds.AutoWorld import AutoWorldRegister, call_all

from . import LASTestBase
from .. import GOAL_EVENT
from ..items import GAME, ITEM_TABLE, LASR_TO_ITEM
from ..lasr import CATEGORIES, LOGIC
from ..lasr_settings import CATEGORY_OPTIONS, MANSION_THRESHOLDS, OPTIONAL_CATEGORIES, optional_locations
from ..locations import LOCATION_NAMES

ACTIVITIES = set().union(*(CATEGORIES[category] for category in OPTIONAL_CATEGORIES.values())) | \
    set(MANSION_THRESHOLDS) | {"owl-statue-rapids"}


def regular_reached(multiworld, player: int, optional) -> set:
    """The player's other locations reached with every item of the multiworld but those at its optional ones."""
    names = {LOCATION_NAMES[loc] for loc in optional}
    state = CollectionState(multiworld)
    state.sweep_for_advancements([loc for loc in multiworld.get_filled_locations()
                                  if not (loc.player == player and loc.name in names)])
    return {loc.name for loc in multiworld.get_locations(player) if loc.name not in names and loc.can_reach(state)}


class TestDefaultOptional(LASTestBase):
    """By default the trade quest, Dampe, the Rapids Ride, fishing and the 40 and 50 seashell rewards are optional."""

    def test_which(self) -> None:
        expected = set(CATEGORIES["Trade_Gifts"]) | set(CATEGORIES["Dampe_Rewards"]) | \
            set(CATEGORIES["Rapids_Rewards"]) | set(CATEGORIES["Fishing_Rewards"]) | \
            {"40-seashell-reward", "50-seashell-reward"}
        self.assertEqual(set(self.world.optional), expected)  # the rapids owl statue gives nothing without owls
        self.assertLessEqual(set(self.world.optional), set(self.world.shuffled))
        self.assertFalse(set(self.world.optional) & (set(self.world.locked) | set(self.world.plain)))
        patch = self.world.patch_data()
        for loc in self.world.optional:
            self.assertEqual(patch["placements"][loc], "ap-item", loc)
            self.assertIn(loc, patch["location_ids"])
        self.assertEqual(self.world.fill_slot_data()["logic"]["optional"], self.world.optional)
        # the activities' settings stay off for the patcher and the trackers
        settings = patch["settings"]
        self.assertEqual((settings["Trade Quest"], settings["Dampe"], settings["Rapids"], settings["Fishing"],
                          settings["Seashell Mansion"]), (False, False, False, False, 30))

    def test_rules(self) -> None:
        """Reached only after the goal; only the slot's own items, no progression where excluded."""
        location = self.multiworld.get_location(LOCATION_NAMES["fishing-50"], self.player)
        state = self.multiworld.get_all_state()
        self.assertTrue(location.can_reach(state))
        state.prog_items[self.player][GOAL_EVENT] = 0
        self.assertFalse(location.can_reach(state))
        other = self.world.create_item("Bombs")
        other.player = self.player + 1
        self.assertFalse(location.item_rule(other))
        self.assertTrue(location.item_rule(self.world.create_item("Bombs")))

    def test_items_in_pool(self) -> None:
        """Their vanilla items are the slot's like any shuffled location's: the trade items, the magnifying lens,
        the bottles, the level-2 sword, Dampe's heart container."""
        items = [item.name for item in self.multiworld.itempool + self.world.get_pre_fill_items()
                 if item.player == self.player]
        items += [self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item.name
                  for loc in self.world.optional]
        for content in ("magnifying-lens", "mermaid-scale", "ribbon", "bottle", "heart-container"):
            self.assertIn(LASR_TO_ITEM[content], items, content)
        self.assertEqual(items.count("Progressive Sword"), 2)  # the tests leave the starting gear in the pool
        self.assertEqual(sum(self.world.item_pool().values()), len(self.world.shuffled) - len(self.world.locked))

    def test_filled_before_the_main_fill(self) -> None:
        for loc in self.world.optional:
            location = self.multiworld.get_location(LOCATION_NAMES[loc], self.player)
            self.assertIsNotNone(location.item, loc)
            self.assertTrue(location.locked, loc)
            self.assertEqual(location.item.player, self.player, loc)
            self.assertFalse(any(item is location.item for item in self.multiworld.itempool), loc)

    def test_never_needed(self) -> None:
        """Every other location of the slot, the goal included, is reached without the optional locations' items."""
        distribute_items_restrictive(self.multiworld)
        call_all(self.multiworld, "post_fill")
        state = self.multiworld.get_all_state()
        everything = {loc.name for loc in self.multiworld.get_locations(self.player) if loc.can_reach(state)}
        optional = {LOCATION_NAMES[loc] for loc in self.world.optional}
        self.assertEqual(regular_reached(self.multiworld, self.player, self.world.optional), everything - optional)


class TestEverythingOptional(LASTestBase):
    """The Mansion at 0, owl statues on and no activity: every activity's location is optional, the seashells count
    for the Mansion's rewards."""
    options = {"seashell_mansion": 0, "owl_statues": "overworld", "seashells": False}

    def test_which(self) -> None:
        self.assertEqual(set(self.world.optional), ACTIVITIES)
        self.assertEqual(optional_locations(self.world.options), ACTIVITIES)
        self.assertTrue(self.world.create_item("Secret Seashell").advancement)
        # seashells left in place are checks holding them, locked
        self.assertIn("pond-island", self.world.locked)


class TestNothingOptional(LASTestBase):
    options = {"trade_quest": True, "dampe": True, "rapids": True, "fishing": True, "seashell_mansion": 50,
               "owl_statues": "all"}

    def test_none(self) -> None:
        self.assertEqual(self.world.optional, [])
        self.assertEqual(optional_locations(self.world.options), set())


class TestOtherCategoriesUnchanged(LASTestBase):
    """Places passed on the way stay vanilla when off: locked checks where the logic can need their item, else no
    locations."""
    options = {name: False for name in CATEGORY_OPTIONS if name not in OPTIONAL_CATEGORIES}

    def test_vanilla(self) -> None:
        others = set().union(*(CATEGORIES[CATEGORY_OPTIONS[name]] for name in self.options))
        self.assertFalse(others & set(self.world.optional))
        self.assertLessEqual(set(self.world.optional), ACTIVITIES)
        for loc in others:
            if loc in self.world.removed or LOGIC[loc]["subtype"] == "shop" and loc not in CATEGORIES["Shop_Items"]:
                continue
            self.assertTrue(loc in self.world.locked or loc in self.world.plain, loc)
        self.assertIn("D2-genie", self.world.plain)  # a boss's heart container
        self.assertIn("shop-slot3-1st", self.world.locked)  # the shovel or the bow


class TestOwnItemsInMultiworld(unittest.TestCase):
    """With two LAS players, each one's optional locations hold its own items, and nothing either needs is there; no
    small key either, which the player could have spent on another door than those the logic counts it for."""

    def test_two_players(self) -> None:
        world_type = AutoWorldRegister.world_types[GAME]
        for seed in range(6):
            multiworld = setup_multiworld([world_type, world_type], gen_steps, seed,
                                          {"keys": "anywhere", "traps": "few"})
            distribute_items_restrictive(multiworld)
            call_all(multiworld, "post_fill")
            self.assertTrue(multiworld.can_beat_game(), seed)
            state = multiworld.get_all_state()
            for player in (1, 2):
                world = multiworld.worlds[player]
                self.assertTrue(world.optional)
                for loc in world.optional:
                    item = multiworld.get_location(LOCATION_NAMES[loc], player).item
                    self.assertEqual(item.player, player, (seed, loc))
                    self.assertFalse(ITEM_TABLE[item.name].lasr_name.startswith("key-"), (seed, loc))
                everything = {loc.name for loc in multiworld.get_locations(player) if loc.can_reach(state)}
                optional = {LOCATION_NAMES[loc] for loc in world.optional}
                self.assertEqual(regular_reached(multiworld, player, world.optional), everything - optional, seed)
