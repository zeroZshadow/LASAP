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
from .lasr import (COUNTED, DOORS, DUNGEON_DOORS, DUNGEON_POOL, DUNGEONS, ENTRANCE_POOL, HOME_DOOR, INSTRUMENTS, LOGIC, WRITTEN_LOGIC,
                   closed_dungeons, door_logic, door_node, door_region, dungeon_of, dungeon_region, entered_by_door,
                   entrance_logic, goal_logic, slot_doors)
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
# Data storage key holding the overworld doors the player has gone through (Entrances), {door: the inside door it leads
# to}: going through a door or out of its inside (the game's DOOR) finds the pair. Formatted with the team and the slot.
ENTRANCES_KEY = "las_entrances_{team}_{player}"
# Data storage key holding the level the game shows (LASR level names: Field, Lv01TailCave, ...), for trackers.
# Formatted with the team and the slot.
LEVEL_KEY = "las_level_{team}_{player}"

def door_pool(entrances: Optional[Mapping[str, str]]) -> List[str]:
    """The slot's door pool, from its slot data's `entrances`: with a dungeon's door among them (Shuffled dungeons
    everything) DUNGEON_POOL, else ENTRANCE_POOL."""
    return DUNGEON_POOL if set(entrances or ()) & set(DUNGEON_DOORS.values()) else ENTRANCE_POOL


def found_entrance(locator: str, to: str, pool: Iterable[str] = ENTRANCE_POOL) -> Optional[Tuple[str, str]]:
    """The shuffled door of `pool` (door_pool) and the inside door it leads to that a level jump finds (the game's
    DOOR <locator> <to>): a jump to the spot inside a door that leads to another inside door's spot went through that
    door, and a jump to an overworld door's spot that leads to another overworld door's spot came out of the inside
    door the first leads to. None for other jumps."""
    outside = set(pool)
    inside_of = {DOORS["pairs"][door]: door for door in outside}
    if locator in inside_of and to in inside_of:
        return inside_of[locator], to
    if locator in outside and to in outside:
        return to, DOORS["pairs"][locator]
    return None


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
                 instruments_required: int = 8, seashells_required: Optional[int] = None,
                 overworld_entrances: Optional[Mapping[str, str]] = None, start: Optional[str] = None):
        """`vanilla`: LASR locations that keep a progression item and are no AP locations; `shuffled`: the slot's
        LASR locations; `dungeon_entrances`: the slot data's, None for none shuffled; `optional`: the shuffled
        locations reached only after the goal, which opens with `instruments_required` instruments or, on a
        seashell hunt (`seashells_required` not None), with that many secret seashells; `overworld_entrances`: the slot
        data's `entrances`, the overworld doors that lead elsewhere; `start`: the slot data's, the door the house's door
        is swapped with (Random start), None for Tarin's own. The rules are the world's (lasr.entrance_logic,
        whose ways out of the dungeons lead as the doors do): each dungeon door a region, and the region it leads into
        entered through it once the player knows where it leads (sweep). With overworld entrances, each shuffled door
        and inside door is a region too (lasr.door_logic's open_doors), entered through the door it is paired with
        once the player knows the pair (sweep), the house's from the start: the player leaves it first. The doors
        are those `overworld_entrances` names: a dungeon's door among them (Shuffled dungeons everything) is such a
        door too, and its dungeon has no door region of its own."""
        entrances = dict(dungeon_entrances) if dungeon_entrances else {d: d for d in DUNGEONS}
        self.pairs = dict(overworld_entrances or {})
        self.home = start or HOME_DOOR  # the door the house's door is paired with
        pool = [door for door in DUNGEON_POOL if door in self.pairs]
        closed = closed_dungeons(pool)
        if self.pairs:
            doors = door_logic(WRITTEN_LOGIC, slot_doors(None, entrances), pool)
        else:
            doors = entrance_logic(None, entrances, start)
        logic = goal_logic(instruments_required, doors, seashells_required)
        compiler = LogicCompiler(logic, preset, tokens, LASR_TO_ITEM, COUNTED)
        vanilla, self.shuffled, optional = list(vanilla), list(shuffled), set(optional)
        # the shuffled doors' regions too: what leads into each through its partner is no rule (sweep)
        sides = [side for door in self.pairs for side in (door, DOORS["pairs"][door])]
        roots = (self.shuffled + vanilla + (["kill-nightmare"] if optional else []) + [door_node(d) for d in closed]
                 + [door_region(side) for side in sides])
        needed = set(compiler.dependencies(roots)) & compiler.region_nodes
        self.regions: Dict[str, _Region] = {name: _Region() for name in needed}

        def rule(name: str):
            return make_rule(compiler.node_expr(name), PLAYER, self.regions)

        def never(state) -> bool:
            return False

        # (its name, region, its rule, the door that leads into it or None): a region a door leads into is entered
        # through that door's region and has no other way in
        led = {dungeon_region(inside): door for door, inside in entrances.items() if door in closed}
        entered = entered_by_door(pool)
        self.entrances = [(name, self.regions[name], never if name in entered else rule(name), led.get(name))
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
        # each shuffled dungeon location's door: the one that leads into its dungeon, by the location's name
        door_into = {inside: door for door, inside in entrances.items()}
        self.location_doors = {loc: self.regions[door_node(door_into[dungeon_of(loc)])]
                               for loc in self.shuffled if dungeon_of(loc) and door_into[dungeon_of(loc)] in closed}

    def lead(self, overworld_entrances: Mapping[str, str]) -> "LogicTracker":
        """This tracker with the shuffled overworld doors leading as these say instead. The rules do not depend on
        where the doors lead, only the sweep does, so one tracker serves every placement of the same doors."""
        if set(overworld_entrances) != set(self.pairs):
            raise ValueError("lead takes the doors the tracker was made with")
        self.pairs = dict(overworld_entrances)
        return self

    def in_logic(self, item_names: Iterable[str], found_doors: Optional[Iterable[str]] = None,
                 found_entrances: Optional[Iterable[str]] = None) -> Set[str]:
        """The shuffled locations reachable with these items (AP item names, starting items included) through the
        doors found (sweep)."""
        return self.sweep(item_names, found_doors, found_entrances)[0]

    def sweep(self, item_names: Iterable[str], found_doors: Optional[Iterable[str]] = None,
              found_entrances: Optional[Iterable[str]] = None) -> Tuple[Set[str], Set[str]]:
        """The shuffled locations reachable with these items, and the shuffled dungeon locations whose dungeon's door
        is reachable with them. A dungeon is entered only through a door whose dungeon the player knows (known_doors of
        `found_doors`, the doors found; None for every door), as the map names a dungeon only once it is entered: until
        then nothing inside it is in logic, nor what its inside reaches (the Rapids' south island chest through the
        Face Shrine, the top of Turtle Rock). The map tracker shows a dungeon's icon by its door while its name is
        hidden. A shuffled overworld door and the inside door it leads to lead into each other only once the player
        has gone through them (`found_entrances`, the overworld doors found; None for every door)."""
        known = known_doors(found_doors)
        found = set(self.pairs) if found_entrances is None else (set(found_entrances) | {self.home}) & set(self.pairs)
        through: Dict[str, List[_Region]] = {}  # a door's region: the regions of the doors found to lead into it
        for door in found:
            outside, inside = door_region(door), door_region(self.pairs[door])
            if outside in self.regions and inside in self.regions:
                through.setdefault(inside, []).append(self.regions[outside])
                through.setdefault(outside, []).append(self.regions[inside])
        counts = Counter(name for name in item_names if name in ITEM_TABLE and is_progression(name))
        state = _State(counts)
        for region in self.regions.values():
            region.reached = False
        pending = list(self.events)
        changed = True
        while changed:
            changed = False
            for name, region, rule, door in self.entrances:
                if not region.reached and ((door in known and self.regions[door_node(door)].reached) or rule(state)
                                           or any(other.reached for other in through.get(name, ()))):
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
