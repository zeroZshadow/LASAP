"""Entrances: which doors move (entrances.py), the doors as AP entrances, plando, and the patch's jumps for the moved
doors."""
import sys
import unittest
from pathlib import Path

from . import LASTestBase
from ..entrances import shuffle_pool
from ..lasr import DOORS as GAME_DOORS, ENTRANCE_POOL, START_DOORS, WRITTEN_LOGIC, door_region, HOUSE_EXIT
from ..logic import ALWAYS, START, parse


def zone(**routes):
    return {"type": "region", **{key.replace("_", "-"): value for key, value in routes.items()}}


# The house in mabe; a two-door cave from mabe up to a ledge; two dead ends in mabe.
DOORS = {"pairs": {"Field_house": "House_01A", "Field_a": "Cave_01A", "Field_b": "Cave_02A", "Field_c": "Shed_01A",
                   "Field_d": "Hut_01A"},
         "one-way": {},
         "groups": {"dungeon": [], "dungeon-other": [], "connector": ["Field_a", "Field_b"],
                    "single": ["Field_c", "Field_d", "Field_house"]}}
WRITTEN = {
    START: zone(doors={"House_01A": ALWAYS}),
    "mabe": zone(doors={"Field_house": ALWAYS, "Field_a": ALWAYS, "Field_c": ALWAYS, "Field_d": "bombs"}),
    "ledge": zone(doors={"Field_b": ALWAYS}),
    "cave": zone(doors={"Cave_01A": ALWAYS, "Cave_02A": ALWAYS}),
    "shed": zone(doors={"Shed_01A": ALWAYS}),
    "hut": zone(doors={"Hut_01A": ALWAYS}),
}


class TestShufflePool(unittest.TestCase):
    def test_the_starts_doors_stay(self) -> None:
        self.assertEqual(shuffle_pool(WRITTEN, DOORS), ["Field_c", "Field_d", "Field_a", "Field_b"])


class TestDoorJumps(unittest.TestCase):
    def test_a_moved_door_leads_in_and_back_out(self) -> None:
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "patcher"))
        import ap_entrances
        lines = ap_entrances.door_jumps({"Field_01A": "Cave_02B", "Field_03C": "House_01A"},
                                        {"Field_01A": "House_01A", "Field_03C": "Cave_02B"})
        self.assertEqual(lines, ["Field House House_01A Cave Cave_02B", "Cave Field Field_03C Field Field_01A",
                                 "Field Cave Cave_02B House House_01A", "House Field Field_01A Field Field_03C"])


    def test_every_jump_home_follows_tarins_house(self) -> None:
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "patcher"))
        import ap_entrances
        lines = ap_entrances.door_jumps({"Field_11C": "Cave_02B", "Field_03C": "MarinTarinHouse_01A"},
                                        {"Field_11C": "MarinTarinHouse_01A", "Field_03C": "Cave_02B"})
        self.assertIn("* Field Field_11C Field Field_03C", lines)
        self.assertNotIn("MarinTarinHouse Field Field_11C Field Field_03C", lines)

    def test_a_jump_out_onto_a_covered_door_opens_it(self) -> None:
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "patcher"))
        import ap_entrances
        lines = ap_entrances.door_jumps({"Field_06C": "Cave_02B", "Field_03C": "House_01A"},
                                        {"Field_06C": "House_01A", "Field_03C": "Cave_02B"})
        self.assertIn("Cave Field Field_03C Field Field_06C AppearStairsFld06C", lines)
        self.assertIn("House Field Field_06C Field Field_03C", lines)

    def test_a_random_start_takes_every_jump_home(self) -> None:
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "patcher"))
        import ap_entrances
        self.assertEqual(ap_entrances.start_jumps("Field_06C"), ["* Field Field_11C Field Field_06C AppearStairsFld06C"])
        self.assertEqual(ap_entrances.start_jumps("Field_03C"), ["* Field Field_11C Field Field_03C"])
        self.assertEqual(ap_entrances.start_jumps(None), [])


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

    def test_the_house_leads_out_of_the_start_door(self) -> None:
        world = self.multiworld.worlds[self.player]
        self.assertIn(world.start, START_DOORS)
        self.assertEqual(world.get_entrance(HOUSE_EXIT).connected_region.name, START_DOORS[world.start])

    def test_a_door_leads_on_to_its_own_zone(self) -> None:
        """Whatever door leads to a door's region, it leads on to the zone of that door, and back through it."""
        world = self.multiworld.worlds[self.player]
        zones = {door: zone for zone, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})}
        for door in ENTRANCE_POOL:
            for side in (door, GAME_DOORS["pairs"][door]):
                exits = {exit.name: exit.connected_region.name for exit in world.get_region(door_region(side)).exits}
                self.assertEqual(set(exits.values()) - {zones[side]}, {exits[side]}, side)


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
        self.assertEqual(world.get_entrance(HOUSE_EXIT).connected_region.name, START_DOORS["Field_11C"])


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
