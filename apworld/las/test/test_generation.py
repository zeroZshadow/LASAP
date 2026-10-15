import os
import re
import tempfile
import unittest

from worlds.AutoWorld import call_all

from . import LASTestBase
from ..container import LASContainer
from ..ids import LOCATION_OFFSETS
from ..lasr import CATEGORIES, ITEM_DEFS, ITEM_LOCATIONS, LOGIC
from ..locations import LOCATION_NAMES


class TestDefault(LASTestBase):
    def test_patch_and_slot_data(self) -> None:
        from Fill import distribute_items_restrictive
        distribute_items_restrictive(self.multiworld)
        call_all(self.multiworld, "post_fill")
        slot_data = self.world.fill_slot_data()
        self.assertEqual(set(slot_data), {"seed_hash", "goal", "instruments_required", "death_link", "location_count",
                                          "settings", "logic", "options", "starting_instruments"})
        self.assertEqual(slot_data["instruments_required"], 8)
        self.assertEqual(self.world.patch_data()["settings"]["Instruments Required"], 8)
        from ..lasr_settings import TRACKER_SETTINGS
        self.assertEqual(tuple(slot_data["settings"]), TRACKER_SETTINGS)
        self.assertEqual(slot_data["settings"]["Shuffle Instruments"], "Anywhere")
        self.assertRegex(slot_data["seed_hash"], r"^[0-9a-f]{8}$")
        self.assertEqual(slot_data["location_count"],
                         len([loc for loc in self.multiworld.get_locations(self.player) if loc.address is not None]))

        with tempfile.TemporaryDirectory() as directory:
            self.world.generate_output(directory)
            files = os.listdir(directory)
            self.assertEqual(len(files), 1)
            self.assertTrue(files[0].endswith(".aplas"))
            container = LASContainer(os.path.join(directory, files[0]))
            container.read()
            patch = container.patch
            self.assertEqual(container.game, self.world.game)
            self.assertEqual(container.player, self.player)
            self.assertEqual(container.player_name, self.multiworld.player_name[self.player])

        self.assertEqual(set(patch), {"format", "seed_name", "player", "slot_name", "seed_hash", "settings",
                                      "starting_items", "placements", "location_ids", "location_names",
                                      "location_models", "location_chests", "dungeon_entrances", "hints",
                                      "ghost_secret", "ghost_relay"})
        self.assertLessEqual(set(patch["location_models"]), set(patch["location_ids"]))
        self.assertEqual(patch["format"], 1)
        self.assertEqual(patch["seed_hash"], slot_data["seed_hash"])
        self.assertEqual(patch["player"], self.player)
        self.assertEqual(patch["slot_name"], self.multiworld.player_name[self.player])
        self.assertEqual(patch["seed_name"], self.multiworld.seed_name)

        owls = set(CATEGORIES["Overworld_Owl_Statues"]) | set(CATEGORIES["Dungeon_Owl_Statues"])
        self.assertEqual(set(patch["placements"]), set(ITEM_LOCATIONS) - owls)  # owls are off by default
        for loc, item in patch["placements"].items():
            self.assertTrue(item == "ap-item" or item in ITEM_DEFS, (loc, item))
        from ..lasr_settings import kept_locations
        shuffled = {loc for loc, item in patch["placements"].items() if item == "ap-item"}
        kept = kept_locations() & set(patch["location_ids"])
        self.assertEqual(set(patch["location_ids"]), shuffled | kept)
        self.assertIn("rooster-statue", kept)
        self.assertFalse(shuffled & kept)
        self.assertEqual(set(patch["location_names"]), shuffled | kept)
        for loc in kept:
            self.assertEqual(patch["placements"][loc], LOGIC[loc]["content"])
        for loc, (item_name, owner) in patch["location_names"].items():
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            self.assertEqual(item_name, item.name)
            self.assertEqual(owner, "" if item.player == self.player else self.multiworld.player_name[item.player])
        for loc, local_id in patch["location_ids"].items():
            self.assertEqual(local_id, LOCATION_OFFSETS[loc])
            self.assertLess(local_id, 0x10000)
            location = self.multiworld.get_location(LOCATION_NAMES[loc], self.player)
            self.assertEqual(location.address - local_id, 0x4C420000)
        for item in patch["starting_items"]:
            self.assertIn(item, ITEM_DEFS)
        self.assertEqual(patch["settings"]["Preset"], "Basic")
        for key in ("Open Mabe", "Classic D2", "Small Keys", "Shuffle Instruments", "Owl Gifts", "Traps"):
            self.assertIn(key, patch["settings"])

    def test_seed_hash(self) -> None:
        import hashlib
        seed_name = self.multiworld.seed_name
        self.assertTrue(re.fullmatch(r"[0-9a-f]{8}", self.world.seed_hash()))
        self.assertEqual(self.world.seed_hash(), hashlib.sha256(f"{seed_name}:1".encode()).hexdigest()[:8])
        self.multiworld.seed_name = seed_name + "x"
        self.assertNotEqual(self.world.seed_hash(), hashlib.sha256(f"{seed_name}:1".encode()).hexdigest()[:8])
        self.multiworld.seed_name = seed_name


class TestLASRDefaults(LASTestBase):
    def test_defaults_match_lasr(self) -> None:
        """The default options are the original randomizer's default settings."""
        from ..lasr_settings import lasr_settings
        from ..options import StartingGear
        settings = lasr_settings(self.world.options)
        expected = {"Preset": "Basic", "Chests": True, "Free Gifts": True, "Trade Quest": False, "Golden Leaves": True,
                    "Heart Pieces": True, "Seashells": True, "Miscellaneous": True, "Shop": True, "Boss Drops": True,
                    "Dampe": False, "Rapids": False, "Fishing": False, "Blue Rupees": False, "Owl Gifts": "None",
                    "Seashell Mansion": 30, "Dungeon Maps": "Start With", "Compasses": "Start With",
                    "Stone Beaks": "Start With", "Small Keys": "Own Dungeon", "Nightmare Keys": "Own Dungeon",
                    "Shuffle Instruments": "Anywhere", "Starting Instruments": 0, "Open Kanalet": True,
                    "Open Mabe": True, "Open Mamu": True, "Completed Bridge": True, "Classic D2": False,
                    "Bad Pets": False, "Fast Fishing": True, "Shuffled Bombs": True, "Free Book": True,
                    "Fast Stalfos": True, "Shuffled Powder": True, "Stealing": "Always", "Traps": "Many",
                    "Damage": "Normal", "Rupees": 100}
        self.assertEqual({k: settings[k] for k in expected}, expected)
        self.assertEqual(StartingGear.default, {"Progressive Sword": 1, "Progressive Shield": 1, "Ocarina": 1,
                                                "Manbo's Mambo": 1})


class TestEverythingShuffled(LASTestBase):
    options = {
        "trade_quest": True,
        "dampe": True,
        "rapids": True,
        "fishing": True,
        "blue_rupees": True,
        "owl_statues": "all",
        "seashell_mansion": 50,
        "keys": "anywhere",
        "dungeon_items": "anywhere",
        "instruments": "anywhere",
        "traps": "many",
    }

    def test_counts(self) -> None:
        # every LASR location and the two companions, except the shop slots LASR does not shuffle and the Trendy
        # Game's heart pieces and chamber stones, whose items never matter
        self.assertEqual(len(self.world.shuffled), 344 + 2 - 7 - 4)


class TestVanillaHeavy(LASTestBase):
    options = {
        "chests": False,
        "free_gifts": False,
        "golden_leaves": False,
        "heart_pieces": False,
        "seashells": False,
        "miscellaneous": False,
        "shop": False,
        "boss_drops": False,
        "seashell_mansion": 0,
        "instruments": "vanilla",
        "starting_instruments": 2,
        "open_mabe": False,
        "open_kanalet": False,
        "open_mamu": False,
        "completed_bridge": False,
    }

    def test_unrandomized_locations(self) -> None:
        """An unrandomized location whose default item the logic can need is a check holding that item, locked; any
        other is no AP location."""
        from BaseClasses import ItemClassification
        from ..items import LASR_TO_ITEM
        from ..lasr import LOGIC
        from ..lasr_settings import MANSION_THRESHOLDS, fixed_locations, kept_locations
        locked, plain = set(self.world.locked), set(self.world.plain)
        # the Mansion at 0: its rewards and the seashells never matter
        self.assertLessEqual(set(MANSION_THRESHOLDS), plain)
        self.assertIn("pond-island", plain)
        self.assertEqual(self.world.create_item("Secret Seashell").classification, ItemClassification.filler)
        self.assertIn("mabe-well", plain)  # a heart piece
        self.assertTrue(any(LOGIC[loc]["content"] == "tail-key" for loc in locked))
        self.assertLessEqual({"moblin-cave", "rooster-statue", "trendy-prize-1"}, locked)
        instruments = {f"D{n}-instrument" for n in range(1, 9)}
        self.assertEqual(len(instruments & locked), 8 - len(self.world.starting_instruments))
        self.assertFalse(plain & set(self.world.shuffled))
        names = {loc.name for loc in self.multiworld.get_locations(self.player)}
        self.assertFalse({LOCATION_NAMES[loc] for loc in plain} & names)
        for loc in locked:
            location = self.multiworld.get_location(LOCATION_NAMES[loc], self.player)
            self.assertEqual(location.address, self.world.location_name_to_id[LOCATION_NAMES[loc]])
            self.assertTrue(location.locked, loc)
            self.assertEqual(location.item.name, LASR_TO_ITEM[LOGIC[loc]["content"]], loc)
            self.assertEqual(location.item.player, self.player)
        self.assertFalse(fixed_locations() & set(self.world.shuffled))
        patch = self.world.patch_data()
        self.assertEqual({loc for loc in locked if patch["placements"][loc] != "ap-item"}, kept_locations() & locked)
        self.assertLessEqual(locked, set(patch["location_ids"]))
        self.assertTrue(all(patch["placements"][loc] == LOGIC[loc]["content"] for loc in plain))
        # trackers and the patcher get the options as they are
        settings = patch["settings"]
        self.assertEqual((settings["Heart Pieces"], settings["Seashell Mansion"], settings["Seashells Important"]),
                         (False, 0, False))

    def test_tracker_counts_a_locked_item_once_received(self) -> None:
        """A locked location's item counts for the map's logic once received, not while the location is reachable:
        Mamasha needs the Yoshi Doll in hand, not a reachable Trendy Game."""
        from ..items import LASR_TO_ITEM
        from ..tracker import LogicTracker
        logic = self.world.fill_slot_data()["logic"]
        tracker = LogicTracker(logic["preset"], logic["tokens"], logic["vanilla"], self.world.shuffled)
        doll = LASR_TO_ITEM[LOGIC["trendy-prize-1"]["content"]]
        # every other item of the slot: the pool's, the starting ones and the other locked locations'
        items = [item.name for item in self.multiworld.precollected_items[self.player]]
        items += [item.name for item in self.multiworld.itempool + self.world.get_pre_fill_items()
                  if item.player == self.player]
        items += [self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item.name for loc in logic["locked"]]
        items = [name for name in items if name != doll]
        before = tracker.in_logic(items)
        self.assertIn("trendy-prize-1", before)
        self.assertNotIn("mamasha", before)
        self.assertIn("mamasha", tracker.in_logic(items + [doll]))


class TestOwnDungeon(LASTestBase):
    options = {
        "keys": "own_dungeon",
        "dungeon_items": "own_dungeon",
        "instruments": "own_dungeon",
        "owl_statues": "dungeons",
        "traps": "few",
    }

    def test_dungeon_items_in_own_dungeon(self) -> None:
        from ..lasr import location_dungeon, item_dungeon, INSTRUMENTS
        from ..items import ITEM_TABLE
        from ..locations import LASR_LOCATIONS
        for location in self.multiworld.get_locations(self.player):
            if location.address is None or location.item is None or location.item.player != self.player:
                continue
            lasr_item = ITEM_TABLE[location.item.name].lasr_name
            dungeon = location_dungeon(LASR_LOCATIONS[location.name])
            if item_dungeon(lasr_item) >= 0:
                self.assertEqual(item_dungeon(lasr_item), dungeon, location.name)
            elif lasr_item in INSTRUMENTS:
                self.assertEqual(INSTRUMENTS.index(lasr_item) + 1, dungeon, location.name)


class TestFewLocations(unittest.TestCase):
    """Every location category off, dungeon items in their own dungeons, no starting gear: nearly all locations are in
    dungeons, and only Tarin's is reachable at the start."""
    options = {**{name: False for name in ("chests", "free_gifts", "golden_leaves", "heart_pieces", "seashells",
                                           "miscellaneous", "shop", "boss_drops", "open_mabe")},
               "seashell_mansion": 0, "keys": "own_dungeon", "dungeon_items": "own_dungeon",
               "instruments": "any_dungeon", "start_inventory_from_pool": {}}

    def test_pre_fill_leaves_room_for_progression(self) -> None:
        from test.general import setup_multiworld
        from .. import LASWorld
        for seed in range(1, 9):
            multiworld = setup_multiworld(LASWorld, seed=seed, options=self.options)
            world = multiworld.worlds[1]
            # an item that opens the most locations is placed early, though none opens EARLY_LOCATIONS_WANTED
            self.assertEqual(len(multiworld.local_early_items[1]), 1, seed)
            self.assertTrue(world._progression_fits(), seed)


class TestAnyDungeon(LASTestBase):
    options = {"instruments": "any_dungeon", "starting_instruments": 1}

    def test_instruments_in_dungeons(self) -> None:
        from ..lasr import location_dungeon, INSTRUMENTS
        from ..items import ITEM_TABLE
        from ..locations import LASR_LOCATIONS
        from ..lasr_settings import lasr_settings
        self.assertEqual(lasr_settings(self.world.options)["Shuffle Instruments"], "Any Dungeon")
        placed = 0
        for location in self.multiworld.get_locations(self.player):
            if location.address is None or location.item is None or location.item.player != self.player:
                continue
            if ITEM_TABLE[location.item.name].lasr_name in INSTRUMENTS:
                placed += 1
                self.assertGreaterEqual(location_dungeon(LASR_LOCATIONS[location.name]), 0, location.name)
        self.assertEqual(placed, len(INSTRUMENTS) - 1)


class TestDungeonRewards(LASTestBase):
    options = {
        "instruments": "dungeon_rewards",
        "starting_instruments": 3,
        "dungeon_items": "start_with",
        "classic_d2": True,
        "stealing": "never",
        "shuffled_bombs": False,
        "shuffled_powder": False,
        "starting_rupees": 0,
    }

    def test_starting_rupees(self) -> None:
        from ..lasr_settings import lasr_settings
        self.assertEqual(lasr_settings(self.world.options)["Rupees"], 0)


class TestAdvanced(LASTestBase):
    options = {"logic": "advanced"}


class TestGlitched(LASTestBase):
    options = {"logic": "glitched", "free_book": False, "fast_fishing": False, "fast_stalfos": False}


class TestHell(LASTestBase):
    options = {"logic": "hell", "keys": "anywhere", "stealing": "always"}


class TestChestAppearance(LASTestBase):
    options = {"chest_appearance": "texture_and_size", "keys": "own_dungeon"}

    def test_every_location_has_a_chest_look(self) -> None:
        from ..lasr_settings import lasr_settings
        from ..locations import LOCATION_NAMES
        self.assertEqual(lasr_settings(self.world.options)["Chest Types"], "Texture + Size")
        looks = self.world.patch_data()["location_chests"]
        # the test world is filled only up to pre_fill: dungeon items
        from ..lasr_settings import kept_locations
        filled = {loc for loc in set(self.world.shuffled) - kept_locations()
                  if self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item is not None}
        self.assertTrue(filled)
        self.assertEqual(set(looks), filled)
        for loc, (item_type, chest_type) in looks.items():
            self.assertIn(item_type, ("important", "important-health", "trade", "seashell", "good", "junk"), loc)
            self.assertIn(chest_type, (None, "junk", "key", "life-upgrade"), loc)

    def test_looks(self) -> None:
        from ..lasr_settings import chest_look
        options = self.world.options
        self.assertEqual(chest_look(options, None, progression=True), ["important", None])
        self.assertEqual(chest_look(options, None, useful=True), ["good", None])
        self.assertEqual(chest_look(options, None), ["junk", "junk"])
        self.assertEqual(chest_look(options, None, progression=True, trap=True), ["junk", "junk"])
        # small keys count as important with keys anywhere, as in LASR's shuffler
        self.assertEqual(chest_look(options, "key-D1"), ["good", "key"])
        options.keys.value = options.keys.option_anywhere
        self.assertEqual(chest_look(options, "key-D1"), ["important", "key"])
        self.assertEqual(chest_look(options, "heart-container"), ["important-health", "life-upgrade"])
        options.chest_appearance.value = options.chest_appearance.option_size
        self.assertEqual(chest_look(options, "heart-container"), ["junk", "life-upgrade"])


class TestGameplayOptions(LASTestBase):
    """Settings that change how the game plays are options, so a seed plays the same for everyone; the client's player
    settings are the visual and audio ones."""
    options = {"damage": "ohko", "movement_speed": False, "movement_360": True, "skip_song_cutscenes": True,
               "instant_text": True, "nice_hookshot": True, "disable_guardian_acorn": True,
               "disable_piece_of_power": True, "easy_fishing": False}

    def test_patch_settings(self) -> None:
        from ..host_settings import PLAYER_DEFAULTS
        settings = self.world.patch_data()["settings"]
        self.assertEqual({k: settings[k] for k in ("Damage", "Movement Speed", "360 Movement", "Song Cutscenes",
                                                   "Instant Text", "Nice Hookshot", "Disable Guardian Acorn",
                                                   "Disable Piece of Power", "Easy Fishing")},
                         {"Damage": "OHKO", "Movement Speed": False, "360 Movement": True, "Song Cutscenes": True,
                          "Instant Text": True, "Nice Hookshot": True, "Disable Guardian Acorn": True,
                          "Disable Piece of Power": True, "Easy Fishing": False})
        self.assertFalse(set(PLAYER_DEFAULTS) & {"Damage", "Movement Speed", "360 Movement", "Song Cutscenes",
                                                 "Instant Text", "Nice Hookshot", "Disable Guardian Acorn",
                                                 "Disable Piece of Power", "Easy Fishing"})
