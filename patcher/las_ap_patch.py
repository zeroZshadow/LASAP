"""Builds the Link's Awakening (Switch) mod for one Archipelago slot.

python las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <output dir>

Runs LASR's mod generator (RandomizerCore, MIT) on the placements from the AP patch file
(format in DESIGN.md). Every location holding "ap-item" gives the Archipelago placeholder,
whose AddItemID index is the location's local id; the exlaunch module reports it as a check.
"""
import argparse
import hashlib
import json
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
# LASR resolves its data, resources and exl folder relative to sys.argv[0].
sys.argv[0] = str(HERE / "las_ap_patch.py")
sys.path.insert(0, str(HERE))

import yaml  # noqa: E402
from RandomizerCore import mod_generator  # noqa: E402
from RandomizerCore.Fixes.Datasheets.items import ItemsDatasheetFixes  # noqa: E402
from RandomizerCore.Helpers.flag_manager import FlagManager  # noqa: E402
from RandomizerCore.Randomizers.keysanity import Keysanity  # noqa: E402
from RandomizerCore.Tools import event_tools  # noqa: E402
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct  # noqa: E402

PATCH_FORMAT = 1
PLACEHOLDER_KEY = "ArchipelagoItem"
PLACEHOLDER_ITEM_ID = 71  # must match AP::PLACEHOLDER_ITEM_ID in the exlaunch module
RECV_FLAG_BITS = 16
PLACEHOLDER_TEXT = "You found an [COLOR_SKYBLUE]Archipelago item[COLOR_WHITE]!"

PLACEHOLDER_DEF = {
    "type": "junk",
    "quantity": 0,
    "item-key": PLACEHOLDER_KEY,
    "model-path": "ItemShellRader.bfres",
    "model-name": "ShellRader",
    "npc-key": "ItemShellRader",
    "message": "UI:ItemName_ShellRader",
    "chest-type": "junk",
}

# Item kinds whose in-game index LASR takes from logic.yml for vanilla placements.
INDEXED_ITEMS = ("seashell", "heart-piece", "heart-container", "bottle", "golden-leaf", "chamber-stone")
DUNGEON_ITEM_PREFIXES = ("compass", "map", "stone", "key", "nightmare")


def _add_ap_flags(self) -> None:
    _original_add_custom_flags(self)
    for i in range(RECV_FLAG_BITS):
        self.addFlag(f"APRecv{i}")


def _add_placeholder_row(self, sheet) -> None:
    _original_make_item_changes(self, sheet)
    base = next(item for item in sheet["values"] if item["symbol"] == "SmallKey")
    row = parseStruct(base)
    row["symbol"] = PLACEHOLDER_KEY
    row["itemID"] = PLACEHOLDER_ITEM_ID
    row["gettingFlag"] = ""
    row["npcKey"] = PLACEHOLDER_DEF["npc-key"]
    sheet["values"].append(dictToStruct(row))


def _add_placeholder_text_event(self) -> None:
    _original_add_text_events(self)
    flow = self.parent.file_manager.readFile("Item.bfevfl")
    event_tools.addEntryPoint(flow.flowchart, PLACEHOLDER_KEY)
    event_tools.createActionChain(flow.flowchart, PLACEHOLDER_KEY, [
        ("Dialog", "Show", {"message": f"Place:{PLACEHOLDER_KEY}"})
    ])
    self.parent.file_manager.writeFile("Item.bfevfl", flow)


def _add_placeholder_text(self) -> None:
    _original_create_text(self)
    for relative_path, msbt in self.parent.file_manager.getAllTextFiles("Place.msbt"):
        msbt.addEntry(PLACEHOLDER_KEY, PLACEHOLDER_TEXT)
        entry = msbt.getEntry(PLACEHOLDER_KEY)
        entry.message.text += "[1:4]"  # wait for input, as LASR's keysanity text does
        self.parent.file_manager.writeTextFile(relative_path, "Place.msbt", msbt)


_original_add_custom_flags = FlagManager.addCustomFlags
_original_make_item_changes = ItemsDatasheetFixes.makeItemChanges
_original_add_text_events = Keysanity.addTextEvents
_original_create_text = Keysanity.createText
FlagManager.addCustomFlags = _add_ap_flags
ItemsDatasheetFixes.makeItemChanges = _add_placeholder_row
Keysanity.addTextEvents = _add_placeholder_text_event
Keysanity.createText = _add_placeholder_text


class APModsProcess(mod_generator.ModsProcess):
    def __init__(self, *args, seed_hash: str, **kwargs):
        super().__init__(*args, **kwargs)
        self.seed_hash = seed_hash
        self.failed = None
        self.error.connect(self._on_error)

    def _on_error(self, message: str) -> None:
        self.failed = message

    def makeConfig(self) -> None:
        super().makeConfig()
        with open(self.config_dir / "config.ini", "a") as f:
            f.write(f"\n[archipelago]\nseed_hash = {self.seed_hash}\n")


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
        elif item in INDEXED_ITEMS and "index" in logic.get(loc, {}):
            placements["indexes"][loc] = logic[loc]["index"]
        elif item.startswith(DUNGEON_ITEM_PREFIXES) and item[-1].isdigit():
            placements["indexes"][loc] = int(item[-1])
    return placements


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("patch", type=Path)
    parser.add_argument("--romfs", type=Path, required=True, help="RomFS dump of the base game (1.0.0)")
    parser.add_argument("--out", type=Path, required=True, help="Output folder (SD card root layout)")
    args = parser.parse_args()

    patch = json.loads(args.patch.read_text(encoding="utf-8"))
    if patch.get("format") != PATCH_FORMAT:
        print(f"Unsupported patch format {patch.get('format')}, expected {PATCH_FORMAT}")
        return 1
    if not (args.romfs / "region_common" / "event" / "PlayerStart.bfevfl").exists():
        print(f"{args.romfs} is not a Link's Awakening RomFS dump")
        return 1
    if not (HERE / "exl" / "subsdk9").exists():
        print("patcher/exl/subsdk9 is missing; build the exlaunch module first (scripts/build_exl.sh)")
        return 1

    item_defs = load_yaml("items.yml")["Item_Pool"]
    item_defs["ap-item"] = dict(PLACEHOLDER_DEF)
    logic = load_yaml("logic.yml")

    placements = build_placements(patch, logic)
    seed = patch["seed_name"] + str(patch["player"])
    rng = random.Random(int(hashlib.sha256(seed.encode()).hexdigest()[:16], 16))

    proc = APModsProcess(placements, args.romfs, args.out, item_defs, seed, rng.getstate(),
                         seed_hash=patch["seed_hash"])
    proc.run()
    if proc.failed:
        print(proc.failed)
        return 1
    print(f"Mod written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
