"""Entrances: which doors move (lasr.shuffle_pool), the doors as AP entrances, plando, and the patch's jumps for the
moved doors."""
import unittest
from unittest import mock

from Options import OptionError
from test.general import gen_steps

from . import LASTestBase, forget, generate, generate_world, patcher_import
from .. import LASWorld, PlacementError
from ..lasr import (DOOR_NAMES, DOORS as GAME_DOORS, DUNGEON_DOORS, DUNGEON_POOL, DUNGEONS, ENTRANCE_POOL, HOME_DOOR,
                    HOUSE_DOOR, HOUSE_EXIT, START_DOORS, WRITTEN_LOGIC, door_entrance, door_label, door_of_label,
                    door_region, dungeon_region, shuffle_pool, start_pairs)
from ..options import PlandoDoors
from ..options import ShuffledDungeons
from ..logic import START, parse

# The house; a two-door cave; two dead ends.
DOORS = {"pairs": {"Field_house": "House_01A", "Field_a": "Cave_01A", "Field_b": "Cave_02A", "Field_c": "Shed_01A",
                   "Field_d": "Hut_01A"},
         "one-way": {},
         "groups": {"dungeon": [], "dungeon-other": [], "connector": ["Field_a", "Field_b"],
                    "single": ["Field_c", "Field_d", "Field_house"]}}


def lead(jumps, level_from: str, level: str, locator: str) -> tuple:
    """Where the jumps' lines lead a jump from level_from to that level and locator, as the module's LeadJump does."""
    for from_, to_level, to, led_level, led in (line.split()[:5] for line in jumps):
        if from_ in (level_from, "*") and (to_level, to) == (level, locator):
            return led_level, led
    return level, locator


def returns_by_landing(lines) -> dict:
    return {(level, spot): (to_level, to) for level, spot, to_level, to in map(str.split, lines)}


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

    def test_a_landing_goes_back_through_the_door_link_took(self) -> None:
        """A return is the jump of the door at a led landing, which the jumps lead to the spot outside the door Link
        came through."""
        entrances = {"Field_01A": "Cave_02B", "Field_03C": "House_01A"}
        pairs = {"Field_01A": "House_01A", "Field_03C": "Cave_02B"}
        jumps = self.ap_entrances.door_jumps(entrances, pairs)
        back = returns_by_landing(self.ap_entrances.returns(entrances, pairs))
        self.assertEqual(len(back), 4)
        # in through Field_01A, which lands on Cave_02B
        self.assertEqual(lead(jumps, "Cave", *back["Cave", "Cave_02B"]), ("Field", "Field_01A"))
        # out of the house, which lands on Field_03C
        self.assertEqual(lead(jumps, "Field", *back["Field", "Field_03C"]), ("House", "House_01A"))

    def test_a_dungeon_landing_goes_back_through_its_door(self) -> None:
        entrances = {"tail-cave": "color-dungeon", "color-dungeon": "tail-cave"}
        entrance, outside, level_of = self.ap_entrances.entrance, self.ap_entrances.outside, self.ap_entrances.level_of
        jumps = self.ap_entrances.jumps(entrances)
        back = returns_by_landing(self.ap_entrances.dungeon_returns(entrances))
        inside = entrance("color-dungeon")
        self.assertEqual(lead(jumps, level_of(inside), *back[level_of(inside), inside]), ("Field", outside("tail-cave")))
        self.assertEqual(lead(jumps, "Field", *back["Field", outside("tail-cave")]), (level_of(inside), inside))

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
        self.assertIn(self.world.start, START_DOORS)
        self.assertEqual(self.world.fill_slot_data()["start"], self.world.start)
        self.assertEqual(self.world.patch_data()["start"], self.world.start)
        zone = START_DOORS[self.world.start]
        tree = parse(str(self.world.door_logic[zone]["condition-basic"]))
        self.assertIn(("tok", START, 1), tree[1] if tree[0] == "or" else [tree])

    def test_the_start_door_leads_back_into_the_house(self) -> None:
        self.assertEqual(self.world.patch_data()["entrances"], start_pairs(self.world.start))
        if self.world.start != HOME_DOOR:
            self.assertEqual(self.world.patch_data()["entrances"][self.world.start], HOUSE_DOOR)


class TestRandomStartPocket(unittest.TestCase):
    """A zone whose only way out is the start door is a pocket once that door leads back into the house."""

    def test_a_dead_end_start_is_never_picked(self) -> None:
        dead_ends = {door for door, zone in START_DOORS.items()
                     if set((WRITTEN_LOGIC[zone].get("doors") or {})) == {door} and not WRITTEN_LOGIC[zone].get("one-way")
                     and not WRITTEN_LOGIC[zone].get("connections")
                     and not any(zone in (node.get("one-way") or {}) or zone in (node.get("connections") or {})
                                 for node in WRITTEN_LOGIC.values())}
        self.assertIn("Field_01N", dead_ends)  # the Tal Tal yard of the five chests
        for seed in range(8):
            world = generate_world({"random_start": True}, 4400 + seed, gen_steps)
            self.assertNotIn(world.start, dead_ends)


class TestEntrancesOff(LASTestBase):
    def test_no_door_moves(self) -> None:
        self.assertEqual(self.world.entrances, {})
        self.assertEqual(self.world.patch_data()["entrances"], {})


class TestDoorEntrances(LASTestBase):
    """With Entrances, each shuffled door and its inside door is an AP entrance (lasr.door_logic's open_doors)."""
    options = {"entrances": "everything", "random_start": True}

    def test_each_door_leads_to_the_door_its_placement_names(self) -> None:
        self.assertEqual(set(self.world.entrances), set(ENTRANCE_POOL))
        for door, inside in self.world.entrances.items():
            self.assertEqual(self.world.get_entrance(door).connected_region.name, door_region(inside))
            self.assertEqual(self.world.get_entrance(inside).connected_region.name, door_region(door))

    def test_the_house_and_the_start_door_lead_into_each_other(self) -> None:
        self.assertIn(self.world.start, START_DOORS)
        self.assertEqual(self.world.entrances[self.world.start], HOUSE_DOOR)
        self.assertEqual(self.world.get_entrance(HOUSE_DOOR).connected_region.name, door_region(self.world.start))

    def test_a_door_leads_on_to_its_own_zone(self) -> None:
        """Whatever door leads to a door's region, it leads on to the zone of that door, and back through it."""
        zones = {door: zone for zone, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})}
        for door in ENTRANCE_POOL:
            for side in (door, GAME_DOORS["pairs"][door]):
                region = self.world.get_region(door_region(side))
                exits = {exit.name: exit.connected_region.name for exit in region.exits}
                self.assertEqual(set(exits.values()) - {zones[side]}, {exits[side]}, side)


class TestDungeonsAmongTheDoors(LASTestBase):
    """Shuffled dungeons everything: the dungeons' doors are doors of the door pool (lasr.DUNGEON_POOL)."""
    options = {"entrances": "everything", "shuffled_dungeons": "everything", "random_start": True}

    def test_the_dungeon_doors_are_placed_with_the_others(self) -> None:
        self.assertEqual(set(self.world.entrances), set(DUNGEON_POOL))
        self.assertEqual(self.world.dungeon_entrances, {dungeon: dungeon for dungeon in DUNGEONS})
        for door in DUNGEON_DOORS.values():
            inside = self.world.entrances[door]
            self.assertEqual(self.world.get_entrance(door).connected_region.name, door_region(inside))
            self.assertEqual(self.world.get_entrance(inside).connected_region.name, door_region(door))
        with self.assertRaises(KeyError):
            self.world.get_entrance(door_entrance(DUNGEONS[0]))

    def test_a_dungeons_inside_door_leads_on_into_the_dungeon(self) -> None:
        for dungeon, door in DUNGEON_DOORS.items():
            region = self.world.get_region(door_region(GAME_DOORS["pairs"][door]))
            self.assertIn(dungeon_region(dungeon), {exit_.connected_region.name for exit_ in region.exits})

    def test_the_patch_leads_the_dungeon_doors_as_doors(self) -> None:
        patch = self.world.patch_data()
        self.assertFalse(patch["settings"]["Shuffled Dungeons"])
        moved = {door for door in DUNGEON_DOORS.values() if self.world.entrances[door] != GAME_DOORS["pairs"][door]}
        self.assertTrue(moved)
        self.assertLessEqual(moved, set(patch["entrances"]))


class TestDungeonsAmongTheDoorsNeedEntrances(LASTestBase):
    """Without Entrances, Shuffled dungeons everything shuffles the dungeons among themselves."""
    options = {"shuffled_dungeons": "everything"}

    def test_dungeons_among_themselves(self) -> None:
        self.assertEqual(self.world.options.shuffled_dungeons.value, ShuffledDungeons.option_dungeons)
        self.assertTrue(self.world.patch_data()["settings"]["Shuffled Dungeons"])
        self.assertEqual(self.world.entrances, {})


# Three single doors of the shuffle: plando swaps the first two's insides, the first written inside first
SINGLES = [door for door in GAME_DOORS["groups"]["single"] if door in ENTRANCE_POOL][:3]
SWAP = [{"entrance": SINGLES[0], "exit": GAME_DOORS["pairs"][SINGLES[1]]},
        {"entrance": GAME_DOORS["pairs"][SINGLES[0]], "exit": SINGLES[1]}]


class TestPlandoDoors(LASTestBase):
    options = {"entrances": "everything", "random_start": True,
               "plando_connections": SWAP + [{"entrance": HOUSE_EXIT, "exit": "Field_11C"}]}

    def test_the_doors_lead_as_planned(self) -> None:
        self.assertEqual(self.world.entrances[SINGLES[0]], GAME_DOORS["pairs"][SINGLES[1]])
        self.assertEqual(self.world.entrances[SINGLES[1]], GAME_DOORS["pairs"][SINGLES[0]])
        self.assertEqual(self.world.get_entrance(SINGLES[0]).connected_region.name,
                         door_region(GAME_DOORS["pairs"][SINGLES[1]]))
        self.assertEqual(self.world.get_entrance(GAME_DOORS["pairs"][SINGLES[1]]).connected_region.name,
                         door_region(SINGLES[0]))

    def test_the_house_leads_out_of_the_planned_door(self) -> None:
        self.assertEqual(self.world.start, "Field_11C")
        self.assertEqual(self.world.get_entrance(HOUSE_DOOR).connected_region.name, door_region("Field_11C"))


class TestPlandoByName(LASTestBase):
    """Plando takes the doors' player-facing names (lasr.door_label), in any case."""
    options = {"entrances": "everything", "random_start": True,
               "plando_connections": [{"entrance": door_label(SINGLES[0]).upper(), "exit": door_label(GAME_DOORS["pairs"][SINGLES[1]])},
                                      {"entrance": door_label(GAME_DOORS["pairs"][SINGLES[0]]), "exit": door_label(SINGLES[1])},
                                      {"entrance": HOUSE_EXIT, "exit": door_label("Field_11C")}]}

    def test_the_named_doors_lead_as_planned(self) -> None:
        self.assertEqual(self.world.entrances[SINGLES[0]], GAME_DOORS["pairs"][SINGLES[1]])
        self.assertEqual(self.world.entrances[SINGLES[1]], GAME_DOORS["pairs"][SINGLES[0]])
        self.assertEqual(self.world.start, "Field_11C")


class TestPlandoWithoutEntrances(LASTestBase):
    """Without Entrances or Random Start the plando's doors and start are left out."""
    options = {"plando_connections": SWAP + [{"entrance": HOUSE_EXIT, "exit": "Field_11C"}]}

    def test_left_out(self) -> None:
        self.assertEqual((self.world.entrances, self.world.plando_doors, self.world.start), ({}, {}, None))


class TestPlandoTwice(unittest.TestCase):
    def test_a_door_in_two_connections_is_refused(self) -> None:
        twice = [SWAP[0], {"entrance": GAME_DOORS["pairs"][SINGLES[1]], "exit": SINGLES[2]}]
        with self.assertRaisesRegex(OptionError, GAME_DOORS["pairs"][SINGLES[1]]):
            generate({"entrances": "everything", "plando_connections": twice}, 1, gen_steps)


class TestDoorNames(unittest.TestCase):
    def test_every_shuffled_door_has_a_name_of_its_own(self) -> None:
        doors = {door for group in GAME_DOORS["groups"].values() for door in group}
        self.assertEqual(set(DOOR_NAMES), doors)
        names = [door_label(side) for door in doors for side in (door, GAME_DOORS["pairs"][door])]
        self.assertEqual(len(set(name.lower() for name in names)), len(names))

    def test_a_name_stands_for_its_door(self) -> None:
        for door in DOOR_NAMES:
            self.assertEqual(door_of_label(door_label(door)), door)
            if GAME_DOORS["pairs"][door] != HOUSE_DOOR:
                self.assertEqual(door_of_label(door_label(GAME_DOORS["pairs"][door])), GAME_DOORS["pairs"][door])
            self.assertEqual(door_of_label(door), door)
        self.assertEqual(door_label(HOUSE_DOOR), HOUSE_EXIT)
        self.assertEqual(door_of_label(HOUSE_EXIT), HOUSE_EXIT)

    def test_plando_takes_names_and_ids(self) -> None:
        inside = GAME_DOORS["pairs"][SINGLES[1]]
        self.assertTrue(PlandoDoors.can_connect(door_label(SINGLES[0]), door_label(inside)))
        self.assertTrue(PlandoDoors.can_connect(SINGLES[0], inside))
        self.assertTrue(PlandoDoors.can_connect(HOUSE_EXIT, door_label(next(iter(START_DOORS)))))
        self.assertFalse(PlandoDoors.can_connect(door_label(SINGLES[0]), door_label(SINGLES[1])))
        self.assertIn(door_label(SINGLES[0]).lower(), PlandoDoors.entrances)


class TestDungeonMapFails(unittest.TestCase):
    def test_no_map_reaching_everything_is_a_placement_error(self) -> None:
        with mock.patch.object(LASWorld, "_reaches", return_value=False), self.assertRaises(PlacementError):
            generate({"shuffled_dungeons": "dungeons"}, 1, ("generate_early",))
