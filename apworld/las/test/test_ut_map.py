"""Universal Tracker's map tab (ut_map.py): tracker/maps.json, tracker/locations.json and tracker/items.json, which
scripts/ut_map.py writes, name the world's maps, locations, door entrances and items."""
import json
import unittest
from pathlib import Path

from worlds.AutoWorld import AutoWorldRegister

from . import generate_world
from ..items import GAME
from ..lasr import DOORS, DUNGEONS, door_entrance
from ..ut_map import MAPS, TRACKER_WORLD, dungeon_map, hidden_door_entrances, map_index

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
                self.assertLessEqual(dungeon_entrances & entrances, doors)

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

    def test_the_map_follows_the_level(self) -> None:
        self.assertEqual(MAPS[map_index("Field")], MAPS[0])
        self.assertEqual(MAPS[map_index("Lv01TailCave")], "D1 - Tail Cave")
        self.assertEqual(MAPS[map_index("Lv08TurtleRock")], "D8 - Turtle Rock")
        self.assertEqual(MAPS[map_index("Lv10ClothesDungeon")], "D0 - Color Dungeon")
        for level in ("Lv09WindFishsEgg", "MarinTarinHouse", "", None):
            self.assertEqual(map_index(level), 0, level)
