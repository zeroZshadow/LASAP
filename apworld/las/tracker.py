"""Which of a slot's locations are in logic for the items it has, for the map tracker (DESIGN.md, Map tracker).

The client evaluates the world's own rules (logic.py) without a MultiWorld. The rules read item counts from
`state.prog_items` and regions through `can_reach`, so a small state and region stand-ins serve. Regions, and the
events of vanilla locations and followers, are added until nothing changes, as Archipelago's sweep does.
The slot data's "logic" entry carries what the rules depend on (LASWorld.fill_slot_data).
"""
from collections import Counter
from typing import Callable, Dict, Iterable, List, Mapping, Set, Tuple

from BaseClasses import ItemClassification

from .items import ITEM_TABLE, LASR_TO_ITEM
from .lasr import LOGIC
from .logic import LogicCompiler, make_rule

PLAYER = 1


class _State:
    def __init__(self, counts: Counter):
        self.prog_items = {PLAYER: counts}


class _Region:
    def __init__(self) -> None:
        self.reached = False

    def can_reach(self, state) -> bool:
        return self.reached


def is_progression(item_name: str) -> bool:
    return bool(ITEM_TABLE[item_name].classification & ItemClassification.progression)


class LogicTracker:
    def __init__(self, preset: str, tokens: Mapping[str, bool], vanilla: Iterable[str], shuffled: Iterable[str],
                 locked: Iterable[str] = ()):
        """`vanilla`: LASR locations that keep a progression item and are no AP locations; `shuffled`: the slot's
        LASR locations; `locked`: those of them that hold their own progression item, locked."""
        compiler = LogicCompiler(LOGIC, preset, tokens, LASR_TO_ITEM)
        vanilla, self.shuffled = list(vanilla) + list(locked), list(shuffled)
        needed = set(compiler.dependencies(self.shuffled + vanilla)) & compiler.region_nodes
        self.regions: Dict[str, _Region] = {name: _Region() for name in needed}

        def rule(name: str):
            return make_rule(compiler.node_expr(name), PLAYER, self.regions)

        self.entrances = [(self.regions[name], rule(name)) for name in LOGIC if name in needed]
        self.locations = [(loc, self.regions[LOGIC[loc]["region"]], rule(loc)) for loc in self.shuffled]
        # (location, region, rule, event item): what a reachable vanilla or locked location, or follower, adds
        self.events: List[Tuple[str, _Region, Callable, str]] = [
            (loc, self.regions[LOGIC[loc]["region"]], rule(loc), LASR_TO_ITEM[LOGIC[loc]["content"]])
            for loc in vanilla]

    def in_logic(self, item_names: Iterable[str], checked: Iterable[str] = ()) -> Set[str]:
        """The shuffled locations reachable with these items (AP item names, starting items included). A locked
        location counts its item once reachable until it is `checked` (LASR names); then the received copy counts."""
        counts = Counter(name for name in item_names if name in ITEM_TABLE and is_progression(name))
        state = _State(counts)
        for region in self.regions.values():
            region.reached = False
        checked = set(checked)
        pending = [event for event in self.events if event[0] not in checked]
        changed = True
        while changed:
            changed = False
            for region, rule in self.entrances:
                if not region.reached and rule(state):
                    region.reached = changed = True
            for event in list(pending):
                _, region, rule, item = event
                if region.reached and rule(state):
                    counts[item] += 1
                    pending.remove(event)
                    changed = True
        return {loc for loc, region, rule in self.locations if region.reached and rule(state)}
