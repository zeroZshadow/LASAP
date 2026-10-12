"""The ghost in Tarin's house (the Ammo Ghost option): shows up once Link has bombs or the bow, and tops his bombs and
arrows (of those he has) up to 15 each time he is spoken to.

The ghost's Npc entry (NpcGhost) is only used by the follower ghost of the vanilla ghost quest, which LASR skips
(the ghost's reward is at his grave), so the entry is rewritten for the placed ghost."""
import copy

from RandomizerCore.Tools import event_tools
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

ROOM = "MarinTarinHouse_01A.leb"
NPC = "NpcGhost"
ACTOR_TYPE = 0x179  # NpcGhost in the game's actor table
ACTOR_KEY = 0x6D0040005CEDEAC0  # unused in the room; the room's actors share the low bits
POSITION = (5.5, 0.0, 7.0)  # in front of the beds, clear of the pots, Tarin and the way in
LAYOUT = 1
ENTRY = "APAmmoGhost"
EVENT_FILE = "Ghost.bfevfl"
EVENT_ACTOR = "Ghost"
TEXT_FILE = "Scenario.msbt"
TEXT_KEY = "APAmmoGhost"
TEXT = "Boo! Running low? Take these,\nthey're no use to a ghost!"
TEXT_STYLE_FROM = "Lv5GhostHouse1"  # one of the ghost's own lines
# What he gives, and the flag that says Link has the item it needs: bombs once found (LASR's flag for them), arrows
# once the bow is found. Every item added sets its Items.gsheet getting flag; the bow gets BOW_FLAG.
BOW_FLAG = "APBowFound"
GIFTS = (("Bomb", "BombsFoundFlag"), ("Arrow", BOW_FLAG))
GIFT_COUNT = 15  # what he tops up to
REFILL_INDEX = 1000  # must match AP::AMMO_REFILL_INDEX: the module tops up to the count instead of adding it
TALK_FROM = "NpcTarin"  # whose talk settings (A prompt range) the ghost gets
TALK_CONDITION = 2  # event trigger on talking


def add(files) -> None:
    _bow_flag(files)
    _npc_entry(files)
    _room_actor(files)
    _event(files)
    _text(files)


def _bow_flag(files) -> None:
    sheet = files.readFile("Items.gsheet")
    for row in sheet["values"]:
        if row["symbol"] == "Bow":
            row["gettingFlag"] = BOW_FLAG
    files.writeFile("Items.gsheet", sheet)


def _npc_entry(files) -> None:
    sheet = files.readFile("Npc.gsheet")
    # Parsed before replacing: changing the sheet invalidates references to its rows.
    rows = [parseStruct(row) for row in sheet["values"]]
    talk = next(row for row in rows if row["symbol"] == TALK_FROM)
    for i, row in enumerate(rows):
        if row["symbol"] != NPC:
            continue
        row["doAction"] = copy.deepcopy(talk["doAction"])
        row["eventTriggers"] = [{"condition": TALK_CONDITION, "additionalConditions": [], "entryPoint": ENTRY}]
        # Shown once Link has bombs or the bow; with no condition met he is not placed.
        row["layoutConditions"] = [{"category": 1, "parameter": flag, "layoutID": LAYOUT} for _, flag in GIFTS]
        sheet["values"][i] = dictToStruct(row)
    files.writeFile("Npc.gsheet", sheet)


def _room_actor(files) -> None:
    room = files.readFile(ROOM)
    template = next(a for a in room.actors if a.name.startswith(b"NpcTarin"))
    ghost = copy.deepcopy(template)
    ghost.key = ACTOR_KEY
    ghost.name = f"{NPC}-{ACTOR_KEY:016X}".encode()
    ghost.type = ACTOR_TYPE
    ghost.posX, ghost.posY, ghost.posZ = POSITION
    ghost.rotY = 0.0
    ghost.parameters = [LAYOUT, b"", b"", b"", b"", b"", b"", b""]
    rel = ghost.relationships
    rel.x = rel.y = rel.z = 0
    rel.section_1, rel.section_2, rel.section_3 = [], [], []
    room.actors.append(ghost)
    files.writeFile(ROOM, room)


def _event(files) -> None:
    flow = files.readFile(EVENT_FILE)
    chart = flow.flowchart
    after = None
    for item, flag in reversed(GIFTS):
        give = event_tools.createActionEvent(chart, "Inventory", "AddItemByKey",
            {"itemKey": item, "count": GIFT_COUNT, "index": REFILL_INDEX, "autoEquip": False}, after)
        after = event_tools.createSwitchEvent(chart, "EventFlags", "CheckFlag", {"symbol": flag}, {0: after, 1: give})
    event_tools.addEntryPoint(chart, ENTRY)
    event_tools.createActionChain(chart, ENTRY, [
        (EVENT_ACTOR, "Talk", {"message": f"Scenario:{TEXT_KEY}"}),
    ], after)
    files.writeFile(EVENT_FILE, flow)


def _text(files) -> None:
    for relative_path, msbt in files.getAllTextFiles(TEXT_FILE):
        msbt.addEntry(TEXT_KEY, TEXT)
        msbt.getEntry(TEXT_KEY).style_index = msbt.getEntry(TEXT_STYLE_FROM).style_index
        files.writeTextFile(relative_path, TEXT_FILE, msbt)
