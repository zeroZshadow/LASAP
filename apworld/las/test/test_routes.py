"""logic.yml's routes (logic.lower_routes): how they read, what they refuse, and what the file itself holds."""
import unittest

from ..lasr import DOORS as SHIPPED_DOORS, LOGIC, WRITTEN_LOGIC
from ..logic import ALWAYS, NEVER, PLACES, START, house_exits, lower_routes, node_tokens, parse, start_doors

DOORS = {"pairs": {"Field_a": "Inside_a", "Field_b": "Inside_b", "Field_d": "Inside_d"}, "one-way": {"Hole": "Field_c"},
         "groups": {"single": ["Field_a", "Field_b", "Field_d"]}}


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

    def test_a_start_swaps_the_houses_door_with_the_start_door(self) -> None:
        """The start door leads into the house and back, the door outside the house into the start door's inside."""
        logic = {START: zone(doors={"Inside_a": ALWAYS}, one_way={"warp": "ocarina"}), "mabe": zone(doors={"Field_a": ALWAYS}),
                 "plains": zone(doors={"Field_b": ALWAYS}), "cave": zone(doors={"Inside_b": ALWAYS}), "warp": zone()}
        doors = start_doors(logic, DOORS, "Field_b")
        self.assertEqual(doors["pairs"], {**DOORS["pairs"], "Field_b": "Inside_a", "Field_a": "Inside_b"})
        moved = lower_routes(logic, doors)
        self.assertEqual(moved["plains"]["condition-basic"], START)
        self.assertEqual(moved["cave"]["condition-basic"], "mabe")
        self.assertEqual(moved["mabe"]["condition-basic"], "cave")
        self.assertEqual(moved["warp"]["condition-basic"], f"{START} & ocarina")
        self.assertIs(start_doors(logic, DOORS, "Field_a"), DOORS)
        self.assertIs(start_doors(logic, DOORS, None), DOORS)

    def test_house_exits_are_overworld_doors_with_no_rule_in(self) -> None:
        """Link can walk back into the house from wherever its door leads."""
        logic = {START: zone(doors={"Inside_a": ALWAYS}), "mabe": zone(doors={"Field_a": ALWAYS, "Field_d": "bombs"}),
                 "plains": zone(doors={"Field_b": {"basic": ALWAYS, "hell": "sword"}}), "pit": zone(doors={"Field_c": ALWAYS}),
                 "cave": zone(doors={"Inside_b": ALWAYS})}
        self.assertEqual(house_exits(logic, DOORS), {"Field_a": "mabe"})

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
        """The house's door comes out in mabe, so mabe has a way in from the house; the house is the start."""
        pairs = {**SHIPPED_DOORS["pairs"], **{b: a for a, b in SHIPPED_DOORS["pairs"].items()}}
        zone_of = {door: name for name, node in WRITTEN_LOGIC.items() for door in (node.get("doors") or {})}
        self.assertEqual({zone_of.get(pairs.get(door)) for door in WRITTEN_LOGIC[START].get("doors") or {}}, {"mabe"})
        self.assertIn(START, node_tokens(parse(LOGIC["mabe"]["condition-basic"])))
        self.assertNotIn("condition-basic", LOGIC[START])

    def test_doors_have_one_name_each(self) -> None:
        names = list(SHIPPED_DOORS["pairs"]) + list(SHIPPED_DOORS["pairs"].values()) + list(SHIPPED_DOORS["one-way"])
        self.assertEqual(len(names), len(set(names)))

    def test_door_groups_hold_pairs_once(self) -> None:
        """An entrance shuffle draws pairs from the groups by their first name."""
        grouped = [door for names in SHIPPED_DOORS["groups"].values() for door in names]
        self.assertEqual(len(grouped), len(set(grouped)))
        self.assertLessEqual(set(grouped), set(SHIPPED_DOORS["pairs"]))
