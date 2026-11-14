"""Logic Studio's Simulator for this game, which the manifest names: it answers logic questions with the real AP world, in
its own process, one JSON request a line on stdin and one reply a line on stdout (Logic Studio's DATA.md has the protocol).

python simulator.py [--logic <logic.yml>]   the file stands in for the world's logic.yml: the world fixes what it derives from it at import.
Any Python starts it: it runs itself in .venv-ap.

Nothing here decides what a rule means: worlds come from setup_multiworld as in the world's tests, rules from its LogicCompiler
and make_rule. The start overlay lowers logic.yml's routes again with the house's door swapped with a door elsewhere (the world's
start_doors), and refreshes the one thing derived from the result, DOOR_LOGIC. Elsewhere is a zone with an overworld door Link can
always go in by (the world's house_exits).
"""
import argparse
import json
import logging
import os
import sys
import time
from collections import Counter, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AP = ROOT / "vendor" / "Archipelago"
STEPS = ("generate_early", "create_regions", "create_items", "set_rules", "connect_entrances", "generate_basic")
# Every category on, so all of logic.yml's locations are in the world
ALL_CHECKS = {"trade_quest": True, "dampe": True, "rapids": True, "fishing": True, "blue_rupees": True, "owl_statues": "all", "seashell_mansion": 50}
EXIT = "mabe"  # where the house's door leads in doors.yml
SETS_SECONDS = 0.4
WORLDS_KEPT = 32  # one per options and from zone; a world is quick to build and small
SETS_SHOWN = 5
SETS_SECONDS_MOST = 5  # the longest a request may ask the search to take


def import_world(logic_file):
    """Imports Archipelago and the world, with logic_file in place of logic.yml. AP prints while it loads its worlds."""
    if logic_file:
        import logic_override
        logic_override.use(logic_file)
    os.chdir(AP)
    sys.path.insert(0, str(AP))
    os.environ["AP_TEST_WORLDS"] = "las"  # AP loads only this world, not every game's; it reads this only under a test runner
    import unittest  # noqa: F401
    logging.disable(logging.CRITICAL)
    from BaseClasses import CollectionState
    from Fill import FillError, distribute_items_restrictive
    from test.general import gen_steps, setup_multiworld
    from worlds.AutoWorld import AutoWorldRegister
    from worlds.las import lasr as lasr_module
    from worlds.las.items import LASR_TO_ITEM
    from worlds.las.lasr import COUNTED, DOOR_LOGIC, ITEM_DEFS, LOGIC, door_logic
    from worlds.las.logic import NEVER, PLACES, PRESETS, START, house_exits, lower_routes, make_rule, node_tokens, parse, start_doors
    from worlds.las.options import LASOptions
    return dict(CollectionState=CollectionState, FillError=FillError, distribute=distribute_items_restrictive, setup=setup_multiworld, gen_steps=gen_steps,
                world_type=AutoWorldRegister.world_types["Links Awakening Switch"], lasr=lasr_module, item_names=LASR_TO_ITEM, counted=COUNTED,
                door_logic_dict=DOOR_LOGIC, item_defs=ITEM_DEFS, logic=LOGIC, door_logic=door_logic, presets=PRESETS, make_rule=make_rule,
                node_tokens=node_tokens, parse=parse, options=LASOptions, places=PLACES, lower_routes=lower_routes, house_exits=house_exits, start=START, never=NEVER,
                start_doors=start_doors)


def smallest_within(pool, holds):
    """A set within a pool that holds, with no unit it can do without: chunks are dropped while it still holds, the chunk
    halving down to single units, so the many units a rule does not read go in a few tries."""
    keep = list(pool)
    chunk = max(1, len(keep) // 2)
    while True:
        at = 0
        while at < len(keep):
            trial = keep[:at] + keep[at + chunk:]
            if holds(trial):
                keep = trial
            else:
                at += chunk
        if chunk == 1:
            return keep
        chunk = max(1, chunk // 2)


class Worker:
    def __init__(self, logic_file=None):
        self.ap = import_world(logic_file)
        self.logic = self.ap["logic"]
        self.written = self.ap["lasr"]._load_yaml("logic.yml"), self.ap["lasr"]._load_yaml("doors.yml")
        self.exits = {}  # zone -> the doors in it the house's exit may lead to (house_exits), the house's own first
        logic, doors = self.written
        pairs = {**doors["pairs"], **{b: a for a, b in doors["pairs"].items()}}
        own = {pairs.get(door) for door in logic.get(self.ap["start"], {}).get("doors") or {}}
        for door, zone in sorted(self.ap["house_exits"](*self.written).items(), key=lambda item: (item[0] not in own, item[0])):
            self.exits.setdefault(zone, []).append(door)
        self.worlds = {}
        self.overlay = EXIT

    # ---- the world for a set of choices

    def options_for(self, request, all_checks=True):
        options = dict(ALL_CHECKS) if all_checks else {}
        options["logic"] = request.get("preset", "basic")
        tokens = request.get("options") or {}
        hints = self.ap["options"].type_hints
        for token, on in tokens.items():
            attribute = token.replace("-", "_")
            if attribute in hints:
                options[attribute] = bool(on)
        if "can-steal" in tokens or "stealing-needs-sword" in tokens:
            options["stealing"] = "never" if not tokens.get("can-steal") else "standard" if tokens.get("stealing-needs-sword") else "always"
        return options

    def set_start(self, start):
        """The zone the house's door leads to: the house's door swaps with the zone's first door of house_exits, so that door leads
        into the house and the door outside the house where it led.
        Fills LOGIC's zones and DOOR_LOGIC anew, in place: the world and the compiler read those dicts."""
        if start == self.overlay:
            return
        if start != EXIT and start not in self.exits:
            raise ValueError(f"{start}: the house's exit leads only to a zone with an overworld door that has no rule to go in")
        logic, doors = self.written
        swapped = self.ap["start_doors"](logic, doors, self.exits[start][0] if start in self.exits else None)
        lowered = self.ap["lasr"].plain_logic(logic, swapped)
        for name, node in lowered.items():
            if node["type"] in self.ap["places"]:
                self.logic[name].clear()
                self.logic[name].update(node)
        fresh = self.ap["door_logic"](logic, swapped)
        doors = self.ap["door_logic_dict"]
        doors.clear()
        doors.update(fresh)
        self.overlay = start
        self.worlds.clear()

    def world_for(self, request):
        """The world for a request's options. With `from`, that zone is reached too, as if the game started there as well.
        With `within`, a list of zones, every other zone never holds: the way stays inside them, not out through a door
        and back in by another, as where a door leads can be shuffled."""
        self.set_start(request.get("start") or EXIT)
        options = self.options_for(request)
        start = request.get("from") or ""
        within = sorted(request.get("within") or [])
        key = json.dumps([options, start, within], sort_keys=True)
        if key not in self.worlds:
            if len(self.worlds) > WORLDS_KEPT:
                self.worlds.pop(next(iter(self.worlds)))
            doors = self.ap["door_logic_dict"]
            kept = dict(doors)
            shut = {name: {**node, **{k: self.ap["never"] for k in node if k.startswith("condition-")}, "condition-basic": self.ap["never"]}
                    for name, node in kept.items() if within and node["type"] in self.ap["places"] and name not in within}
            doors.update(shut)  # the world compiles its rules from DOOR_LOGIC while it is set up
            try:
                multiworld = self.ap["setup"](self.ap["world_type"], STEPS, 1, options)
            finally:
                doors.clear()
                doors.update(kept)
            world = multiworld.worlds[1]
            if shut:  # rules the compiler makes later read the zones shut too
                world.compiler.logic = {**world.compiler.logic, **shut}
                world.compiler._inline.clear()
            if start:
                if start not in world.node_regions:
                    raise LookupError(f"{start}: no check, region or the goal needs it, so the world has no region for it")
                world.get_region(world.origin_region_name).connect(world.node_regions[start], f"from {start}")
            self.worlds[key] = {"multiworld": multiworld, "world": world, "rules": {}}
        return self.worlds[key]

    # ---- rules, through the world's compiler

    def expr_rule(self, built, expr):
        return self.ap["make_rule"](expr, 1, built["world"].node_regions)

    def node_rule(self, built, name):
        rules = built["rules"]
        if name not in rules:
            rules[name] = self.expr_rule(built, built["world"].compiler.node_expr(name))
        return rules[name]

    def state_for(self, built, items):
        state = self.ap["CollectionState"](built["multiworld"])
        have = state.prog_items[1]
        for name, count in items.items():
            ap_name = self.ap["item_names"].get(name)
            if ap_name and count > 0:
                have[ap_name] += count
        state.stale[1] = True
        state.sweep_for_advancements()
        return state

    def holds(self, built, state, name):
        """A logic node's or check's state, as the world's region and location rules have it."""
        world = built["world"]
        node = self.logic[name]
        if node["type"] == "item":
            return world.node_regions[node["region"]].can_reach(state) and self.node_rule(built, name)(state)
        if name in world.node_regions:
            return world.node_regions[name].can_reach(state)
        if node["type"] in ("region", "room"):  # the world makes a region only for one a check, the goal or a region needs
            raise LookupError(f"{name}: no check, region or the goal needs it, so the world has no region for it")
        return self.node_rule(built, name)(state)

    def reach(self, request):
        started = time.perf_counter()
        built = self.world_for(request)
        state = self.state_for(built, request.get("items") or {})
        checks, zones = {}, {}
        for name, node in self.logic.items():
            if node["type"] == "item":
                checks[name] = bool(self.holds(built, state, name))
            elif node["type"] in ("region", "room") and name in built["world"].node_regions:
                zones[name] = bool(self.holds(built, state, name))
        goal = self.expr_rule(built, built["world"].compiler.token("kill-nightmare", 1))(state)
        return {"checks": checks, "zones": zones, "goal": bool(goal), "instruments_required": built["world"].goal_instruments, "ms": round((time.perf_counter() - started) * 1000, 1)}

    # ---- why

    def tree(self, built, state, tree):
        kind = tree[0]
        if kind == "tok":
            expr = built["world"].compiler.token(tree[1], tree[2])
            return {"tok": tree[1], "count": tree[2], "holds": bool(self.expr_rule(built, expr)(state))}
        if kind == "not":
            inner = self.tree(built, state, tree[1])
            return {"op": "not", "kids": [inner], "holds": not inner["holds"]}
        kids = [self.tree(built, state, part) for part in tree[1]]
        return {"op": kind, "kids": kids, "holds": (all if kind == "and" else any)(k["holds"] for k in kids)}

    def rule_trees(self, built, state, name):
        """Each condition of a node with what holds in it. The text is logic.yml's; `read` is false for one the preset does not read."""
        node = self.logic[name]
        extra = built["world"].compiler.extra_keys
        out = []
        for key, text in node.items():
            if key.startswith("condition-") and text not in (None, ""):
                read = key == "condition-basic" or (key in extra and node["type"] != "enemy")
                out.append({"key": key[len("condition-"):], "text": str(text), "read": read, "tree": self.tree(built, state, self.ap["parse"](str(text)))})
        return out

    def explain(self, request):
        built = self.world_for(request)
        state = self.state_for(built, request.get("items") or {})
        name = request["name"]
        node = self.logic[name]
        reply = {"name": name, "type": node["type"], "holds": bool(self.holds(built, state, name)), "rules": self.rule_trees(built, state, name)}
        if node["type"] == "item":
            zone = node["region"]
            reply["zone"] = {"name": zone, "holds": bool(self.holds(built, state, zone)), "rules": self.rule_trees(built, state, zone)}
            reply["rule_holds"] = bool(self.node_rule(built, name)(state))
        return reply

    # ---- smallest item sets

    def needed_units(self, built, name):
        """(item, nth copy) a node's rules can use: the items of its conditions and of everything they lead to, up to the counts asked for."""
        compiler = built["world"].compiler
        lasr = self.ap["lasr"]
        door_into = {lasr.dungeon_region(target): lasr.door_node(door) for door, target in built["world"].dungeon_entrances.items()}
        roots = {name}
        # A dungeon's region holds no conditions of its own: the way in is the door that leads there
        while True:
            nodes = compiler.dependencies(roots)
            doors = {door_into[n] for n in nodes if n in door_into} - roots
            if not doors:
                break
            roots |= doors
        most = Counter()
        for node_name in {*nodes, name}:
            node = compiler.logic[node_name]
            for key, text in node.items():
                if not key.startswith("condition-") or not text:
                    continue
                if key != "condition-basic" and key not in compiler.extra_keys and key not in ("condition-pit", "condition-heavy"):
                    continue
                self.count_tokens(self.ap["parse"](str(text)), most)
        return most

    def count_tokens(self, tree, most):
        if tree[0] == "tok":
            name, count = tree[1], tree[2]
            base = name.split("[")[0]
            if base in self.ap["counted"]:
                for item in self.ap["counted"][base]:
                    most[item] = max(most[item], 1)
            elif base in self.ap["item_names"]:
                most[base] = max(most[base], count)
        elif tree[0] == "not":
            self.count_tokens(tree[1], most)
        else:
            for part in tree[1]:
                self.count_tokens(part, most)

    def smallest_sets(self, request):
        """The smallest sets of items to add for a node to hold, by Reiter's hitting set tree over what the node can use: `complete`
        when the time allowed walked the whole tree, so that there are no smaller sets than these."""
        built = self.world_for(request)
        name = request["name"]
        held = Counter({k: v for k, v in (request.get("items") or {}).items() if v > 0})

        def holds_with(units):
            items = Counter(held)
            for item, _ in units:
                items[item] += 1
            return self.holds(built, self.state_for(built, items), name)

        if holds_with(()):
            return {"name": name, "sets": [], "complete": True, "holds": True}
        most = self.needed_units(built, name)
        universe = [(item, n) for item, top in most.items() for n in range(held[item] + 1, top + 1)]
        if not holds_with(universe):
            return {"name": name, "sets": [], "complete": True, "holds": False, "never": True}
        deadline = time.perf_counter() + min(float(request.get("seconds") or SETS_SECONDS), SETS_SECONDS_MOST)
        found = []
        queue = deque([frozenset()])
        seen = set()
        complete = True
        while queue:
            if time.perf_counter() > deadline:
                complete = False
                break
            removed = queue.popleft()
            if removed in seen:
                continue
            seen.add(removed)
            label = next((f for f in found if not f & removed), None)
            if label is None:
                pool = [u for u in universe if u not in removed]
                if not holds_with(pool):
                    continue
                label = frozenset(smallest_within(pool, holds_with))
                found.append(label)
            queue.extend(removed | {unit} for unit in label)
        sets = sorted({tuple(sorted(Counter(item for item, _ in f).items())) for f in found}, key=lambda s: (sum(n for _, n in s), s))
        return {"name": name, "sets": [[{"item": item, "count": n} for item, n in s] for s in sets[:int(request.get("shown") or SETS_SHOWN)]], "found": len(sets), "complete": complete, "holds": False}

    # ---- whole seeds

    def fill(self, request, emit):
        """Real seeds per start: reach with every item, the goal, and whether a fill succeeds; a start in a pocket the
        fill cannot get out of (the world's start_opens_up) gets no seeds."""
        seeds = request.get("seeds", 5)
        for start in request["starts"]:
            self.set_start(start)
            options = self.options_for(request, all_checks=False)
            ok = 0
            first_error = ""
            before = self.ap["setup"](self.ap["world_type"], STEPS, 1, options)
            state = before.get_all_state()
            locations = [before.get_location(self.ap_name(loc), 1) for loc in before.worlds[1].shuffled]
            shuffled = len(locations)
            reached = sum(1 for location in locations if location.can_reach(state))
            goal = bool(before.can_beat_game(state))
            if not before.worlds[1].start_opens_up():  # a pocket: no seed starts there
                emit({"start": start, "reached": reached, "shuffled": shuffled, "goal": goal, "fills": 0, "seeds": 0, "error": "", "pocket": True})
                continue
            for seed in range(seeds):
                try:
                    self.ap["distribute"](self.ap["setup"](self.ap["world_type"], self.ap["gen_steps"], seed + 1, options))
                    ok += 1
                except self.ap["FillError"] as error:
                    first_error = first_error or str(error).split("\n")[0][:200]
            emit({"start": start, "reached": reached, "shuffled": shuffled, "goal": goal, "fills": ok, "seeds": seeds, "error": first_error})
        self.set_start(EXIT)
        return {"done": True}

    @staticmethod
    def ap_name(location):
        from worlds.las.locations import LOCATION_NAMES
        return LOCATION_NAMES[location]

    # ---- what the UI can choose

    def meta(self, _request):
        defaults = self.world_for({"preset": "basic"})["world"].settings_tokens
        read = Counter()
        for node in self.logic.values():
            for key, text in node.items():
                if key.startswith("condition-") and text:
                    self.count_tokens(self.ap["parse"](str(text)), read)
        items = {name: {"quantity": max(self.ap["item_defs"].get(name, {}).get("quantity", 1), read[name])} for name in sorted(read)}
        hints = self.ap["options"].type_hints
        tokens = {token.replace("_", "-") for token in hints}
        spec = self.ap["lasr"].SPEC
        return {"items": items, "unread_items": sorted(set(spec["items"]) - set(items)), "all_options": sorted(spec["options"]), "defaults": sorted(defaults),
                "option_tokens": sorted(tokens | {"can-steal", "stealing-needs-sword"}), "presets": list(self.ap["presets"]), "start": EXIT,
                "starts": [{"zone": zone, "doors": self.exits[zone]} for zone in sorted(self.exits, key=lambda z: (z != EXIT, z))]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--logic")
    args = parser.parse_args()
    # The protocol keeps the real stdout; whatever AP prints goes nowhere
    reply_stream = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
    silent = open(os.devnull, "w", encoding="utf-8")
    os.dup2(silent.fileno(), 1)
    os.dup2(silent.fileno(), 2)
    sys.stdout = sys.stderr = silent

    def send(message):
        reply_stream.write(json.dumps(message, separators=(",", ":")) + "\n")
        reply_stream.flush()

    if not (AP / "worlds" / "las").exists():
        send({"fatal": "vendor/Archipelago has no worlds/las: run bash scripts/worktree.sh setup"})
        return
    try:
        worker = Worker(args.logic)
    except Exception as error:  # Logic Studio shows why the world cannot run
        send({"fatal": f"The world does not load this logic: {type(error).__name__}: {error}"})
        return
    send({"ready": True})
    commands = {"reach": worker.reach, "explain": worker.explain, "sets": worker.smallest_sets, "meta": worker.meta}
    for line in sys.stdin:
        request = json.loads(line)
        try:
            if request["cmd"] == "fill":
                result = worker.fill(request, lambda row: send({"id": request["id"], "row": row}))
            else:
                result = commands[request["cmd"]](request)
            send({"id": request["id"], "result": result})
        except Exception as error:  # a bad request must not end the worker
            send({"id": request["id"], "error": f"{type(error).__name__}: {error}"})


if __name__ == "__main__":
    import venv_ap
    venv_ap.relaunch()
    main()
