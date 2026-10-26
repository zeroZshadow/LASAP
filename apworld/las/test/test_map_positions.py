"""The map tracker's dungeon positions (patcher/map_positions.json, scripts/map_positions.py) against the RomFS rooms:
each dot lies in its room's box and on its check's actor, in a room the dungeon map shows on a floor."""
import json
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATCHER = ROOT / "patcher"


def romfs() -> Path:
    """The RomFS dump: LASAP_ROMFS, else "romfs" in this machine's scripts/local_paths.json (gitignored)."""
    if "LASAP_ROMFS" in os.environ:
        return Path(os.environ["LASAP_ROMFS"])
    try:
        return Path(json.loads((ROOT / "scripts" / "local_paths.json").read_text(encoding="utf-8"))["romfs"])
    except (OSError, KeyError, ValueError):
        return ROOT / "romfs-not-set"


ROMFS = romfs()
LEVELS = ROMFS / "region_common" / "level"
AREAS = ROMFS / "region_common" / "datasheets" / "UiDungeonArea.gsheet"
CHEST, OWL = b"ObjTreasureBox", b"ObjDungeonOw"


@unittest.skipUnless(AREAS.exists(), "RomFS dump not found (LASAP_ROMFS or local_paths.json romfs)")
class TestDungeonPositions(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        saved = list(sys.path)
        sys.path.insert(0, str(PATCHER))
        try:
            from RandomizerCore.Randomizers.chests import CHEST_ROOMS
            from RandomizerCore.Tools.leb import Room
            from RandomizerCore.Tools.oead_tools import parseStruct, readSheet
        finally:
            sys.path[:] = saved
        cls.chest_rooms = CHEST_ROOMS
        cls.floors = {str(row["mLeaName"]): row["mFloor"] for row in map(parseStruct, readSheet(str(AREAS))["values"])}
        cls.dungeon = json.loads((PATCHER / "map_positions.json").read_text(encoding="utf-8"))["dungeon"]
        cls.rooms = {}
        for places in cls.dungeon.values():
            for room, *_ in places:
                if room not in cls.rooms:
                    path = LEVELS / room.split("_")[0] / f"{room}.leb"
                    cls.rooms[room] = Room(path.read_bytes(), edit_grid=True)

    @classmethod
    def tearDownClass(cls) -> None:
        for name in list(sys.modules):
            if name.split(".")[0] == "RandomizerCore":
                del sys.modules[name]

    def actors(self, room: str, prefix: bytes) -> list:
        return [(a.posX, a.posZ) for a in self.rooms[room].actors if a.name.startswith(prefix)]

    def test_in_room(self) -> None:
        """Every place lies in its room's box (the room's grid; Eagle's Tower's rooms do not follow their names)."""
        for loc, places in self.dungeon.items():
            for room, x, z in places:
                info = self.rooms[room].grid.info
                x1, z1 = info.x_coord + info.room_width * info.tile_size, info.z_coord + info.room_height * info.tile_size
                with self.subTest(loc=loc, room=room):
                    self.assertTrue(info.x_coord <= x <= x1 and info.z_coord <= z <= z1, (x, z))

    def test_on_floor(self) -> None:
        """Every place's room is on a floor of the dungeon map: Eagle's Tower's collapsed top (mFloor 14) shows only
        once it has collapsed, so its checks also have a place in the same room on 3F before."""
        for loc, places in self.dungeon.items():
            floors = [self.floors.get(room) for room, *_ in places]
            with self.subTest(loc=loc):
                self.assertNotIn(None, floors)
                self.assertTrue(any(floor < 10 for floor in floors), floors)
                if 14 in floors:
                    self.assertEqual(sorted(floors), [3, 14])

    def test_chests(self) -> None:
        """A chest's places are at a chest of the room, as LASR's room for it and its 3F copy have."""
        for loc, room in self.chest_rooms.items():
            if loc not in self.dungeon:
                continue
            places = self.dungeon[loc]
            with self.subTest(loc=loc):
                self.assertEqual(places[0][0], room)
                for room, x, z in places:
                    self.assertIn((x, z), [(round(cx, 2), round(cz, 2)) for cx, cz in self.actors(room, CHEST)])

    def test_owl_statues(self) -> None:
        for loc, places in self.dungeon.items():
            if "owl-statue" not in loc:
                continue
            (room, x, z), = places
            with self.subTest(loc=loc):
                self.assertIn((x, z), [(round(ox, 2), round(oz, 2)) for ox, oz in self.actors(room, OWL)])

    def test_eagles_tower(self) -> None:
        """Eagle's Tower: the 3F chests in their rooms before and after the collapse, the boss and the instrument on
        4F, in their rooms though these sit two rooms east of their names' columns."""
        self.assertEqual(self.dungeon["D7-3f-horseheads"],
                         [["Lv07EagleTower_01G", 132.75, 3.75], ["Lv07EagleTower_05G", 132.75, 75.75]])
        self.assertEqual(self.dungeon["D7-grim-creeper"],
                         [["Lv07EagleTower_02H", 147.75, 15.75], ["Lv07EagleTower_06H", 147.75, 87.75]])
        for loc in ("D7-instrument", "D7-eagle"):
            (room, x, z), = self.dungeon[loc]
            self.assertEqual(self.floors[room], 4)
            self.assertGreaterEqual(x, 120.0, loc)
        self.assertEqual(sorted(re.sub(r".*_", "", room) for loc, places in self.dungeon.items()
                                if loc.startswith("D7-") for room, *_ in places if self.floors[room] in (3, 14)),
                         ["01G", "02H", "05G", "06H"])


if __name__ == "__main__":
    unittest.main()
