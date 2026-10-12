"""Compiles LASR condition strings (logic.yml) into CollectionState rules.

Grammar: names, ``&``, ``|``, ``!``, parentheses, and ``name:N`` counts. Precedence follows the Python LASR
rewrites conditions into: ``!`` > ``&`` > ``|``.

Name resolution matches LASR's shuffler (``checkAccess``/``canReachLocation``):
- items from items.yml count copies (``sword:2``);
- logic nodes (region, room, interaction, enemy, trick) hold once their condition does; regions, rooms and
  nodes on a reference cycle become AP regions (checked with region reachability), the others are inlined;
- ``kill-x[pit]`` / ``kill-x[heavy]`` hold when ``kill-x`` holds or the enemy's ``condition-pit``/``-heavy`` holds;
- settings tokens are constants; anything else is undefined and never holds.
Nodes use condition-basic (or True when absent) OR the higher-preset conditions the preset enables; enemies only
ever use condition-basic, as in LASR.
"""
import re
from functools import lru_cache
from typing import Callable, Dict, Iterable, List, Mapping, Optional, Set, Tuple, Union

PRESETS = ("basic", "advanced", "glitched", "hell")
_TOKEN = re.compile(r"\s*(?:([a-zA-Z0-9\-\[\]]+)(?::(\d+))?|([&|!()]))")

Node = Tuple  # ("or", [..]) | ("and", [..]) | ("not", node) | ("tok", name, count)


@lru_cache(maxsize=None)
def parse(condition: str) -> Node:
    tokens: List[Tuple[str, Union[str, Tuple[str, int]]]] = []
    pos = 0
    condition = condition.strip()
    while pos < len(condition):
        match = _TOKEN.match(condition, pos)
        if not match:
            raise ValueError(f"bad condition at {pos}: {condition!r}")
        pos = match.end()
        if match.group(1):
            tokens.append(("tok", (match.group(1), int(match.group(2) or 1))))
        else:
            tokens.append(("op", match.group(3)))
        while pos < len(condition) and condition[pos].isspace():
            pos += 1
    index = 0

    def peek() -> Optional[str]:
        return tokens[index][1] if index < len(tokens) and tokens[index][0] == "op" else None

    def parse_or() -> Node:
        nonlocal index
        parts = [parse_and()]
        while peek() == "|":
            index += 1
            parts.append(parse_and())
        return parts[0] if len(parts) == 1 else ("or", parts)

    def parse_and() -> Node:
        nonlocal index
        parts = [parse_not()]
        while peek() == "&":
            index += 1
            parts.append(parse_not())
        return parts[0] if len(parts) == 1 else ("and", parts)

    def parse_not() -> Node:
        nonlocal index
        if peek() == "!":
            index += 1
            return ("not", parse_not())
        if peek() == "(":
            index += 1
            inner = parse_or()
            if peek() != ")":
                raise ValueError(f"missing ')' in {condition!r}")
            index += 1
            return inner
        if index >= len(tokens) or tokens[index][0] != "tok":
            raise ValueError(f"expected a name in {condition!r}")
        name, count = tokens[index][1]
        index += 1
        return ("tok", name, count)

    tree = parse_or()
    if index != len(tokens):
        raise ValueError(f"trailing tokens in {condition!r}")
    return tree


def condition_keys(preset: str) -> Tuple[str, ...]:
    return tuple(f"condition-{p}" for p in PRESETS[1:PRESETS.index(preset) + 1])


def node_tokens(tree: Node) -> Iterable[str]:
    if tree[0] == "tok":
        yield tree[1]
    elif tree[0] == "not":
        yield from node_tokens(tree[1])
    else:
        for part in tree[1]:
            yield from node_tokens(part)


def referenced_tokens(logic: Mapping[str, Mapping]) -> Set[str]:
    """Every name used by any condition of any preset."""
    tokens: Set[str] = set()
    for definition in logic.values():
        for key, value in definition.items():
            if key.startswith("condition-") and value:
                tokens.update(node_tokens(parse(str(value))))
    return tokens


Expr = Union[bool, str]  # constant, or Python source over `c` (the player's prog_items counter)


def _and(parts: List[Expr]) -> Expr:
    parts = [p for p in parts if p is not True]
    if any(p is False for p in parts):
        return False
    if not parts:
        return True
    return parts[0] if len(parts) == 1 else "(" + " and ".join(parts) + ")"


def _or(parts: List[Expr]) -> Expr:
    parts = [p for p in parts if p is not False]
    if any(p is True for p in parts):
        return True
    if not parts:
        return False
    return parts[0] if len(parts) == 1 else "(" + " or ".join(parts) + ")"


class LogicCompiler:
    """Turns LASR logic into rule expressions for one player's options.

    item_names maps LASR item names (and follower contents) to the AP item names counted in state.
    Nodes in `region_nodes` are AP regions of the same name; rules reach them through `_R[name]`.
    """

    def __init__(self, logic: Mapping[str, Mapping], preset: str, settings: Mapping[str, bool],
                 item_names: Mapping[str, str]):
        self.logic = logic
        self.preset = preset
        self.settings = settings
        self.item_names = item_names
        self.extra_keys = condition_keys(preset)
        self.region_nodes = self._region_nodes()
        self._inline: Dict[str, Expr] = {}

    def _references(self, name: str) -> Set[str]:
        """Logic nodes named by a node's conditions (any preset)."""
        found = set()
        for key, value in self.logic[name].items():
            if key.startswith("condition-") and value:
                for token in node_tokens(parse(str(value))):
                    base = token.split("[")[0]
                    if base in self.logic and self.logic[base]["type"] != "item":
                        found.add(base)
        return found

    def _region_nodes(self) -> Set[str]:
        """Nodes that become AP regions: regions, rooms, and nodes on a reference cycle; the rest are inlined."""
        nodes = {k for k, v in self.logic.items() if v["type"] in ("region", "room")}
        graph = {k: self._references(k) - nodes for k, v in self.logic.items()
                 if v["type"] not in ("item", "region", "room", "follower")}
        # Tarjan's strongly connected components
        index: Dict[str, int] = {}
        low: Dict[str, int] = {}
        stack: List[str] = []

        def visit(node: str) -> None:
            index[node] = low[node] = len(index)
            stack.append(node)
            for target in graph[node]:
                if target not in index:
                    visit(target)
                    low[node] = min(low[node], low[target])
                elif target in stack:
                    low[node] = min(low[node], index[target])
            if low[node] == index[node]:
                component = []
                while True:
                    member = stack.pop()
                    component.append(member)
                    if member == node:
                        break
                if len(component) > 1 or node in graph[node]:
                    nodes.update(component)

        for node in graph:
            if node not in index:
                visit(node)
        return nodes

    def _node(self, name: str) -> Expr:
        if name in self.region_nodes:
            return f"_R[{name!r}].can_reach(state)"
        if name not in self._inline:
            self._inline[name] = self.node_expr(name)
        return self._inline[name]

    def _count(self, name: str, count: int) -> Expr:
        return f"c[{name!r}] >= {count}"

    def token(self, name: str, count: int) -> Expr:
        if name in self.settings:
            return bool(self.settings[name]) and count <= 1
        if "[" in name:
            base, param = name[:-1].split("[")
            node = self.logic.get(base)
            if node is None or node["type"] != "enemy":
                return False
            if count > 1:
                return False
            variant = self.compile(node[f"condition-{param}"]) if f"condition-{param}" in node else False
            return _or([self._node(base), variant])
        if name in self.item_names:
            return self._count(self.item_names[name], count)
        if name in self.logic and self.logic[name]["type"] != "item":
            # nodes are added to LASR's access once
            return self._node(name) if count <= 1 else False
        return False

    def compile(self, condition: Optional[str]) -> Expr:
        if condition is None:
            return True
        return self._compile(parse(str(condition)))

    def _compile(self, tree: Node) -> Expr:
        kind = tree[0]
        if kind == "tok":
            return self.token(tree[1], tree[2])
        if kind == "not":
            inner = self._compile(tree[1])
            if isinstance(inner, bool):
                return not inner
            raise ValueError(f"negated non-constant condition: {tree!r}")
        parts = [self._compile(part) for part in tree[1]]
        return _and(parts) if kind == "and" else _or(parts)

    def node_expr(self, name: str) -> Expr:
        """Condition of a node or location, without the region requirement of locations and followers."""
        node = self.logic[name]
        basic = self.compile(node.get("condition-basic"))
        if node["type"] == "enemy":
            return basic
        return _or([basic] + [self.compile(node[key]) for key in self.extra_keys if key in node])

    def dependencies(self, roots: Iterable[str]) -> List[str]:
        """Logic nodes (non-item) reachable through references from roots, in logic.yml order."""
        stack = list(roots)
        seen: Set[str] = {name for name in stack if self.logic[name]["type"] != "item"}
        while stack:
            name = stack.pop()
            node = self.logic[name]
            for key, value in node.items():
                if not key.startswith("condition-") or not value:
                    continue
                if node["type"] == "enemy" and key not in ("condition-basic", "condition-pit", "condition-heavy"):
                    continue
                if node["type"] != "enemy" and key != "condition-basic" and key not in self.extra_keys:
                    continue
                for token in node_tokens(parse(str(value))):
                    base = token.split("[")[0]
                    if token in self.settings or base in seen:
                        continue
                    target = self.logic.get(base)
                    if target is not None and target["type"] != "item":
                        seen.add(base)
                        stack.append(base)
            region = node.get("region")
            if region and region not in seen:
                seen.add(region)
                stack.append(region)
        return [name for name in self.logic if name in seen]


_REGION_REF = re.compile(r"_R\['([^']+)'\]")


def referenced_regions(expr: Expr) -> Set[str]:
    return set(_REGION_REF.findall(expr)) if isinstance(expr, str) else set()


def make_rule(expr: Expr, player: int, regions: Mapping[str, object]) -> Callable:
    """Builds a rule function; regions maps the node names used by the expression to their AP regions."""
    if expr is True:
        return lambda state: True
    if expr is False:
        return lambda state: False
    namespace: Dict = {"_R": regions}
    exec(f"def rule(state):\n    c = state.prog_items[{player}]\n    return {expr}\n", namespace)
    return namespace["rule"]
