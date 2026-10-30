"""Starting gear (start_inventory_from_pool) comes out of the item pool: Archipelago takes every copy it gives at the
start out of multiworld.itempool, so each must be there, also when the options leave its vanilla location unrandomized
or the pre-fill would place it (LASWorld._replaced_by_starting_gear)."""
from collections import Counter

from . import LASTestBase
from ..locations import LOCATION_NAMES


class StartingGearBase(LASTestBase):
    def assert_gear_in_pool(self) -> None:
        pool = Counter(item.name for item in self.multiworld.itempool if item.player == self.player)
        for name, count in self.world.options.start_inventory_from_pool.value.items():
            self.assertGreaterEqual(pool[name], count, name)
        self.assertEqual(sum(self.world.item_pool().values()), len(self.world.shuffled) - len(self.world.locked))


class TestStartingGearAtLockedLocations(StartingGearBase):
    """With Miscellaneous and Free Gifts off, the Ocarina's and Manbo's Mambo's locations are locked checks holding
    their vanilla item; with them in the starting gear those hold 50 Rupees, and both items stay in the pool."""
    options = {"miscellaneous": False, "free_gifts": False}

    def test_locations_hold_rupees(self) -> None:
        for loc in ("dream-shrine-left", "manbo"):
            self.assertIn(loc, self.world.locked)
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            self.assertEqual(item.name, "50 Rupees", loc)
        self.assertNotIn("dream-shrine-left", self.world.fill_slot_data()["logic"]["locked"])

    def test_gear_in_pool(self) -> None:
        self.assert_gear_in_pool()


class TestStartingGearBossDrops(StartingGearBase):
    """With Boss Drops off the bosses' heart containers are locked and Dampe's is the pool's only one; two starting
    Heart Containers make one boss hold 50 Rupees, and the pool holds both."""
    options = {"boss_drops": False, "start_inventory_from_pool": {"Heart Container": 2}}

    def test_location_and_pool(self) -> None:
        replaced = [loc for loc, item in self.world.replaced.items() if item == "rupee-50"]
        self.assertEqual(len(replaced), 1)
        self.assertIn(replaced[0], self.world.locked)
        item = self.multiworld.get_location(LOCATION_NAMES[replaced[0]], self.player).item
        self.assertEqual(item.name, "50 Rupees")
        self.assert_gear_in_pool()


class TestStartingGearPreFill(StartingGearBase):
    """With keys in their own dungeon the pre-fill places the small keys; a starting Tail Cave key stays in the pool."""
    options = {"small_keys": "own_dungeon", "nightmare_keys": "own_dungeon", "start_inventory_from_pool": {"Small Key (Tail Cave)": 1}}

    def test_gear_in_pool(self) -> None:
        self.assertIn("Small Key (Tail Cave)", self.world.restricted)
        self.assert_gear_in_pool()
        # the pre-fill and the pool hold the dungeon's keys once each; Archipelago takes the starting one out
        name = "Small Key (Tail Cave)"
        keys = [item.name for item in self.world.get_pre_fill_items() + self.multiworld.itempool].count(name)
        self.assertEqual(keys, sum(1 for loc in self.world.shuffled if self.world.content(loc) == "key-D1"))
