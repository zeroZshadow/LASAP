"""Compiles LASR condition strings (logic.yml) into CollectionState rules.

Grammar (data/logic_spec.yml): names, ``&``, ``|``, ``!``, parentheses, and ``name:N`` counts. Precedence follows the
Python LASR rewrites conditions into: ``!`` > ``&`` > ``|``.

Name resolution matches LASR's shuffler (``checkAccess``/``canReachLocation``):
- items from items.yml count copies (``sword:2``);
- logic nodes (region, room, interaction, enemy, trick) hold once their condition does; regions, rooms and
  nodes on a reference cycle become AP regions (checked with region reachability), the others are inlined;
- ``kill-x[pit]`` / ``kill-x[heavy]`` hold when ``kill-x`` holds or the enemy's ``condition-pit``/``-heavy`` holds;
- settings tokens are constants;
- a counted group (``instruments:3``) holds with that many different items of the group;
anything else is undefined and never holds.
Nodes use condition-basic (or True when absent) OR the higher-preset conditions the preset enables; enemies only
ever use condition-basic, as in LASR.
"""
import pkgutil
import re
from functools import lru_cache
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Sequence, Set, Tuple, Union

import yaml



def _spec() -> Dict[str, Any]:
    """data/logic_spec.yml: the names and levels logic.yml must have. Read as package data; from the file beside this
    module when it is loaded on its own (Logic Studio)."""
    package = __name__.rpartition(".")[0]
    raw = pkgutil.get_data(package, "data/logic_spec.yml") if package else (Path(__file__).parent / "data" / "logic_spec.yml").read_bytes()
    return yaml.safe_load(raw)


SPEC = _spec()
PRESETS = tuple(SPEC["levels"])
GRAMMAR = SPEC["grammar"]
_NOT, _AND, _OR, _OPEN, _CLOSE = (GRAMMAR[k] for k in ("not", "and", "or", "open", "close"))
_TOKEN = re.compile(rf"\s*(?:({GRAMMAR['name']})(?:{re.escape(GRAMMAR['count'])}(\d+))?|"
                    rf"({'|'.join(re.escape(op) for op in (_AND, _OR, _NOT, _OPEN, _CLOSE))}))")

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
        while peek() == _OR:
            index += 1
            parts.append(parse_and())
        return parts[0] if len(parts) == 1 else ("or", parts)

    def parse_and() -> Node:
        nonlocal index
        parts = [parse_not()]
        while peek() == _AND:
            index += 1
            parts.append(parse_not())
        return parts[0] if len(parts) == 1 else ("and", parts)

    def parse_not() -> Node:
        nonlocal index
        if peek() == _NOT:
            index += 1
            return ("not", parse_not())
        if peek() == _OPEN:
            index += 1
            inner = parse_or()
            if peek() != _CLOSE:
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


# Routes. logic.yml writes how zones (regions and rooms) join under the zone a way leaves from: `connections` (two-way),
# `one-way`, and `doors` (a door of this zone: where it leads is in doors.yml, its rule applies from this side). A rule is
# plain text for Basic, or a map from level to text; `always` says it has no requirements. lower_routes gives each zone
# the condition-* keys the compiler reads, which name the zones a way comes from.
START = SPEC["start"]  # always holds; a way back into it changes nothing
ALWAYS = "always"
NEVER = "never-holds"  # no such name, so it never holds (LogicCompiler.token)
PLACES = ("region", "room")
ROUTE_KEYS = ("connections", "one-way", "doors")
FIELD_DOOR = "Field_"  # an overworld door: a door goes by its room, and the overworld's rooms are Field_<row><column>


def _route_levels(value: object, where: str) -> Dict[str, str]:
    """A rule by level. Empty is refused: `always` says there are no requirements."""
    levels = value if isinstance(value, Mapping) else {"basic": value}
    if not levels:
        raise ValueError(f"{where}: no rule, write {ALWAYS}")
    found = {}
    for level, text in levels.items():
        if text is None or text == "":
            raise ValueError(f"{where}: no rule, write {ALWAYS}")
        text = str(text)
        if text != ALWAYS and ALWAYS in node_tokens(parse(text)):
            raise ValueError(f"{where}: {ALWAYS} stands alone: {text!r}")
        found[str(level)] = text
    return found


def _route_edges(logic: Mapping[str, Mapping], doors: Mapping[str, Mapping], house_exit: Optional[str],
                 entered: Optional[Mapping[str, str]] = None,
                 through: Optional[Mapping[str, str]] = None) -> List[Tuple[str, str, Dict[str, str]]]:
    """Every way between zones as (from, to, rule by level). With house_exit, the start's doors lead to that zone.
    `entered` maps a door to the region just inside it: a door that leads to it leads into that region, which leads on
    to the door's zone; `through` maps a door to the region it leads into instead, wherever it leads (lasr.door_logic)."""
    edges = []
    written: Set[Tuple[str, str]] = set()
    door_zone: Dict[str, str] = {}
    door_rule: Dict[str, Dict[str, str]] = {}
    for name, node in logic.items():
        for key in ROUTE_KEYS:
            if key in node and node["type"] not in PLACES:
                raise ValueError(f"{name}: only a region or a room has {key}")
        for key in ("connections", "one-way"):
            for target, value in (node.get(key) or {}).items():
                where = f"{name} {key} {target}"
                if logic.get(target, {}).get("type") not in PLACES:
                    raise ValueError(f"{where}: no such zone")
                if key == "connections" and ((name, target) in written or (target, name) in written):
                    raise ValueError(f"{where}: written twice")
                if key == "connections":
                    written.add((name, target))
                rule = _route_levels(value, where)
                edges.append((name, target, rule))
                if key == "connections":
                    edges.append((target, name, rule))
        for door, value in (node.get("doors") or {}).items():
            if door in door_zone:
                raise ValueError(f"{door}: door of both {door_zone[door]} and {name}")
            door_zone[door] = name
            door_rule[door] = _route_levels(value, f"{name} door {door}")
    leads = {}
    for door, other in doors["pairs"].items():
        leads[door], leads[other] = other, door
    leads.update(doors["one-way"])
    landings = set(leads) | set(leads.values())
    for door, zone in door_zone.items():
        if door not in landings:
            raise ValueError(f"{zone} door {door}: not in doors.yml")
        if door not in leads:  # only a place to land
            continue
        landing = leads[door]
        if through and door in through:
            edges.append((zone, through[door], door_rule[door]))
            continue
        if entered and landing in entered:
            edges.append((zone, entered[landing], door_rule[door]))
            continue
        target = house_exit if house_exit and zone == START else door_zone.get(landing)
        if target and target != zone:
            edges.append((zone, target, door_rule[door]))
    for landing, region in (entered or {}).items():
        if landing in door_zone:
            edges.append((region, door_zone[landing], {"basic": ALWAYS}))
    return edges


def house_exits(logic: Mapping[str, Mapping], doors: Mapping[str, Mapping]) -> Dict[str, str]:
    """{door: its zone}: the overworld doors the start's door may lead to. Each is one of a pair in doors.yml and has no
    rule to go in at any level, so Link can always walk back into the house from where he comes out."""
    paired = set(doors["pairs"]) | set(doors["pairs"].values())
    found = {}
    for zone, node in logic.items():
        if node.get("type") not in PLACES:
            continue
        for door, value in (node.get("doors") or {}).items():
            if door.startswith(FIELD_DOOR) and door in paired and set(_route_levels(value, f"{zone} door {door}").values()) == {ALWAYS}:
                found[door] = zone
    return found


def lower_routes(logic: Mapping[str, Mapping], doors: Mapping[str, Mapping], house_exit: Optional[str] = None,
                 entered: Optional[Mapping[str, str]] = None,
                 through: Optional[Mapping[str, str]] = None) -> Dict[str, Dict]:
    """The logic with each zone's ways in as its condition-* keys, the routes gone. Other nodes stay as they are.
    `entered` and `through` as _route_edges reads them."""
    incoming: Dict[str, Dict[str, List[str]]] = {}
    for source, target, rule in _route_edges(logic, doors, house_exit, entered, through):
        levels = incoming.setdefault(target, {})
        for level, text in rule.items():
            if text == ALWAYS:
                part = source
            elif parse(text)[0] == "or":
                part = f"{source} & ({text})"
            else:
                part = f"{source} & {text}"
            levels.setdefault(level, []).append(part)
    lowered = {}
    for name, node in logic.items():
        if node["type"] not in PLACES:
            lowered[name] = node
            continue
        own = [key for key in node if key.startswith("condition-")]
        if own:
            raise ValueError(f"{name}: a zone's rules go on its routes, not {own[0]}")
        lowered[name] = {key: value for key, value in node.items() if key not in ROUTE_KEYS}
        if name == START:
            continue
        levels = incoming.get(name, {})
        lowered[name]["condition-basic"] = _join_ways(levels.get("basic")) or NEVER
        for level, parts in levels.items():
            if level != "basic":
                lowered[name][f"condition-{level}"] = _join_ways(parts)
    return lowered


def _join_ways(parts: Optional[List[str]]) -> str:
    if not parts:
        return ""
    return parts[0] if len(parts) == 1 else " | ".join(f"({part})" for part in parts)


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

    item_names maps LASR item names (and follower contents) to the AP item names counted in state. counted maps group
    tokens to their LASR items.
    Nodes in `region_nodes` are AP regions of the same name; rules reach them through `_R[name]`.
    """

    def __init__(self, logic: Mapping[str, Mapping], preset: str, settings: Mapping[str, bool],
                 item_names: Mapping[str, str], counted: Mapping[str, Sequence[str]] = {}):
        self.logic = logic
        self.preset = preset
        self.settings = settings
        self.item_names = item_names
        self.counted = counted
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
        if name in self.counted:
            if count <= 0:
                return True
            held = " + ".join(f"(c[{self.item_names[item]!r}] > 0)" for item in self.counted[name])
            return f"({held} >= {count})"
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

    def ways(self, name: str) -> Dict[Optional[str], Expr]:
        """A place's condition split by the region node leading each way (`source`, `source & rule`, as lower_routes
        writes them): per such region the rule from there, and under None the ways no region node leads. Ways that
        never hold are left out."""
        node = self.logic[name]
        found: Dict[Optional[str], List[Expr]] = {}
        for key in ("condition-basic",) + self.extra_keys:
            if not node.get(key):
                continue
            tree = parse(str(node[key]))
            for part in tree[1] if tree[0] == "or" else [tree]:
                lead = part if part[0] == "tok" else part[1][0] if part[0] == "and" else None
                if lead and lead[0] == "tok" and lead[2] <= 1 and lead[1] in self.region_nodes:
                    rest = [] if part is lead else part[1][1:]
                    rule = self._compile(rest[0]) if len(rest) == 1 else self._compile(("and", rest)) if rest else True
                    found.setdefault(lead[1], []).append(rule)
                else:
                    found.setdefault(None, []).append(self._compile(part))
        return {source: rule for source, parts in found.items() for rule in [_or(parts)] if rule is not False}

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


@lru_cache(maxsize=50000)
def _rule_code(expr: str, player: int) -> Any:
    """A rule's compiled code. Worlds and trackers build the same rules again with other regions; the bound keeps a
    process that generates many seeds (a fuzzer's worker) from keeping every slot's shuffled rules."""
    return compile(f"def rule(state):\n    c = state.prog_items[{player}]\n    return {expr}\n", "<rule>", "exec")


def make_rule(expr: Expr, player: int, regions: Mapping[str, object]) -> Callable:
    """Builds a rule function; regions maps the node names used by the expression to their AP regions."""
    if expr is True:
        return lambda state: True
    if expr is False:
        return lambda state: False
    namespace: Dict = {"_R": regions}
    exec(_rule_code(expr, player), namespace)
    return namespace["rule"]
