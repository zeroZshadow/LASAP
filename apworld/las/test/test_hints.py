"""In-game hints (hints.py): which locations and areas the owl statues and books name, by hint_distribution."""
import re
import unittest
from collections import Counter

from BaseClasses import CollectionState
from Fill import distribute_items_restrictive
from test.general import gen_steps, setup_multiworld
from worlds.AutoWorld import AutoWorldRegister, call_all

from ..hints import ALWAYS, DISTRIBUTIONS, JOKES, KINDS, SOURCES, area, candidates
from ..items import GAME, ITEM_TABLE
from ..lasr import item_dungeon, location_dungeon
from ..lasr_settings import kept_locations
from ..locations import LASR_LOCATIONS, LOCATION_NAMES

LONG_CHECKS = {"trade_quest": True, "dampe": True, "rapids": True, "fishing": True, "seashell_mansion": 50}


def filled(seed: int, options):
    world_type = AutoWorldRegister.world_types[GAME]
    multiworld = setup_multiworld([world_type, world_type], gen_steps, seed, options)
    distribute_items_restrictive(multiworld)
    call_all(multiworld, "post_fill")
    return multiworld


class HintsBase(unittest.TestCase):
    seed = 7
    options = {"start_hints": ["Hookshot"], "start_location_hints": ["Tail Cave - Feather Chest"]}

    def setUp(self) -> None:
        self.multiworld = filled(self.seed, self.options)
        self.world = self.multiworld.worlds[1]
        self.hints = self.world.in_game_hints()
        self.locations = {(loc.player, loc.address): loc for loc in self.multiworld.get_filled_locations()
                          if isinstance(loc.address, int)}

    def of_kind(self, kind: str):
        return [hint for hint in self.hints.values() if hint["kind"] == kind]

    def hinted(self, kind=None):
        return [self.locations[hint["player"], hint["location"]] for hint in self.hints.values()
                if "location" in hint and kind in (None, hint["kind"])]

    def parse(self, pattern: str, text: str) -> "re.Match":
        match = re.fullmatch(pattern, text)
        assert match is not None, text
        return match

    def own(self, area_name: str):
        """Player 1's locations in the area."""
        return [loc for loc in self.multiworld.get_locations(1) if " - " in loc.name and area(loc) == area_name]

    def required(self):
        """Player 1's items in the multiworld without which player 1 cannot win."""
        everything = self.multiworld.get_filled_locations()
        found = set()
        for location in everything:
            if location.item.player != 1 or not location.item.advancement:
                continue
            state = CollectionState(self.multiworld)
            state.sweep_for_advancements([loc for loc in everything if loc is not location])
            if not self.multiworld.completion_condition[1](state):
                found.add(location)
        return found

    def test_every_source(self) -> None:
        self.assertEqual(list(self.hints), list(SOURCES))
        for hint in self.hints.values():
            self.assertIn(hint["kind"], KINDS)
            self.assertTrue(hint["text"], hint)
        texts = [hint["text"] for hint in self.hints.values() if hint["kind"] != "joke"]
        self.assertEqual(len(texts), len(set(texts)))

    def test_server_data_on_single_locations(self) -> None:
        for hint in self.hints.values():
            self.assertEqual("location" in hint, hint["kind"] in ("always", "precise"), hint)
            self.assertEqual("location" in hint, "player" in hint, hint)

    def test_precise(self) -> None:
        hinted = self.hinted("precise")
        self.assertEqual(len(hinted), len(set(hinted)))
        pool = candidates(self.world)
        for location in hinted:
            self.assertIn(location, pool)
            self.assertEqual(location.item.player, 1)
            self.assertTrue(location.item.advancement or location.item.useful, location)
            where = "at" if location.player == 1 else "in Tester2's world at"
            self.assertIn(f"Your {location.item.name} is {where} {location.name}.",
                          [hint["text"] for hint in self.hints.values()])

    def test_exclusions(self) -> None:
        kept = kept_locations()
        for location in self.hinted("precise"):
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

    def test_always(self) -> None:
        for location in self.hinted("always"):
            self.assertEqual(location.player, 1)
            self.assertIn(LASR_LOCATIONS[location.name], ALWAYS)
            self.assertIn(LASR_LOCATIONS[location.name], self.world.shuffled)
            self.assertNotIn(LASR_LOCATIONS[location.name], self.world.locked)
            self.assertTrue(location.item.advancement or location.item.useful, location)

    def test_barren(self) -> None:
        for hint in self.of_kind("barren"):
            name = self.parse(r"(.+) holds nothing anyone needs\.", hint["text"]).group(1)
            locations = self.own(name)
            self.assertGreaterEqual(len(locations), 2, name)
            for location in locations:
                self.assertFalse(location.item.advancement, location)

    def test_required(self) -> None:
        required = self.required()
        for hint in self.of_kind("required"):
            match = self.parse(r"Something in (.+) is needed to wake the Wind Fish\.", hint["text"])
            other = re.fullmatch(r"Tester2's world at (.+)", match.group(1))
            if other:
                where = [self.multiworld.get_location(other.group(1), 2)]
            else:
                where = self.own(match.group(1))
                self.assertTrue(where, hint)
            self.assertTrue(required & set(where), hint)

    def test_area(self) -> None:
        for hint in self.of_kind("area"):
            match = self.parse(r"Your (.+) is in (.+)\.", hint["text"])
            item, where = match.groups()
            if where == "Tester2's world":
                locations = self.multiworld.get_locations(2)
            else:
                locations = self.own(where)
            self.assertTrue(any(loc.item.player == 1 and loc.item.name == item for loc in locations), hint)

    def test_deterministic(self) -> None:
        self.assertEqual(filled(self.seed, self.options).worlds[1].in_game_hints(), self.hints)

    def test_patch_and_spoiler(self) -> None:
        import io
        self.assertEqual(self.world.patch_data()["hints"], self.hints)
        spoiler = io.StringIO()
        self.world.write_spoiler(spoiler)
        self.assertIn("In-game hints (Tester1):", spoiler.getvalue())
        hint = self.hints["book-Blue4"]
        self.assertIn(f"Library - Shield Book ({hint['kind']}): {hint['text']}\n", spoiler.getvalue())


class TestPrecise(HintsBase):
    options = {**HintsBase.options, "hint_distribution": "precise"}

    def test_own_progression_first(self) -> None:
        self.assertLessEqual({hint["kind"] for hint in self.hints.values()}, {"precise", "joke"})
        hinted = self.hinted()
        # useful items and jokes only once no progression item is left to hint
        if any(not loc.item.advancement for loc in hinted) or self.of_kind("joke"):
            progression = {loc for loc in candidates(self.world) if loc.item.advancement}
            self.assertLessEqual(progression, set(hinted) | self.owl_gifts())
        self.assertTrue(any(loc.player == 2 for loc in hinted))
        for hint in self.of_kind("joke"):
            self.assertIn(hint["text"], JOKES)

    def owl_gifts(self):
        return {self.multiworld.get_location(LOCATION_NAMES[source], 1) for source in SOURCES
                if source in LOCATION_NAMES and LOCATION_NAMES[source] in self.multiworld.regions.location_cache[1]}


class TestBalanced(HintsBase):
    options = {**HintsBase.options, **LONG_CHECKS, "hint_distribution": "balanced"}

    def test_mix(self) -> None:
        counts = Counter(hint["kind"] for hint in self.hints.values())
        for kind, count in DISTRIBUTIONS["balanced"]:
            if kind != "precise":
                self.assertLessEqual(counts[kind], count, kind)
        for kind in ("always", "required", "barren", "area", "precise"):
            self.assertGreater(counts[kind], 0, kind)
        self.assertEqual(counts["required"], 8)
        self.assertEqual(counts["area"], 8)


class TestVague(HintsBase):
    options = {**HintsBase.options, **LONG_CHECKS, "hint_distribution": "vague"}

    def test_mix(self) -> None:
        counts = Counter(hint["kind"] for hint in self.hints.values())
        self.assertLessEqual(counts["precise"], 3)
        self.assertEqual(counts["required"], 12)
        self.assertGreater(counts["area"], counts["precise"] + counts["always"])
        self.assertEqual(counts["joke"], 0)


class TestOwlGifts(HintsBase):
    options = {**HintsBase.options, "owl_statues": "all"}

    def test_owl_not_its_own_gift(self) -> None:
        for source, hint in self.hints.items():
            if "location" in hint and source in LOCATION_NAMES:
                location = self.locations[hint["player"], hint["location"]]
                self.assertFalse(location.player == 1 and location.name == LOCATION_NAMES[source], source)


class TestTextOnly(unittest.TestCase):
    def test_no_server_hints(self) -> None:
        for distribution in DISTRIBUTIONS:
            hints = filled(3, {"in_game_hints": "text", "hint_distribution": distribution}).worlds[1].in_game_hints()
            self.assertEqual(list(hints), list(SOURCES))
            self.assertTrue(all(set(hint) == {"kind", "text"} for hint in hints.values()))

    def test_off(self) -> None:
        world = filled(3, {"in_game_hints": "off"}).worlds[1]
        self.assertEqual(world.in_game_hints(), {})
        self.assertEqual(world.patch_data()["hints"], {})
