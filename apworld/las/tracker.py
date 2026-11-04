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
from typing import Any, Callable, Dict, Iterable, List, Mapping, Optional, Set, Tuple

from BaseClasses import ItemClassification

from .items import ITEM_TABLE, LASR_TO_ITEM
from .lasr import (COUNTED, DOOR_LOGIC, DUNGEONS, ENTERED_BY_DOOR, INSTRUMENTS, LOGIC, door_node, dungeon_region,
                   goal_logic)
from .logic import LogicCompiler, make_rule
from .options import Goal, SeashellsRequired

PLAYER = 1
# Data storage key the client sets to true once the player has read the sign at the foot of the egg's stairs, which
# names the instruments the egg needs (DESIGN.md, Goal). Formatted with the team and the slot.
SIGN_KEY = "las_sign_{team}_{player}"
# Data storage key holding the dungeon doors the player has found, {door: dungeon} by the slot data's dungeon_entrances
# names: entering a dungeon (the game's DUNGEON) finds the door that leads into it. Formatted with the team and the
# slot.
DOORS_KEY = "las_doors_{team}_{player}"


def known_doors(found: Optional[Iterable[str]]) -> Set[str]:
    """The dungeon doors whose dungeon the player knows: those found (DOORS_KEY), and the last once every other is
    found, as it leads into the dungeon left. Every door for None (dungeons not shuffled)."""
    if found is None:
        return set(DUNGEONS)
    known = set(found) & set(DUNGEONS)
    return set(DUNGEONS) if len(known) >= len(DUNGEONS) - 1 else known


def tracked_instruments(required: int, known: bool, sign_read: bool) -> int:
    """The instruments the trackers' rules take for the egg. A count left to chance (slot data "instruments_known"
    false) stays hidden until the player has read the sign: until then they take all eight."""
    return required if known or sign_read else len(INSTRUMENTS)


def tracked_seashells(slot_data: Mapping[str, Any], sign_read: bool) -> Optional[int]:
    """The seashells the egg needs on a seashell hunt, from the slot data; None for the instruments goal. A count left
    to chance (slot data "seashells_known" false) stays hidden until the player has read the sign: until then they
    take every secret seashell."""
    if slot_data.get("options", {}).get("goal") != Goal.option_seashell_hunt:
        return None
    if slot_data.get("seashells_known", True) or sign_read:
        return slot_data.get("seashells_required", 0)
    return SeashellsRequired.range_end


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
                 instruments_required: int = 8, seashells_required: Optional[int] = None):
        """`vanilla`: LASR locations that keep a progression item and are no AP locations; `shuffled`: the slot's
        LASR locations; `dungeon_entrances`: the slot data's, None for none shuffled; `optional`: the shuffled
        locations reached only after the goal, which opens with `instruments_required` instruments or, on a
        seashell hunt (`seashells_required` not None), with that many secret seashells. The rules are the world's
        (lasr.door_logic): each door a region, and the region it leads into entered through it once the player knows
        where it leads (sweep)."""
        entrances = dict(dungeon_entrances) if dungeon_entrances else {d: d for d in DUNGEONS}
        logic = goal_logic(instruments_required, DOOR_LOGIC, seashells_required)
        compiler = LogicCompiler(logic, preset, tokens, LASR_TO_ITEM, COUNTED)
        vanilla, self.shuffled, optional = list(vanilla), list(shuffled), set(optional)
        roots = self.shuffled + vanilla + (["kill-nightmare"] if optional else []) + [door_node(d) for d in DUNGEONS]
        needed = set(compiler.dependencies(roots)) & compiler.region_nodes
        self.regions: Dict[str, _Region] = {name: _Region() for name in needed}

        def rule(name: str):
            return make_rule(compiler.node_expr(name), PLAYER, self.regions)

        def never(state) -> bool:
            return False

        # (region, its rule, the door that leads into it or None): a region a door leads into is entered through that
        # door's region; all but the Face Shrine's entrance room have no other way in
        led = {dungeon_region(inside): door for door, inside in entrances.items()}
        self.entrances = [(self.regions[name], never if name in ENTERED_BY_DOOR else rule(name), led.get(name))
                          for name in logic if name in needed]
        goal = make_rule(compiler.token("kill-nightmare", 1), PLAYER, self.regions)

        def after_goal(reach: Callable) -> Callable:
            return lambda state: goal(state) and reach(state)

        self.locations = [(loc, self.regions[LOGIC[loc]["region"]], after_goal(rule(loc)) if loc in optional else
                           rule(loc)) for loc in self.shuffled]
        # (location, region, rule, event item): what a reachable vanilla location adds
        self.events: List[Tuple[str, _Region, Callable, str]] = [
            (loc, self.regions[LOGIC[loc]["region"]], rule(loc), LASR_TO_ITEM[LOGIC[loc]["content"]])
            for loc in vanilla]
        # each shuffled dungeon location's door: the one that leads into its dungeon
        door_into = {inside: door for door, inside in entrances.items()}
        self.location_doors = {loc: self.regions[door_node(door_into[LOGIC[loc]["region"]])]
                               for loc in self.shuffled if LOGIC[loc]["region"] in DUNGEONS}

    def in_logic(self, item_names: Iterable[str], found_doors: Optional[Iterable[str]] = None) -> Set[str]:
        """The shuffled locations reachable with these items (AP item names, starting items included) through the
        doors found (sweep)."""
        return self.sweep(item_names, found_doors)[0]

    def sweep(self, item_names: Iterable[str],
              found_doors: Optional[Iterable[str]] = None) -> Tuple[Set[str], Set[str]]:
        """The shuffled locations reachable with these items, and the shuffled dungeon locations whose dungeon's door
        is reachable with them. A dungeon is entered only through a door whose dungeon the player knows (known_doors of
        `found_doors`, the doors found; None for every door), as the map names a dungeon only once it is entered: until
        then nothing inside it is in logic, nor what its inside reaches (the Rapids' south island chest through the
        Face Shrine, the top of Turtle Rock). The map tracker shows a dungeon's icon by its door while its name is
        hidden."""
        known = known_doors(found_doors)
        counts = Counter(name for name in item_names if name in ITEM_TABLE and is_progression(name))
        state = _State(counts)
        for region in self.regions.values():
            region.reached = False
        pending = list(self.events)
        changed = True
        while changed:
            changed = False
            for region, rule, door in self.entrances:
                if not region.reached and ((door in known and self.regions[door_node(door)].reached) or rule(state)):
                    region.reached = changed = True
            for event in list(pending):
                _, region, rule, item = event
                if region.reached and rule(state):
                    counts[item] += 1
                    pending.remove(event)
                    changed = True
        reachable = {loc for loc, region, rule in self.locations if region.reached and rule(state)}
        doors = {loc for loc, door in self.location_doors.items() if door.reached}
        return reachable, doors
