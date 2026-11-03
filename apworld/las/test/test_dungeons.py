"""Dungeon entrance shuffle: the doors' map in the patch and slot data, and the logic following each door."""
import io
import random
import unittest

from . import LASTestBase
from ..items import ITEM_TABLE
from ..lasr import (DUNGEONS, ENTERED_BY_DOOR, FACE_SHRINE_ROOM, ITEM_DEFS, LOGIC, door_entrance, door_logic,
                    door_node, dungeon_logic, dungeon_region, shuffle_dungeons)
from ..lasr_settings import logic_tokens
from ..tracker import LogicTracker

IDENTITY = {d: d for d in DUNGEONS}
SWAP = {**IDENTITY, "tail-cave": "turtle-rock", "turtle-rock": "tail-cave"}
SHRINE_SWAP = {**IDENTITY, "face-shrine": "key-cavern", "key-cavern": "face-shrine"}


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
        logic = dungeon_logic(LOGIC, SHRINE_SWAP)
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
        """Through Tail Cave's door into Turtle Rock: Turtle Rock and its top without the Ocarina, Tail Cave not
        without it."""
        lasr = {data.lasr_name: name for name, data in ITEM_TABLE.items()}
        everything = [lasr[k] for k, v in ITEM_DEFS.items() if k in lasr for _ in range(v["quantity"])]
        items = [name for name in everything if name != lasr["ocarina"]]
        tokens = {"open-mabe": True, "free-book": True, "shuffled-bombs": True, "can-steal": True}
        d1 = [k for k in LOGIC if k.startswith("D1-") and LOGIC[k]["type"] == "item"]
        d8 = [k for k in LOGIC if k.startswith("D8-") and LOGIC[k]["type"] == "item"] + ["above-turtle-rock"]
        vanilla = LogicTracker("basic", tokens, [], d1 + d8).in_logic(items)
        swapped = LogicTracker("basic", tokens, [], d1 + d8, SWAP).in_logic(items)
        self.assertTrue(set(d1) <= vanilla and not set(d8) & vanilla)
        self.assertTrue(set(d8) <= swapped and not set(d1) & swapped)

    def test_turtle_rock_top(self) -> None:
        """The top of Turtle Rock is a region of its own, reached from Turtle Rock's inside, with the heart piece."""
        self.assertEqual(LOGIC["turtle-rock-top"], {"type": "region", "condition-basic": "D8-4C"})
        self.assertEqual(LOGIC["above-turtle-rock"]["region"], "turtle-rock-top")
        self.assertFalse(any(key.startswith("condition-") for key in LOGIC["above-turtle-rock"]))

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

    def test_client_publishes_found_doors(self) -> None:
        """A dungeon the game reports entered (DUNGEON) finds the door that leads into it: the client adds it to
        las_doors_<team>_<slot> once per connection, for the slot of the seed the game runs only. Turtle Rock is behind
        Tail Cave's door."""
        import asyncio
        from ..client import LASContext, PROTOCOL_VERSION

        async def run() -> list:
            ctx = LASContext(None, None, "127.0.0.1", 0)
            sent = []

            async def send_msgs(msgs):
                sent.extend(msgs)
            ctx.send_msgs = send_msgs
            ctx.server = object()
            ctx.slot, ctx.team = 1, 0
            ctx.slot_data = {"dungeon_entrances": SWAP}
            ctx.slot_seed_hash = "aaaa"
            # another seed's game: not its doors; then the game of the slot's seed, SYNC sending its dungeons again
            for line in (f"HELLO {PROTOCOL_VERSION} bbbb", "DUNGEON 8", f"HELLO {PROTOCOL_VERSION} aaaa",
                         "DUNGEON 8", "DUNGEON 3", "DUNGEON 8", "DUNGEON 3", "DUNGEON 9"):
                ctx.handle_game_line(line)
                await asyncio.sleep(0)
            sent.append("reconnected")
            ctx.doors_sent.clear()  # a new connection to the server sends them again
            await ctx.send_doors()
            return [msg if isinstance(msg, str) else msg["operations"] for msg in sent
                    if isinstance(msg, str) or (msg["cmd"] == "Set" and msg["key"] == "las_doors_0_1")]
        sent = asyncio.run(run())
        self.assertEqual(sent, [
            [{"operation": "update", "value": {"tail-cave": "turtle-rock"}}],
            [{"operation": "update", "value": {"key-cavern": "key-cavern"}}],
            "reconnected",
            [{"operation": "update", "value": {"tail-cave": "turtle-rock", "key-cavern": "key-cavern"}}],
        ])


class TestDoorLogic(unittest.TestCase):
    """The world's logic: each door a region holding the door's conditions, the regions doors lead into entered only
    through a door's entrance (lasr.door_logic)."""

    def test_doors(self) -> None:
        logic = door_logic(LOGIC)
        for dungeon in DUNGEONS:
            self.assertEqual(logic[door_node(dungeon)]["type"], "region")
        self.assertEqual(conditions(logic[door_node("tail-cave")]), conditions(LOGIC["tail-cave"]))
        self.assertEqual(conditions(logic[door_node("face-shrine")]),
                         conditions(dungeon_logic(LOGIC, SHRINE_SWAP)["key-cavern"]))
        self.assertEqual(set(logic) - set(LOGIC), {door_node(dungeon) for dungeon in DUNGEONS})

    def test_insides(self) -> None:
        """No way into a dungeon but its door's entrance, and the Face Shrine's side room into its entrance room."""
        logic = door_logic(LOGIC)
        self.assertEqual(ENTERED_BY_DOOR, set(DUNGEONS) - {"face-shrine"})
        for region in ENTERED_BY_DOOR:
            self.assertEqual(conditions(logic[region]), {}, region)
            self.assertEqual(logic[region]["type"], LOGIC[region]["type"])
        room = conditions(logic[FACE_SHRINE_ROOM])
        self.assertEqual(list(room), ["condition-basic"])
        self.assertTrue(room["condition-basic"].startswith("D6-3A & "))
        self.assertEqual(dungeon_region("face-shrine"), FACE_SHRINE_ROOM)
        self.assertEqual(logic["face-shrine"], LOGIC["face-shrine"])
        changed = {k for k in LOGIC if logic[k] != LOGIC[k]}
        self.assertEqual(changed, set(ENTERED_BY_DOOR) | {FACE_SHRINE_ROOM})


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
        for door, dungeon in entrances.items():
            entrance = self.world.get_entrance(door_entrance(door))
            self.assertEqual(entrance.parent_region.name, door_node(door))
            self.assertEqual(entrance.connected_region.name, dungeon_region(dungeon))
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
