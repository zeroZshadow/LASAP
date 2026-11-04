"""The options' edits (DESIGN.md, Options; ap_options.py), each behind an option flag the module answers
(AP::Options::Flag):

- events: `gated` puts LASR's edits behind a CheckFlag of APOpt.<option>;
- sheet conditions and NPC layouts: `option_if` names a condition per way of the option;
- rooms: the module leaves out, retypes or moves LEB actors (ap_options.cpp's kActorEdits), so their rooms stay as
  the game has them; and the sheets of some options take their values when the game makes them.
"""
from contextlib import contextmanager

import evfl
from RandomizerCore.Tools import event_tools
from RandomizerCore.Tools.oead_tools import dictToStruct

from ap_options import FLAG_PREFIX, IF_PREFIX, OPTIONS


def flag(option: str) -> str:
    assert option in OPTIONS, option
    return f"{FLAG_PREFIX}{option}"


def option_if(option: str, on: tuple, off: tuple) -> dict:
    """A flag condition that holds as `on` while the option is on, else as `off`: each (Conditions_Check type,
    argument); type 0 holds unless its argument is "false"."""
    assert option in OPTIONS, option
    return {"category": 1, "parameter": f"{IF_PREFIX}{option}|{on[0]}|{on[1]}|{off[0]}|{off[1]}"}


NEVER = (0, "false")


def _links(event):
    """An event's links the gate looks at: its next event, its switch cases, its fork branches."""
    data = event.data
    nxt = getattr(data, "nxt", None)
    return (nxt.v if nxt is not None else None,
            {case: link.v for case, link in (getattr(data, "cases", None) or {}).items()},
            [branch.v for branch in getattr(data, "forks", None) or []])


@contextmanager
def gated(flowchart, option: str):
    """The edits made inside take effect while the option is on: an event or entry point that existed before and now
    leads elsewhere leads on through a CheckFlag of the option's flag, to its new next event when on and to its
    former one when off; a switch case likewise; a removed fork branch ends at once when on. Events and entry points
    made inside stay as made: only the gated ones lead to them."""
    events = {id(e): (e, _links(e)) for e in flowchart.events}
    entries = {id(entry): (entry, entry.main_event.v) for entry in flowchart.entry_points}
    yield

    def gate(old, new):
        name = event_tools.createSwitchEvent(flowchart, "EventFlags", "CheckFlag", {"symbol": flag(option)},
                                             {0: old.name if old is not None else None,
                                              1: new.name if new is not None else None})
        return event_tools.findEvent(flowchart, name)

    def point(ref, target) -> None:
        ref.v = target
        ref.set_index(event_tools.invertList(flowchart.events))

    for event, (nxt, cases, forks) in events.values():
        now_nxt, now_cases, now_forks = _links(event)
        if now_nxt is not nxt:
            point(event.data.nxt, gate(nxt, now_nxt))
        for case, old in cases.items():
            if now_cases.get(case) is not old:
                event.data.cases[case] = evfl.util.make_rindex(None)
                point(event.data.cases[case], gate(old, now_cases.get(case)))
        if len(now_forks) != len(forks):
            kept = {id(branch) for branch in now_forks}
            event.data.forks = [evfl.util.make_rindex(None) for _ in forks]
            for ref, branch in zip(event.data.forks, forks):
                point(ref, branch if id(branch) in kept else gate(branch, None))
    for entry, main in entries.values():
        if entry.main_event.v is not main:
            point(entry.main_event, gate(main, entry.main_event.v))


# ---- Sheets (mod._write_file) ----

def _append_once(conditions, condition: dict) -> None:
    if not any(c["parameter"] == condition["parameter"] for c in conditions):
        conditions.append(dictToStruct(condition))


def npc_rows(rows) -> None:
    """The refills of bombs and powder hide until Link finds them (Shuffled Bombs, Powder), and Kiki leaves with the
    bridge (Completed Bridge); LASR's Fixes/Datasheets/npc.py."""
    hides = {"ItemBomb": ("shuffled_bombs", "BombsFoundFlag"), "ItemFeatherBomb": ("shuffled_bombs", "BombsFoundFlag"),
             "ItemMagicPowder": ("shuffled_powder", "GetMagicPowder"),
             "ItemFeatherMagicPowder": ("shuffled_powder", "GetMagicPowder")}
    for row in rows:
        if row["symbol"] in hides:
            option, found = hides[row["symbol"]]
            _append_once(row["layoutConditions"], {**option_if(option, (1, f"!{found}"), NEVER), "layoutID": -1})
        elif row["symbol"] == "NpcKiki":
            row["layoutConditions"][0] = dictToStruct(
                {**option_if("completed_bridge", (1, "KikiGone"), (1, "StickDrop")), "layoutID": -1})


def condition_rows(rows) -> None:
    """The shop sells bombs once Link finds them (Shuffled Bombs), else with the Conch Horn obtained."""
    for row in rows:
        if row["symbol"] == "ShopBombCondition":
            row["conditions"][0] = dictToStruct(option_if("shuffled_bombs", (1, "BombsFoundFlag"), (11, "ConchHorn")))


def crane_rows(rows) -> None:
    """The crane game's bombs once Link finds them (Shuffled Bombs), else with the Surf Harp; the red rupee that
    takes their place before; LASR's Fixes/Datasheets/crane_game.py."""
    for row in rows:
        if row["symbol"] == "Bomb":
            row["layouts"][0]["conditions"][0] = dictToStruct(
                option_if("shuffled_bombs", (1, "BombsFoundFlag"), (2, "SurfHarp")))
        elif row["symbol"] == "RupeeRed":
            row["layouts"][2]["conditions"][0] = dictToStruct(
                option_if("shuffled_bombs", (1, "!BombsFoundFlag"), (2, "!SurfHarp")))


SHEETS = {"Npc.gsheet": npc_rows, "Conditions.gsheet": condition_rows, "CranePrize.gsheet": crane_rows}


# ---- Events: LASR's methods with both ways ----

# LASR's dungeon owl statues -> the event their gift follows; ("sub", n): the subflow of shared_entries' entry n
DUNGEON_OWLS = {
    "D1-owl-statue-spinies": ("sub", 0), "D1-owl-statue-3-of-a-kind": ("sub", 1),
    "D1-owl-statue-long-hallway": "Event48", "D2-owl-statue-first-switch": "Event59",
    "D2-owl-statue-push-puzzle": "Event62", "D2-owl-statue-past-hinox": "Event65",
    "D3-owl-statue-basement-north": "Event27", "D3-owl-statue-arrow": "Event73",
    "D3-owl-statue-northwest": "Event76", "D4-owl-statue": "Event79", "D5-owl-statue-triple-stalfos": "Event82",
    "D5-owl-statue-before-slime-eel": "Event85", "D6-owl-statue-ledge": "Event88",
    "D6-owl-statue-southeast": "Event91", "D6-owl-statue-canal": "Event94", "D7-owl-statue-ball": "Event97",
    "D7-owl-statue-kirbys": "Event100", "D7-owl-statue-hookshot-chest": "Event103",
    "D8-owl-statue-above-smasher": "Event39", "D8-owl-statue-below-gibdos": "Event109",
    "D8-owl-statue-eye-statue": "Event112", "D0-owl-statue-nine-switches": ("sub", 2),
    "D0-owl-statue-first-switches": ("sub", 3), "D0-owl-statue-before-mini-boss": ("sub", 4),
}
# The dungeon statues that share an event: their rooms' entry point (LEB parameter 0) and the shared event
SHARED_OWLS = (("Lv01TailCave_04B", 0, "examine_Tail04B", "examine_TailShared"),
               (None, None, "examine_Tail04B05F", "examine_TailShared"),
               ("Lv10ClothesDungeon_06C", 9, "examine_Color06C", "examine_Color"),
               ("Lv10ClothesDungeon_07D", 4, "examine_Color07D", "examine_Color"),
               ("Lv10ClothesDungeon_05F", 4, "examine_Color05F", "examine_Color"))


def owl_changes(self) -> None:
    """The owl statues' gifts (OwlStatueRandomizer), each behind its option."""
    files = self.parent.file_manager
    flow = files.readFile("FieldOwlStatue.bfevfl")
    with gated(flow.flowchart, "owl_overworld"):
        self.makeFieldChanges(flow.flowchart)
    files.writeFile("FieldOwlStatue.bfevfl", flow)
    flow = files.readFile("DungeonOwlStatue.bfevfl")
    _dungeon_owls(self, flow.flowchart)
    files.writeFile("DungeonOwlStatue.bfevfl", flow)
    for room, index, entry, _ in SHARED_OWLS:
        if room is not None:
            room_data = files.readFile(f"{room}.leb")
            room_data.actors[index].parameters[0] = entry.encode()
            files.writeFile(f"{room}.leb", room_data)


def _dungeon_owls(self, flowchart) -> None:
    """The dungeon statues: those that share an event get an entry point each, which
    subflows to the shared one (Tail Cave's 04B and 05F, the Color Dungeon's three), in every mod; the gifts follow
    behind the option."""
    event_tools.addEntryPoint(flowchart, "examine_TailShared")
    event_tools.insertEventAfter(flowchart, "examine_TailShared", "Event22")
    subflows = []
    for _, _, entry, shared in SHARED_OWLS:
        if event_tools.findEntryPoint(flowchart, entry) is None:
            event_tools.addEntryPoint(flowchart, entry)
        subflows.append(event_tools.createSubFlowEvent(flowchart, "", shared, {}))
        event_tools.insertEventAfter(flowchart, entry, subflows[-1])
    with gated(flowchart, "owl_dungeons"):
        for location, after in DUNGEON_OWLS.items():
            after = subflows[after[1]] if isinstance(after, tuple) else after
            gift = self.parent.item_get_manager.get(flowchart, location, None, None, True)
            flag_set = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag",
                                                     {"symbol": location, "value": True}, gift)
            flag_check = event_tools.createSwitchEvent(flowchart, "EventFlags", "CheckFlag", {"symbol": location},
                                                       {0: flag_set, 1: None})
            event_tools.insertEventAfter(flowchart, after, flag_check)
        # LASR's: the item-get plays without Link backing up
        event_tools.findEvent(flowchart, "Event50").data.forks.pop(5)


def marin_song(self, flowchart) -> None:
    """FreeGiftsRandomizer.marinChanges's Song Cutscenes way, after LASR's other: the cutscene skipped, the item-get
    at once, and Link sad about it."""
    with gated(flowchart, "song_cutscenes"):
        sad_face = event_tools.createActionEvent(flowchart, "Link", "SetFacialExpression", {"expression": "sad"}, None)
        flag_set = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag",
                                                 {"symbol": "MarinsongGet", "value": True}, sad_face)
        event_tools.insertEventAfter(flowchart, "Event92", flag_set)
        self.parent.item_get_manager.get(flowchart, "marin", sad_face, "Event666", True)


def song_item(self, flowchart, location: str, got_flag: str, skip: str, after: str) -> None:
    """Manbo's and Mamu's Song Cutscenes way, after LASR's other: the item-get before the song's cutscene."""
    with gated(flowchart, "song_cutscenes"):
        flag_event = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag",
                                                   {"symbol": got_flag, "value": True}, after)
        self.parent.item_get_manager.get(flowchart, location, skip, flag_event, True)


def bridge_changes(flowchart) -> None:
    """TradeQuestRandomizer.kikiChanges's Completed Bridge part: Kiki skips building the bridge."""
    with gated(flowchart, "completed_bridge"):
        event_tools.insertEventAfter(flowchart, "Event9", "Event31")
        event_tools.insertEventAfter(flowchart, "Event10", "Event31")
        fork = event_tools.findEvent(flowchart, "Event28")
        for index in (6, 4, 3, 2, 1):
            fork.data.forks.pop(index)


def powder_guard(flowchart) -> None:
    """Shuffled Powder (SkeletalGuardEventFixes): the blue guard sells powder only once Link has found his."""
    with gated(flowchart, "shuffled_powder"):
        check = event_tools.createSwitchEvent(flowchart, "EventFlags", "CheckFlag", {"symbol": "GetMagicPowder"},
                                              {0: "Event54", 1: "Event46"})
        event_tools.setSwitchEventCase(flowchart, "Event7", 1, check)
