"""DeathLink in the game over (`Common.bfevfl`, entry `GameOver`; DESIGN.md, DeathLink).

The vanilla flow plays Link's death animation, waits, and then saves him with a bottled fairy unless the shopkeeper is
taking revenge for a theft; otherwise the game over (or the Chamber Dungeon's) follows. The module sets DEATHLINK_FLAG
when it applies a received death: the flow then clears it and skips the fairy, so the death stands and is not sent back.
Any other death that stands sets DIED_FLAG, which the module reports to the client, so a fairy's rescue sends nothing.
"""
from RandomizerCore.Tools import event_tools

DEATHLINK_FLAG = "APDeathLink"  # AP::DEATHLINK_FLAG
DIED_FLAG = "APDied"            # AP::DIED_FLAG
FLAGS = (DEATHLINK_FLAG, DIED_FLAG)
ENTRY = "GameOver"
# The vanilla flow's events
DYING = "Event61"           # the wait after Link's death animation
CHECKS = "Event71"          # then the shopkeeper's revenge check, before the fairy check
COUNTERATTACK = "Event72"   # clears the revenge flag; no fairy saves Link from the shopkeeper
FAIRY = "Event57"           # Inventory.HasItem itemType 22 (a bottled fairy): 1 revives Link
FAIRY_ITEM_TYPE = 22
DEAD = "Event94"            # the death stands: the Chamber Dungeon's or the normal game over


def patch_game_over(flowchart) -> None:
    fairy = event_tools.findEvent(flowchart, FAIRY)
    dying = event_tools.findEvent(flowchart, DYING)
    if (fairy is None or fairy.data.params.data.get("itemType") != FAIRY_ITEM_TYPE or dying is None
            or dying.data.nxt.v is None or dying.data.nxt.v.name != CHECKS
            or event_tools.findEvent(flowchart, COUNTERATTACK) is None):
        raise RuntimeError("Common.bfevfl's GameOver is not the one the DeathLink patch expects")
    died = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag", {"symbol": DIED_FLAG, "value": True}, DEAD)
    event_tools.insertEventAfter(flowchart, COUNTERATTACK, died)
    event_tools.setSwitchEventCase(flowchart, FAIRY, 0, died)
    received = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag",
                                             {"symbol": DEATHLINK_FLAG, "value": False}, DEAD)
    check = event_tools.createSwitchEvent(flowchart, "EventFlags", "CheckFlag", {"symbol": DEATHLINK_FLAG},
                                          {0: CHECKS, 1: received})
    event_tools.insertEventAfter(flowchart, DYING, check)
