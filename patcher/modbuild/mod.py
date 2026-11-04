"""The mod's RomFS, the same for every seed: LASR's mod generator (RandomizerCore, MIT) with the patcher's edits on
top. scripts/build_assets.py runs `build` on a patch with the locations the world gives every seed (make_test_patch.py)
and stores the files as assets (ap_assets.py). Every AP location gives the Archipelago placeholder, whose AddItemID
index is the location's local id; the exlaunch module reports it as a check. Needs .venv-lasr.
"""
import copy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PATCHER = HERE.parent
sys.path.insert(0, str(PATCHER))
sys.path.insert(0, str(HERE))

import evfl  # noqa: E402
from RandomizerCore import mod_generator  # noqa: E402
from RandomizerCore.Fixes.Datasheets.items import ItemsDatasheetFixes  # noqa: E402
from RandomizerCore.Fixes.Events import player_start  # noqa: E402
from RandomizerCore.Helpers.file_manager import FileManager  # noqa: E402
from RandomizerCore.Helpers.flag_manager import FlagManager  # noqa: E402
from RandomizerCore.Helpers.item_get_manager import ItemGetManager  # noqa: E402
from RandomizerCore.Randomizers import chests as lasr_chests  # noqa: E402
from RandomizerCore.Randomizers.chests import ChestRandomizer  # noqa: E402
from RandomizerCore.Randomizers.data import (BGM_TRACKS, DUNGEON_ENTRANCES, DUNGEON_MAP_ICONS,  # noqa: E402
                                             MODEL_ROTATIONS, MODEL_SIZES, SIDESCROLLER_ENVIRONMENTS,
                                             TOPDOWN_ENVIRONMENTS)
from RandomizerCore.Randomizers.heart_pieces import HEART_ROOMS, sunken as HEART_SUNKEN  # noqa: E402
from RandomizerCore.Randomizers.instruments import InstrumentRandomizer  # noqa: E402
from RandomizerCore.Randomizers.shop import TEXT_TO_CHANGE as SHOP_TEXT  # noqa: E402
from RandomizerCore.Tools import event_tools  # noqa: E402
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct  # noqa: E402
from RandomizerCore.Tools.text_tools import TextFile  # noqa: E402
import ap_ballad  # noqa: E402
import ap_chest  # noqa: E402
import ap_deathlink  # noqa: E402
import ap_drop  # noqa: E402
import ap_ghost  # noqa: E402
import ap_goal  # noqa: E402
import ap_hint_edits  # noqa: E402
import ap_hints  # noqa: E402
import ap_map  # noqa: E402
import ap_map_edits  # noqa: E402
import ap_model  # noqa: E402
import ap_option_edits  # noqa: E402
import ap_options  # noqa: E402
import ap_player_ghost  # noqa: E402
import ap_shuffles  # noqa: E402
from las_ap_patch import (COMPANION_FLAGS, PLACEHOLDER_KEY, START_ITEMS_FLAG, dungeon_item,  # noqa: E402
                          load_yaml, model_token)

PLACEHOLDER_ITEM_ID = 71  # must match AP::PLACEHOLDER_ITEM_ID in the exlaunch module
# An AP location's chests name CHEST_TOKEN and its local id as their model file; the module shows the chest of the
# location's item, as LASR's "Chest Types" would (the models' C lines).
CHEST_TOKEN = "apchest:"  # AP::CHEST_TOKEN
CHEST_FILES = {**lasr_chests.CHEST_TEXTURES, ap_chest.CHEST_TYPE: ap_chest.FILE_NAME}  # chest-type -> model file
# LASR's chests for las_ap_patch.chest_look and the seed's chests (ap_assets.data)
CHESTS = {"rooms": list(lasr_chests.CHEST_ROOMS), "sizes": lasr_chests.CHEST_SIZES, "files": CHEST_FILES}
CHEST_MODEL = "TreasureBox"  # the model in every chest file
CHEST_TWINS = {"D7-grim-creeper": "Lv07EagleTower_06H", "D7-3f-horseheads": "Lv07EagleTower_05G"}  # LASR's
# Locations whose model LASR writes into a sheet the game reads it from directly (ShopItem.gsheet, Npc.gsheet), not
# into actor parameters: they keep the placeholder's model, which the module swaps by their stand-in row.
SHEET_MODEL_LOCATIONS = ("shop-slot3-1st", "shop-slot3-2nd", "shop-slot6", "bay-fisherman", "syrup", "walrus")
RECV_FLAG_BITS = 16
# Secret Medicines received while Link holds one wait in a count of these flags (AP::MEDICINE_FLAG)
MEDICINE_FLAG = "APMedicine"
MEDICINE_FLAG_BITS = 2
PLACEHOLDER_TEXT = "You found an [COLOR_SKYBLUE]Archipelago item[COLOR_WHITE]!"
# The exlaunch module replaces both texts at runtime with the item and player names (AP::FOUND_LABEL, AP::RECEIVED_LABEL).
RECEIVED_KEY = "APReceived"
RECEIVED_TEXT = "You got an [COLOR_PINK]Archipelago item[COLOR_WHITE]!"
# The module plays a received item's item-get as this Common.bfevfl event, so the game locks the controls and returns
# the camera when it ends. The module swaps the placeholder and message for the received item (AP::ITEM_EVENT_ENTRY).
ITEM_EVENT_ENTRY = "APItemGet"
# Received traps play LASR's trap effects as the Common.bfevfl events AP<trap key> (AP::TRAP_EVENT_PREFIX).
TRAP_EVENT_PREFIX = "AP"
TRAP_EVENT_KEYS = ("ZapTrap", "DrownTrap", "SquishTrap", "DeathballTrap", "QuakeTrap")
# An item-get whose message entry starts with this plays as the game's own also with Instant Pickups; the module drops
# the mark (AP::NATIVE_ITEM_GET_MARK). The instruments' pedestals have it: their whiteout and warp follow at once.
NATIVE_ITEM_GET_MARK = "!"
# Holding Minus opens this Common.bfevfl event (AP::WARP_MENU_ENTRY): a choice to warp home, to the start point outside
# Tarin's door that leaving the house uses (Field_11C, ExitOut). B picks the bottom choice, so Continue is there.
WARP_MENU_ENTRY = "APWarpMenu"
WARP_MENU_KEY = "APWarpMenu"
WARP_MENU_TEXT = "Return to Tarin's house?[CHOICE_TOP]Warp home[CHOICE_BOTTOM]Continue"
WARP_MENU_STYLE_FROM = "CheckShieldBook"  # a vanilla yes/no prompt in System.msbt
WARP_HOME = {"level": "Field", "locator": "Field_11C", "offsetX": 0.0, "offsetZ": 0.0}
ITEM_TEXT_STYLE_FROM = "GetHookShot"  # System.msbt entry whose window style the AP texts use
PLACEHOLDER_NAME_KEY = "ItemName_ArchipelagoItem"
PLACEHOLDER_NAME = "Archipelago Item"
# The placeholder shows the Archipelago model (ap_model.py), built from the Seashell Sensor's.
PLACEHOLDER_SOURCE_NPC = "ItemShellRader"
PLACEHOLDER_DEF = {
    "type": "junk",
    "quantity": 0,
    "item-key": PLACEHOLDER_KEY,
    "model-path": ap_model.FILE_NAME,
    "model-name": ap_model.MODEL_NAME,
    "npc-key": "ItemArchipelago",
    "message": f"UI:{PLACEHOLDER_NAME_KEY}",
}
# Item kinds whose in-game index LASR takes from logic.yml for vanilla placements.
INDEXED_ITEMS = ("seashell", "heart-piece", "heart-container", "bottle", "golden-leaf", "chamber-stone")
ROOSTER_EVENT_FILE = "RoosterBones.bfevfl"
# Items.gsheet instrument rows -> their jingle: the instrument's own song, which the pedestal's whiteout plays in the
# game (MusicalInstrument.bfevfl's PlayAndWaitMusicalInstrument), in place of the fanfare all eight share
# (BGM_FANFARE_INST_GET). Every item-get and find of an instrument plays it.
INSTRUMENT_JINGLES = {"FullMoonCello": "BGM_INST_VIOLIN", "ConchHorn": "BGM_INST_HORN", "SeaLilysBell": "BGM_INST_BELL",
                      "SurfHarp": "BGM_INST_HARP", "WindMarimba": "BGM_INST_MARIMBA",
                      "CoralTriangle": "BGM_INST_TRIANGLE", "EveningCalmOrgan": "BGM_INST_ORGAN",
                      "ThunderDrum": "BGM_INST_DRUM"}

# LASR's start event's flags (cutscenes watched, the Walrus awake, ...). The ghost's reward is a drop at its grave
# (ap_drop.GHOST_DROP); LASR gives it in the owl's talk at the grave, which shows once the farewell turned
# Ghost4_Clear on, and so leaves its watched flag out: with the drop the owl stays away too.
START_FLAGS = (*player_start.START_FLAGS, "EighthClear")
# Items.gsheet tunic rows -> the npc key their item-get holds up (items.yml's npc-key; the green tunic has none)
TUNIC_NPC_KEYS = {"ClothesRed": "NpcSkeletalGuardRed", "ClothesBlue": "NpcSkeletalGuardBlue"}


def _add_ap_flags(self) -> None:
    _original_add_custom_flags(self)
    for i in range(RECV_FLAG_BITS):
        self.addFlag(f"APRecv{i}")
    for i in range(MEDICINE_FLAG_BITS):
        self.addFlag(f"{MEDICINE_FLAG}{i}")
    self.addFlag(ap_ghost.BOW_FLAG)
    self.addFlag(ap_goal.SIGN_FLAG)
    self.addFlag(START_ITEMS_FLAG)
    for flag in ap_deathlink.FLAGS:
        self.addFlag(flag)
    for flag in ap_hints.FLAGS:
        self.addFlag(flag)
    for flag in ap_drop.FLAGS:
        self.addFlag(flag)


def _add_placeholder_row(self, sheet) -> None:
    """The placeholder's row, with its own item id and model; the rows' ids and npc keys go to the process's
    item_rows."""
    _original_make_item_changes(self, sheet)
    # LASR empties the tunics' npcKey (vanilla's rows show the ocarina), so their item-get holds up nothing and shows
    # no glow; they hold up the model LASR shows for them in the world.
    for item in sheet["values"]:
        if item["symbol"] in TUNIC_NPC_KEYS:
            item["npcKey"] = TUNIC_NPC_KEYS[item["symbol"]]
        if item["symbol"] in INSTRUMENT_JINGLES:
            item["jingle"] = INSTRUMENT_JINGLES[item["symbol"]]
    # Parsed up front: appending to the sheet invalidates references to its rows.
    rows = {str(item["symbol"]): parseStruct(item) for item in sheet["values"]}
    if any(row["itemID"] == PLACEHOLDER_ITEM_ID for row in rows.values()):
        raise ValueError(f"Items.gsheet already uses the placeholder's item id {PLACEHOLDER_ITEM_ID}")
    self.parent.item_rows.update({key: (row["itemID"], str(row["npcKey"])) for key, row in rows.items()})
    row = copy.deepcopy(rows["SmallKey"])
    row["symbol"] = PLACEHOLDER_KEY
    row["itemID"] = PLACEHOLDER_ITEM_ID
    row["gettingFlag"] = ""
    row["npcKey"] = PLACEHOLDER_DEF["npc-key"]
    sheet["values"].append(dictToStruct(row))


def add_events(proc) -> None:
    """The entry points the module plays: the placeholder's and received items' texts (Item.bfevfl), received items'
    item-get, the traps and the warp menu (Common.bfevfl)."""
    flow = proc.file_manager.readFile("Item.bfevfl")
    event_tools.addEntryPoint(flow.flowchart, PLACEHOLDER_KEY)
    event_tools.createActionChain(flow.flowchart, PLACEHOLDER_KEY, [
        ("Dialog", "Show", {"message": f"Place:{PLACEHOLDER_KEY}"})
    ])
    # The module starts received items' item-get with this entry point as its message entry.
    event_tools.addEntryPoint(flow.flowchart, RECEIVED_KEY)
    event_tools.createActionChain(flow.flowchart, RECEIVED_KEY, [
        ("Dialog", "Show", {"message": f"System:{RECEIVED_KEY}"})
    ])
    proc.file_manager.writeFile("Item.bfevfl", flow)
    common = proc.file_manager.readFile("Common.bfevfl")
    event_tools.addEntryPoint(common.flowchart, ITEM_EVENT_ENTRY)
    event_tools.createActionChain(common.flowchart, ITEM_EVENT_ENTRY, [
        ("Link", "GenericItemGetSequenceByKey", {"itemKey": PLACEHOLDER_KEY, "keepCarry": False,
                                                 "messageEntry": RECEIVED_KEY}),
    ])
    for key in TRAP_EVENT_KEYS:
        entry = TRAP_EVENT_PREFIX + key
        event_tools.addEntryPoint(common.flowchart, entry)
        proc.item_get_manager.getWithAnimation(common.flowchart, key, -1, entry)
    warp = event_tools.createActionEvent(common.flowchart, "GameControl", "RequestLevelJump", dict(WARP_HOME))
    choice = event_tools.createSwitchEvent(common.flowchart, "Dialog", "GetLastResult", {}, {0: warp})
    event_tools.addEntryPoint(common.flowchart, WARP_MENU_ENTRY)
    event_tools.createActionChain(common.flowchart, WARP_MENU_ENTRY, [
        ("Dialog", "Show", {"message": f"System:{WARP_MENU_KEY}"}),
    ], choice)
    ap_deathlink.patch_game_over(common.flowchart)
    proc.file_manager.writeFile("Common.bfevfl", common)


def add_texts(proc) -> None:
    """The texts of the placeholder, received items and the warp menu, in every language."""
    files = proc.file_manager
    # Our texts use the window style of native item texts; added entries otherwise copy the file's first entry.
    styles = {path: msbt.getEntry(ITEM_TEXT_STYLE_FROM).style_index for path, msbt in files.getAllTextFiles("System.msbt")}
    for relative_path, msbt in files.getAllTextFiles("Place.msbt"):
        msbt.addEntry(PLACEHOLDER_KEY, PLACEHOLDER_TEXT)
        entry = msbt.getEntry(PLACEHOLDER_KEY)
        entry.message.text += "[1:4]"  # wait for input
        entry.style_index = styles[relative_path]
        files.writeTextFile(relative_path, "Place.msbt", msbt)
    for relative_path, msbt in files.getAllTextFiles("System.msbt"):
        msbt.addEntry(RECEIVED_KEY, RECEIVED_TEXT)
        entry = msbt.getEntry(RECEIVED_KEY)
        entry.message.text += "[1:4]"
        entry.style_index = styles[relative_path]
        msbt.addEntry(WARP_MENU_KEY, WARP_MENU_TEXT)
        msbt.getEntry(WARP_MENU_KEY).style_index = msbt.getEntry(WARP_MENU_STYLE_FROM).style_index
        files.writeTextFile(relative_path, "System.msbt", msbt)
    for relative_path, msbt in files.getAllTextFiles("UI.msbt"):
        msbt.addEntry(PLACEHOLDER_NAME_KEY, PLACEHOLDER_NAME)
        files.writeTextFile(relative_path, "UI.msbt", msbt)


def _placeholder_get_first(self, flowchart, item, index, before=None, after=None):
    """The placeholder is added before its item-get, unlike LASR's other items: the module learns the location
    from the AddItemID call and names the item in the text that follows."""
    if item != PLACEHOLDER_KEY:
        return _original_get_with_animation(self, flowchart, item, index, before, after)
    return event_tools.createActionChain(flowchart, before, [
        ("Inventory", "AddItemByKey", {"itemKey": PLACEHOLDER_KEY, "count": 1, "index": index, "autoEquip": False}),
        ("Link", "GenericItemGetSequenceByKey", {"itemKey": item, "keepCarry": False,
                                                 "messageEntry": PLACEHOLDER_KEY}),
    ], after)


def _chest_placeholder_branch(self) -> None:
    """Vanilla chests play the item-get before adding the item; chests with the placeholder add it first."""
    _original_write_chest_event(self)
    files = self.parent.file_manager
    flow = files.readFile("TreasureBox.bfevfl")
    chart = flow.flowchart
    open_next = event_tools.findEvent(chart, "Event32").data.nxt.v.name  # LASR's item switch chain
    auto_save = event_tools.findEvent(chart, "Event40").data.nxt.v.name  # LASR's RequestAutoSave
    ap_get = event_tools.createActionChain(chart, None, [
        ("Inventory", "AddItemByKey", {"itemKey": PLACEHOLDER_KEY, "count": 1,
                                       "index": evfl.common.Argument("itemIndex"), "autoEquip": False}),
        ("Link", "GenericItemGetSequenceByKey", {"itemKey": evfl.common.Argument("itemKey"), "keepCarry": False,
                                                 "messageEntry": PLACEHOLDER_KEY}),
    ], auto_save)
    ap_check = event_tools.createSwitchEvent(chart, "FlowControl", "CompareString",
        {"value1": "itemKey", "value2": PLACEHOLDER_KEY}, {0: ap_get, 1: open_next})
    event_tools.insertEventAfter(chart, "Event32", ap_check)
    event_tools.insertEventAfter(chart, "Event28", ap_check)
    files.writeFile("TreasureBox.bfevfl", flow)


def _native_instrument_get(self, flowchart, room, room_data):
    """The instrument pedestal's item-gets (any item, progressive ones on both branches) keep the game's item-get. The
    music stops before it, as in the game (MusicalInstrument.bfevfl), so the item's jingle plays alone; LASR's whiteout
    after it lets the jingle play on (its StopAllBGM is in a chain its fork cuts off) until the warp out."""
    _original_change_instrument(self, flowchart, room, room_data)
    first = event_tools.findEntryPoint(flowchart, room).main_event.v.name
    event_tools.createActionChain(flowchart, room, [("Audio", "StopAllBGM", {"duration": 1.0})], first)
    mark_native_item_gets(flowchart, room)


def mark_native_item_gets(chart, entry) -> None:
    """Marks every Link item-get reachable from an entry point (NATIVE_ITEM_GET_MARK)."""
    todo = [event_tools.findEntryPoint(chart, entry).main_event.v]
    seen = set()
    while todo:
        event = todo.pop()
        if event is None or event.name in seen:
            continue
        seen.add(event.name)
        data = event.data
        if isinstance(data, evfl.event.ActionEvent) and data.actor.v.identifier.name == "Link" and \
                data.actor_action.v.v in ("GenericItemGetSequenceByKey", "GenericItemGetSequence"):
            entry_param = data.params.data.get("messageEntry", "")
            if isinstance(entry_param, str) and not entry_param.startswith(NATIVE_ITEM_GET_MARK):
                data.params.data["messageEntry"] = NATIVE_ITEM_GET_MARK + entry_param
        if getattr(data, "nxt", None) is not None:
            todo.append(data.nxt.v)
        if isinstance(data, evfl.event.SwitchEvent):
            todo.extend(case.v for case in data.cases.values())
        if isinstance(data, evfl.event.ForkEvent):
            todo.extend(fork.v for fork in data.forks)
            todo.append(data.join.v)


def _write_file(self, file_name: str, data) -> None:
    """The Archipelago model gets an Npc entry, which the item-get shows, and the chest's entry takes its model from its
    actor's parameters. The small key's flowchart stays the game's:
    LASR's edits are its stand-ins' entries, and every stand-in is a drop actor, with a flowchart of its own (ap_drop)."""
    if file_name == ap_drop.SMALL_KEY_EVENT:
        return
    # Once: the sheet is written again after LASR's run (ap_ghost).
    if file_name == "Npc.gsheet" and not any(row["symbol"] == PLACEHOLDER_DEF["npc-key"] for row in data["values"]):
        # Parsed before appending: appending to the sheet invalidates references to its rows.
        source = next(parseStruct(row) for row in data["values"] if row["symbol"] == PLACEHOLDER_SOURCE_NPC)
        source["symbol"] = PLACEHOLDER_DEF["npc-key"]
        source["graphics"]["path"] = ap_model.FILE_NAME
        source["graphics"]["model"] = ap_model.MODEL_NAME
        data["values"].append(dictToStruct(source))
    if file_name == "Npc.gsheet":
        # Every chest names its model file and model name, as LASR's "Texture + Size" has it (_chest_contents)
        chest = next(row for row in data["values"] if row["symbol"] == "ObjTreasureBox")
        chest["graphics"]["path"], chest["graphics"]["model"] = "$6", "$7"
    if file_name in ap_option_edits.SHEETS:
        ap_option_edits.SHEETS[file_name](data["values"])
    _original_write_file(self, file_name, data)


_original_get_with_animation = ItemGetManager.getWithAnimation
_original_write_file = FileManager.writeFile
FileManager.writeFile = _write_file
_original_write_chest_event = ChestRandomizer.writeChestEvent
ItemGetManager.getWithAnimation = _placeholder_get_first
ChestRandomizer.writeChestEvent = _chest_placeholder_branch
_original_change_instrument = InstrumentRandomizer.changeInstrument
InstrumentRandomizer.changeInstrument = _native_instrument_get
_original_add_custom_flags = FlagManager.addCustomFlags
_original_make_item_changes = ItemsDatasheetFixes.makeItemChanges
FlagManager.addCustomFlags = _add_ap_flags
ItemsDatasheetFixes.makeItemChanges = _add_placeholder_row


def _chest_contents(self) -> None:
    """LASR's chest contents, each chest naming its model file (the Npc row's $6, _write_file): every chest location is
    an AP location, and its chests name its CHEST_TOKEN, the same in every seed (the module shows the chest of the
    location's item)."""
    mod = self.parent
    files = mod.file_manager
    for level in lasr_chests.PANEL_CHEST_ROOMS.values():
        room = files.readFile(f"{level}.leb")
        for actor in room.actors:
            if actor.type == ap_map.CHEST_ACTOR:
                actor.parameters[6], actor.parameters[7] = CHEST_FILES["default"].encode(), CHEST_MODEL.encode()
        files.writeFile(f"{level}.leb", room)
    for loc, level in lasr_chests.CHEST_ROOMS.items():
        key, index = mod.item_info_manager.getItemInfo(loc)
        if key != PLACEHOLDER_KEY:
            raise ValueError(f"the chest location {loc} holds {key}, not the placeholder")
        for name in (level, CHEST_TWINS[loc]) if loc in CHEST_TWINS else (level,):
            room = files.readFile(f"{name}.leb")
            for i in range(5 if loc == "taltal-5-chest-puzzle" else 1):
                room.setChestContent(key, index, i, 1.0, chest_token(index))
            files.writeFile(f"{name}.leb", room)


ChestRandomizer.makeChestContentFixes = _chest_contents


class APModsProcess(mod_generator.ModsProcess):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.item_rows = {}  # Items.gsheet's rows as the mod has them: item key -> (item id, npc key)

    def run(self) -> None:
        super().run()
        add_events(self)
        add_texts(self)


def build_placements(patch: dict, logic: dict) -> dict:
    """Converts the AP patch file into the placements dict LASR's ModsProcess expects."""
    settings = dict(patch["settings"])
    settings.setdefault("Excluded Locations", [])
    settings["Starting Gear"] = list(patch["starting_items"])
    # The module leads the doors of shuffled dungeons (ap_entrances): LASR's doors, exits and warps stay vanilla.
    settings["Shuffled Dungeons"] = False
    settings.update(ap_options.LASR_SETTINGS)

    placements = {
        "settings": settings,
        "force-junk": [],
        "force-vanilla": [loc for loc, item in patch["placements"].items() if item != "ap-item"],
        "starting-items": list(patch["starting_items"]),
        "dungeon-entrances": dict(patch["dungeon_entrances"]),
        "indexes": {},
        "moblin-cave": "bow-wow",
        "rooster-statue": "rooster",
    }
    ids = gift_ids(patch)
    for loc, item in patch["placements"].items():
        placements[loc] = item
        if item == "ap-item":
            placements["indexes"][loc] = int(ids[loc])
            placements[loc] = location_item(loc)
        elif item in INDEXED_ITEMS and "index" in logic.get(loc, {}):
            placements["indexes"][loc] = logic[loc]["index"]
        elif dungeon_item(item):
            placements["indexes"][loc] = dungeon_item(item)[1]
    for loc in set(ids) - set(patch["placements"]):
        placements[loc] = location_item(loc)
        placements["indexes"][loc] = int(ids[loc])
    return placements


def gift_ids(patch: dict) -> dict:
    """The local ids the mod gives: the AP locations', and the owl statues' the options leave out
    (removed_location_ids), whose gifts the module turns off (ap_options)."""
    return {**patch.get("removed_location_ids", {}), **patch["location_ids"]}


def chest_token(local_id) -> str:
    """What a location's chests name as their model file."""
    return f"{CHEST_TOKEN}{int(local_id)}"


def add_rooster_flag(files) -> None:
    """Reviving the rooster (RoosterBones.bfevfl, FlyingCucco's Join) sets no flag; it sets RoosterFoundFlag (added by
    LASR's flag manager) right after."""
    flow = files.readFile(ROOSTER_EVENT_FILE)
    chart = flow.flowchart
    joins = [event for event in chart.events if getattr(event.data, "actor", None) is not None
             and event.data.actor.v.identifier.name == "FlyingCucco" and str(event.data.actor_action.v) == "Join"]
    assert len(joins) == 1, joins
    after = joins[0].data.nxt.v.name if joins[0].data.nxt.v is not None else None
    flag = event_tools.createActionEvent(chart, "EventFlags", "SetFlag",
                                         {"symbol": COMPANION_FLAGS["rooster-statue"], "value": True}, after)
    event_tools.insertEventAfter(chart, joins[0].name, flag)
    files.writeFile(ROOSTER_EVENT_FILE, flow)


def location_item(loc: str) -> str:
    return f"ap-item:{loc}"


def add_location_items(patch: dict, item_defs: dict) -> None:
    """Gives every AP location its own item definition: the placeholder, its model the location's token (in a sheet,
    the Archipelago model's)."""
    ids = gift_ids(patch)
    for loc in [loc for loc, item in patch["placements"].items() if item == "ap-item"] + sorted(set(ids) - set(patch["placements"])):
        item_def = dict(PLACEHOLDER_DEF)
        if loc in ids and loc not in SHEET_MODEL_LOCATIONS:
            item_def["model-path"] = item_def["model-name"] = model_token(ids[loc])
        item_defs[location_item(loc)] = item_def


def build(romfs: Path, out: Path, patch: dict) -> dict:
    """Makes the mod for `patch` from the RomFS dump in `out` (the SD card's layout; the assets it shares with every
    seed are not in it) and returns what the seed's files need of it and of LASR (ap_assets.data), but the patch's
    shape."""
    item_defs = load_yaml("items.yml")["Item_Pool"]
    item_defs["ap-item"] = dict(PLACEHOLDER_DEF)
    add_location_items(patch, item_defs)
    logic = load_yaml("logic.yml")
    placements = build_placements(patch, logic)
    proc = APModsProcess(placements, romfs, out, item_defs)
    proc.run()
    if "rooster-statue" in patch["location_ids"]:
        add_rooster_flag(proc.file_manager)
    ap_player_ghost.add(proc.file_manager)  # from NpcGhost's row before the Ammo Ghost rewrites it
    ap_ghost.add(proc.file_manager)  # the module leaves him out without the option (ap_options.cpp)
    ap_hint_edits.add(proc.file_manager)
    ap_ballad.add(romfs, proc.romfs_dir)
    ap_map_edits.add(proc.file_manager)
    # before the drops, which change the AP chests' item the map finds them by
    map_chests = ap_map_edits.dungeon_chests(proc.file_manager, patch["location_ids"], PLACEHOLDER_KEY)
    ap_drop.add(proc, PLACEHOLDER_KEY)
    return {
        "item_rows": proc.item_rows,
        "chests": CHESTS,
        "heart_rooms": list(HEART_ROOMS),
        "heart_sunken": list(HEART_SUNKEN),
        "model_sizes": MODEL_SIZES,
        "model_rotations": MODEL_ROTATIONS,
        "shop_text": SHOP_TEXT,
        "start_flags": START_FLAGS,
        "boss_flags": player_start.BOSS_FLAGS,
        "dungeon_entrances": DUNGEON_ENTRANCES,
        "dungeon_map_icons": DUNGEON_MAP_ICONS,
        "map_doors": ap_map_edits.door_groups(proc.file_manager),
        "map_chests": map_chests,
        "bgm_tracks": BGM_TRACKS,
        "environments": [TOPDOWN_ENVIRONMENTS, SIDESCROLLER_ENVIRONMENTS],
        "texts": ap_shuffles.text_pools(
            lambda name: TextFile(romfs / ap_shuffles.TEXT_DIR / f"{name}.msbt").getTextDict()),
    }
