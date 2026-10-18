"""The Wind Fish's egg opens with fewer instruments (the Instruments Required option).

The egg's song (`WindFishsEgg.bfevfl`, entry `orchestra`) asks `Inventory.HasAllInstruments` at four switches: whether
the instruments play, which song plays, how the scene fades and whether the egg opens. Each switch becomes a chain of
`Inventory.HasItem` checks, one per instrument, that takes the switch's case 1 once the required number is held and
its case 0 once that number can no longer be reached.

The sign at the foot of the egg's stairs (`Place:SBMountTamaranch`, only used there) names the number, so a random
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


def set_instrument_count(files, count: int) -> None:
    """Opens the egg with `count` instruments and says so on the sign; with all of them the event stays as it is."""
    _sign(files, count)
    if count >= len(INSTRUMENT_ITEM_TYPES):
        return
    flow = files.readFile(EVENT_FILE)
    rewrite(flow.flowchart, count)
    files.writeFile(EVENT_FILE, flow)


def sign_text(count: int) -> str:
    total = len(INSTRUMENT_ITEM_TYPES)
    if count >= total:
        needed = f"all {total} instruments"
    elif count == 0:
        needed = "no instruments"
    else:
        needed = f"{count} instrument{'s' if count > 1 else ''}"
    return f"The Wind Fish's Egg opens\nwith {needed}."


def _sign(files, count: int) -> None:
    """Adds the count below the sign's place name, in every language (in English)."""
    for relative_path, msbt in files.getAllTextFiles(SIGN_FILE):
        entry = msbt.getEntry(SIGN_KEY)
        text = entry.message.text
        if text.endswith(SIGN_WAIT):
            text = text[:-len(SIGN_WAIT)]
        entry.message.text = f"{text}\n{sign_text(count)}{SIGN_WAIT}"
        files.writeTextFile(relative_path, SIGN_FILE, msbt)


def _query(event) -> Optional[str]:
    query = getattr(event.data, "actor_query", None)
    return None if query is None else query.v.v


def rewrite(chart, count: int) -> None:
    """Replaces each ALL_QUERY switch in the flowchart by HasItem checks that need `count` instruments."""
    switches = [event for event in chart.events if _query(event) == ALL_QUERY]
    assert len(switches) == SWITCHES, [event.name for event in switches]
    actor = event_tools.findActor(chart, ACTOR)
    event_tools.addActorQuery(actor, ITEM_QUERY)
    for switch in switches:
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

    _set_query(chart, switch, 0)
    if count == 0:
        # decided before any check: both cases lead on
        _set_cases(switch, {1: held, 0: held})
    else:
        node(0, 0, switch)


def _params(index: int) -> dict:
    return {"itemType": INSTRUMENT_ITEM_TYPES[index], "count": 1}


def _set_query(chart, event, index: int) -> None:
    data = event.data
    data.actor = make_rindex(event_tools.findActor(chart, ACTOR))
    data.actor_query = make_rindex(data.actor.v.find_query(ITEM_QUERY))
    data.params = Container()
    data.params.data = _params(index)


def _set_cases(event, cases: dict) -> None:
    event.data.cases = {value: make_rindex(target) for value, target in cases.items() if target is not None}
