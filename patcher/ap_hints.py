"""In-game hints (DESIGN.md, In-game hints): owl statues and library books say the patch's `hints`.

Each source shows its own message, Hint:APHint<n> for owl statues and System:APHint<n> for books (n: the source's
place in SOURCES), with the window style of the text it replaces. A hint that names a location (server mode) sets the
event flag APHint<n> after its text; ap_hints.txt tells the module which location each flag hints.

Owl statues that share their text get their own: the Tail Cave statue in room 04B (which shares 05F's entry) and the
three Color Dungeon statues get entries of their own, as LASR's Owl Gifts give them (owls.py), each with its own copy
of the statue's events. With Owl Gifts on, the gift follows the hint."""
from pathlib import Path

from RandomizerCore.Tools import event_tools

FILE_NAME = "ap_hints.txt"  # next to config.ini; AP::HINTS_PATH in the module
FLAG_PREFIX = "APHint"  # flag and message label prefix
LINE_WIDTH = 42  # characters per line
PAGE_LINES = 3  # lines per text box
PAGE_BREAK = "[1:4][0:4]"  # wait for input, then a new box
OWL_COLOR, WHITE = "[0:3 62-CF-FF-FF]", "[0:3 ED-ED-ED-FF]"  # the owl statues' own colors

FIELD_FLOW, DUNGEON_FLOW, BOOK_FLOW = "FieldOwlStatue.bfevfl", "DungeonOwlStatue.bfevfl", "Book.bfevfl"
# Overworld owl statues: their message (Hint.msbt) in FieldOwlStatue.bfevfl
FIELD_OWLS = {
    "owl-statue-below-D8": "WestTalTalStoneOwl",
    "owl-statue-pothole": "PotholeStoneOwl",
    "owl-statue-above-cave": "UkukuStoneOwl",
    "owl-statue-moblin-cave": "TalTalHeightStoneOwl",
    "owl-statue-south-bay": "MarthaStoneOwl",
    "owl-statue-desert": "YarnaStoneOwl",
    "owl-statue-maze": "FaceStoneOwl",
    "owl-statue-taltal-east": "EastTalTalStoneOwl",
    "owl-statue-rapids": "RapidsRideStoneOwl",
}
# Dungeon owl statues: their message in DungeonOwlStatue.bfevfl
DUNGEON_OWLS = {
    "D1-owl-statue-spinies": "Lv1StoneOwl1",
    "D1-owl-statue-3-of-a-kind": "Lv1StoneOwl1",
    "D1-owl-statue-long-hallway": "Lv1StoneOwl2",
    "D2-owl-statue-first-switch": "Lv2StoneOwl1",
    "D2-owl-statue-push-puzzle": "Lv2StoneOwl2",
    "D2-owl-statue-past-hinox": "Lv2StoneOwl3",
    "D3-owl-statue-basement-north": "Lv3StoneOwl1",
    "D3-owl-statue-arrow": "Lv3StoneOwl2",
    "D3-owl-statue-northwest": "Lv3StoneOwl3",
    "D4-owl-statue": "Lv4StoneOwl",
    "D5-owl-statue-triple-stalfos": "Lv5StoneOwl1",
    "D5-owl-statue-before-slime-eel": "Lv5StoneOwl2",
    "D6-owl-statue-ledge": "Lv6StoneOwl1",
    "D6-owl-statue-southeast": "Lv6StoneOwl2",
    "D6-owl-statue-canal": "Lv6StoneOwl3",
    "D7-owl-statue-ball": "Lv7StoneOwl1",
    "D7-owl-statue-kirbys": "Lv7StoneOwl2",
    "D7-owl-statue-hookshot-chest": "Lv7StoneOwl3",
    "D8-owl-statue-above-smasher": "Lv8StoneOwl1",
    "D8-owl-statue-below-gibdos": "Lv8StoneOwl2",
    "D8-owl-statue-eye-statue": "Lv8StoneOwl3",
    "D0-owl-statue-nine-switches": "ColorDungeonStoneOwl",
    "D0-owl-statue-first-switches": "ColorDungeonStoneOwl",
    "D0-owl-statue-before-mini-boss": "ColorDungeonStoneOwl",
}
# Statues whose entry point other statues share: (their own entry, their room, the shared entry). The room's statue
# is pointed at its own entry, as LASR does for Owl Gifts.
OWN_ENTRIES = {
    "D1-owl-statue-spinies": ("examine_Tail04B", "Lv01TailCave_04B", "examine_Tail04B05F"),
    "D1-owl-statue-3-of-a-kind": ("examine_Tail04B05F", None, None),
    "D0-owl-statue-nine-switches": ("examine_Color06C", "Lv10ClothesDungeon_06C", "examine_Color"),
    "D0-owl-statue-first-switches": ("examine_Color07D", "Lv10ClothesDungeon_07D", "examine_Color"),
    "D0-owl-statue-before-mini-boss": ("examine_Color05F", "Lv10ClothesDungeon_05F", "examine_Color"),
}
# Library books: their text (System.msbt) in Book.bfevfl. The map, the egg book and the Color Dungeon's book stay.
BOOKS = {
    "book-Blue4": "ReadShieldBook",
    "book-Green1": "ReadItemSelect",
    "book-Orange2": "ReadMapandMemo",
    "book-Red3": "ReadTurnAttack",
    "book-Blue2": "ReadWarpRule",
    "book-Red1": "ReadLiftBomb",
}
SOURCES = (*FIELD_OWLS, *DUNGEON_OWLS, *BOOKS)  # flag and label numbers, in this order
BEAK_ITEM_TYPE = 63  # Inventory.HasItem itemType of the dungeon's stone beak

FLAGS: list = []  # the flags of the hints with a location, added to GlobalFlags by las_ap_patch


def label(source: str) -> str:
    return f"{FLAG_PREFIX}{SOURCES.index(source)}"


def clean(text: str) -> str:
    """Names come from any game: no tags (square brackets), control characters or characters beyond the BMP."""
    text = text.replace("[", "(").replace("]", ")")
    return "".join(c if c.isprintable() and ord(c) <= 0xFFFF else "?" for c in " ".join(text.split()))


def wrap(text: str) -> list:
    """The text's pages, each a list of up to PAGE_LINES lines of up to LINE_WIDTH characters."""
    lines, line = [], ""
    for word in clean(text).split(" "):
        while len(word) > LINE_WIDTH:  # a name without spaces
            if line:
                lines.append(line)
                line = ""
            lines.append(word[:LINE_WIDTH])
            word = word[LINE_WIDTH:]
        if line and len(line) + 1 + len(word) > LINE_WIDTH:
            lines.append(line)
            line = word
        else:
            line = f"{line} {word}" if line else word
    if line:
        lines.append(line)
    return [lines[i:i + PAGE_LINES] for i in range(0, len(lines), PAGE_LINES)]


def message(text: str, owl: bool) -> str:
    """The MSBT text: pages broken by PAGE_BREAK, owl statues' text in their color, ending with a wait for input."""
    start, end = (OWL_COLOR, WHITE) if owl else ("", "")
    return PAGE_BREAK.join(start + "\n".join(page) + end for page in wrap(text)) + "[1:4]"


def prepare(hints: dict) -> None:
    """Before LASR runs: the flags it adds to GlobalFlags."""
    FLAGS[:] = [label(source) for source in SOURCES if "location" in hints.get(source, {})]


def add(files, config_dir: Path, hints: dict) -> None:
    hints = {source: hint for source, hint in hints.items() if source in SOURCES}
    flags = {source: label(source) for source, hint in hints.items() if "location" in hint}
    _field_owls(files, hints, flags)
    _dungeon_owls(files, hints, flags)
    _books(files, hints, flags)
    _text(files, "Hint.msbt", {source: hint for source, hint in hints.items() if source not in BOOKS}, owl=True)
    _text(files, "System.msbt", {source: hint for source, hint in hints.items() if source in BOOKS}, owl=False)
    lines = [f"{SOURCES.index(source)} {flag} {int(hints[source]['player'])} {int(hints[source]['location'])}"
             for source, flag in flags.items()]
    path = config_dir / FILE_NAME
    if lines:
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    else:
        path.unlink(missing_ok=True)


def _message_event(chart, message_name: str):
    found = [e for e in chart.events if e.data.__class__.__name__ == "ActionEvent"
             and e.data.params is not None and e.data.params.data.get("message") == message_name]
    assert len(found) == 1, (message_name, [e.name for e in found])
    return found[0]


def _fork_of(chart, event):
    """The fork with the event as a branch."""
    forks = [e for e in chart.events if e.data.__class__.__name__ == "ForkEvent"
             and any(f.v is event for f in e.data.forks)]
    assert len(forks) == 1, event.name
    return forks[0]


def _insert_after(chart, event, new_name: str) -> None:
    """Puts the new event (without a next one) between the event and the one after it."""
    following = event.data.nxt.v
    if following is not None:
        event_tools.insertEventAfter(chart, new_name, following.name)
    event_tools.insertEventAfter(chart, event.name, new_name)


def _set_flag(chart, flag: str, after=None) -> str:
    return event_tools.createActionEvent(chart, "EventFlags", "SetFlag", {"symbol": flag, "value": True}, after)


def _in_place(chart, source: str, old: str, new: str, flags: dict, past=()) -> None:
    """Shows the new message instead of the old one and sets the hint's flag after the message's fork, and after a
    subflow there to one of `past`."""
    event = _message_event(chart, old)
    event.data.params.data["message"] = new
    anchor = _fork_of(chart, event).data.join.v
    following = anchor.data.nxt.v
    if following is not None and getattr(following.data, "entry_point_name", None) in past:
        anchor = following
    if source in flags:
        _insert_after(chart, anchor, _set_flag(chart, flags[source]))


def _field_owls(files, hints: dict, flags: dict) -> None:
    flow = files.readFile(FIELD_FLOW)
    for source, old in FIELD_OWLS.items():
        if source in hints:
            _in_place(flow.flowchart, source, f"Hint:{old}", f"Hint:{label(source)}", flags)
    files.writeFile(FIELD_FLOW, flow)


def _dungeon_owls(files, hints: dict, flags: dict) -> None:
    flow = files.readFile(DUNGEON_FLOW)
    chart = flow.flowchart
    for source, old in DUNGEON_OWLS.items():
        if source not in hints:
            continue
        new = f"Hint:{label(source)}"
        entry, room, shared = OWN_ENTRIES.get(source, (None, None, None))
        entry_point = event_tools.findEntryPoint(chart, entry) if entry else None
        if entry_point is not None and getattr(entry_point.main_event.v.data, "entry_point_name", None):
            # LASR's Owl Gifts: the entry runs the shared events as a subflow, then the gift
            gift = entry_point.main_event.v.data.nxt.v
            event_tools.insertEventAfter(chart, entry, _owl_events(chart, new, flags.get(source),
                                                                   gift.name if gift else None))
        elif entry and entry_point is None:
            event_tools.addEntryPoint(chart, entry)
            event_tools.insertEventAfter(chart, entry, _owl_events(chart, new, flags.get(source), None))
            _own_entry(files, room, shared, entry)
        else:
            _in_place(chart, source, f"Hint:{old}", new, flags, past=("Beak_ed",))
    files.writeFile(DUNGEON_FLOW, flow)


def _owl_events(chart, message_name: str, flag, after) -> str:
    """A dungeon owl statue's events, as the game's: without the beak NoBeak, else the beak's start, the message and
    its end; then the flag and `after`."""
    if flag:
        after = _set_flag(chart, flag, after)
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


def _books(files, hints: dict, flags: dict) -> None:
    flow = files.readFile(BOOK_FLOW)
    for source, old in BOOKS.items():
        if source in hints:
            _in_place(flow.flowchart, source, f"System:{old}", f"System:{label(source)}", flags)
    files.writeFile(BOOK_FLOW, flow)


def _text(files, file_name: str, hints: dict, owl: bool) -> None:
    style_from = {**FIELD_OWLS, **DUNGEON_OWLS, **BOOKS}
    for relative_path, msbt in files.getAllTextFiles(file_name):
        for source, hint in hints.items():
            msbt.addEntry(label(source), message(hint["text"], owl))
            msbt.getEntry(label(source)).style_index = msbt.getEntry(style_from[source]).style_index
        files.writeTextFile(relative_path, file_name, msbt)
