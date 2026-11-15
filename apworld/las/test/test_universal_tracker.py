"""Universal Tracker regenerates a slot from its slot data (interpret_slot_data) with the same locations and rules, and
connects the dungeon doors' entrances once the player has found them."""
import random
import unittest
from types import SimpleNamespace

from BaseClasses import CollectionState
from worlds.AutoWorld import AutoWorldRegister

from . import OPTION_SETS, generate, generate_world, regenerate
from ..items import GAME, LASR_TO_ITEM, TRAP_ITEMS
from ..lasr import (DOORS, DUNGEONS, ENTRANCE_POOL, HOUSE_DOOR, door_entrance, door_region, dungeon_region,
                    location_dungeon)
from ..locations import LASR_LOCATIONS
from ..ut_map import SPOT_ZONES, find_route, spot_region

OPTIONS = OPTION_SETS + [
    {"small_keys": "anywhere", "nightmare_keys": "anywhere", "dungeon_items": "own_dungeon",
     "instruments": "own_dungeon", "starting_instruments": 2, "owl_statues": "dungeons", "traps": "few"},
    {"small_keys": "any_dungeon", "nightmare_keys": "start_with", "dungeon_items": "own_world"},
    {"random_start": True},
    {"entrances": "everything", "random_start": True, "shuffled_dungeons": True},
    {"entrances": "everything", "random_start": True, "shuffled_dungeons": "everything"},
    {"instruments": "dungeon_rewards", "seashell_mansion": 5, "heart_pieces": False, "seashells": False,
     "golden_leaves": False, "shop": False},
]


class TestUniversalTracker(unittest.TestCase):
    samples = 6

    def check(self, options: dict, seed: int) -> None:
        original = generate(options, seed)
        world = original.worlds[1]
        slot_data = world.fill_slot_data()
        regen = regenerate(slot_data, seed + 1, "off")  # every door connected, as in the generation
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
        # its item panel shows the item pool's items: every item the slot gets is in it (the traps are random)
        locked = [LASR_TO_ITEM[world.content(loc)] for loc in world.locked]
        self.assertEqual({item.name for item in regen.itempool if item.player == 1} - set(TRAP_ITEMS),
                         set(names + starting + locked) - set(TRAP_ITEMS))
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

    def test_slot_data_holds_options(self) -> None:
        world = generate_world(OPTIONS[-1], 1)
        slot_data = world.fill_slot_data()
        world_type = AutoWorldRegister.world_types[GAME]
        self.assertIs(world_type.interpret_slot_data(slot_data), slot_data)
        self.assertTrue(world_type.ut_can_gen_without_yaml)
        self.assertNotIn("start_inventory_from_pool", slot_data["options"])
        self.assertEqual(slot_data["options"]["instruments"], world.options.instruments.option_dungeon_rewards)
        self.assertEqual(slot_data["options"]["seashell_mansion"], 5)


class TestDeferredDoors(unittest.TestCase):
    """With shuffled dungeons Universal Tracker connects a door's entrance once the player has found the door
    (DOORS_KEY), and the last door's once every other is found: until then the dungeon behind it is out of reach."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.original = generate({"shuffled_dungeons": True}, 4200)
        cls.world = cls.original.worlds[1]
        cls.slot_data = cls.world.fill_slot_data()

    def connected(self, world) -> dict:
        entrances = {door: world.get_entrance(door_entrance(door)) for door in DUNGEONS}
        return {door: entrance.connected_region.name for door, entrance in entrances.items()
                if entrance.connected_region is not None}

    def reached_dungeons(self, multiworld) -> set:
        """The dungeons with a check in reach with every item."""
        names = [item.name for item in self.original.itempool + self.world.get_pre_fill_items() if item.player == 1]
        state = CollectionState(multiworld, True)
        for name in names:
            state.collect(multiworld.worlds[1].create_item(name), prevent_sweep=True)
        state.sweep_for_advancements()
        return {location_dungeon(LASR_LOCATIONS[loc.name]) for loc in multiworld.get_reachable_locations(state, 1)
                if loc.name in LASR_LOCATIONS} - {-1}

    def test_doors_wait_until_found(self) -> None:
        regen = regenerate(self.slot_data, 4201)
        again = regen.worlds[1]
        entrances = self.slot_data["dungeon_entrances"]
        self.assertEqual(self.connected(again), {})
        self.assertEqual(self.reached_dungeons(regen), set())
        door = DUNGEONS[0]
        again.reconnect_found_entrances("las_doors_0_1", {door: entrances[door]})
        self.assertEqual(self.connected(again), {door: dungeon_region(entrances[door])})
        self.assertEqual(self.reached_dungeons(regen), {(DUNGEONS.index(entrances[door]) + 1) % len(DUNGEONS)})
        # the last door leads into the one dungeon left
        again.reconnect_found_entrances("las_doors_0_1", {door: entrances[door] for door in DUNGEONS[:-1]})
        self.assertEqual(self.connected(again), self.connected(self.world))
        self.assertEqual(self.reached_dungeons(regen), self.reached_dungeons(self.original))

    def test_not_deferred(self) -> None:
        """Every door is connected with Universal Tracker's enforce_deferred_entrances off, and with unshuffled
        dungeons, whose doors lead where they always do."""
        again = regenerate(self.slot_data, 4202, "off").worlds[1]
        self.assertEqual(self.connected(again), self.connected(self.world))
        unshuffled = generate_world(seed=4203)
        again = regenerate(unshuffled.fill_slot_data(), 4204).worlds[1]
        self.assertEqual(self.connected(again), {door: dungeon_region(door) for door in DUNGEONS})


class TestDeferredEntrances(unittest.TestCase):
    """With Entrances Universal Tracker connects a shuffled door's entrance, and its inside door's, once the player has
    gone through either (ENTRANCES_KEY); the house's door and the start door at once."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.original = generate({"entrances": "everything", "random_start": True}, 4300)
        cls.slot_data = cls.original.worlds[1].fill_slot_data()

    @staticmethod
    def connected(world) -> dict:
        return {door: world.get_entrance(door).connected_region.name for door in ENTRANCE_POOL
                if world.get_entrance(door).connected_region is not None}

    def reached(self, multiworld) -> int:
        state = CollectionState(multiworld, True)
        for item in self.original.itempool + self.original.worlds[1].get_pre_fill_items():
            state.collect(multiworld.worlds[1].create_item(item.name), prevent_sweep=True)
        state.sweep_for_advancements()
        return len(multiworld.get_reachable_locations(state, 1))

    def test_doors_wait_until_gone_through(self) -> None:
        regen = regenerate(self.slot_data, 4301)
        again = regen.worlds[1]
        entrances = self.slot_data["entrances"]
        start = self.slot_data["start"]
        house = {start: door_region(HOUSE_DOOR)}
        self.assertEqual(self.connected(again), house)
        self.assertEqual(again.get_entrance(HOUSE_DOOR).connected_region.name, door_region(start))
        door = next(door for door in ENTRANCE_POOL if door != start)
        again.reconnect_found_entrances("las_entrances_0_1", {door: entrances[door]})
        self.assertEqual(self.connected(again), {**house, door: door_region(entrances[door])})
        self.assertEqual(again.get_entrance(entrances[door]).connected_region.name, door_region(door))
        before = self.reached(regen)
        again.reconnect_found_entrances("las_entrances_0_1", entrances)
        self.assertEqual(self.connected(again), self.connected(self.original.worlds[1]))
        self.assertLess(before, self.reached(regen))
        self.assertEqual(self.reached(regen), self.reached(self.original))

    def test_not_deferred(self) -> None:
        again = regenerate(self.slot_data, 4302, "off").worlds[1]
        self.assertEqual(self.connected(again), self.connected(self.original.worlds[1]))

    def test_logical_path_lists_doors(self) -> None:
        """/get_logical_path writes only the doors of its way (explain_path), each with the door it comes out of."""
        world = self.original.worlds[1]
        state = CollectionState(self.original)
        door = next(door for door in ENTRANCE_POOL if world.get_entrance(door).connected_region is not None)
        out = world.get_entrance(door).connected_region.name[:-5].replace("-", "_")
        self.assertEqual(world.explain_path(world.get_entrance(door), state),
                         [{"type": "color", "color": "blue", "text": door}, {"type": "text", "text": f" -> {out}"}])
        route = next(e for e in self.original.get_entrances(1) if " -> " in e.name)
        self.assertIsNone(world.explain_path(route, state))

    def full_state(self) -> CollectionState:
        state = CollectionState(self.original, True)
        for item in self.original.itempool + self.original.worlds[1].get_pre_fill_items():
            state.collect(self.original.worlds[1].create_item(item.name), prevent_sweep=True)
        state.sweep_for_advancements()
        return state

    def test_route_from_a_door(self) -> None:
        """A route starts in the region Link stands in after a jump to a door's spot (spot_region) and goes on, each
        entrance after the one before, each rule passed."""
        regions = self.original.regions.region_cache[1]
        state = self.full_state()
        door = next(door for door in ENTRANCE_POOL if door != self.slot_data["start"])
        self.assertIs(spot_region(door, regions), regions[door_region(door)])
        self.assertIs(spot_region(DOORS["pairs"][door], regions), regions[door_region(DOORS["pairs"][door])])
        kept = next(door for door in SPOT_ZONES if door_region(door) not in regions and SPOT_ZONES[door] in regions)
        self.assertIs(spot_region(kept, regions), regions[SPOT_ZONES[kept]])
        self.assertIsNone(spot_region("Field_99Z", regions))
        start = spot_region(door, regions)
        self.assertEqual(find_route(start, start, state), [])
        for location in random.Random(4303).sample(sorted(self.original.get_reachable_locations(state, 1),
                                                          key=lambda loc: loc.name), 20):
            route = find_route(start, location.parent_region, state)
            self.assertIsNotNone(route, location.name)
            at = start
            for way in route:
                self.assertIs(way.parent_region, at)
                self.assertTrue(way.access_rule(state), way.name)
                at = way.connected_region
            self.assertIs(at, location.parent_region)

    def test_route_legs(self) -> None:
        """On a map the route's legs are walked but after a door Link goes into, and to a door's pin he comes out of a
        level at (TrackerMixin.route_points); a ring marks the start door's pin."""
        from ..ut_context import TrackerMixin
        world = self.original.worlds[1]
        entrances = self.slot_data["entrances"]
        doors = [door for door in ENTRANCE_POOL if door != self.slot_data["start"]][:3]
        start, into, out = doors  # Link goes into `into`, and out of the inside door `out` leads into
        target = next(loc.name for loc in self.original.get_locations(1) if loc.address is not None)
        pins = [start, into, out, target]
        locs = [{"sections": [{"name": name}], "map_locations": [{"map": "M", "x": n, "y": 0}]}
                for n, name in enumerate(pins)]
        core = SimpleNamespace(get_current_world=lambda: world, multiworld=self.original, player_id=1)

        def pins_of():
            return TrackerMixin.section_pins(SimpleNamespace(locs=locs, route_pins=None))

        def points(route_start):
            ctx = SimpleNamespace(logical_path_entrances=[into, "x -> y", entrances[out]], route_start=route_start,
                                  route_target=target, tracker_core=core, section_pins=pins_of)
            return TrackerMixin.route_points(ctx, "M")

        self.assertEqual(points(start), ([(0, 0, True), (1, 0, True), (2, 0, False), (3, 0, True)], True))
        # from inside the level the start door leads into: at the start door's pin, through the level
        self.assertEqual(points(entrances[start])[0][:2], [(0, 0, True), (1, 0, False)])
        self.assertEqual(points(None), ([(1, 0, True), (2, 0, False), (3, 0, True)], False))
        # Link ends inside a level: to the target's pin through it
        ctx = SimpleNamespace(logical_path_entrances=[into], route_start=None, route_target=target, tracker_core=core,
                              section_pins=pins_of)
        self.assertEqual(TrackerMixin.route_points(ctx, "M"), ([(1, 0, True), (3, 0, False)], False))
