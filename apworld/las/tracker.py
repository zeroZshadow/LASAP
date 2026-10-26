"""Which of a slot's locations are in logic for the items it has, for the map tracker (DESIGN.md, Map tracker).

The client evaluates the world's own rules (logic.py) without a MultiWorld. The rules read item counts from
`state.prog_items` and regions through `can_reach`, so a small state and region stand-ins serve. Regions, and the
items of vanilla locations (no AP locations), are added until nothing changes, as Archipelago's sweep does. A location
is in logic only for the items the player has: the item of a locked location (a trade item, a companion) counts once
it is received, not while the location could be reached, as the player may not know where it is. An optional location
(LASWorld.optional) is in logic only once the goal is.
The slot data's "logic" entry carries what the rules depend on (LASWorld.fill_slot_data).
"""
from collections import Counter
from typing import Callable, Dict, Iterable, List, Mapping, Optional, Set, Tuple

from BaseClasses import ItemClassification

from .items import ITEM_TABLE, LASR_TO_ITEM
from .lasr import COUNTED, DUNGEONS, INSTRUMENTS, LOGIC, dungeon_logic, goal_logic
from .logic import LogicCompiler, make_rule

PLAYER = 1
# Data storage key the client sets to true once the player has read the sign at the foot of the egg's stairs, which
# names the instruments the egg needs (DESIGN.md, Goal). Formatted with the team and the slot.
SIGN_KEY = "las_sign_{team}_{player}"


def tracked_instruments(required: int, known: bool, sign_read: bool) -> int:
    """The instruments the trackers' rules take for the egg. A count left to chance (slot data "instruments_known"
    false) stays hidden until the player has read the sign: until then they take all eight."""
    return required if known or sign_read else len(INSTRUMENTS)


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
                 dungeon_entrances: Optional[Mapping[str, str]] = None, optional: Iterable[str] = (),
                 instruments_required: int = 8):
        """`vanilla`: LASR locations that keep a progression item and are no AP locations; `shuffled`: the slot's
        LASR locations; `dungeon_entrances`: the slot data's, None for none shuffled; `optional`: the shuffled
        locations reached only after the goal, which opens with `instruments_required` instruments."""
        logic = dungeon_logic(LOGIC, dict(dungeon_entrances)) if dungeon_entrances else LOGIC
        compiler = LogicCompiler(goal_logic(instruments_required, logic), preset, tokens, LASR_TO_ITEM, COUNTED)
        vanilla, self.shuffled, optional = list(vanilla), list(shuffled), set(optional)
        roots = self.shuffled + vanilla + (["kill-nightmare"] if optional else [])
        needed = set(compiler.dependencies(roots)) & compiler.region_nodes
        self.regions: Dict[str, _Region] = {name: _Region() for name in needed}

        def rule(name: str):
            return make_rule(compiler.node_expr(name), PLAYER, self.regions)

        self.entrances = [(self.regions[name], rule(name)) for name in LOGIC if name in needed]
        goal = make_rule(compiler.token("kill-nightmare", 1), PLAYER, self.regions)

        def after_goal(reach: Callable) -> Callable:
            return lambda state: goal(state) and reach(state)

        self.locations = [(loc, self.regions[LOGIC[loc]["region"]], after_goal(rule(loc)) if loc in optional else
                           rule(loc)) for loc in self.shuffled]
        # (location, region, rule, event item): what a reachable vanilla location adds
        self.events: List[Tuple[str, _Region, Callable, str]] = [
            (loc, self.regions[LOGIC[loc]["region"]], rule(loc), LASR_TO_ITEM[LOGIC[loc]["content"]])
            for loc in vanilla]

    def in_logic(self, item_names: Iterable[str]) -> Set[str]:
        """The shuffled locations reachable with these items (AP item names, starting items included)."""
        return self.sweep(item_names)[0]

    def sweep(self, item_names: Iterable[str]) -> Tuple[Set[str], Set[str]]:
        """The shuffled locations reachable with these items, and the shuffled dungeon locations whose dungeon's door
        is reachable with them: a dungeon's region is entered through the door that leads into it (lasr.dungeon_logic),
        so it stands for that door. The map tracker shows a dungeon's icon by its door while its name is hidden."""
        counts = Counter(name for name in item_names if name in ITEM_TABLE and is_progression(name))
        state = _State(counts)
        for region in self.regions.values():
            region.reached = False
        pending = list(self.events)
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
        reachable = {loc for loc, region, rule in self.locations if region.reached and rule(state)}
        doors = {loc for loc, region, _ in self.locations if region.reached and LOGIC[loc]["region"] in DUNGEONS}
        return reachable, doors
