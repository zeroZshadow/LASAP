"""Dungeon entrance shuffle: the doors' map in the patch and slot data, and the logic following each door."""
import io
import random
import unittest

from . import LASTestBase
from ..items import ITEM_TABLE
from ..lasr import DUNGEONS, FACE_SHRINE_ROOM, ITEM_DEFS, LOGIC, dungeon_logic, shuffle_dungeons
from ..lasr_settings import logic_tokens
from ..tracker import LogicTracker

IDENTITY = {d: d for d in DUNGEONS}
SWAP = {**IDENTITY, "tail-cave": "turtle-rock", "turtle-rock": "tail-cave"}


def conditions(node: dict) -> dict:
    return {k: v for k, v in node.items() if k.startswith("condition-")}


class TestDungeonLogic(unittest.TestCase):
    def test_unshuffled_is_lasr_logic(self) -> None:
        self.assertIs(dungeon_logic(LOGIC, IDENTITY), LOGIC)

    def test_dungeon_needs_the_door_leading_into_it(self) -> None:
        logic = dungeon_logic(LOGIC, SWAP)
        self.assertEqual(conditions(logic["turtle-rock"]), conditions(LOGIC["tail-cave"]))
        self.assertEqual(conditions(logic["tail-cave"]), conditions(LOGIC["turtle-rock"]))
        self.assertEqual(logic["key-cavern"], LOGIC["key-cavern"])
        self.assertEqual(logic["turtle-rock"]["type"], "region")

    def test_face_shrine_door(self) -> None:
        """The Face Shrine's door is the outside part of its entrance room; its region (either entrance room) stays."""
        logic = dungeon_logic(LOGIC, {**IDENTITY, "face-shrine": "key-cavern", "key-cavern": "face-shrine"})
        self.assertEqual(logic["key-cavern"]["condition-basic"], "river & face-key & (hookshot | rooster)")
        self.assertEqual(logic["key-cavern"]["condition-hell"], LOGIC[FACE_SHRINE_ROOM]["condition-hell"])
        room = logic[FACE_SHRINE_ROOM]
        self.assertTrue(room["condition-basic"].startswith(f"({LOGIC['key-cavern']['condition-basic']}) | (D6-3A"))
        self.assertEqual(room["condition-glitched"], LOGIC["key-cavern"]["condition-glitched"])
        self.assertNotIn("condition-hell", room)
        self.assertEqual(logic["face-shrine"], LOGIC["face-shrine"])

    def test_every_map_keeps_the_conditions(self) -> None:
        """Each door's conditions end up behind exactly one door, for any map."""
        rng = random.Random(3)
        for _ in range(20):
            entrances = shuffle_dungeons(rng)
            self.assertEqual(sorted(entrances.values()), sorted(DUNGEONS))
            logic = dungeon_logic(LOGIC, entrances)
            self.assertEqual(set(logic), set(LOGIC))
            changed = {k for k in LOGIC if logic[k] != LOGIC[k]}
            self.assertLessEqual(changed, set(DUNGEONS) | {FACE_SHRINE_ROOM})

    def test_tracker_follows_the_doors(self) -> None:
        """Through Tail Cave's door into Turtle Rock: Turtle Rock without the Ocarina, Tail Cave not without it."""
        lasr = {data.lasr_name: name for name, data in ITEM_TABLE.items()}
        everything = [lasr[k] for k, v in ITEM_DEFS.items() if k in lasr for _ in range(v["quantity"])]
        items = [name for name in everything if name != lasr["ocarina"]]
        tokens = {"open-mabe": True, "free-book": True, "shuffled-bombs": True, "can-steal": True}
        d1 = [k for k in LOGIC if k.startswith("D1-") and LOGIC[k]["type"] == "item"]
        d8 = [k for k in LOGIC if k.startswith("D8-") and LOGIC[k]["type"] == "item"]
        vanilla = LogicTracker("basic", tokens, [], d1 + d8).in_logic(items)
        swapped = LogicTracker("basic", tokens, [], d1 + d8, SWAP).in_logic(items)
        self.assertTrue(set(d1) <= vanilla and not set(d8) & vanilla)
        self.assertTrue(set(d8) <= swapped and not set(d1) & swapped)

    def test_tracker_doors(self) -> None:
        """Behind a reachable door: every check of the dungeon it leads into, in logic or not. With the Tail Key alone,
        Tail Cave's door (into Turtle Rock) and none of Turtle Rock's checks."""
        lasr = {data.lasr_name: name for name, data in ITEM_TABLE.items()}
        tokens = {"open-mabe": True, "free-book": True, "shuffled-bombs": True, "can-steal": True}
        d1 = [k for k in LOGIC if k.startswith("D1-") and LOGIC[k]["type"] == "item"]
        d8 = [k for k in LOGIC if k.startswith("D8-") and LOGIC[k]["type"] == "item"]
        reachable, doors = LogicTracker("basic", tokens, [], d1 + d8, SWAP).sweep([lasr["tail-key"]])
        self.assertEqual(doors, set(d8))
        self.assertFalse(reachable & set(d8))
        reachable, doors = LogicTracker("basic", tokens, [], d1 + d8).sweep([lasr["tail-key"]])
        self.assertEqual(doors, set(d1))
        self.assertTrue(reachable < set(d1))

    def test_client_sends_doors(self) -> None:
        """The client sends the locations behind reachable doors (DOORS) before those in logic (INLOGIC)."""
        import asyncio
        from ..client import LASContext
        from ..ids import LOCATION_BASE, LOCATION_OFFSETS

        lasr = {data.lasr_name: name for name, data in ITEM_TABLE.items()}
        d1 = [k for k in LOGIC if k.startswith("D1-") and LOGIC[k]["type"] == "item"]
        d8 = [k for k in LOGIC if k.startswith("D8-") and LOGIC[k]["type"] == "item"]
        tokens = {"open-mabe": True, "free-book": True, "shuffled-bombs": True, "can-steal": True}

        class Writer:
            def __init__(self) -> None:
                self.lines = []

            def write(self, data: bytes) -> None:
                self.lines.append(data.decode().rstrip("\n"))

        writer = Writer()

        async def run() -> None:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            ctx.slot, ctx.team, ctx.game_writer = 1, 0, writer  # type: ignore[assignment]
            ctx.server_locations = {LOCATION_BASE + LOCATION_OFFSETS[loc] for loc in d1 + d8}
            ctx.slot_data = {"dungeon_entrances": SWAP, "logic": {
                "preset": "basic", "tokens": tokens, "vanilla": [], "optional": [], "starting": [lasr["tail-key"]]}}
            ctx.make_logic_tracker()
            ctx.send_logic()
            ctx.send_logic()  # nothing changed: nothing sent
        asyncio.run(run())
        doors = " ".join(str(local_id) for local_id in sorted(LOCATION_OFFSETS[loc] for loc in d8))
        self.assertEqual(writer.lines, ["DOORS CLEAR", f"DOORS {doors}", "INLOGIC CLEAR"])


class TestShuffledDungeons(LASTestBase):
    options = {"shuffled_dungeons": True}
    seed = 7

    def test_patch_slot_data_and_spoiler(self) -> None:
        entrances = self.world.dungeon_entrances
        self.assertEqual(sorted(entrances), sorted(DUNGEONS))
        self.assertEqual(sorted(entrances.values()), sorted(DUNGEONS))
        self.assertNotEqual(entrances, IDENTITY)
        self.assertEqual(self.world.fill_slot_data()["dungeon_entrances"], entrances)
        patch = self.world.patch_data()
        self.assertEqual(patch["dungeon_entrances"], entrances)
        self.assertIs(patch["settings"]["Shuffled Dungeons"], True)
        self.assertEqual(self.world.compiler.logic, dungeon_logic(LOGIC, entrances))
        spoiler = io.StringIO()
        self.world.write_spoiler(spoiler)
        self.assertIn("Dungeon entrances", spoiler.getvalue())
        self.assertIn("Tail Cave entrance -> ", spoiler.getvalue())


class TestUnshuffledDungeons(LASTestBase):
    def test_identity(self) -> None:
        self.assertEqual(self.world.dungeon_entrances, IDENTITY)
        self.assertEqual(self.world.patch_data()["dungeon_entrances"], IDENTITY)
        self.assertIs(self.world.patch_data()["settings"]["Shuffled Dungeons"], False)
        self.assertNotIn("shuffled-dungeons", logic_tokens(self.world.options))
        spoiler = io.StringIO()
        self.world.write_spoiler(spoiler)
        self.assertNotIn("Dungeon entrances", spoiler.getvalue())
