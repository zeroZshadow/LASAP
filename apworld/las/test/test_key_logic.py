"""Small keys: the logic's key counts hold against a player who spends keys as badly as possible (key_logic.py), and
key_locks.yml has exactly the RomFS's locks."""
import copy
import sys
import unittest
from pathlib import Path

import yaml

from . import romfs
from .key_logic import KeyLogicCheck
from ..lasr import LOGIC
from ..logic import PRESETS

LOCKS = yaml.safe_load((Path(__file__).with_name("key_locks.yml")).read_text(encoding="utf-8"))
PATCHER = Path(__file__).resolve().parents[3] / "patcher"
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
                accepted = {target for target, presets in (model.get("accepted") or {}).items() if preset in presets}
                with self.subTest(dungeon=dungeon, preset=preset):
                    failures = KeyLogicCheck(LOGIC, preset, int(dungeon[1:]), model).run()
                    self.assertEqual([], [describe(f) for f in failures if f.target not in accepted][:10])
                    self.assertEqual(accepted, accepted & {f.target for f in failures}, "accepted, though sound")

    def test_catches_too_few_keys(self) -> None:
        """Bottle Grotto's Hinox area with 2 keys instead of 3: both keys can go into the doors west and south of the
        torches."""
        logic = copy.deepcopy(LOGIC)
        logic["D2-4C"]["condition-basic"] = logic["D2-4C"]["condition-basic"].replace("key-D2:3", "key-D2:2")
        failures = KeyLogicCheck(logic, "basic", 2, LOCKS["D2"]).check()
        self.assertIn(("D2-4C", 2, frozenset({710, 711})), [(f.target, f.keys, f.opened) for f in failures])


@unittest.skipUnless(LEVELS.exists(), "RomFS dump not found (LASAP_ROMFS or local_paths.json romfs)")
class TestKeyLocksRomFS(unittest.TestCase):
    def test_locks(self) -> None:
        """Each dungeon's ObjLockDoor and ObjLockBlock opened flags, as key_locks.yml lists them."""
        saved = list(sys.path)
        sys.path.insert(0, str(PATCHER / "modbuild"))
        try:
            from RandomizerCore.Tools.leb import Room
        finally:
            sys.path[:] = saved
            for name in [name for name in sys.modules if name.split(".")[0] == "RandomizerCore"]:
                del sys.modules[name]
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
