"""Every hint source's own message in the mod, the same in every patch (ap_hints.py; DESIGN.md, In-game hints)."""
from RandomizerCore.Tools import event_tools

from ap_hints import (BEAK_ITEM_TYPE, BOOK_FLOW, BOOKS, DUNGEON_FLOW, DUNGEON_OWLS, FIELD_FLOW, FIELD_OWLS, OWN_ENTRIES,
                      SOURCES, label)


def add(files) -> None:
    """Every source's own message, the same in every patch."""
    _field_owls(files)
    _dungeon_owls(files)
    _books(files)
    _text(files, "Hint.msbt", [source for source in SOURCES if source not in BOOKS])
    _text(files, "System.msbt", list(BOOKS))


def _message_event(chart, message_name: str):
    found = [e for e in chart.events if e.data.__class__.__name__ == "ActionEvent"
             and e.data.params is not None and e.data.params.data.get("message") == message_name]
    assert len(found) == 1, (message_name, [e.name for e in found])
    return found[0]


def _in_place(chart, old: str, new: str) -> None:
    """Shows the new message instead of the old one."""
    _message_event(chart, old).data.params.data["message"] = new


def _field_owls(files) -> None:
    flow = files.readFile(FIELD_FLOW)
    for source, old in FIELD_OWLS.items():
        _in_place(flow.flowchart, f"Hint:{old}", f"Hint:{label(source)}")
    files.writeFile(FIELD_FLOW, flow)


def _dungeon_owls(files) -> None:
    flow = files.readFile(DUNGEON_FLOW)
    chart = flow.flowchart
    for source, old in DUNGEON_OWLS.items():
        new = f"Hint:{label(source)}"
        entry, room, shared = OWN_ENTRIES.get(source, (None, None, None))
        entry_point = event_tools.findEntryPoint(chart, entry) if entry else None
        if entry_point is not None and getattr(entry_point.main_event.v.data, "entry_point_name", None):
            # LASR's Owl Gifts: the entry runs the shared events as a subflow, then the gift
            gift = entry_point.main_event.v.data.nxt.v
            event_tools.insertEventAfter(chart, entry, _owl_events(chart, new, gift.name if gift else None))
        elif entry and entry_point is None:
            event_tools.addEntryPoint(chart, entry)
            event_tools.insertEventAfter(chart, entry, _owl_events(chart, new, None))
            _own_entry(files, room, shared, entry)
        else:
            _in_place(chart, f"Hint:{old}", new)
    files.writeFile(DUNGEON_FLOW, flow)


def _owl_events(chart, message_name: str, after) -> str:
    """A dungeon owl statue's events, as the game's: without the beak NoBeak, else the beak's start, the message and
    its end; then `after`."""
    beak_end = event_tools.createSubFlowEvent(chart, "", "Beak_ed", {}, after)
    branches = [
        event_tools.createSubFlowEvent(chart, "", "Beak_st", {}),
        event_tools.createActionEvent(chart, "DungeonOwlStatue", "Examine", {"message": message_name}),
        event_tools.createActionEvent(chart, "Link", "PlayAnimation", {"name": "wait", "blendTime": 0.1}),
    ]
    fork, _ = event_tools.createForkEvent(chart, None, branches, beak_end)
    no_beak = event_tools.createSubFlowEvent(chart, "", "examine_NoBeak", {})
    return event_tools.createSwitchEvent(chart, "Inventory", "HasItem", {"itemType": BEAK_ITEM_TYPE, "count": 1},
                                         {0: no_beak, 1: fork})


def _own_entry(files, room: str, shared: str, entry: str) -> None:
    data = files.readFile(f"{room}.leb")
    statues = [a for a in data.actors if a.parameters and a.parameters[0] in (shared.encode(), entry.encode())]
    assert len(statues) == 1, (room, len(statues))
    statues[0].parameters[0] = entry.encode()
    files.writeFile(f"{room}.leb", data)


def _books(files) -> None:
    flow = files.readFile(BOOK_FLOW)
    for source, old in BOOKS.items():
        _in_place(flow.flowchart, f"System:{old}", f"System:{label(source)}")
    files.writeFile(BOOK_FLOW, flow)


def _text(files, file_name: str, sources: list) -> None:
    """Each source's message: a copy of the one it replaces, text and window style."""
    copy_from = {**FIELD_OWLS, **DUNGEON_OWLS, **BOOKS}
    for relative_path, msbt in files.getAllTextFiles(file_name):
        for source in sources:
            msbt.copyEntry(copy_from[source], label(source))
        files.writeTextFile(relative_path, file_name, msbt)
