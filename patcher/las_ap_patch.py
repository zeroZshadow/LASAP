"""Builds the Link's Awakening (Switch) mod for one Archipelago slot.

python las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <output dir> [--settings <JSON object>]

Runs LASR's mod generator (RandomizerCore, MIT) on the placements from the AP patch file
(format in DESIGN.md). Every location holding "ap-item" gives the Archipelago placeholder,
whose AddItemID index is the location's local id; the exlaunch module reports it as a check.
--settings overrides the player settings (PLAYER_SETTINGS) the patch file carries.
"""
import argparse
import copy
import hashlib
import json
import random
import sys
import zipfile
import zlib
from pathlib import Path
from typing import List, Optional

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import lasr_qt  # noqa: E402

lasr_qt.install()
# LASR looks for its data, resources and module next to its entry script, or elsewhere when it runs frozen
# (PyInstaller). The patcher keeps them next to this file in both cases, where the frozen patcher has them too.
from RandomizerCore.Paths import randomizer_paths  # noqa: E402

randomizer_paths.IS_RUNNING_FROM_SOURCE = True
randomizer_paths.ROOT_PATH = HERE
randomizer_paths.SETTINGS_PATH = HERE / "settings.txt"
randomizer_paths.LOGS_PATH = HERE / "log.txt"
randomizer_paths.EXL_PATH = HERE / "exl"
randomizer_paths.DATA_PATH = HERE / "RandomizerCore" / "Data"
randomizer_paths.RESOURCE_PATH = HERE / "RandomizerUI" / "Resources"
randomizer_paths.VERSION_PATH = HERE / "version.txt"

import evfl  # noqa: E402
import yaml  # noqa: E402
from RandomizerCore import mod_generator  # noqa: E402
from RandomizerCore.Fixes.Datasheets.item_drop import ItemDropDatasheetFixes  # noqa: E402
from RandomizerCore.Fixes.Datasheets.items import ItemsDatasheetFixes  # noqa: E402
from RandomizerCore.Helpers.file_manager import FileManager  # noqa: E402
from RandomizerCore.Helpers.flag_manager import FlagManager  # noqa: E402
from RandomizerCore.Helpers.item_get_manager import ItemGetManager  # noqa: E402
from RandomizerCore.Randomizers import chests as lasr_chests  # noqa: E402
from RandomizerCore.Randomizers.chests import ChestRandomizer  # noqa: E402
from RandomizerCore.Randomizers.keysanity import Keysanity  # noqa: E402
from RandomizerCore.Randomizers.shop import TEXT_TO_CHANGE as SHOP_TEXT  # noqa: E402
from RandomizerCore.Tools import event_tools  # noqa: E402
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct  # noqa: E402
import ap_assets  # noqa: E402
import ap_deathlink  # noqa: E402
import ap_ghost  # noqa: E402
import ap_hints  # noqa: E402
import ap_goal  # noqa: E402
import ap_map  # noqa: E402
import ap_model  # noqa: E402

PATCH_FORMAT = 1
PLACEHOLDER_KEY = "ArchipelagoItem"
PLACEHOLDER_ITEM_ID = 71  # must match AP::PLACEHOLDER_ITEM_ID in the exlaunch module
# Item ids of the model variants: the item-get picks its model by id. Neither the game nor LASR uses these (LASR's
# Dampe and shop dummies take 63-70, its traps 127-131 and its trade items 200-202), and the module counts them as the
# placeholder (AP::IsPlaceholderItem). Item ids are a byte; the item-get treats all of these like the placeholder.
VARIANT_ITEM_IDS = [*range(PLACEHOLDER_ITEM_ID + 1, 100), 103, 107, 109, 118, 119, *range(132, 200), *range(203, 256)]
# Item actors hide while the inventory bit of their parameters[0] is set: ItemHeartPiece checks the heart piece bits,
# ItemSlimeKey (LASR's static seashells) the seashell bits. The index must be unique per actor type. Received items
# take the lowest free bit below the game's count (32 heart pieces, 50 seashells; kHeartPieceBits and kSeashellBits in
# the module), so the randomized actors get indices above that. LASR's misc items already use heart piece 51-54.
ACTOR_INDICES = {
    0xB0: [i for i in range(32, 64) if not 50 <= i <= 54],  # ItemHeartPiece
    0x8A: list(range(50, 64)),  # ItemSlimeKey
}
_actor_index: dict = {}
RECV_FLAG_BITS = 16
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
NAME_MAX = 40
# Shop slots show the item's name on the price tag; each AP item there gets its own name entry in UI.msbt.
SHOP_LOCATIONS = tuple(SHOP_TEXT)

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
    "chest-type": "junk",
}

# A room with a chest whose item key has 32 characters or more crashes the game as it loads (std::length_error).
# Longer variant keys are cut short and end in a hash of the item key, which keeps them unique.
VARIANT_KEY_MAX = 31


def variant_key(item_key: str) -> str:
    """The placeholder variant's item key for a model: ArchipelagoItem_<item key>, at most VARIANT_KEY_MAX long."""
    key = f"{PLACEHOLDER_KEY}_{item_key}"
    if len(key) <= VARIANT_KEY_MAX:
        return key
    return key[:VARIANT_KEY_MAX - 4] + f"{zlib.crc32(item_key.encode()) & 0xFFFF:04x}"

# Item kinds whose in-game index LASR takes from logic.yml for vanilla placements.
INDEXED_ITEMS = ("seashell", "heart-piece", "heart-container", "bottle", "golden-leaf", "chamber-stone")
# Items counted by inventory bits, as the module names them in ap_indices.txt (AP::INDICES_PATH): kind letter per item
INDEX_KINDS = {"seashell": "S", "heart-piece": "H", "golden-leaf": "L", "heart-container": "C"}
# Item key -> kind and the bits the module gives received ones (AP::GrantItem, kHeartPieceBits, kSeashellBits)
INDEX_BITS = {"Seashell": ("S", 50), "HeartPiece": ("H", 32), "GoldenLeaf": ("L", 5), "HeartContainer": ("C", 16)}
INDICES_FILE = "ap_indices.txt"
VANILLA_FILE = "ap_vanilla.txt"
# The companions' locations and the flag their joining sets
COMPANION_FLAGS = {"moblin-cave": "BowWowJoin", "rooster-statue": "RoosterFoundFlag"}
ROOSTER_EVENT_FILE = "RoosterBones.bfevfl"
DUNGEON_ITEM_PREFIXES = ("compass", "map", "stone", "key", "nightmare")
# LASR settings that change neither the logic nor the item pool: the player picks them when patching (--settings),
# over the values the patch file carries. Their default and, for choices, the values allowed.
PLAYER_SETTINGS = {
    "360 Movement": False,
    "Movement Speed": True,
    "Blur Removal": False,
    "Song Cutscenes": False,
    "Nice Hookshot": False,
    "Damage": "Normal",
    "Music": "Vanilla",
    "Randomize Text": False,
    "Randomize Environments": False,
    "Disable Guardian Acorn": False,
    "Disable Piece of Power": False,
    "Instant Text": False,
    "Disable Low Health Beep": False,
}
PLAYER_CHOICES = {"Damage": ("None", "Normal", "OHKO"), "Music": ("Vanilla", "Shuffled", "Removed")}
# Walking and swimming speed with Movement Speed on: LASR's single speed (dashing stays at 1.0)
MOVEMENT_SPEED = 1.2
# ItemDrop.gsheet rows the game drops after a run of kills without damage; the settings empty them.
BUFF_DROPS = {"Disable Guardian Acorn": "GuardianAcorn", "Disable Piece of Power": "PowerPiece"}


def _add_ap_flags(self) -> None:
    _original_add_custom_flags(self)
    for i in range(RECV_FLAG_BITS):
        self.addFlag(f"APRecv{i}")
    self.addFlag(ap_ghost.BOW_FLAG)
    for flag in ap_deathlink.FLAGS:
        self.addFlag(flag)
    for flag in ap_hints.FLAGS:
        self.addFlag(flag)


def _add_placeholder_row(self, sheet) -> None:
    """The placeholder and its model variants: rows with the placeholder's item id that differ in their model."""
    _original_make_item_changes(self, sheet)
    # Parsed up front: appending to the sheet invalidates references to its rows.
    rows = {str(item["symbol"]): parseStruct(item) for item in sheet["values"]}
    taken = {row["itemID"] for row in rows.values()} & {PLACEHOLDER_ITEM_ID, *VARIANT_ITEM_IDS}
    if taken:
        raise ValueError(f"Items.gsheet already uses the placeholder's item ids {sorted(taken)}")
    variants = [(PLACEHOLDER_KEY, PLACEHOLDER_DEF["npc-key"], PLACEHOLDER_ITEM_ID)]
    # The most used models get ids first; variants past the free ids keep the placeholder's id, and so its model in
    # the item-get.
    used = sorted(VARIANTS.items(), key=lambda v: (-VARIANT_USES[v[0]], v[0]))
    for i, (key, (item_key, npc_key)) in enumerate(used):
        item_id = VARIANT_ITEM_IDS[i] if i < len(VARIANT_ITEM_IDS) else PLACEHOLDER_ITEM_ID
        variants.append((key, rows.get(item_key, {}).get("npcKey") or npc_key, item_id))
    for key, npc_key, item_id in variants:
        row = copy.deepcopy(rows["SmallKey"])
        row["symbol"] = key
        row["itemID"] = item_id
        row["gettingFlag"] = ""
        row["npcKey"] = npc_key
        sheet["values"].append(dictToStruct(row))


def _add_placeholder_text_event(self) -> None:
    _original_add_text_events(self)
    flow = self.parent.file_manager.readFile("Item.bfevfl")
    for key in [PLACEHOLDER_KEY, *sorted(VARIANTS)]:
        event_tools.addEntryPoint(flow.flowchart, key)
        event_tools.createActionChain(flow.flowchart, key, [
            ("Dialog", "Show", {"message": f"Place:{PLACEHOLDER_KEY}"})
        ])
    # The module starts received items' item-get with this entry point as its message entry.
    event_tools.addEntryPoint(flow.flowchart, RECEIVED_KEY)
    event_tools.createActionChain(flow.flowchart, RECEIVED_KEY, [
        ("Dialog", "Show", {"message": f"System:{RECEIVED_KEY}"})
    ])
    self.parent.file_manager.writeFile("Item.bfevfl", flow)
    common = self.parent.file_manager.readFile("Common.bfevfl")
    event_tools.addEntryPoint(common.flowchart, ITEM_EVENT_ENTRY)
    event_tools.createActionChain(common.flowchart, ITEM_EVENT_ENTRY, [
        ("Link", "GenericItemGetSequenceByKey", {"itemKey": PLACEHOLDER_KEY, "keepCarry": False,
                                                 "messageEntry": RECEIVED_KEY}),
    ])
    for key in TRAP_EVENT_KEYS:
        entry = TRAP_EVENT_PREFIX + key
        event_tools.addEntryPoint(common.flowchart, entry)
        self.parent.item_get_manager.getWithAnimation(common.flowchart, key, -1, entry)
    warp = event_tools.createActionEvent(common.flowchart, "GameControl", "RequestLevelJump", dict(WARP_HOME))
    choice = event_tools.createSwitchEvent(common.flowchart, "Dialog", "GetLastResult", {}, {0: warp})
    event_tools.addEntryPoint(common.flowchart, WARP_MENU_ENTRY)
    event_tools.createActionChain(common.flowchart, WARP_MENU_ENTRY, [
        ("Dialog", "Show", {"message": f"System:{WARP_MENU_KEY}"}),
    ], choice)
    ap_deathlink.patch_game_over(common.flowchart)
    self.parent.file_manager.writeFile("Common.bfevfl", common)


def _add_placeholder_text(self) -> None:
    _original_create_text(self)
    files = self.parent.file_manager
    # Our texts use the window style of native item texts; added entries otherwise copy the file's first entry.
    styles = {path: msbt.getEntry(ITEM_TEXT_STYLE_FROM).style_index for path, msbt in files.getAllTextFiles("System.msbt")}
    for relative_path, msbt in files.getAllTextFiles("Place.msbt"):
        msbt.addEntry(PLACEHOLDER_KEY, PLACEHOLDER_TEXT)
        entry = msbt.getEntry(PLACEHOLDER_KEY)
        entry.message.text += "[1:4]"  # wait for input, as LASR's keysanity text does
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
        for key in sorted(VARIANTS):
            msbt.addEntry(f"ItemName_{key}", PLACEHOLDER_NAME)
        for key, name in SHOP_NAMES.items():
            msbt.addEntry(key, name)
        files.writeTextFile(relative_path, "UI.msbt", msbt)


def _placeholder_get_first(self, flowchart, item, index, before=None, after=None):
    """The placeholder is added before its item-get, unlike LASR's other items: the module learns the location
    from the AddItemID call and names the item in the text that follows."""
    if not item.startswith(PLACEHOLDER_KEY):
        return _original_get_with_animation(self, flowchart, item, index, before, after)
    return event_tools.createActionChain(flowchart, before, [
        ("Inventory", "AddItemByKey", {"itemKey": PLACEHOLDER_KEY, "count": 1, "index": index, "autoEquip": False}),
        ("Link", "GenericItemGetSequenceByKey", {"itemKey": item, "keepCarry": False,
                                                 "messageEntry": PLACEHOLDER_KEY}),
    ], after)


def _chest_placeholder_branch(self) -> None:
    """Vanilla chests play the item-get before adding the item; chests with the placeholder add it first."""
    _original_write_chest_event(self)
    if not self.parent.thread_active:
        return
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
    ap_check = open_next
    for key in [PLACEHOLDER_KEY, *sorted(VARIANTS)]:
        ap_check = event_tools.createSwitchEvent(chart, "FlowControl", "CompareString",
            {"value1": "itemKey", "value2": key}, {0: ap_get, 1: ap_check})
    event_tools.insertEventAfter(chart, "Event32", ap_check)
    event_tools.insertEventAfter(chart, "Event28", ap_check)
    files.writeFile("TreasureBox.bfevfl", flow)


def _starting_index(self, item, index):
    """LASR adds starting items with index -1, which counts for none of the inventory bits that make up the count of
    indexed items (heart containers, heart pieces, seashells, golden leaves). They take the bits a received item would
    (STARTING_BITS)."""
    if index == -1 and STARTING_BITS.get(item):
        index = STARTING_BITS[item].pop(0)
    return _original_get_without_animation(self, item, index)


def _drop_buffs(self, sheet, settings) -> None:
    """Disabled Guardian Acorns and Pieces of Power drop nothing, as LASR does for the drops it removes."""
    _original_item_drop_changes(self, sheet, settings)
    keys = {BUFF_DROPS[k] for k in BUFF_DROPS if settings.get(k)}
    for row in sheet["values"]:
        if row["mKey"] in keys:
            row["mLotTable"][0]["mType"] = ""


def _write_file(self, file_name: str, data) -> None:
    """Randomized heart piece and seashell actors show by their location flag alone. The Archipelago model gets an
    Npc entry, which the item-get shows."""
    # Once: the sheet is written again after LASR's run (ap_ghost).
    if file_name == "Npc.gsheet" and not any(row["symbol"] == PLACEHOLDER_DEF["npc-key"] for row in data["values"]):
        # Parsed before appending: appending to the sheet invalidates references to its rows.
        source = next(parseStruct(row) for row in data["values"] if row["symbol"] == PLACEHOLDER_SOURCE_NPC)
        source["symbol"] = PLACEHOLDER_DEF["npc-key"]
        source["graphics"]["path"] = ap_model.FILE_NAME
        source["graphics"]["model"] = ap_model.MODEL_NAME
        data["values"].append(dictToStruct(source))
    if file_name.endswith("leb"):
        for actor in data.actors:
            free = ACTOR_INDICES.get(actor.type)
            if (free is not None and actor.parameters[4] not in (b"", 0)
                    and isinstance(actor.parameters[0], int) and actor.parameters[0] < 50):
                key = (actor.type, actor.parameters[3])
                if key not in _actor_index:
                    _actor_index[key] = free[sum(1 for t, _ in _actor_index if t == actor.type)]
                actor.parameters[0] = _actor_index[key]
    _original_write_file(self, file_name, data)


# UI.msbt entry -> shop name, filled from the patch file's location_names before the mod is built.
SHOP_NAMES: dict = {}
# Placeholder item keys that show another item's model: key -> (that item's key, its npc key in items.yml).
VARIANTS: dict = {}
VARIANT_USES: dict = {}  # key -> number of locations showing it
# Indexed item key -> inventory bits for the starting items, lowest first (starting_bits)
STARTING_BITS: dict = {}

_original_get_with_animation = ItemGetManager.getWithAnimation
_original_write_file = FileManager.writeFile
FileManager.writeFile = _write_file
_original_write_chest_event = ChestRandomizer.writeChestEvent
ItemGetManager.getWithAnimation = _placeholder_get_first
ChestRandomizer.writeChestEvent = _chest_placeholder_branch
_original_add_custom_flags = FlagManager.addCustomFlags
_original_make_item_changes = ItemsDatasheetFixes.makeItemChanges
_original_add_text_events = Keysanity.addTextEvents
_original_create_text = Keysanity.createText
_original_get_without_animation = ItemGetManager.getWithoutAnimation
ItemGetManager.getWithoutAnimation = _starting_index
_original_item_drop_changes = ItemDropDatasheetFixes.makeDatasheetChanges
ItemDropDatasheetFixes.makeDatasheetChanges = _drop_buffs
FlagManager.addCustomFlags = _add_ap_flags
ItemsDatasheetFixes.makeItemChanges = _add_placeholder_row
Keysanity.addTextEvents = _add_placeholder_text_event
Keysanity.createText = _add_placeholder_text


def _chest_textures(chest_bfres: str, folder: str, textures) -> None:
    """LASR's chest textures ("Texture + Size") come from the assets, into the folder LASR copies them from."""
    romfs = Path(chest_bfres).parents[2]
    Path(folder).mkdir(parents=True, exist_ok=True)
    for path in ap_assets.CHEST_TEXTURES:
        (Path(folder) / Path(path).name).write_bytes(ap_assets.load(romfs, path))


# The title screen and the chest textures are the same for every seed: assets (ap_assets.py), not built here.
mod_generator.TitleScreenFixes = lambda proc: None
lasr_chests.createChestBfresWithCustomTexturesIfMissing = _chest_textures


class APModsProcess(mod_generator.ModsProcess):
    def __init__(self, *args, seed_hash: str, location_names: dict, **kwargs):
        super().__init__(*args, **kwargs)
        self.seed_hash = seed_hash
        self.location_names = location_names
        self.failed = None
        self.error.connect(self._on_error)

    def _on_error(self, message: str) -> None:
        self.failed = message

    def makeConfig(self) -> None:
        super().makeConfig()
        # LASR writes full_direction and speed; the module reads full_360 and a speed per kind of movement.
        speed = MOVEMENT_SPEED if self.settings["Movement Speed"] else 1.0
        path = self.config_dir / "config.ini"
        text = path.read_text()
        text = text.replace("full_direction = ", f"full_360 = {self.settings['360 Movement']}\n"
                            f"walk_speed = {speed}\nswim_speed = {speed}\nfull_direction = ", 1)
        # Settings LASR lists but never implemented; the module does them.
        text += (f"\n[tweaks]\ninstant_text = {self.settings.get('Instant Text', False)}\n"
                 f"no_low_health_beep = {self.settings.get('Disable Low Health Beep', False)}\n")
        path.write_text(text + f"\n[archipelago]\nseed_hash = {self.seed_hash}\n")
        # What each location holds, so the found text names it before the client connects (DESIGN.md)
        with open(self.config_dir / "ap_names.txt", "w", encoding="utf-8", newline="\n") as f:
            for local_id, (item, owner) in sorted(self.location_names.items()):
                f.write(f"{local_id} {clean_name(item)}|{clean_name(owner)}\n")


def clean_name(text: str) -> str:
    """Names travel inside a line, separated by '|' (as in the client)."""
    return " ".join(text.replace("|", "/").split())


def load_patch(path: Path) -> dict:
    """An .aplas is a zip with patch.json (an Archipelago player container); a bare patch.json works too."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            return json.loads(zf.read("patch.json").decode("utf-8"))
    return json.loads(path.read_text(encoding="utf-8"))


def player_settings(text: str) -> dict:
    """The --settings JSON object: player settings only, with values of their kind."""
    try:
        values = json.loads(text)
    except ValueError as e:
        raise ValueError(f"--settings is not JSON: {e}") from None
    if not isinstance(values, dict):
        raise ValueError("--settings is not a JSON object")
    for key, value in values.items():
        if key not in PLAYER_SETTINGS:
            raise ValueError(f"--settings: {key} is no player setting")
        allowed = PLAYER_CHOICES.get(key, (False, True))
        if value not in allowed or type(value) is not type(PLAYER_SETTINGS[key]):
            raise ValueError(f"--settings: {key} must be one of {', '.join(map(str, allowed))}")
    return values


def load_yaml(name: str):
    with open(HERE / "RandomizerCore" / "Data" / name, "r") as f:
        return yaml.safe_load(f)


def build_placements(patch: dict, logic: dict) -> dict:
    """Converts the AP patch file into the placements dict LASR's ModsProcess expects."""
    settings = dict(patch["settings"])
    settings.setdefault("Excluded Locations", [])
    settings["Starting Gear"] = list(patch["starting_items"])

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
    for loc, item in patch["placements"].items():
        placements[loc] = item
        if item == "ap-item":
            placements["indexes"][loc] = int(patch["location_ids"][loc])
            placements[loc] = location_item(loc)
        elif item in INDEXED_ITEMS and "index" in logic.get(loc, {}):
            placements["indexes"][loc] = logic[loc]["index"]
        elif item.startswith(DUNGEON_ITEM_PREFIXES) and item[-1].isdigit():
            placements["indexes"][loc] = int(item[-1])
    return placements


def write_indices(config_dir: Path, logic: dict, patch: dict) -> None:
    """One line per seashell, heart piece, golden leaf and heart container location: its kind, its vanilla inventory
    bit and the local id of the location when it holds the placeholder, else -1. A location that keeps its vanilla
    item sets its bit when collected, so the module gives received items other bits; the Memories map marks bit i's
    vanilla place."""
    lines = []
    for loc, entry in logic.items():
        kind = INDEX_KINDS.get(entry.get("content"))
        if kind and entry.get("type") == "item" and "index" in entry:
            placeholder = patch["placements"].get(loc) == "ap-item"
            lines.append(f"{kind} {entry['index']} {int(patch['location_ids'][loc]) if placeholder else -1}")
    (config_dir / INDICES_FILE).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def starting_bits(logic: dict, patch: dict) -> dict:
    """Per indexed item key, the inventory bits no vanilla source of the seed sets (see write_indices), lowest first:
    the bits the module gives received items, which the starting items take first."""
    vanilla = {kind: set() for kind, _ in INDEX_BITS.values()}
    for loc, entry in logic.items():
        kind = INDEX_KINDS.get(entry.get("content"))
        if kind and entry.get("type") == "item" and "index" in entry and patch["placements"].get(loc) != "ap-item":
            vanilla[kind].add(entry["index"])
    return {key: [i for i in range(count) if i not in vanilla[kind]] for key, (kind, count) in INDEX_BITS.items()}


def write_vanilla(config_dir: Path, logic: dict, item_defs: dict, patch: dict) -> None:
    """One line per AP location that keeps its vanilla item. The Trendy Game's prizes: the item's key, its index (-1
    for none) and the location's local id; the module sends the check when the game adds that item. The companions:
    `F`, the flag their joining sets and the local id; the module sends the check once the flag is set."""
    lines = []
    for loc, local_id in patch["location_ids"].items():
        item = patch["placements"].get(loc)
        if loc in COMPANION_FLAGS:
            lines.append(f"F {COMPANION_FLAGS[loc]} {int(local_id)}")
        elif item != "ap-item":
            lines.append(f"{item_defs[item]['item-key']} {logic[loc].get('index', -1)} {int(local_id)}")
    (config_dir / VANILLA_FILE).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


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


def shop_name(names: list) -> str:
    item, owner = names
    text = f"{item} for {owner}" if owner else item
    return text if len(text) <= NAME_MAX else text[:NAME_MAX - 1] + "…"


def add_location_items(patch: dict, item_defs: dict) -> None:
    """Gives every AP location its own item definition: the model of the item it holds (`location_models`),
    shown by the location and by the item-get, its chest's size and texture (`location_chests`, for LASR's
    "Chest Types"), and in the shop a price tag naming the item."""
    names = patch.get("location_names", {})
    models = patch.get("location_models", {})
    chests = patch.get("location_chests", {})
    for loc, item in patch["placements"].items():
        if item != "ap-item":
            continue
        item_def = dict(PLACEHOLDER_DEF)
        model = item_defs.get(models.get(loc, ""), {})
        if model.get("model-path") and not model["item-key"].startswith("$"):
            key = variant_key(model["item-key"])
            VARIANTS[key] = (model["item-key"], model["npc-key"])
            VARIANT_USES[key] = VARIANT_USES.get(key, 0) + 1
            item_def.update({"item-key": key, "model-path": model["model-path"], "model-name": model["model-name"],
                             "npc-key": model["npc-key"]})
        if loc in chests:
            item_def["type"], chest_type = chests[loc]
            if chest_type:
                item_def["chest-type"] = chest_type
            else:
                item_def.pop("chest-type", None)
        if loc in SHOP_LOCATIONS and loc in names:
            key = f"ItemName_AP_{loc}"
            SHOP_NAMES[key] = shop_name(names[loc]).replace("[", "(").replace("]", ")")
            item_def["message"] = f"UI:{key}"
        item_defs[location_item(loc)] = item_def


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("patch", type=Path)
    parser.add_argument("--romfs", type=Path, required=True, help="RomFS dump of the base game (1.0.0)")
    parser.add_argument("--out", type=Path, required=True, help="Output folder (SD card root layout)")
    parser.add_argument("--settings", default="{}", help="JSON object of player settings, e.g. "
                                                         "'{\"Music\": \"Shuffled\"}'")
    args = parser.parse_args(argv)

    patch = load_patch(args.patch)
    if patch.get("format") != PATCH_FORMAT:
        print(f"Unsupported patch format {patch.get('format')}, expected {PATCH_FORMAT}")
        return 1
    try:
        patch["settings"].update(player_settings(args.settings))
    except ValueError as e:
        print(e)
        return 1
    if not (args.romfs / "region_common" / "event" / "PlayerStart.bfevfl").exists():
        print(f"{args.romfs} is not a Link's Awakening RomFS dump")
        return 1
    if not (HERE / "exl" / "subsdk9").exists():
        print("patcher/exl/subsdk9 is missing; build the exlaunch module first (scripts/build_exl.sh)")
        return 1

    item_defs = load_yaml("items.yml")["Item_Pool"]
    item_defs["ap-item"] = dict(PLACEHOLDER_DEF)
    add_location_items(patch, item_defs)
    logic = load_yaml("logic.yml")

    placements = build_placements(patch, logic)
    hints = patch.get("hints", {})
    ap_hints.prepare(hints)
    STARTING_BITS.update(starting_bits(logic, patch))
    seed = patch["seed_name"] + str(patch["player"])
    rng = random.Random(int(hashlib.sha256(seed.encode()).hexdigest()[:16], 16))

    proc = APModsProcess(placements, args.romfs, args.out, item_defs, seed, rng.getstate(),
                         seed_hash=patch["seed_hash"],
                         location_names={patch["location_ids"][loc]: names
                                         for loc, names in patch.get("location_names", {}).items()
                                         if loc in patch["location_ids"]})
    proc.run()
    if proc.failed:
        print(proc.failed)
        return 1
    write_indices(proc.config_dir, logic, patch)
    write_vanilla(proc.config_dir, logic, item_defs, patch)
    if "rooster-statue" in patch["location_ids"]:
        add_rooster_flag(proc.file_manager)
    if patch["settings"].get("Ammo Ghost", True):  # the option's default, also for patches without it
        ap_ghost.add(proc.file_manager)
    if hints:
        ap_hints.add(proc.file_manager, proc.config_dir, hints)
    # all eight for patches from before the option
    ap_goal.set_instrument_count(proc.file_manager, patch["settings"].get("Instruments Required", 8))
    map_tracker = patch["settings"].get("Map Tracker", False)  # off for patches from before the option
    if map_tracker:
        ap_map.add(proc.file_manager, proc.config_dir, patch["location_ids"], PLACEHOLDER_KEY)
    try:
        for path in (ap_assets.MODEL, ap_assets.UI) + ((ap_assets.GAME,) if map_tracker else ()):
            ap_assets.install(args.romfs, proc.romfs_dir, path)
    except ap_assets.AssetError as e:
        print(e)
        return 1
    print(f"Mod written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
