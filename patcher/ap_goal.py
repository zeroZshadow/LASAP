"""The Wind Fish's egg opens with fewer instruments, or with secret seashells instead (the Goal, Instruments Required
and Seashells Required options).

The egg's song (`WindFishsEgg.bfevfl`, entry `orchestra`) asks `Inventory.HasAllInstruments` at four switches: whether
the instruments play, which song plays, how the scene fades and whether the egg opens. For the instruments goal each
switch becomes a chain of `Inventory.HasItem` checks, one per instrument, that takes the switch's case 1 once the
required number is held and its case 0 once that number can no longer be reached. For the seashell hunt each switch
checks SEASHELL_FLAG instead, which the module answers as set once Link holds that many seashells (config.ini
[archipelago] seashells_required); the instruments do not count.

For the seashell hunt the module also opens the egg once Link holds the seashells (ap_egg.cpp), and the egg's door in
EGG_ROOM leads to the end instead of into the egg: where the stairs after the fight with the Nightmare lead
(Lv09WindFishsEgg.bfevfl, StairsUp's RequestLevelJump), the Wind Fish's ending.

The sign at the foot of the egg's stairs (`Place:SBMountTamaranch`, only used there) names the numbers, so a random
number can be read in the game. The module sets SIGN_FLAG in the save once the sign's text shows, for the trackers."""
from typing import Dict, Optional

from evfl.container import Container
from evfl.util import make_rindex

from RandomizerCore.Tools import event_tools

EVENT_FILE = "WindFishsEgg.bfevfl"
ACTOR = "Inventory"
ALL_QUERY = "HasAllInstruments"
ITEM_QUERY = "HasItem"
SWITCHES = 4  # the switches on ALL_QUERY in the orchestra
# Inventory.HasItem's itemType of the eight instruments, Full Moon Cello to Thunder Drum
INSTRUMENT_ITEM_TYPES = tuple(range(45, 53))
SIGN_FILE = "Place.msbt"
SIGN_KEY = "SBMountTamaranch"
SIGN_WAIT = "[1:4]"  # the sign's text ends waiting for input
SIGN_FLAG = "APSignRead"  # AP::SIGN_FLAG, added to GlobalFlags by las_ap_patch
FLAG_ACTOR = "EventFlags"
FLAG_QUERY = "CheckFlag"
SEASHELL_FLAG = "APSeashellGoal"  # AP::SEASHELL_GOAL_FLAG: never stored, the module answers it
EGG_ROOM = "Field_01G"
EGG_LEVEL = "Lv09WindFishsEgg"
LOADING_ZONE = 0x190  # AreaLevelOpen: parameters level, locator (a TagPlayerStart there), ...
ENDING_LEVEL = "Ending"
ENDING_LOCATOR = "Lv09WindFishsEgg_05H"  # in Ending_13O


def set_goal(files, count: int, seashells: int = 0) -> None:
    """Opens the egg with `count` instruments, or with `seashells` secret seashells and no instruments, and says so on
    the sign; with all instruments the event stays as it is. With seashells the egg's door leads to the ending."""
    _sign(files, count, seashells)
    if count >= len(INSTRUMENT_ITEM_TYPES) and not seashells:
        return
    flow = files.readFile(EVENT_FILE)
    rewrite(flow.flowchart, count, seashells)
    files.writeFile(EVENT_FILE, flow)
    if seashells:
        room = files.readFile(f"{EGG_ROOM}.leb")
        egg_door(room).parameters[:2] = [ENDING_LEVEL.encode(), ENDING_LOCATOR.encode()]
        files.writeFile(f"{EGG_ROOM}.leb", room)


def egg_door(room):
    """The loading zone of EGG_ROOM that leads into the egg, or (patched) to the ending."""
    doors = [actor for actor in room.actors if actor.type == LOADING_ZONE and
             actor.parameters[0] in (EGG_LEVEL.encode(), ENDING_LEVEL.encode())]
    assert len(doors) == 1, doors
    return doors[0]


def sign_text(count: int, seashells: int = 0) -> str:
    total = len(INSTRUMENT_ITEM_TYPES)
    if seashells:
        needed = f"{seashells} secret seashell{'s' if seashells > 1 else ''}"
    elif count >= total:
        needed = f"all {total} instruments"
    elif count == 0:
        needed = "no instruments"
    else:
        needed = f"{count} instrument{'s' if count > 1 else ''}"
    return f"The Wind Fish's Egg opens\nwith {needed}."


def _sign(files, count: int, seashells: int = 0) -> None:
    """Adds the numbers below the sign's place name, in every language (in English)."""
    for relative_path, msbt in files.getAllTextFiles(SIGN_FILE):
        entry = msbt.getEntry(SIGN_KEY)
        text = entry.message.text
        if text.endswith(SIGN_WAIT):
            text = text[:-len(SIGN_WAIT)]
        entry.message.text = f"{text}\n{sign_text(count, seashells)}{SIGN_WAIT}"
        files.writeTextFile(relative_path, SIGN_FILE, msbt)


def _query(event) -> Optional[str]:
    query = getattr(event.data, "actor_query", None)
    return None if query is None else query.v.v


def rewrite(chart, count: int, seashells: int = 0) -> None:
    """Replaces each ALL_QUERY switch in the flowchart by HasItem checks that need `count` instruments or, with
    `seashells`, by a check of SEASHELL_FLAG alone, with the switch's cases."""
    switches = [event for event in chart.events if _query(event) == ALL_QUERY]
    assert len(switches) == SWITCHES, [event.name for event in switches]
    actor, query = (FLAG_ACTOR, FLAG_QUERY) if seashells else (ACTOR, ITEM_QUERY)
    event_tools.addActorQuery(event_tools.findActor(chart, actor), query)
    for switch in switches:
        if seashells:
            _set_query(chart, switch, FLAG_ACTOR, FLAG_QUERY, {"symbol": SEASHELL_FLAG})
        else:
            cases = {value: case.v for value, case in switch.data.cases.items()}
            _replace(chart, switch, count, cases.get(1), cases.get(0))


def _replace(chart, switch, count: int, held, missing) -> None:
    """Turns `switch` into the first check. `held` and `missing` are the events of case 1 and case 0 (None: the
    branch ends there)."""
    total = len(INSTRUMENT_ITEM_TYPES)
    made: Dict[tuple, object] = {}

    def node(index: int, have: int, event=None):
        """The check of instrument `index` with `have` instruments counted, or the case it decides; `event` is
        the switch event to reuse for it."""
        if have >= count:
            return held
        if have + total - index < count:
            return missing
        if (index, have) in made:
            return made[(index, have)]
        if event is None:
            name = event_tools.createSwitchEvent(chart, ACTOR, ITEM_QUERY, _params(index), {})
            event = event_tools.findEvent(chart, name)
        made[(index, have)] = event
        _set_cases(event, {1: node(index + 1, have + 1), 0: node(index + 1, have)})
        return event

    _set_query(chart, switch, ACTOR, ITEM_QUERY, _params(0))
    if count == 0:
        # decided before any check: both cases lead on
        _set_cases(switch, {1: held, 0: held})
    else:
        node(0, 0, switch)


def _params(index: int) -> dict:
    return {"itemType": INSTRUMENT_ITEM_TYPES[index], "count": 1}


def _set_query(chart, event, actor: str, query: str, params: dict) -> None:
    data = event.data
    data.actor = make_rindex(event_tools.findActor(chart, actor))
    data.actor_query = make_rindex(data.actor.v.find_query(query))
    data.params = Container()
    data.params.data = params


def _set_cases(event, cases: dict) -> None:
    event.data.cases = {value: make_rindex(target) for value, target in cases.items() if target is not None}
