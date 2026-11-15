"""Randomize Text's pools and lines (patcher ap_shuffles.py)."""
import random
import unittest

from . import patcher_import


class TestText(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shuffles = ap_shuffles = patcher_import("ap_shuffles")[0]
        cls.lines = [line.split() for line in ap_shuffles.text_lines(random.Random("test"))]

    def test_kept_messages_stay(self):
        kept = ("Npc:DanpeiGreeting", "SubEvent:DanpeiPanelLead1", "Talker:NpcDanpei", "System:AdvDeadEnd",
                "System:AdvTutorial_Next", "System:PanelTheme1_1", "System:PzlTutorial_First_1", "Hint:Panel_Pod04C",
                "System:CheckSecretBook1", "System:BadSecretBook", "System:PriceTagBow")
        named = {key for line in self.lines for key in line[1:3]}
        for label in kept:
            self.assertNotIn(label, named)

    def test_a_hold_only_swaps_with_a_hold(self):
        ends = self.shuffles.ap_assets.data()["texts"]["dialogue"]
        holds = {key for key, end in ends.items() if end.endswith(self.shuffles.HOLD)}
        self.assertIn("SubEvent:BattleEvilOrb", holds)
        for _, place, donor, fix in self.lines:
            self.assertEqual(place in holds, donor in holds, place)
            if place in holds:
                self.assertEqual(fix, "=", place)
