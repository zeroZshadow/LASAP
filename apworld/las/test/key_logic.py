"""Small-key soundness: the logic counts the keys Link has received, the game takes one per lock opened.

key_locks.yml models each dungeon's locks (ObjLockDoor and ObjLockBlock, by their opened flag): where a lock is opened
from (`at`) and the logic nodes and locations its key requirement stands for (`opens`). In the game's terms a node needs
those locks open: each key token of the node is replaced by its locks, and a node without a key token of its own needs
them on top of its condition. `routes` adds ways to a node that the logic does not count, in the game's terms only.
`at` and the routes are conditions in the logic's grammar, in which `lock-<flag>` holds once that lock is open.

A player who spends keys as badly as possible opens any lock they can reach, in any order. The logic is sound when,
whatever they opened, everything the logic gives them with that many keys is still reachable:
- with every key spent (each set of locks opened in a valid order, against the logic with that many keys);
- with keys left and no lock left to reach (against the logic with all of the dungeon's keys).
More items only let that player reach more locks, so the worst cases have nearly everything: every item and setting,
and every item and setting but one or two (a missing item can close the route that needs no key).
"""
import itertools
from typing import Dict, FrozenSet, Iterable, List, Mapping, NamedTuple, Set, Tuple

from ..lasr import COUNTED, ITEM_DEFS, location_dungeon
from ..logic import condition_keys, node_tokens, parse


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
        self.locks = {flag: dict(lock) for flag, lock in model["locks"].items()}
        self.routes: Mapping[str, List[str]] = model.get("routes") or {}
        self.node_locks: Dict[str, FrozenSet[int]] = {}
        for flag, lock in self.locks.items():
            for name in lock.get("opens") or ():
                assert name in logic, (dungeon, flag, name)
                self.node_locks[name] = self.node_locks.get(name, frozenset()) | {flag}
        self.targets = [name for name, node in logic.items()
                        if (node["type"] == "item" and location_dungeon(name) == dungeon)
                        or (node["type"] == "room" and name.startswith(f"D{dungeon}-"))]
        self.extra_keys = condition_keys(preset)
        exprs = [lock["at"] for lock in self.locks.values()] + [r for routes in self.routes.values() for r in routes]
        self.nodes = self._closure(self.targets + list(self.routes) + [n for e in exprs for n in self._expr_nodes(e)])
        self.variables: Set[str] = set()
        self.count_rules = {name: self._node_rule(name, False) for name in self.nodes}
        self.game_rules = {name: self._node_rule(name, True) for name in self.nodes}
        self.at_rules = {flag: self._rule(self._compile(parse(lock["at"]), "", True)) for flag, lock in self.locks.items()}
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
            if base in self.logic and self.logic[base]["type"] != "item":
                yield base

    def _closure(self, roots: List[str]) -> List[str]:
        seen, stack = set(roots), list(roots)
        while stack:
            name = stack.pop()
            node = self.logic[name]
            exprs = self._conditions_of(node) + list(self.routes.get(name, ()))
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

    def _compile(self, tree, owner: str, game: bool) -> str:
        kind = tree[0]
        if kind == "not":
            return f"(not {self._compile(tree[1], owner, game)})"
        if kind in ("and", "or"):
            return "(" + f" {kind} ".join(self._compile(part, owner, game) for part in tree[1]) + ")"
        name, count = tree[1], tree[2]
        if "[" in name:
            base, param = name[:-1].split("[")
            variant = self.logic[base].get(f"condition-{param}")
            inner = self._compile(parse(str(variant)), base, game) if variant else "False"
            return f"({base!r} in R or {inner})"
        if name == self.key:
            if not game:
                return f"(K >= {count})"
            locks = self.node_locks.get(owner, frozenset())
            assert locks, f"{owner}: {name} stands for no lock in key_locks.yml"
            return f"(O >= {set(locks)!r})"
        if name in ITEM_DEFS:
            return f"(c.get({name!r}, 0) >= {count})"
        if name in COUNTED:
            return "(" + " + ".join(f"(c.get({i!r}, 0) > 0)" for i in COUNTED[name]) + f" >= {count})"
        if name in self.logic and self.logic[name]["type"] != "item":
            return f"({name!r} in R)" if count <= 1 else "False"
        if name.startswith("lock-"):
            assert game and int(name[5:]) in self.locks, name
            return f"({int(name[5:])} in O)"
        self.variables.add(name)  # a setting
        return f"(c.get({name!r}, 0) >= {count})"

    def _node_rule(self, name: str, game: bool):
        node = self.logic[name]
        conditions = self._conditions(name)
        expr = " or ".join(self._compile(parse(c), name, game) for c in conditions) if conditions else "True"
        if node.get("region") in self.logic:
            expr = f"({node['region']!r} in R) and ({expr})"
        if game and name in self.node_locks and not any(self.key in node_tokens(parse(c)) for c in conditions):
            expr = f"(O >= {set(self.node_locks[name])!r}) and ({expr})"
        if game and name in self.routes:
            expr = " or ".join([f"({expr})"] + [self._compile(parse(route), name, True) for route in self.routes[name]])
        return self._rule(expr)

    @staticmethod
    def _rule(expr: str):
        return eval(f"lambda c, R, K, O: {expr}")

    def _dynamic(self) -> Set[str]:
        """Nodes that depend on keys or locks."""
        dynamic = set(self.node_locks) | set(self.routes)
        for name in self.nodes:
            if any(self.key in node_tokens(parse(c)) for c in self._conditions(name)):
                dynamic.add(name)
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
