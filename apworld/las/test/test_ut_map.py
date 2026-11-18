"""Universal Tracker's map tab (ut_map.py): tracker/maps.json, tracker/locations.json and tracker/items.json, which
scripts/ut_map.py writes, name the world's maps, locations, door entrances and items."""
import json
import unittest
from pathlib import Path

from worlds.AutoWorld import AutoWorldRegister

from . import generate_world
from .. import GOAL_LOCATION
from ..items import GAME
from ..lasr import CATEGORIES, DOORS, DUNGEONS, WARP_POINTS, WRITTEN_LOGIC, door_entrance, door_region
from ..locations import LOCATION_NAMES
from ..ut_map import (CONNECTOR_INSIDES, LOCATION_DUNGEONS, MAPS, TRACKER_WORLD, check_label, connector_exits,
                      door_pin_colour,
                      dungeon_map, dungeon_squares, hidden_door_entrances, hidden_locations, map_index,
                      zone_places)

TRACKER = Path(__file__).resolve().parent.parent / TRACKER_WORLD["map_page_folder"]


def load(name: str):
    return json.loads((TRACKER / name).read_text(encoding="utf-8"))


def group(name: str) -> list:
    return next(g["children"] for g in load(TRACKER_WORLD["map_page_locations"]) if g["name"] == name)


def section_names(name: str) -> set:
    return {s["name"] for loc in group(name) for s in loc["sections"]}


class TestUTMap(unittest.TestCase):
    def test_maps_are_the_packs_under_the_worlds_names(self) -> None:
        self.assertEqual([m["name"] for m in load(TRACKER_WORLD["map_page_maps"])], list(MAPS))
        for name in ("Locations", "Doors"):
            for loc in group(name):
                for pin in loc["map_locations"]:
                    self.assertIn(pin["map"], MAPS, loc["name"])

    def test_every_panel_item_has_an_icon(self) -> None:
        items = {item["name"] for item in load(TRACKER_WORLD["item_page_items"])}
        panel = {item for side in ("left", "right") for layout in TRACKER_WORLD[f"map_page_item_layouts_{side}"]
                 for item in layout["items"]}
        self.assertEqual(items, panel)
        self.assertLessEqual(items, set(AutoWorldRegister.world_types[GAME].item_name_to_id))

    def test_every_location_has_a_pin(self) -> None:
        self.assertEqual(section_names("Locations"), set(AutoWorldRegister.world_types[GAME].location_name_to_id))

    def test_doors_are_the_worlds_entrances(self) -> None:
        dungeon_entrances = {door_entrance(dungeon) for dungeon in DUNGEONS}
        doors = section_names("Doors")
        self.assertLessEqual(doors, set(DOORS["pairs"]) | dungeon_entrances)
        for seed, options in enumerate(({"entrances": "everything", "shuffled_dungeons": "everything",
                                         "random_start": True},
                                        {"entrances": "everything", "shuffled_dungeons": True})):
            with self.subTest(options=options):
                world = generate_world(options, 4300 + seed)
                entrances = {e.name for e in world.multiworld.get_entrances(1)}
                self.assertLessEqual(set(world.door_pool), doors & entrances)
                # the connectors' other ends a door's pin also shows (connector_exits)
                self.assertLessEqual({way for ways in CONNECTOR_INSIDES.values() for way in ways}, entrances)
                self.assertLessEqual(dungeon_entrances & entrances, doors)

    def test_zone_places(self) -> None:
        """Every door and warp point has its spot, and every zone its place, but the warp map and the Dream Shrine,
        which no zone paints and no door of their own places (tracker/zone_places.json: run scripts/ut_map.py after
        Logic Studio changes)."""
        regions, warps = zone_places()
        self.assertEqual(set(warps), set(WARP_POINTS))
        doors = set(DOORS["pairs"]) | set(DOORS["pairs"].values())
        self.assertLessEqual({door_region(door) for door in doors}, set(regions))
        zones = {name for name, node in WRITTEN_LOGIC.items() if node.get("type") in ("region", "room")}
        self.assertEqual(zones - set(regions), {"warp-map", "dream-shrine"})

    def test_connector_exits(self) -> None:
        """A door's pin also shows the ways out of the other ends of the connector it leads into in the slot."""
        self.assertEqual(connector_exits({"Field_11A": "FaceShrineEnter_01A"}, "Field_11A"), ["FaceShrineEnter_01B"])
        self.assertEqual(connector_exits({"Field_11A": "TownWell_01A"}, "Field_11A"), [])  # a single door's inside
        self.assertEqual(connector_exits({}, "Field_11A"), [])  # no Entrances
        # only the ends a way leads to from where Link comes in: Kanalet Castle's back right door's zone has none
        self.assertEqual(connector_exits({"Field_11A": "KanaletCastle_02D"}, "Field_11A"), [])
        self.assertEqual(connector_exits({"Field_11A": "KanaletCastle_02B"}, "Field_11A"), ["KanaletCastle_02C"])
        for level, insides in CONNECTOR_INSIDES.items():
            self.assertGreater(len(insides), 1, level)

    def test_door_pin_colour(self) -> None:
        """A door pin takes its entrances' strongest status. Once the player went through the door, the connector's
        other ends he has not gone out of count too: in logic keeps it in logic, out of reach makes it out of logic, and
        it is collected once he has gone out of every end. Before, they do not count (they would tell where it leads)."""
        door, near, far, other = "Field_15H", "Tamaranch01_02A", "Tamaranch01_01C", "Tamaranch01_04B"
        cases = [
            # two doors: the far end in logic, out of reach, gone out of while now out of reach, gone out of
            ({door: "passed", far: "passable"}, {door}, "in_logic"),
            ({door: "passed", far: "impassable"}, {door}, "out_of_logic"),
            ({door: "passed", far: "impassable"}, {door, far}, "collected"),
            ({door: "passed", far: "passed"}, {door, far}, "collected"),
            ({door: "passed", far: "path"}, {door}, "hinted"),
            # more ends: one gone out of and one not, in logic or out of reach; one in logic and one out of reach
            ({door: "passed", far: "passed", other: "passable"}, {door, far}, "in_logic"),
            ({door: "passed", far: "passed", other: "impassable"}, {door, far}, "out_of_logic"),
            ({door: "passed", far: "passable", other: "impassable"}, {door}, "in_logic"),
            # the door not gone through: its own status only
            ({door: "passable", far: "impassable"}, set(), "in_logic"),
            ({door: "impassable", far: "passable"}, set(), "out_of_logic"),
            # a dungeon map's pin is the inside door's own: its status counts
            ({near: "impassable", door_entrance("tail-cave"): "passed"}, set(), "out_of_logic")]
        for statuses, connected, colour in cases:
            with self.subTest(statuses=statuses, connected=connected):
                self.assertEqual(door_pin_colour(statuses, connected.__contains__), colour)

    def test_dungeon_doors_are_on_their_dungeons_maps(self) -> None:
        """A pin inside each door into a dungeon's level, on that dungeon's map."""
        pins = group("Dungeon doors")
        self.assertEqual({pin["name"] for pin in pins},
                         {DOORS["pairs"][door] for g in ("dungeon", "dungeon-other") for door in DOORS["groups"][g]})
        for pin in pins:
            (spot,) = pin["map_locations"]
            self.assertEqual(spot["map"], MAPS[map_index(pin["name"].partition("_")[0])], pin["name"])

    def test_dungeon_doors_are_the_worlds_entrances(self) -> None:
        """Every dungeon door pin shows: one of its sections is an entrance of the world, with or without shuffled
        doors."""
        for seed, options in enumerate(({}, {"shuffled_dungeons": True},
                                        {"entrances": "everything", "shuffled_dungeons": "everything"})):
            with self.subTest(options=options):
                world = generate_world(options, 4400 + seed)
                entrances = {e.name for e in world.multiworld.get_entrances(1)}
                for pin in group("Dungeon doors"):
                    self.assertTrue({s["name"] for s in pin["sections"]} & entrances, pin["name"])

    def test_a_dungeon_map_shows_the_door_known_to_lead_into_it(self) -> None:
        every = {door_entrance(door) for door in DUNGEONS}
        hidden = hidden_door_entrances(None, DUNGEONS)
        for dungeon in DUNGEONS:
            self.assertEqual(every - set(hidden[dungeon_map(dungeon)]), {door_entrance(dungeon)})
        leads = dict(zip(DUNGEONS, DUNGEONS[1:] + DUNGEONS[:1]))
        hidden = hidden_door_entrances(leads, [DUNGEONS[0]])
        self.assertEqual(every - set(hidden[dungeon_map(DUNGEONS[1])]), {door_entrance(DUNGEONS[0])})
        for dungeon in DUNGEONS[2:] + DUNGEONS[:1]:
            self.assertEqual(set(hidden[dungeon_map(dungeon)]), every, dungeon)

    def test_the_goal_pin_is_the_worlds_goal_event(self) -> None:
        """The egg's pin is the goal's event location, on the island."""
        ((pin,),) = [group("Goal")]
        self.assertEqual([s["name"] for s in pin["sections"]], [GOAL_LOCATION])
        self.assertEqual([spot["map"] for spot in pin["map_locations"]], [MAPS[0]])
        world = generate_world({}, 4500)
        self.assertIsNone(world.get_location(GOAL_LOCATION).address)

    def test_check_labels(self) -> None:
        """A pin's checks show without their area, and still differ within the pin."""
        self.assertEqual(check_label("Tal Tal Mountains West - Taltal West Chest"), "Taltal West Chest")
        self.assertEqual(check_label(GOAL_LOCATION), GOAL_LOCATION)
        checks = set(LOCATION_NAMES.values())
        for pin in group("Locations"):
            labels = [check_label(s["name"]) for s in pin["sections"] if s["name"] in checks]
            self.assertEqual(len(labels), len(set(labels)), pin["name"])

    def test_hidden_checks(self) -> None:
        """The map leaves off the optional checks and the Trendy Game's prizes the slot does not need; the patch tells
        the in-game map the same ones."""
        trendy = {LOCATION_NAMES[loc] for loc in CATEGORIES["Trendy_Rewards"]}
        yoshi, shells = "Mabe Village - Trendy Prize 1", {"Mabe Village - Trendy Prize 3", "Mabe Village - Trendy Prize 6"}
        for seed, (options, shown) in enumerate((({"seashell_mansion": 0, "rapids": False}, set()),
                                                 ({"trade_quest": True, "seashell_mansion": 5}, {yoshi} | shells),
                                                 ({"goal": "seashell_hunt", "seashell_mansion": 0}, shells))):
            with self.subTest(options=options):
                world = generate_world(options, 4600 + seed)
                hidden = set(hidden_locations(world))
                self.assertEqual(hidden - trendy, {LOCATION_NAMES[loc] for loc in world.optional})
                self.assertEqual(trendy - hidden, shown)
                in_game = {LOCATION_NAMES[loc] for loc in world.patch_data()["map_hidden_locations"]}
                self.assertEqual(in_game, {LOCATION_NAMES[loc] for loc in world.shuffled} & hidden)
                self.assertTrue(trendy - shown <= in_game)
                if not options.get("rapids", True):
                    self.assertIn("Rapids Ride - Rapids Race 30", hidden)

    def test_dungeon_squares(self) -> None:
        """The island's dungeon check squares: at each door known to lead into a dungeon, that dungeon's checks; doors
        whose dungeon is unknown have none."""
        self.assertEqual(dungeon_squares({d: d for d in DUNGEONS}, DUNGEONS), {d: d for d in DUNGEONS})
        leads = dict(zip(DUNGEONS, DUNGEONS[1:] + DUNGEONS[:1]))
        self.assertEqual(dungeon_squares(leads, [DUNGEONS[0]]), {DUNGEONS[0]: DUNGEONS[1]})
        self.assertEqual(dungeon_squares(leads, []), {})
        self.assertEqual(set(LOCATION_DUNGEONS.values()), set(DUNGEONS))
        self.assertEqual(LOCATION_DUNGEONS["Tail Cave - Moldorm"], "tail-cave")
        doors = {s["name"] for pin in group("Doors") for s in pin["sections"]}
        self.assertLessEqual({door_entrance(d) for d in DUNGEONS}, doors)

    def test_stair_letters(self) -> None:
        """Each letter on a dungeon's map marks at least two stairs; Turtle Rock's six side-view passages, A to F."""
        maps = {m["name"]: m for m in load(TRACKER_WORLD["map_page_maps"])}
        for name in MAPS[1:]:
            letters = [s["letter"] for s in maps[name].get("stairs", ())]
            for letter in set(letters):
                self.assertGreaterEqual(letters.count(letter), 2, (name, letter))
        self.assertEqual(sorted(set(s["letter"] for s in maps["D8 - Turtle Rock"]["stairs"])), list("ABCDEF"))

    def test_the_map_follows_the_level(self) -> None:
        self.assertEqual(MAPS[map_index("Field")], MAPS[0])
        self.assertEqual(MAPS[map_index("Lv01TailCave")], "D1 - Tail Cave")
        self.assertEqual(MAPS[map_index("Lv08TurtleRock")], "D8 - Turtle Rock")
        self.assertEqual(MAPS[map_index("Lv10ClothesDungeon")], "D0 - Color Dungeon")
        for level in ("Lv09WindFishsEgg", "MarinTarinHouse", "", None):
            self.assertEqual(map_index(level), 0, level)
