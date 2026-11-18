"""Warp Points: the items that open the overworld's warp points (DESIGN.md, Warp points)."""
from collections import Counter
from types import SimpleNamespace

from BaseClasses import CollectionState

from . import LASTestBase
from .. import JUNK
from ..items import LASR_TO_ITEM
from ..lasr import WARP_POINTS
from ..locations import LOCATION_NAMES
from ..models import model_for
from ..ut_map import MAMBO, find_route, route_jumps

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

    def test_checks_count_the_warps(self) -> None:
        """The placement checks (_reaches, _start_opens_up) count each progression item of the pool: _pool and the
        starting items hold it at least as often as item_pool."""
        held = self.world._pool()
        held.update(self.world.starting_items)
        for item, count in self.world.item_pool().items():
            if self.world._is_progression(item):
                self.assertLessEqual(count, held[item], item)

    def test_models(self) -> None:
        """A warp point's item shows the warp medallion: the patch names the item as its location's model."""
        for item in WARP_POINTS:
            self.assertEqual(model_for(self.world.create_item(LASR_TO_ITEM[item])), item)

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

    def test_warp_point_to_warp_point(self) -> None:
        """Walking onto an open warp point leads to every other open one, with no Ocarina."""
        self.collect_by_name(LASR_TO_ITEM["bracelet"])
        self.assertTrue(self.can_reach_region("prairie"))
        self.collect_by_name(LASR_TO_ITEM["warp-turtle-rock"])
        self.assertFalse(self.can_reach_location(ABOVE_TURTLE_ROCK))
        self.collect_by_name(LASR_TO_ITEM["warp-ukuku-prairie"])
        self.assertTrue(self.can_reach_location(ABOVE_TURTLE_ROCK))

    def state_of(self, *items: str) -> CollectionState:
        state = CollectionState(self.multiworld)
        for item in items:
            state.collect(self.world.create_item(LASR_TO_ITEM[item]), prevent_sweep=True)
        return state

    def region(self, name: str):
        return self.multiworld.get_region(name, self.player)

    def test_rock_over_platform(self) -> None:
        """Dampe's Shack's platform lies under a rock: warping from it needs the Power Bracelet."""
        goal = self.region("turtle-dungeon-top")
        state = self.state_of("warp-dampes-shack", "warp-turtle-rock")
        self.assertIsNone(find_route(self.region("goponga-central"), goal, state))
        state.collect(self.world.create_item(LASR_TO_ITEM["bracelet"]), prevent_sweep=True)
        self.assertIsNotNone(find_route(self.region("goponga-central"), goal, state))

    def test_route_by_mambo(self) -> None:
        """The tracker's route plays Manbo's Mambo where Link stands, outside a dungeon (ut_map.route_jumps)."""
        goal = self.region("turtle-dungeon-top")
        state = self.state_of("ocarina", "song-mambo", "warp-turtle-rock")
        self.assertIsNone(find_route(self.region("mabe"), goal, state))
        route = find_route(self.region("mabe"), goal, state, route_jumps(self.world))
        self.assertEqual([way.name for way in route], [MAMBO, "warp-map -> turtle-dungeon-top"])
        self.assertEqual(self.world.explain_path(route[0], state), [{"type": "color", "color": "blue", "text": "Manbo's Mambo"}])
        self.assertEqual(self.world.explain_path(route[1], state),
                         [{"type": "text", "text": f" -> {LASR_TO_ITEM['warp-turtle-rock']}"}])
        inside = [way.name for way in find_route(self.region("D1.a"), goal, state, route_jumps(self.world))]
        self.assertIn(MAMBO, inside[1:])

    def test_route_warps_across_the_island(self) -> None:
        """The tracker's route takes the shorter way: a long walk across the island gives way to Manbo's Mambo once Link
        has it and the warp point at the end."""
        walker = ("sword", "shield", "bracelet", "boots", "feather", "flippers", "hookshot", "bomb", "ocarina")
        goal = self.region("animal-village")
        walked = find_route(self.region("mabe"), goal, self.state_of(*walker), route_jumps(self.world))
        self.assertNotIn(MAMBO, [way.name for way in walked])
        route = find_route(self.region("mabe"), goal, self.state_of(*walker, "song-mambo", "warp-animal-village"),
                           route_jumps(self.world))
        self.assertEqual([way.name for way in route], [MAMBO, "warp-map -> animal-village"])

    def test_route_by_warp_point(self) -> None:
        """The tracker's route walks onto a warp point, named by the item Link has, and warps from it."""
        goal = self.region("turtle-dungeon-top")
        state = self.state_of("warp-seashell-mansion", "warp-turtle-rock")
        route = find_route(self.region("prairie"), goal, state, route_jumps(self.world))
        self.assertEqual([way.name for way in route], ["prairie -> warp-map", "warp-map -> turtle-dungeon-top"])
        self.assertEqual(self.world.explain_path(route[0], state),
                         [{"type": "color", "color": "blue", "text": LASR_TO_ITEM["warp-seashell-mansion"]}])

    def test_route_legs(self) -> None:
        """On the map a warp is a jump: walked to the warp point Link warps from, dashed to the one he lands on, and
        dashed from where Link stands for Manbo's Mambo (TrackerMixin.route_points)."""
        from ..ut_context import TrackerMixin
        target = LOCATION_NAMES["above-turtle-rock"]
        locs = [{"sections": [{"name": name}], "map_locations": [{"map": "M", "x": x, "y": 0}]}
                for name, x in (("Field_X", 0), (target, 3))]
        warps = {item: None for item in WARP_POINTS}
        warps.update({"warp-ukuku-prairie": (1, 0), "warp-seashell-mansion": (9, 0), "warp-turtle-rock": (2, 0)})
        core = SimpleNamespace(get_current_world=lambda: self.world, multiworld=self.multiworld, player_id=self.player)

        def points(route, *items, start="Field_X"):
            ctx = SimpleNamespace(logical_path_entrances=route, route_start=start, route_target=target,
                                  tracker_core=core, warp_pins=lambda _: warps, tracker_state=self.state_of(*items),
                                  section_pins=lambda: TrackerMixin.section_pins(SimpleNamespace(locs=locs, route_pins=None)))
            return TrackerMixin.route_points(ctx, "M")

        self.assertEqual(points(["prairie -> warp-map", "warp-map -> turtle-dungeon-top"],
                                "warp-ukuku-prairie", "warp-turtle-rock")[0],
                         [(0, 0, True), (1, 0, True), (2, 0, False), (3, 0, True)])
        self.assertEqual(points([MAMBO, "warp-map -> turtle-dungeon-top"], "warp-turtle-rock")[0],
                         [(0, 0, True), (2, 0, False), (3, 0, True)])
        # from the warp point Link warped to: its place is the start
        self.assertEqual(points(["prairie -> warp-map", "warp-map -> turtle-dungeon-top"],
                                "warp-ukuku-prairie", "warp-turtle-rock", start="Warp_UkukuPrairie"),
                         ([(1, 0, True), (2, 0, False), (3, 0, True)], True))


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
