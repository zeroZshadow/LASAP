"""Small-key soundness: the logic counts the keys Link has received, the game takes one per lock opened.

key_locks.yml models each dungeon's locks (ObjLockDoor and ObjLockBlock, by their opened flag) as the zones each stands
between: the first, and each other zone, whose way to the first goes through the lock. Link opens a lock from any of its
zones. The logic writes a lock's key count on the routes through it; lowered, a zone's condition is a way per route in,
each led by the zone the route comes from. In the game's terms a key token on a way is its lock open: the lock between
the way's zone and the zone it leads to. A two-way route through a lock reads the key on the way back too, where in the
game the lock is open once Link has come through it.

Every place outside the dungeon counts as reached: with the entrances shuffled, any of the dungeon's doors can be the
way in, and an exit leads back in by another door.

A player who spends keys as badly as possible opens any lock they can reach, in any order. The logic is sound when,
whatever they opened, everything the logic gives them with that many keys is still reachable:
- with every key spent (each set of locks opened in a valid order, against the logic with that many keys);
- with keys left and no lock left to reach (against the logic with all of the dungeon's keys).
More items only let that player reach more locks, so the worst cases have nearly everything: every item and setting,
and every item and setting but one or two (a missing item can close the route that needs no key).
"""
import itertools
from typing import Dict, FrozenSet, Iterable, List, Mapping, NamedTuple, Optional, Set, Tuple

from ..lasr import COUNTED, ITEM_DEFS, location_dungeon
from ..logic import PLACES, condition_keys, node_tokens, parse


class Failure(NamedTuple):
    target: str
    keys: int  # keys received
    opened: FrozenSet[int]  # locks opened with them
    missing: Tuple[Tuple[str, int], ...]  # items and settings taken away from everything: (name, count left)
    stuck: bool  # keys left, no lock left to reach


class KeyLogicCheck:
    def __init__(self, logic: Mapping[str, Mapping], preset: str, dungeon: int, model: Mapping[str, Mapping]):
        self.logic = logic
        self.preset = preset
        self.key = f"key-D{dungeon}"
        self.total_keys = ITEM_DEFS[self.key]["quantity"]
        self.locks: Dict[int, List[str]] = {flag: list(zones) for flag, zones in model["locks"].items()}
        self.pair_locks: Dict[FrozenSet[str], int] = {}
        for flag, zones in self.locks.items():
            for zone in zones:
                assert zone in logic and logic[zone]["type"] in PLACES, (dungeon, flag, zone)
            for zone in zones[1:]:
                pair = frozenset((zones[0], zone))
                assert self.pair_locks.get(pair, flag) == flag, (dungeon, flag, sorted(pair))
                self.pair_locks[pair] = flag
        self.prefix = prefix = f"D{dungeon}."
        self.targets = [name for name, node in logic.items()
                        if (node["type"] == "item" and location_dungeon(name) == dungeon)
                        or (node["type"] in PLACES and name.startswith(prefix))]
        self.extra_keys = condition_keys(preset)
        self.nodes = self._closure(self.targets + [zone for zones in self.locks.values() for zone in zones])
        self.variables: Set[str] = set()
        self.count_rules = {name: self._node_rule(name, False) for name in self.nodes}
        self.game_rules = {name: self._node_rule(name, True) for name in self.nodes}
        self.at_rules = {flag: self._rule(" or ".join(f"({zone!r} in R)" for zone in zones))
                         for flag, zones in self.locks.items()}
        self.dynamic = self._dynamic()
        self.static = [name for name in self.nodes if name not in self.dynamic]
        self.dynamic_order = [name for name in self.nodes if name in self.dynamic]

    # --- compiling ---

    def _conditions(self, name: str) -> List[str]:
        node = self.logic[name]
        keys = ("condition-basic",) if node["type"] == "enemy" else ("condition-basic",) + self.extra_keys
        return [str(node[k]) for k in keys if node.get(k)]

    def _expr_nodes(self, expr: str) -> Iterable[str]:
        for token in node_tokens(parse(expr)):
            base = token.split("[")[0]
            if base in self.logic and self.logic[base]["type"] != "item" and not self._outside(base):
                yield base

    def _closure(self, roots: List[str]) -> List[str]:
        seen, stack = set(roots), list(roots)
        while stack:
            name = stack.pop()
            node = self.logic[name]
            exprs = self._conditions_of(node)
            if node.get("region") in self.logic:
                exprs.append(node["region"])
            for expr in exprs:
                for base in self._expr_nodes(expr):
                    if base not in seen:
                        seen.add(base)
                        stack.append(base)
        return [name for name in self.logic if name in seen]

    def _conditions_of(self, node: Mapping) -> List[str]:
        keys = ("condition-basic", "condition-pit", "condition-heavy") if node["type"] == "enemy" else \
            ("condition-basic",) + self.extra_keys
        return [str(node[k]) for k in keys if node.get(k)]

    def _outside(self, name: str) -> bool:
        return self.logic[name]["type"] in PLACES and not name.startswith(self.prefix)

    def _is_place(self, tree) -> bool:
        return tree[0] == "tok" and tree[2] <= 1 and self.logic.get(tree[1], {}).get("type") in PLACES

    def _source(self, way) -> Optional[str]:
        """The zone a lowered way comes from: the way itself or the zone leading its `&`."""
        if self._is_place(way):
            return way[1]
        if way[0] == "and":
            return next((part[1] for part in way[1] if self._is_place(part)), None)
        return None

    def _compile_condition(self, text: str, owner: str, game: bool) -> str:
        tree = parse(text)
        ways = tree[1] if tree[0] == "or" else [tree]
        return "(" + " or ".join(self._compile(way, owner, game, self._source(way)) for way in ways) + ")"

    def _compile(self, tree, owner: str, game: bool, source: Optional[str]) -> str:
        kind = tree[0]
        if kind == "not":
            return f"(not {self._compile(tree[1], owner, game, source)})"
        if kind in ("and", "or"):
            return "(" + f" {kind} ".join(self._compile(part, owner, game, source) for part in tree[1]) + ")"
        name, count = tree[1], tree[2]
        if "[" in name:
            base, param = name[:-1].split("[")
            variant = self.logic[base].get(f"condition-{param}")
            inner = self._compile(parse(str(variant)), base, game, None) if variant else "False"
            return f"({base!r} in R or {inner})"
        if name == self.key:
            if not game:
                return f"(K >= {count})"
            flag = self.pair_locks.get(frozenset((source, owner))) if source else None
            assert flag is not None, f"{owner} from {source}: {name} on a way through no lock in key_locks.yml"
            return f"({flag} in O)"
        if name in ITEM_DEFS:
            return f"(c.get({name!r}, 0) >= {count})"
        if name in COUNTED:
            return "(" + " + ".join(f"(c.get({i!r}, 0) > 0)" for i in COUNTED[name]) + f" >= {count})"
        if name in self.logic and self.logic[name]["type"] != "item":
            if count > 1:
                return "False"
            return "True" if self._outside(name) else f"({name!r} in R)"
        self.variables.add(name)  # a setting
        return f"(c.get({name!r}, 0) >= {count})"

    def _node_rule(self, name: str, game: bool):
        node = self.logic[name]
        conditions = self._conditions(name)
        expr = " or ".join(self._compile_condition(c, name, game) for c in conditions) if conditions else "True"
        if node.get("region") in self.logic:
            expr = f"({node['region']!r} in R) and ({expr})"
        return self._rule(expr)

    @staticmethod
    def _rule(expr: str):
        return eval(f"lambda c, R, K, O: {expr}")

    def _dynamic(self) -> Set[str]:
        """Nodes that depend on keys or locks."""
        dynamic = {name for name in self.nodes if any(self.key in node_tokens(parse(c)) for c in self._conditions(name))}
        changed = True
        while changed:
            changed = False
            for name in self.nodes:
                if name in dynamic:
                    continue
                node = self.logic[name]
                refs = [b for c in self._conditions_of(node) for b in self._expr_nodes(c)] + [node.get("region")]
                if any(ref in dynamic for ref in refs):
                    dynamic.add(name)
                    changed = True
        return dynamic

    # --- evaluating ---

    @staticmethod
    def _fixpoint(names: List[str], rules, c, R: Set[str], K, O) -> Set[str]:
        changed = True
        while changed:
            changed = False
            for name in names:
                if name not in R and rules[name](c, R, K, O):
                    R.add(name)
                    changed = True
        return R

    def items(self, missing: Iterable[Tuple[str, int]] = ()) -> Dict[str, int]:
        items = {name: definition.get("quantity", 1) for name, definition in ITEM_DEFS.items()}
        items.update({name: 1 for name in self.variables})
        for name, left in missing:
            items[name] = min(items[name], left)
        return items

    def check(self, missing: Tuple[Tuple[str, int], ...] = ()) -> List[Failure]:
        c = self.items(missing)
        c[self.key] = self.total_keys  # what the logic's own key tokens read is K
        base = self._fixpoint(self.static, self.count_rules, c, set(), None, None)
        logic = [self._fixpoint(self.dynamic_order, self.count_rules, c, set(base), k, None)
                 for k in range(self.total_keys + 1)]
        failures = []
        seen = {frozenset()}
        frontier = [frozenset()]
        while frontier:
            opened = frontier.pop()
            reached = self._fixpoint(self.dynamic_order, self.game_rules, c, set(base), None, opened)
            reachable = [flag for flag in self.locks if flag not in opened and self.at_rules[flag](c, reached, None, opened)]
            checks = [(len(opened), False)] if len(opened) <= self.total_keys else []
            if not reachable and len(opened) < self.total_keys:
                checks.append((self.total_keys, True))
            for keys, stuck in checks:
                for target in self.targets:
                    if target in logic[keys] and target not in reached:
                        failures.append(Failure(target, keys, opened, missing, stuck))
            if len(opened) < self.total_keys:
                for flag in reachable:
                    child = opened | {flag}
                    if child not in seen:
                        seen.add(child)
                        frontier.append(child)
        return failures

    def cut_candidates(self) -> List[Tuple[str, int]]:
        """Items and settings the dungeon's logic reads, other than its keys, each with none left and, for an item of
        several copies, one copy fewer."""
        names = set(self.variables)
        for name in self.nodes:
            for condition in self._conditions_of(self.logic[name]):
                names.update(t for t in node_tokens(parse(condition)) if t in ITEM_DEFS)
        names.discard(self.key)
        full = self.items()
        return [(name, left) for name in sorted(names) for left in range(full[name])]

    def run(self, depth: int = 2) -> List[Failure]:
        candidates = self.cut_candidates()
        failures = []
        for size in range(depth + 1):
            for missing in itertools.combinations(candidates, size):
                if len({name for name, _ in missing}) == size:
                    failures += self.check(missing)
        return failures
