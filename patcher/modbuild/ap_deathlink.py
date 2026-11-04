"""DeathLink in the game over (`Common.bfevfl`, entry `GameOver`; DESIGN.md, DeathLink).

The vanilla flow plays Link's death animation, waits, and then revives him with Tracy's Secret Medicine unless the
shopkeeper is taking revenge for a theft; otherwise the game over (or the Chamber Dungeon's) follows. A bottled fairy
never revives Link in this game. The module sets DEATHLINK_FLAG when it applies a received death, and the flow runs
as for any death: the medicine saves Link from a received death too. The flow clears the flag wherever it ends, and a
death that stands sets DIED_FLAG, which the module reports to the client, unless the flag says the death was received.
So neither the medicine's rescue nor a received death is sent out.
"""
from RandomizerCore.Tools import event_tools

DEATHLINK_FLAG = "APDeathLink"  # AP::DEATHLINK_FLAG
DIED_FLAG = "APDied"            # AP::DIED_FLAG
FLAGS = (DEATHLINK_FLAG, DIED_FLAG)
ENTRY = "GameOver"
# The vanilla flow's events
COUNTERATTACK = "Event72"   # clears the revenge flag; no medicine saves Link from the shopkeeper
MEDICINE = "Event57"        # Inventory.HasItem itemType 22 (the Secret Medicine): 1 revives Link
MEDICINE_ITEM_TYPE = 22
DEAD = "Event94"            # the death stands: the Chamber Dungeon's or the normal game over


def patch_game_over(flowchart) -> None:
    medicine = event_tools.findEvent(flowchart, MEDICINE)
    if (medicine is None or medicine.data.params.data.get("itemType") != MEDICINE_ITEM_TYPE
            or medicine.data.cases[1].v is None or event_tools.findEvent(flowchart, COUNTERATTACK) is None):
        raise RuntimeError("Common.bfevfl's GameOver is not the one the DeathLink patch expects")
    rescued = medicine.data.cases[1].v
    died = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag", {"symbol": DIED_FLAG, "value": True}, DEAD)
    received = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag",
                                             {"symbol": DEATHLINK_FLAG, "value": False}, DEAD)
    stands = event_tools.createSwitchEvent(flowchart, "EventFlags", "CheckFlag", {"symbol": DEATHLINK_FLAG},
                                           {0: died, 1: received})
    event_tools.insertEventAfter(flowchart, COUNTERATTACK, stands)
    event_tools.setSwitchEventCase(flowchart, MEDICINE, 0, stands)
    saved = event_tools.createActionEvent(flowchart, "EventFlags", "SetFlag",
                                          {"symbol": DEATHLINK_FLAG, "value": False}, rescued.name)
    event_tools.setSwitchEventCase(flowchart, MEDICINE, 1, saved)
