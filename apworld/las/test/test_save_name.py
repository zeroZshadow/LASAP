"""A new save's name: the slot name as the game takes it (patcher/ap_save_name.py)."""
import os
import sys
import unittest
from pathlib import Path

PATCHER = Path(os.path.realpath(__file__)).parents[3] / "patcher"


def _import(*names):
    saved = list(sys.path)
    sys.path.insert(0, str(PATCHER))
    try:
        return [__import__(name, fromlist=["_"]) for name in names]
    finally:
        sys.path[:] = saved


def _forget(*prefixes) -> None:
    for name in list(sys.modules):
        if name.split(".")[0] in prefixes:
            del sys.modules[name]


class TestSaveName(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ap_save_name, = _import("ap_save_name")

    @classmethod
    def tearDownClass(cls) -> None:
        _forget("ap_save_name")

    def test_rule(self) -> None:
        cases = {
            "Tester": "Tester",
            "Archipelago Player 7": "Archipel",  # cut to 8
            "Link Two": "Link Two",
            "Ab  Cd   ": "Ab Cd",  # runs of spaces as one, none at the ends
            "Twelve 3 4567": "Twelve 3",
            "Seven 7 x": "Seven 7",  # a cut that ends on a space drops it
            "Zoë[AP]_Archipelago": "ZoëAP_Ar",  # Latin-1 letters stay, brackets go
            "50%@home\\": "50home",  # the keyboard refuses @, % and \
            "リンク": "Link",  # nothing left
            "★": "Link",
            "   ": "Link",
            "": "Link",
            "A×B÷C": "ABC",
        }
        for slot, name in cases.items():
            with self.subTest(slot=slot):
                self.assertEqual(self.ap_save_name.save_name(slot), name)
                self.assertLessEqual(len(name), self.ap_save_name.MAX_LENGTH)

