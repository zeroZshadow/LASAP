"""Small keys: the logic's key counts hold against a player who spends keys as badly as possible (key_logic.py), and
key_locks.yml has exactly the RomFS's locks."""
import copy
import unittest
from pathlib import Path

import yaml

from . import forget, patcher_import, romfs
from .key_logic import KeyLogicCheck
from ..lasr import LOGIC
from ..logic import PRESETS

LOCKS = yaml.safe_load((Path(__file__).with_name("key_locks.yml")).read_text(encoding="utf-8"))
LEVELS = romfs() / "region_common" / "level"


def describe(failure) -> str:
    missing = ", ".join(f"{name} {left}" for name, left in failure.missing) or "nothing"
    left = ", keys left and no lock to reach" if failure.stuck else ""
    return (f"{failure.target} with {failure.keys} keys: locks {sorted(failure.opened)} opened{left}; "
            f"missing {missing}")


class TestKeyLogic(unittest.TestCase):
    def test_sound(self) -> None:
        self.assertEqual(sorted(LOCKS), [f"D{n}" for n in range(9)])
        for dungeon, model in LOCKS.items():
            for preset in PRESETS:
                with self.subTest(dungeon=dungeon, preset=preset):
                    failures = KeyLogicCheck(LOGIC, preset, int(dungeon[1:]), model).run()
                    self.assertEqual([], [describe(f) for f in failures][:10])

    def test_catches_too_few_keys(self) -> None:
        """Bottle Grotto's pushblock room with 2 keys instead of 3: both keys can go into the doors west and south of
        the torches."""
        logic = copy.deepcopy(LOGIC)
        logic["D2.h"]["condition-basic"] = logic["D2.h"]["condition-basic"].replace("key-D2:3", "key-D2:2")
        failures = KeyLogicCheck(logic, "basic", 2, LOCKS["D2"]).check()
        self.assertIn(("D2.h", 2, frozenset({710, 711})), [(f.target, f.keys, f.opened) for f in failures])


@unittest.skipUnless(LEVELS.exists(), "RomFS dump not found (LASAP_ROMFS or local_paths.json romfs)")
class TestKeyLocksRomFS(unittest.TestCase):
    def test_locks(self) -> None:
        """Each dungeon's ObjLockDoor and ObjLockBlock opened flags, as key_locks.yml lists them."""
        try:
            Room = patcher_import("RandomizerCore.Tools.leb", modbuild=True)[0].Room
        finally:
            forget("RandomizerCore")
        for dungeon in LOCKS:
            number = int(dungeon[1:])
            level = next(LEVELS.glob(f"Lv{10 if number == 0 else number:02d}*"))
            flags = set()
            for path in level.glob(f"{level.name}_*.leb"):
                for actor in Room(path.read_bytes()).actors:
                    if actor.name.startswith((b"ObjLockDoor", b"ObjLockBlock")):
                        flags.add(actor.switches[0][1])
            with self.subTest(dungeon=dungeon):
                self.assertEqual(flags, set(LOCKS[dungeon]["locks"]))
