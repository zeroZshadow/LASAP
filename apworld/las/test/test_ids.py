import importlib.util
import pathlib
import unittest

from ..ids import ITEM_BASE, ITEM_OFFSETS, LOCATION_BASE, LOCATION_OFFSETS
from ..items import ITEM_GAME_INFO, ITEM_TABLE
from ..lasr import ITEM_DEFS, RAW_LOGIC
from ..locations import LOCATION_NAME_TO_ID, LOCATION_NAMES

GEN_IDS = pathlib.Path(__file__).resolve().parents[2] / "tools" / "gen_ids.py"

# Offsets that must never change once published (spot checks across the table).
PINNED_ITEMS = {"sword": 1, "seashell": 45, "zol-trap": 62, "key-D1": 71, "map-D0": 115}
PINNED_LOCATIONS = {"sale": 1, "tarin": 53, "D1-instrument": 154, "D0-rupee-28": 335}


class TestIds(unittest.TestCase):
    def test_every_name_has_an_offset(self) -> None:
        self.assertEqual(set(ITEM_DEFS) - set(ITEM_OFFSETS), set())
        item_locations = {k for k, v in RAW_LOGIC.items() if v["type"] == "item"}
        self.assertEqual(item_locations - set(LOCATION_OFFSETS), set())

    def test_offsets_unique_and_in_range(self) -> None:
        for table in (ITEM_OFFSETS, LOCATION_OFFSETS):
            self.assertEqual(len(set(table.values())), len(table))
            self.assertTrue(all(0 < offset < 0x10000 for offset in table.values()))

    def test_pinned_offsets(self) -> None:
        for name, offset in PINNED_ITEMS.items():
            self.assertEqual(ITEM_OFFSETS[name], offset, name)
        for name, offset in PINNED_LOCATIONS.items():
            self.assertEqual(LOCATION_OFFSETS[name], offset, name)

    @unittest.skipUnless(GEN_IDS.exists(), "apworld/tools/gen_ids.py not found")
    def test_gen_ids_appends(self) -> None:
        spec = importlib.util.spec_from_file_location("gen_ids", GEN_IDS)
        gen_ids = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gen_ids)
        table = dict(LOCATION_OFFSETS)
        items, locations = gen_ids.load_names()
        self.assertEqual(gen_ids.extend(table, ["new-location"] + locations), ["new-location"])
        self.assertEqual(table["new-location"], max(LOCATION_OFFSETS.values()) + 1)
        self.assertEqual({k: table[k] for k in LOCATION_OFFSETS}, LOCATION_OFFSETS)
        self.assertEqual(gen_ids.extend(dict(ITEM_OFFSETS), items), [])

    def test_game_info(self) -> None:
        self.assertEqual(ITEM_GAME_INFO["Small Key (Tail Cave)"], ("SmallKey", 0))
        self.assertEqual(ITEM_GAME_INFO["Nightmare Key (Turtle Rock)"], ("NightmareKey", 7))
        self.assertEqual(ITEM_GAME_INFO["Stone Beak (Color Dungeon)"], ("StoneBeak", 9))
        self.assertEqual(ITEM_GAME_INFO["Progressive Sword"], ("SwordLv1", -1))
        self.assertEqual(set(ITEM_GAME_INFO), set(ITEM_TABLE))

    def test_world_ids(self) -> None:
        for name, data in ITEM_TABLE.items():
            self.assertEqual(data.code, ITEM_BASE + ITEM_OFFSETS[data.lasr_name])
        for lasr_name, name in LOCATION_NAMES.items():
            self.assertEqual(LOCATION_NAME_TO_ID[name], LOCATION_BASE + LOCATION_OFFSETS[lasr_name])
