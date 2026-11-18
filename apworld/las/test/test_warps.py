"""Warp Points: the items that open the overworld's warp points (DESIGN.md, Warp points)."""
from collections import Counter

from . import LASTestBase
from .. import JUNK
from ..items import LASR_TO_ITEM
from ..lasr import WARP_POINTS
from ..locations import LOCATION_NAMES

ABOVE_TURTLE_ROCK = LOCATION_NAMES["above-turtle-rock"]


def pool_names(test: LASTestBase) -> Counter:
    return Counter(item.name for item in test.multiworld.itempool if item.player == test.player)


class TestWarpPoints(LASTestBase):
    options = {"warp_points": True}

    def test_items(self) -> None:
        """The pool holds each warp point's item once, and no warp point is a location."""
        pool = pool_names(self)
        for item in WARP_POINTS:
            self.assertEqual(pool[LASR_TO_ITEM[item]], 1, item)
        locations = {location.name for location in self.multiworld.get_locations(self.player)}
        self.assertFalse({name for name in locations if "Warp" in name})

    def test_junk_traded(self) -> None:
        """The warp items take ten junk items' places."""
        on = self.world.item_pool()
        self.world.options.warp_points.value = 0
        try:
            off = self.world.item_pool()
        finally:
            self.world.options.warp_points.value = 1
        self.assertEqual(sum(off[junk] for junk in JUNK) - sum(on[junk] for junk in JUNK), len(WARP_POINTS))
        self.assertEqual(sum(on.values()), sum(off.values()))

    def test_patch(self) -> None:
        """The patch lists every warp point as opened by its item."""
        patch = self.world.patch_data()
        self.assertEqual(patch["warp_points"], sorted(WARP_POINTS.values()))
        self.assertTrue(patch["settings"]["Warp Points"])

    def test_turtle_rock_warp(self) -> None:
        """With the Ocarina and Manbo's Mambo, the Turtle Rock warp leads onto Turtle Rock's top."""
        self.collect_by_name([LASR_TO_ITEM["ocarina"], LASR_TO_ITEM["song-mambo"]])
        self.assertFalse(self.can_reach_location(ABOVE_TURTLE_ROCK))
        self.collect_by_name(LASR_TO_ITEM["warp-turtle-rock"])
        self.assertTrue(self.can_reach_location(ABOVE_TURTLE_ROCK))


class TestWarpPointsOff(LASTestBase):
    def test_no_warp_items(self) -> None:
        """Off, the pool has no warp item and the patch lists no warp point."""
        pool = pool_names(self)
        for item in WARP_POINTS:
            self.assertNotIn(LASR_TO_ITEM[item], pool)
        self.assertEqual(self.world.patch_data()["warp_points"], [])



class TestWarpPointsTrapsanity(LASTestBase):
    options = {"warp_points": True, "traps": "trapsanity"}

    def test_items(self) -> None:
        """Traps that take every junk item leave the warp items their places."""
        pool = pool_names(self)
        for item in WARP_POINTS:
            self.assertEqual(pool[LASR_TO_ITEM[item]], 1, item)
