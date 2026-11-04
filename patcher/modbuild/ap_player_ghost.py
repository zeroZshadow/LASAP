"""Multiplayer ghosts (DESIGN.md, Ghosts): the rows the module draws other players with. Their model,
PlayerGhost.bfres, the patcher makes from the player's Playerlink.bfres (patcher/ap_ghost_model.py).

The row APGhost is NpcGhost's row drawing that model with Link's animations, with no collision, action, event
triggers or conditions. The module spawns ghosts as NpcGhost actors that read the APGhost row. It is copied from
the game's NpcGhost row before the Ammo Ghost option (ap_ghost.py) rewrites that one. Its facial row, APGhost in
Facial.gsheet, names Link's face animations (eye, mouth, ear) so the NPC gets a material animation slot for each,
held at frame 0 until the module sets the sender's frames.
"""
import copy

from ap_ghost_model import FILE_NAME

MODEL_NAME = "link"
ROW = "APGhost"
FROM_ROW = "NpcGhost"
FACIAL_FROM_ROW = "Ghost"  # NpcGhost's facial row, the APGhost one's base
# The face's material animations on Link's model (eye, mouth, then the others): the facial row gives the NPC a slot
# for each
FACE_ANIMATIONS = ("eye", "mouth", "ear")
# The row's animations: the NPC component's own (idle, talk, walk, run) by Link's names
ANIMATIONS = {"idle": "wait", "talk": "talk", "walk": "walk", "run": "run"}


def ghost_row(row: dict) -> dict:
    """The APGhost row made from NpcGhost's (parsed) row."""
    row["symbol"] = ROW
    graphics = row["graphics"]
    graphics["path"] = FILE_NAME
    graphics["model"] = MODEL_NAME
    graphics["animations"] = dict(ANIMATIONS)
    graphics["animSettings"] = [{"name": name, "blendTime": 4, "lipsync": 0, "cull": 2}
                                for name in ANIMATIONS.values()]
    graphics["facial"] = ROW
    row["eventTriggers"] = []
    row["doAction"]["type"] = 0  # no talk or other action, so no DoAction component
    row["popConditions"]["conditions"] = []
    row["layoutConditions"] = []
    row["collision"]["shape"] = 0  # no Collision component: nothing blocks Link, nothing moves the ghost
    row["damageCollision"]["enable"] = False
    return row


def add_row(sheet: dict) -> None:
    """Adds the APGhost row to a read Npc.gsheet (replacing one there), from its NpcGhost row."""
    from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

    rows = [parseStruct(value) for value in sheet["values"]]
    source = next((row for row in rows if row["symbol"] == FROM_ROW), None)
    if source is None:
        raise ValueError(f"no {FROM_ROW} row")
    sheet["values"] = [value for value, row in zip(sheet["values"], rows) if row["symbol"] != ROW]
    sheet["values"].append(dictToStruct(ghost_row(source)))


def add_facial_row(sheet: dict) -> None:
    """Adds the APGhost row to a read Facial.gsheet (replacing one there): Link's face animations, held at frame 0
    with no blinking or lip sync (the module sets their frames from the sender's)."""
    from RandomizerCore.Tools import gsheet
    from RandomizerCore.Tools.oead_tools import parseStruct

    rows = [parseStruct(value) for value in sheet["values"]]
    source = next((value for value, row in zip(sheet["values"], rows) if row["mKey"] == FACIAL_FROM_ROW), None)
    if source is None:
        raise ValueError(f"no {FACIAL_FROM_ROW} facial row")
    row = copy.deepcopy(source)
    row["mKey"] = ROW
    eye, mouth, *others = FACE_ANIMATIONS
    names = row["mMaterialAnimNames"]
    names["mEye"], names["mMouth"], names["mOthers"] = eye, mouth, gsheet.StringArray(others)
    normal = row["mExpressions"][0]
    normal.update(mLabel="normal", mEyeFrame=0, mMouthFrame=0, mEyeLength=1, mMouthLength=1, mEyeBlinkFrame=0,
                  mEyeBlinkSettings=None, mLipSyncCloseFrame=0, mLipSyncOpenFrame=0, mLipSyncSettings=None,
                  mOtherFrames=gsheet.IntArray([0] * len(others)), mOtherLengths=gsheet.IntArray([1] * len(others)))
    row["mExpressions"] = gsheet.StructArray([normal])
    sheet["values"] = [value for value, r in zip(sheet["values"], rows) if r["mKey"] != ROW]
    sheet["values"].append(row)


def add(files) -> None:
    """At patch time, before ap_ghost.add: the APGhost rows in the mod's Npc.gsheet and Facial.gsheet."""
    sheet = files.readFile("Npc.gsheet")
    add_row(sheet)
    files.writeFile("Npc.gsheet", sheet)
    facial = files.readFile("Facial.gsheet")
    add_facial_row(facial)
    files.writeFile("Facial.gsheet", facial)
