"""logic.yml's routes (logic.lower_routes): how they read, what they refuse, and what the file itself holds."""
import unittest
from pathlib import Path

import yaml

from ..lasr import LOGIC
from ..logic import ALWAYS, NEVER, PLACES, START, lower_routes

DATA = Path(__file__).resolve().parents[1] / "data"
DOORS = {"pairs": {"Field_a": "Inside_a", "Field_b": "Inside_b"}, "one-way": {"Hole": "Field_c"}}


def zone(**routes):
    return {"type": "region", **{key.replace("_", "-"): value for key, value in routes.items()}}


def lowered(**nodes):
    logic = {START: zone(), **{name.replace("_", "-"): node for name, node in nodes.items()}}
    return lower_routes(logic, DOORS)


class TestRoutes(unittest.TestCase):
    def test_one_way_goes_one_way(self) -> None:
        logic = lowered(a=zone(one_way={"b": "sword"}, connections={"c": ALWAYS}), b=zone(), c=zone())
        self.assertEqual(logic["b"]["condition-basic"], "a & sword")
        self.assertEqual(logic["a"]["condition-basic"], "c")

    def test_connection_goes_both_ways_with_one_rule(self) -> None:
        logic = lowered(a=zone(connections={"b": "sword | bow"}), b=zone())
        self.assertEqual(logic["b"]["condition-basic"], "a & (sword | bow)")
        self.assertEqual(logic["a"]["condition-basic"], "b & (sword | bow)")

    def test_always_has_no_requirements(self) -> None:
        self.assertEqual(lowered(a=zone(one_way={"b": ALWAYS}), b=zone())["b"]["condition-basic"], "a")

    def test_levels_stay_apart(self) -> None:
        logic = lowered(a=zone(one_way={"b": {"basic": "sword", "hell": "bow"}}), b=zone())
        self.assertEqual(logic["b"]["condition-basic"], "a & sword")
        self.assertEqual(logic["b"]["condition-hell"], "a & bow")

    def test_several_ways_in(self) -> None:
        logic = lowered(a=zone(one_way={"c": "sword"}), b=zone(one_way={"c": ALWAYS}), c=zone())
        self.assertEqual(logic["c"]["condition-basic"], "(a & sword) | (b)")

    def test_no_way_in_never_holds(self) -> None:
        self.assertEqual(lowered(a=zone())["a"]["condition-basic"], NEVER)

    def test_above_basic_only_does_not_hold_at_basic(self) -> None:
        self.assertEqual(lowered(a=zone(one_way={"b": {"hell": "bow"}}), b=zone())["b"]["condition-basic"], NEVER)

    def test_start_always_holds(self) -> None:
        logic = lowered(a=zone(connections={START: ALWAYS}))
        self.assertNotIn("condition-basic", logic[START])
        self.assertEqual(logic["a"]["condition-basic"], START)

    def test_routes_leave_the_nodes(self) -> None:
        logic = lowered(a=zone(one_way={"b": ALWAYS}), b=zone())
        self.assertEqual(set(logic["a"]), {"type", "condition-basic"})

    def test_door_pair_leads_both_ways_with_each_sides_rule(self) -> None:
        logic = lowered(a=zone(doors={"Field_a": "key"}), b=zone(doors={"Inside_a": ALWAYS}))
        self.assertEqual(logic["b"]["condition-basic"], "a & key")
        self.assertEqual(logic["a"]["condition-basic"], "b")

    def test_door_nobody_lists_on_the_other_side_leads_nowhere(self) -> None:
        self.assertEqual(lowered(a=zone(doors={"Field_a": ALWAYS}))["a"]["condition-basic"], NEVER)

    def test_one_way_door(self) -> None:
        logic = lowered(a=zone(doors={"Hole": ALWAYS}), b=zone(doors={"Field_c": ALWAYS}))
        self.assertEqual(logic["b"]["condition-basic"], "a")
        self.assertEqual(logic["a"]["condition-basic"], NEVER)

    def test_house_exit_moves_only_the_houses_door(self) -> None:
        logic = {START: zone(doors={"Inside_a": ALWAYS}, one_way={"warp": "ocarina"}), "mabe": zone(doors={"Field_a": ALWAYS}),
                 "plains": zone(one_way={"mabe": "sword"}), "warp": zone()}
        moved = lower_routes(logic, DOORS, house_exit="plains")
        self.assertEqual(moved["plains"]["condition-basic"], START)
        self.assertEqual(moved["mabe"]["condition-basic"], "plains & sword")
        self.assertEqual(moved["warp"]["condition-basic"], f"{START} & ocarina")

    def test_refused(self) -> None:
        refused = {
            "empty rule": dict(a=zone(one_way={"b": None}), b=zone()),
            "empty door rule": dict(a=zone(doors={"Field_a": None})),
            "empty level": dict(a=zone(one_way={"b": {"basic": ""}}), b=zone()),
            "always inside a rule": dict(a=zone(one_way={"b": f"{ALWAYS} & sword"}), b=zone()),
            "no such zone": dict(a=zone(one_way={"b": ALWAYS})),
            "a check as target": dict(a=zone(one_way={"b": ALWAYS}), b={"type": "item", "region": "a"}),
            "two-way at both ends": dict(a=zone(connections={"b": ALWAYS}), b=zone(connections={"a": ALWAYS})),
            "a zone's own rule": dict(a={"type": "region", "condition-basic": "sword"}),
            "routes on a check": dict(a={"type": "item", "connections": {"b": ALWAYS}}, b=zone()),
            "no such door": dict(a=zone(doors={"Nowhere": ALWAYS})),
            "a door in two zones": dict(a=zone(doors={"Field_a": ALWAYS}), b=zone(doors={"Field_a": ALWAYS})),
            "a bad rule": dict(a=zone(one_way={"b": "sword &"}), b=zone()),
        }
        for why, nodes in refused.items():
            with self.subTest(why), self.assertRaises(ValueError):
                lowered(**nodes)


class TestLogicFile(unittest.TestCase):
    def test_every_zone_but_the_start_has_a_way_in(self) -> None:
        """Without one a zone would never hold."""
        for name, node in LOGIC.items():
            if node["type"] in PLACES and name != START:
                self.assertNotEqual(node["condition-basic"], NEVER, name)

    def test_the_house_leads_to_mabe_through_its_door(self) -> None:
        self.assertEqual(LOGIC["mabe"]["condition-basic"], START)
        self.assertNotIn("condition-basic", LOGIC[START])

    def test_doors_have_one_name_each(self) -> None:
        doors = yaml.safe_load((DATA / "doors.yml").read_text(encoding="utf-8"))
        names = list(doors["pairs"]) + list(doors["pairs"].values()) + list(doors["one-way"])
        self.assertEqual(len(names), len(set(names)))
