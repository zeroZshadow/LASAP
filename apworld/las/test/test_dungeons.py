"""Dungeon entrance shuffle: the doors' map in the patch and slot data, and the logic following each door."""
import io
import random
import unittest
from unittest import mock

from . import LASTestBase
from .. import LASWorld
from ..items import LASR_TO_ITEM
from ..lasr import (DOOR_LOGIC, DOORS, DUNGEON_DOORS, DUNGEONS, ENTERED_BY_DOOR, ITEM_DEFS, LOGIC, WRITTEN_LOGIC,
                    door_entrance, door_logic, door_node, dungeon_name, dungeon_of, dungeon_region, entrance_logic, plain_logic,
                    shuffle_dungeons, slot_doors)
from ..logic import SPEC, node_tokens, parse
from ..lasr_settings import logic_tokens
from ..tracker import LogicTracker

IDENTITY = {d: d for d in DUNGEONS}
SWAP = {**IDENTITY, "tail-cave": "turtle-rock", "turtle-rock": "tail-cave"}
SHRINE_SWAP = {**IDENTITY, "face-shrine": "key-cavern", "key-cavern": "face-shrine"}
TOKENS = {"open-mabe": True, "free-book": True, "shuffled-bombs": True, "can-steal": True}
EVERYTHING = [LASR_TO_ITEM[k] for k, v in ITEM_DEFS.items() if k in LASR_TO_ITEM for _ in range(v["quantity"])]


def checks(n: int) -> list:
    """Dungeon n's item locations."""
    return [k for k in LOGIC if k.startswith(f"D{n}-") and LOGIC[k]["type"] == "item"]


def conditions(node: dict) -> dict:
    return {k: v for k, v in node.items() if k.startswith("condition-")}


def door_zone(door: str) -> str:
    return next(zone for zone, node in WRITTEN_LOGIC.items() if door in (node.get("doors") or {}))


def inside_zone(dungeon: str) -> str:
    """The zone of a dungeon's inside door."""
    return door_zone(DOORS["pairs"][DUNGEON_DOORS[dungeon]])


def shuffled(entrances: dict) -> dict:
    return plain_logic(WRITTEN_LOGIC, slot_doors(None, entrances))


class TestDungeonLogic(unittest.TestCase):
    def test_a_locations_dungeon_is_in_its_name(self) -> None:
        self.assertEqual(dungeon_of("D1-moldorm"), "tail-cave")
        self.assertEqual(dungeon_of("D8-instrument"), "turtle-rock")
        self.assertEqual(dungeon_of("D0-fairy-1"), "color-dungeon")
        self.assertIsNone(dungeon_of("goriya-trader"))
        self.assertIsNone(dungeon_of("above-turtle-rock"))

    def test_unshuffled_is_lasr_logic(self) -> None:
        self.assertIs(entrance_logic(None, IDENTITY), DOOR_LOGIC)
        self.assertIs(slot_doors(None, IDENTITY), DOORS)

    def test_a_door_leads_into_the_region_just_inside_it(self) -> None:
        """Its way in is the door's, and it leads on to the zone of the dungeon's inside door."""
        for dungeon in DUNGEONS:
            region = dungeon_region(dungeon)
            self.assertIn(door_zone(DUNGEON_DOORS[dungeon]), LOGIC[region]["condition-basic"], dungeon)
            self.assertIn(region, LOGIC[inside_zone(dungeon)]["condition-basic"], dungeon)

    def test_dungeon_needs_the_door_leading_into_it(self) -> None:
        logic = shuffled(SWAP)
        self.assertEqual(conditions(logic["turtle-rock"]), conditions(LOGIC["tail-cave"]))
        self.assertEqual(conditions(logic["tail-cave"]), conditions(LOGIC["turtle-rock"]))
        self.assertEqual(logic["key-cavern"], LOGIC["key-cavern"])

    def test_the_way_out_leads_where_the_way_in_came_from(self) -> None:
        """Out of Turtle Rock's door to the place in front of Tail Cave's door, which leads into it."""
        logic = shuffled(SWAP)
        outside = door_zone(DUNGEON_DOORS["tail-cave"])
        self.assertIn(inside_zone("turtle-rock"), logic[outside]["condition-basic"])
        self.assertNotIn(inside_zone("tail-cave"), logic[outside]["condition-basic"])

    def test_face_shrine_door(self) -> None:
        """The Face Shrine's door leads into the region in front of its entrance room."""
        self.assertEqual(dungeon_region("face-shrine"), SPEC["dungeons"]["face-shrine"])
        logic = shuffled(SHRINE_SWAP)
        self.assertEqual(conditions(logic["key-cavern"]), conditions(LOGIC[dungeon_region("face-shrine")]))
        self.assertEqual(conditions(logic[dungeon_region("face-shrine")]), conditions(LOGIC["key-cavern"]))

    def test_every_map_keeps_the_conditions(self) -> None:
        """Each door's way in ends up behind exactly one door, for any map."""
        rng = random.Random(3)
        ways = sorted(str(conditions(LOGIC[dungeon_region(d)])) for d in DUNGEONS)
        for _ in range(20):
            entrances = shuffle_dungeons(rng)
            self.assertEqual(sorted(entrances.values()), sorted(DUNGEONS))
            logic = shuffled(entrances)
            self.assertEqual(set(logic), set(LOGIC))
            self.assertEqual(sorted(str(conditions(logic[dungeon_region(d)])) for d in DUNGEONS), ways)

    def test_tracker_follows_the_doors(self) -> None:
        """Through Tail Cave's door into Turtle Rock: Turtle Rock and its top without the Ocarina, Tail Cave not
        without it."""
        items = [name for name in EVERYTHING if name != LASR_TO_ITEM["ocarina"]]
        d1, d8 = checks(1), checks(8) + ["above-turtle-rock"]
        vanilla = LogicTracker("basic", TOKENS, [], d1 + d8).in_logic(items)
        swapped = LogicTracker("basic", TOKENS, [], d1 + d8, SWAP).in_logic(items)
        self.assertTrue(set(d1) <= vanilla and not set(d8) & vanilla)
        self.assertTrue(set(d8) <= swapped and not set(d1) & swapped)

    def test_turtle_rock_top(self) -> None:
        """The top of Turtle Rock is a region of its own, reached from Turtle Rock's inside, with the heart piece."""
        top = LOGIC["turtle-dungeon-top"]
        self.assertEqual(top["type"], "region")
        self.assertTrue(node_tokens(parse(top["condition-basic"])))
        self.assertTrue(all(name.startswith("D8.") for name in node_tokens(parse(top["condition-basic"]))))
        self.assertEqual(LOGIC["above-turtle-rock"]["region"], "turtle-dungeon-top")
        self.assertFalse(any(key.startswith("condition-") for key in LOGIC["above-turtle-rock"]))

    def test_tracker_doors(self) -> None:
        """Behind a reachable door: every check of the dungeon it leads into, in logic or not. With the Tail Key alone,
        Tail Cave's door (into Turtle Rock) and none of Turtle Rock's checks."""
        d1, d8 = checks(1), checks(8)
        reachable, doors = LogicTracker("basic", TOKENS, [], d1 + d8, SWAP).sweep([LASR_TO_ITEM["tail-key"]])
        self.assertEqual(doors, set(d8))
        self.assertFalse(reachable & set(d8))
        reachable, doors = LogicTracker("basic", TOKENS, [], d1 + d8).sweep([LASR_TO_ITEM["tail-key"]])
        self.assertEqual(doors, set(d1))
        self.assertTrue(reachable < set(d1))

    def test_tracker_waits_for_found_doors(self) -> None:
        """A dungeon is in logic only once the door that leads into it is found, and so is what its inside reaches:
        the Rapids' south island chest through the Face Shrine, the top of Turtle Rock. The door's own look (DOORS)
        does not wait. The last door counts as found once the other eight are."""
        items = EVERYTHING
        d6, d8 = checks(6) + ["rapids-south-island"], checks(8) + ["above-turtle-rock"]
        entrances = {**IDENTITY, "key-cavern": "face-shrine", "face-shrine": "turtle-rock",
                     "turtle-rock": "key-cavern"}
        tracker = LogicTracker("basic", TOKENS, [], d6 + d8, entrances)
        others = [door for door in DUNGEONS if door not in ("key-cavern", "face-shrine", "turtle-rock")]
        reachable, doors = tracker.sweep(items, others)
        self.assertFalse(reachable & set(d6 + d8))
        self.assertEqual(doors, set(d6 + d8) - {"rapids-south-island", "above-turtle-rock"})
        reachable = tracker.in_logic(items, others + ["key-cavern"])
        self.assertTrue(set(d6) <= reachable and not set(d8) & reachable)
        reachable = tracker.in_logic(items, others + ["face-shrine"])
        self.assertTrue(set(d8) <= reachable and not set(d6) & reachable)
        self.assertTrue(set(d6 + d8) <= tracker.in_logic(items, others + ["key-cavern", "face-shrine"]))
        self.assertEqual(tracker.in_logic(items), tracker.in_logic(items, DUNGEONS))


class TestDoorLogic(unittest.TestCase):
    """The world's logic: each door a region holding the door's conditions, the regions doors lead into entered only
    through a door's entrance (lasr.door_logic)."""

    def test_doors(self) -> None:
        for dungeon in DUNGEONS:
            self.assertEqual(DOOR_LOGIC[door_node(dungeon)]["type"], "region")
            self.assertEqual(conditions(DOOR_LOGIC[door_node(dungeon)]), conditions(LOGIC[dungeon_region(dungeon)]))
        self.assertEqual(set(DOOR_LOGIC) - set(LOGIC), {door_node(dungeon) for dungeon in DUNGEONS})

    def test_insides(self) -> None:
        """No way into the region a door leads into but the door's entrance."""
        self.assertEqual(ENTERED_BY_DOOR, set(SPEC["dungeons"].values()))
        for region in ENTERED_BY_DOOR:
            self.assertEqual(conditions(DOOR_LOGIC[region]), {}, region)
            self.assertEqual(DOOR_LOGIC[region]["type"], "region")
        changed = {k for k in LOGIC if DOOR_LOGIC[k] != LOGIC[k]}
        self.assertEqual(changed, set(ENTERED_BY_DOOR))

    def test_the_way_out_follows_the_map(self) -> None:
        logic = door_logic(WRITTEN_LOGIC, slot_doors(None, SWAP))
        self.assertIn(inside_zone("turtle-rock"), logic[door_zone(DUNGEON_DOORS["tail-cave"])]["condition-basic"])


class TestShuffledDungeons(LASTestBase):
    options = {"shuffled_dungeons": True}

    def test_patch_slot_data_and_spoiler(self) -> None:
        entrances = self.world.dungeon_entrances
        self.assertEqual(sorted(entrances), sorted(DUNGEONS))
        self.assertEqual(sorted(entrances.values()), sorted(DUNGEONS))
        self.assertNotEqual(entrances, IDENTITY)
        self.assertEqual(self.world.fill_slot_data()["dungeon_entrances"], entrances)
        patch = self.world.patch_data()
        self.assertEqual(patch["dungeon_entrances"], entrances)
        self.assertIs(patch["settings"]["Shuffled Dungeons"], True)
        for door, dungeon in entrances.items():
            entrance = self.world.get_entrance(door_entrance(door))
            self.assertEqual(entrance.parent_region.name, door_node(door))
            self.assertEqual(entrance.connected_region.name, dungeon_region(dungeon))
        self.world.write_spoiler_header(io.StringIO())
        doors = {entry["entrance"]: entry["exit"] for entry in self.multiworld.spoiler.entrances.values()}
        self.assertEqual(doors, {door_entrance(door): dungeon_name(dungeon) for door, dungeon in entrances.items()})


class TestDoorDeadlock(LASTestBase):
    """With Boss Drops and Free Gifts off the Hookshot stays in Catfish's Maw and the Rooster at the weathervane: behind
    a door that needs one of them, Catfish's Maw and much of the island stay out of reach."""
    options = {"shuffled_dungeons": True, "boss_drops": False, "free_gifts": False}
    run_default_tests = False
    seed = 1
    STUCK = {**IDENTITY, "eagle-tower": "catfish-maw", "catfish-maw": "eagle-tower"}

    def test_stuck_map_rolls_again(self) -> None:
        with mock.patch(f"{LASWorld.__module__}.shuffle_dungeons", side_effect=[self.STUCK, self.STUCK, SWAP]):
            self.world_setup(self.seed)
        self.assertEqual(self.world.dungeon_entrances, SWAP)
        for door, dungeon in SWAP.items():
            entrance = self.world.get_entrance(door_entrance(door))
            self.assertEqual(entrance.connected_region.name, dungeon_region(dungeon))
            self.assertEqual(entrance.connected_region.entrances.count(entrance), 1)

    def reaches(self) -> bool:
        return self.world._reaches(self.world.entrances, self.world.goal_instruments, self.world.goal_seashells)

    def test_every_seed_reaches_everything(self) -> None:
        for seed in range(30):
            self.world_setup(seed)
            self.assertTrue(self.reaches(), seed)
            into_catfish = next(d for d, target in self.world.dungeon_entrances.items() if target == "catfish-maw")
            self.assertNotIn(into_catfish, ("eagle-tower", "face-shrine", "turtle-rock"), seed)

    def test_stuck_map_is_seen(self) -> None:
        with mock.patch.object(LASWorld, "_place_doors", return_value=True), mock.patch.object(LASWorld, "pre_fill"), \
                mock.patch(f"{LASWorld.__module__}.shuffle_dungeons", return_value=self.STUCK):
            self.world_setup(self.seed)
        self.assertFalse(self.reaches())


class TestUnshuffledDungeons(LASTestBase):
    def test_identity(self) -> None:
        self.assertEqual(self.world.dungeon_entrances, IDENTITY)
        self.assertEqual(self.world.patch_data()["dungeon_entrances"], IDENTITY)
        self.assertIs(self.world.patch_data()["settings"]["Shuffled Dungeons"], False)
        self.assertNotIn("shuffled-dungeons", logic_tokens(self.world.options))
        self.world.write_spoiler_header(io.StringIO())
        self.assertEqual(self.multiworld.spoiler.entrances, {})
