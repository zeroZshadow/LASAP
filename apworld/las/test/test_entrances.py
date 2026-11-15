"""Entrances: which doors move (lasr.shuffle_pool), the doors as AP entrances, plando, and the patch's jumps for the
moved doors."""
import unittest

from . import LASTestBase, forget, patcher_import
from ..lasr import (DOORS as GAME_DOORS, DUNGEON_DOORS, DUNGEON_POOL, DUNGEONS, ENTRANCE_POOL, HOME_DOOR, HOUSE_DOOR,
                    HOUSE_EXIT, START_DOORS, WRITTEN_LOGIC, door_entrance, door_region, dungeon_region, shuffle_pool,
                    start_pairs)
from ..options import ShuffledDungeons
from ..logic import START, parse

# The house; a two-door cave; two dead ends.
DOORS = {"pairs": {"Field_house": "House_01A", "Field_a": "Cave_01A", "Field_b": "Cave_02A", "Field_c": "Shed_01A",
                   "Field_d": "Hut_01A"},
         "one-way": {},
         "groups": {"dungeon": [], "dungeon-other": [], "connector": ["Field_a", "Field_b"],
                    "single": ["Field_c", "Field_d", "Field_house"]}}


class TestShufflePool(unittest.TestCase):
    def test_the_groups_doors_move_the_houses_too(self) -> None:
        self.assertEqual(shuffle_pool(DOORS), ["Field_c", "Field_d", "Field_house", "Field_a", "Field_b"])


class TestDoorJumps(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ap_entrances, = patcher_import("ap_entrances")

    @classmethod
    def tearDownClass(cls) -> None:
        forget("ap_entrances")

    def test_a_moved_door_leads_in_and_back_out(self) -> None:
        lines = self.ap_entrances.door_jumps({"Field_01A": "Cave_02B", "Field_03C": "House_01A"},
                                             {"Field_01A": "House_01A", "Field_03C": "Cave_02B"})
        self.assertEqual(lines, ["Field House House_01A Cave Cave_02B", "Cave Field Field_03C Field Field_01A",
                                 "Field Cave Cave_02B House House_01A", "House Field Field_01A Field Field_03C"])

    def test_every_jump_home_follows_tarins_house(self) -> None:
        lines = self.ap_entrances.door_jumps({"Field_11C": "Cave_02B", "Field_03C": "MarinTarinHouse_01A"},
                                             {"Field_11C": "MarinTarinHouse_01A", "Field_03C": "Cave_02B"})
        self.assertIn("* Field Field_11C Field Field_03C", lines)
        self.assertNotIn("MarinTarinHouse Field Field_11C Field Field_03C", lines)

    def test_a_jump_out_onto_a_covered_door_opens_it(self) -> None:
        lines = self.ap_entrances.door_jumps({"Field_06C": "Cave_02B", "Field_03C": "House_01A"},
                                             {"Field_06C": "House_01A", "Field_03C": "Cave_02B"})
        self.assertIn("Cave Field Field_03C Field Field_06C AppearStairsFld06C", lines)
        self.assertIn("House Field Field_06C Field Field_03C", lines)

    def test_a_random_start_swaps_the_houses_door(self) -> None:
        """The start door leads into the house and every jump home out of it; Tarin's door into its inside, and back."""
        level_of = self.ap_entrances.level_of
        moved = start_pairs("Field_06C")
        self.assertEqual(moved, {"Field_06C": HOUSE_DOOR, HOME_DOOR: GAME_DOORS["pairs"]["Field_06C"]})
        inside = GAME_DOORS["pairs"]["Field_06C"]
        lines = self.ap_entrances.door_jumps(moved, {door: GAME_DOORS["pairs"][door] for door in moved})
        self.assertEqual(sorted(lines), sorted([
            f"Field {level_of(inside)} {inside} MarinTarinHouse {HOUSE_DOOR}",
            "* Field Field_11C Field Field_06C AppearStairsFld06C",
            f"Field MarinTarinHouse {HOUSE_DOOR} {level_of(inside)} {inside}",
            f"{level_of(inside)} Field Field_06C Field Field_11C"]))
        self.assertEqual(start_pairs(HOME_DOOR), {})
        self.assertEqual(start_pairs(None), {})


class TestRandomStart(LASTestBase):
    options = {"random_start": True}

    def test_the_house_leads_out_of_the_start_door(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertIn(world.start, START_DOORS)
        self.assertEqual(world.fill_slot_data()["start"], world.start)
        self.assertEqual(world.patch_data()["start"], world.start)
        zone = START_DOORS[world.start]
        tree = parse(str(world.door_logic[zone]["condition-basic"]))
        self.assertIn(("tok", START, 1), tree[1] if tree[0] == "or" else [tree])

    def test_the_start_door_leads_back_into_the_house(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(world.patch_data()["entrances"], start_pairs(world.start))
        if world.start != HOME_DOOR:
            self.assertEqual(world.patch_data()["entrances"][world.start], HOUSE_DOOR)


class TestRandomStartPocket(unittest.TestCase):
    """A zone whose only way out is the start door is a pocket once that door leads back into the house."""

    def test_a_dead_end_start_is_never_picked(self) -> None:
        from test.general import setup_multiworld
        from .. import LASWorld
        dead_ends = {door for door, zone in START_DOORS.items()
                     if set((WRITTEN_LOGIC[zone].get("doors") or {})) == {door} and not WRITTEN_LOGIC[zone].get("one-way")
                     and not WRITTEN_LOGIC[zone].get("connections")
                     and not any(zone in (node.get("one-way") or {}) or zone in (node.get("connections") or {})
                                 for node in WRITTEN_LOGIC.values())}
        self.assertIn("Field_01N", dead_ends)  # the Tal Tal yard of the five chests
        for seed in range(8):
            world = setup_multiworld(LASWorld, seed=4400 + seed, options={"random_start": True}).worlds[1]
            self.assertNotIn(world.start, dead_ends)


class TestEntrancesOff(LASTestBase):
    def test_no_door_moves(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(world.entrances, {})
        self.assertEqual(world.patch_data()["entrances"], {})


class TestDoorEntrances(LASTestBase):
    """With Entrances, each shuffled door and its inside door is an AP entrance (lasr.door_logic's open_doors)."""
    options = {"entrances": "everything", "random_start": True}

    def test_each_door_leads_to_the_door_its_placement_names(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(set(world.entrances), set(ENTRANCE_POOL))
        for door, inside in world.entrances.items():
            self.assertEqual(world.get_entrance(door).connected_region.name, door_region(inside))
            self.assertEqual(world.get_entrance(inside).connected_region.name, door_region(door))

    def test_the_house_and_the_start_door_lead_into_each_other(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertIn(world.start, START_DOORS)
        self.assertEqual(world.entrances[world.start], HOUSE_DOOR)
        self.assertEqual(world.get_entrance(HOUSE_DOOR).connected_region.name, door_region(world.start))

    def test_a_door_leads_on_to_its_own_zone(self) -> None:
        """Whatever door leads to a door's region, it leads on to the zone of that door, and back through it."""
        world = self.multiworld.worlds[self.player]
        zones = {door: zone for zone, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})}
        for door in ENTRANCE_POOL:
            for side in (door, GAME_DOORS["pairs"][door]):
                exits = {exit.name: exit.connected_region.name for exit in world.get_region(door_region(side)).exits}
                self.assertEqual(set(exits.values()) - {zones[side]}, {exits[side]}, side)


class TestDungeonsAmongTheDoors(LASTestBase):
    """Shuffled dungeons everything: the dungeons' doors are doors of the door pool (lasr.DUNGEON_POOL)."""
    options = {"entrances": "everything", "shuffled_dungeons": "everything", "random_start": True}

    def test_the_dungeon_doors_are_placed_with_the_others(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(set(world.entrances), set(DUNGEON_POOL))
        self.assertEqual(world.dungeon_entrances, {dungeon: dungeon for dungeon in DUNGEONS})
        for door in DUNGEON_DOORS.values():
            inside = world.entrances[door]
            self.assertEqual(world.get_entrance(door).connected_region.name, door_region(inside))
            self.assertEqual(world.get_entrance(inside).connected_region.name, door_region(door))
        with self.assertRaises(KeyError):
            world.get_entrance(door_entrance(DUNGEONS[0]))

    def test_a_dungeons_inside_door_leads_on_into_the_dungeon(self) -> None:
        world = self.multiworld.worlds[self.player]
        for dungeon, door in DUNGEON_DOORS.items():
            region = world.get_region(door_region(GAME_DOORS["pairs"][door]))
            self.assertIn(dungeon_region(dungeon), {exit_.connected_region.name for exit_ in region.exits})

    def test_the_patch_leads_the_dungeon_doors_as_doors(self) -> None:
        world = self.multiworld.worlds[self.player]
        patch = world.patch_data()
        self.assertFalse(patch["settings"]["Shuffled Dungeons"])
        moved = {door for door in DUNGEON_DOORS.values() if world.entrances[door] != GAME_DOORS["pairs"][door]}
        self.assertTrue(moved)
        self.assertLessEqual(moved, set(patch["entrances"]))


class TestDungeonsAmongTheDoorsNeedEntrances(LASTestBase):
    """Without Entrances, Shuffled dungeons everything shuffles the dungeons among themselves."""
    options = {"shuffled_dungeons": "everything"}

    def test_dungeons_among_themselves(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(world.options.shuffled_dungeons.value, ShuffledDungeons.option_dungeons)
        self.assertTrue(world.patch_data()["settings"]["Shuffled Dungeons"])
        self.assertEqual(world.entrances, {})


# Three single doors of the shuffle: plando swaps the first two's insides, the first written inside first
SINGLES = [door for door in GAME_DOORS["groups"]["single"] if door in ENTRANCE_POOL][:3]
SWAP = [{"entrance": SINGLES[0], "exit": GAME_DOORS["pairs"][SINGLES[1]]},
        {"entrance": GAME_DOORS["pairs"][SINGLES[0]], "exit": SINGLES[1]}]


class TestPlandoDoors(LASTestBase):
    options = {"entrances": "everything", "random_start": True,
               "plando_connections": SWAP + [{"entrance": HOUSE_EXIT, "exit": "Field_11C"}]}

    def test_the_doors_lead_as_planned(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(world.entrances[SINGLES[0]], GAME_DOORS["pairs"][SINGLES[1]])
        self.assertEqual(world.entrances[SINGLES[1]], GAME_DOORS["pairs"][SINGLES[0]])
        self.assertEqual(world.get_entrance(SINGLES[0]).connected_region.name,
                         door_region(GAME_DOORS["pairs"][SINGLES[1]]))
        self.assertEqual(world.get_entrance(GAME_DOORS["pairs"][SINGLES[1]]).connected_region.name,
                         door_region(SINGLES[0]))

    def test_the_house_leads_out_of_the_planned_door(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual(world.start, "Field_11C")
        self.assertEqual(world.get_entrance(HOUSE_DOOR).connected_region.name, door_region("Field_11C"))


class TestPlandoWithoutEntrances(LASTestBase):
    """Without Entrances or Random Start the plando's doors and start are left out."""
    options = {"plando_connections": SWAP + [{"entrance": HOUSE_EXIT, "exit": "Field_11C"}]}

    def test_left_out(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertEqual((world.entrances, world.plando_doors, world.start), ({}, {}, None))


class TestPlandoTwice(unittest.TestCase):
    def test_a_door_in_two_connections_is_refused(self) -> None:
        from Options import OptionError
        from test.general import setup_multiworld
        from .. import LASWorld
        twice = [SWAP[0], {"entrance": GAME_DOORS["pairs"][SINGLES[1]], "exit": SINGLES[2]}]
        with self.assertRaisesRegex(OptionError, GAME_DOORS["pairs"][SINGLES[1]]):
            setup_multiworld(LASWorld, seed=1, options={"entrances": "everything", "plando_connections": twice})


class TestDungeonMapFails(unittest.TestCase):
    def test_no_map_reaching_everything_is_a_placement_error(self) -> None:
        from unittest import mock
        from test.general import setup_multiworld
        from .. import LASWorld, PlacementError
        with mock.patch.object(LASWorld, "_reaches", return_value=False), self.assertRaises(PlacementError):
            setup_multiworld(LASWorld, ("generate_early",), 1, {"shuffled_dungeons": "dungeons"})
