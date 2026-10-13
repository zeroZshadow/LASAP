"""In-game hints (hints.py): which locations the owl statues and books name."""
import unittest

from Fill import distribute_items_restrictive
from test.general import gen_steps, setup_multiworld
from worlds.AutoWorld import AutoWorldRegister, call_all

from ..hints import JOKES, SOURCES
from ..items import GAME, ITEM_TABLE
from ..lasr import item_dungeon, location_dungeon
from ..lasr_settings import kept_locations
from ..locations import LASR_LOCATIONS, LOCATION_NAMES


def filled(seed: int, options):
    world_type = AutoWorldRegister.world_types[GAME]
    multiworld = setup_multiworld([world_type, world_type], gen_steps, seed, options)
    distribute_items_restrictive(multiworld)
    call_all(multiworld, "post_fill")
    return multiworld


class TestHints(unittest.TestCase):
    options = {"start_hints": ["Hookshot"], "start_location_hints": ["Tail Cave - Feather Chest"]}

    def setUp(self) -> None:
        self.multiworld = filled(7, self.options)
        self.world = self.multiworld.worlds[1]
        self.hints = self.world.in_game_hints()
        self.locations = {(loc.player, loc.address): loc for loc in self.multiworld.get_filled_locations()
                          if isinstance(loc.address, int)}

    def hinted(self):
        return [self.locations[hint["player"], hint["location"]] for hint in self.hints.values() if "location" in hint]

    def test_every_source(self) -> None:
        self.assertEqual(list(self.hints), list(SOURCES))
        for hint in self.hints.values():
            self.assertTrue(hint["text"].startswith("Your "), hint)

    def test_own_progression_first(self) -> None:
        hinted = self.hinted()
        self.assertEqual(len(hinted), len(set(hinted)))
        for location in hinted:
            self.assertEqual(location.item.player, 1)
            self.assertTrue(location.item.advancement or location.item.useful, location)
        # useful items and jokes only once no progression item is left to hint
        if any(not loc.item.advancement for loc in hinted) or \
                any(hint["text"] in JOKES for hint in self.hints.values()):
            from ..hints import candidates
            progression = {loc for loc in candidates(self.world) if loc.item.advancement}
            self.assertLessEqual(progression, set(hinted) | self.owl_gifts())
        self.assertTrue(any(loc.player == 2 for loc in hinted))

    def owl_gifts(self):
        return {self.multiworld.get_location(LOCATION_NAMES[source], 1) for source in SOURCES
                if source in LOCATION_NAMES and LOCATION_NAMES[source] in self.multiworld.regions.location_cache[1]}

    def test_text(self) -> None:
        for hint in self.hints.values():
            if "location" not in hint:
                continue
            location = self.locations[hint["player"], hint["location"]]
            where = "at" if location.player == 1 else "in Tester2's world at"
            self.assertEqual(hint["text"], f"Your {location.item.name} is {where} {location.name}.")

    def test_exclusions(self) -> None:
        kept = kept_locations()
        for location in self.hinted():
            item = location.item
            self.assertNotEqual(item.name, "Hookshot")
            self.assertNotEqual(location.name, "Tail Cave - Feather Chest")
            lasr_item = ITEM_TABLE[item.name].lasr_name
            self.assertNotEqual(lasr_item, "seashell")
            if location.player == 1:
                lasr_location = LASR_LOCATIONS[location.name]
                self.assertNotIn(lasr_location, kept)
                self.assertNotIn(lasr_location, self.world.locked)
                self.assertFalse(item_dungeon(lasr_item) >= 0 and location_dungeon(lasr_location) == item_dungeon(
                    lasr_item), location)

    def test_deterministic(self) -> None:
        self.assertEqual(filled(7, self.options).worlds[1].in_game_hints(), self.hints)

    def test_patch_and_spoiler(self) -> None:
        import io
        self.assertEqual(self.world.patch_data()["hints"], self.hints)
        spoiler = io.StringIO()
        self.world.write_spoiler(spoiler)
        self.assertIn("In-game hints (Tester1):", spoiler.getvalue())
        self.assertIn("Library - Shield Book: Your ", spoiler.getvalue())


class TestOwlGifts(TestHints):
    options = {**TestHints.options, "owl_statues": "all"}

    def test_owl_not_its_own_gift(self) -> None:
        for source, hint in self.hints.items():
            if "location" in hint and source in LOCATION_NAMES:
                location = self.locations[hint["player"], hint["location"]]
                self.assertFalse(location.player == 1 and location.name == LOCATION_NAMES[source], source)


class TestTextOnly(unittest.TestCase):
    def test_no_server_hints(self) -> None:
        hints = filled(3, {"in_game_hints": "text"}).worlds[1].in_game_hints()
        self.assertEqual(list(hints), list(SOURCES))
        self.assertTrue(all(set(hint) == {"text"} for hint in hints.values()))

    def test_off(self) -> None:
        world = filled(3, {"in_game_hints": "off"}).worlds[1]
        self.assertEqual(world.in_game_hints(), {})
        self.assertEqual(world.patch_data()["hints"], {})
