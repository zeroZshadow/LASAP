import os
import re
import tempfile

from worlds.AutoWorld import call_all

from . import LASTestBase
from ..container import LASContainer
from ..ids import LOCATION_OFFSETS
from ..lasr import CATEGORIES, ITEM_DEFS, ITEM_LOCATIONS
from ..locations import LOCATION_NAMES


class TestDefault(LASTestBase):
    def test_patch_and_slot_data(self) -> None:
        from Fill import distribute_items_restrictive
        distribute_items_restrictive(self.multiworld)
        call_all(self.multiworld, "post_fill")
        slot_data = self.world.fill_slot_data()
        self.assertEqual(set(slot_data), {"seed_hash", "goal", "death_link", "location_count"})
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
                                      "location_models", "dungeon_entrances"})
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
        shuffled = {loc for loc, item in patch["placements"].items() if item == "ap-item"}
        self.assertEqual(set(patch["location_ids"]), shuffled)
        self.assertEqual(set(patch["location_names"]), shuffled)
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
        # every LASR location except the always-vanilla shop slots and Trendy prizes
        self.assertEqual(len(self.world.shuffled), 344 - 7 - 7)


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


class TestDungeonRewards(LASTestBase):
    options = {
        "instruments": "dungeon_rewards",
        "starting_instruments": 3,
        "dungeon_items": "start_with",
        "classic_d2": True,
        "bad_pets": True,
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
