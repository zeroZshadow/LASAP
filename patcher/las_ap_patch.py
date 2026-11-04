"""Makes the Link's Awakening (Switch) mod for one Archipelago slot.

python las_ap_patch.py <patch.aplas> --romfs <romfs dump> --out <output dir> [--settings <JSON object>]

The mod's RomFS is the same for every seed: the assets (ap_assets.py), which scripts/build_assets.py builds with LASR's
mod generator (RandomizerCore, MIT; patcher/modbuild) and the patcher applies to the player's RomFS dump. Every
location holding "ap-item" gives the Archipelago placeholder, whose AddItemID index is the location's local id; the
exlaunch module reports it as a check. What a seed changes goes to config.ini and the seed file, which the module reads
(format in DESIGN.md). --settings overrides the player settings (PLAYER_SETTINGS) the patch file carries.
"""
import argparse
import configparser
import io
import json
import random
import shutil
import sys
import zipfile
from pathlib import Path
from typing import List, Optional

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import yaml  # noqa: E402
import ap_assets  # noqa: E402
import ap_entrances  # noqa: E402
import ap_goal  # noqa: E402
import ap_hints  # noqa: E402
import ap_map  # noqa: E402
import ap_options  # noqa: E402
import ap_save_name  # noqa: E402
import ap_shuffles  # noqa: E402
import romfs_check  # noqa: E402
import seed_file  # noqa: E402

DATA_DIR = HERE / "data"  # LASR's items.yml and logic.yml (its RandomizerCore/Data)
EXL_DIR = HERE / "exl"  # the exlaunch module (scripts/build_exl.sh)
GAME_DIR = Path("atmosphere", "contents", "01006BB00C6F0000")  # the mod's RomFS and exefs, in the SD card's layout
CONFIG_DIR = Path("config", "lasr-exl")

PATCH_FORMAT = 1
PLACEHOLDER_KEY = "ArchipelagoItem"
# The model each AP location shows, which the module picks at runtime (the seed file's models; DESIGN.md, Item
# models). A location's actors name MODEL_TOKEN and its local id as their model file and model name, the same in every
# seed.
MODEL_TOKEN = "ap:"  # AP::MODEL_TOKEN
# LASR's Items.gsheet rows that stand in for a location's item (Fixes/Datasheets/items.py): Dampe's rewards, the
# shop's goods and the items NPCs show before they give them. The module shows the location's model for their ids.
STAND_IN_ROWS = {"Dampe1": "dampe-page-1", "DampeHeart": "dampe-heart-challenge", "Dampe2": "dampe-page-2",
                 "DampeBottle": "dampe-bottle-challenge", "DampeFinal": "dampe-final",
                 "ShopShovel": "shop-slot3-1st", "ShopBow": "shop-slot3-2nd", "ShopHeart": "shop-slot6",
                 "FishNecklace": "bay-fisherman", "SyrupPowder": "syrup", "WalrusShell": "walrus"}
# LASR's sunken seashells, which it makes heart piece actors (Randomizers/seashells.py, randomizeStaticShells)
SUNKEN_SHELLS = ("kanalet-moat-north", "taltal-sunken")
HEART_MODELS = ("HeartPiece", "HeartContainer")  # the models LASR shows unshrunk at heart piece places
# Items counted by inventory bits, as the module names them in the seed file's indices: kind letter per item
INDEX_KINDS = {"seashell": "S", "heart-piece": "H", "golden-leaf": "L", "heart-container": "C", "bottle": "B",
               "chamber-stone": "P"}
# The companions' locations and the flag their joining sets
COMPANION_FLAGS = {"moblin-cave": "BowWowJoin", "rooster-statue": "RoosterFoundFlag"}
DUNGEON_ITEM_PREFIXES = ("compass", "map", "stone", "key", "nightmare")
# What a new save starts with (the seed file's start; DESIGN.md, Start state): "I <item key> <dungeon index>" (-1 for none) per
# starting item, "R <rupees>", "F <flag>" per flag. The module grants the items once and sets START_ITEMS_FLAG, and
# sets the flags the first time Link stands outside Tarin's house while START_MARKER is unset, as LASR's start event
# does on his first exit. Both must match the module's.
START_ITEMS_FLAG = "APStartItems"
START_MARKER = "FirstClear"
# The flags LASR's start event sets for these settings
START_SETTING_FLAGS = {
    "Open Kanalet": ("GateOpen_Switch_KanaletCastle_01B",),
    "Completed Bridge": ("StickDrop",),  # the bridge's; Kiki uses another flag (trade_quest.py)
    "Open Mamu": ("MamuMazeClear",),
    "Randomize Enemies": ("AppearStairsFld10N", "AppearStairsFld11O"),  # the stairs under the Armos statues
    "Fast Stalfos": ("DoorOpen_Btl1_L05_05F", "DoorOpen_Btl2_L05_04H", "DoorOpen_Btl3_L05_01F"),
}  # and Boss Cutscenes: LASR's boss flags (ap_assets.data's boss_flags)
# LASR's Start With settings -> the kind of dungeon item (items.yml: <kind>-D<n>) each dungeon starts with
START_WITH_KINDS = {"Dungeon Maps": "map", "Compasses": "compass", "Stone Beaks": "stone-beak",
                    "Small Keys": "key", "Nightmare Keys": "nightmare-key"}
# A second copy of these starting items is their upgrade
START_UPGRADES = {"SwordLv1": "SwordLv2", "Shield": "MirrorShield", "PowerBraceletLv1": "PowerBraceletLv2"}
# Visual and audio settings: the player picks them when patching (--settings), over the values the patch file
# carries. Their default and, for choices, the values allowed. Settings that change how the game plays are options in
# the patch file only, so a seed plays the same for everyone (races).
PLAYER_SETTINGS = {
    "Blur Removal": False,
    "Music": "Vanilla",
    "Randomize Text": False,
    "Randomize Environments": False,
    "Disable Low Health Beep": False,
    "No 30 FPS Drops": "On emulators",
    "Instant Pickups": False,
}
PLAYER_CHOICES = {"Music": ("Vanilla", "Shuffled", "Removed"), "No 30 FPS Drops": ("On emulators", "Always", "Never")}
# No 30 FPS Drops as config.ini [tweaks] no_30fps_drops: the module finds out whether it runs on an emulator
NO_30FPS_DROPS = {"On emulators": "emulator", "Always": "true", "Never": "false"}
# Walking and swimming speed with Movement Speed on: LASR's single speed (dashing stays at 1.0)
MOVEMENT_SPEED = 1.2


def name_lines(location_names: dict) -> List[str]:
    """What each location holds, so the found text names it before the client connects (DESIGN.md)."""
    return [f"{local_id} {clean_name(item)}|{clean_name(owner)}"
            for local_id, (item, owner) in sorted(location_names.items())]


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
    with open(DATA_DIR / name, "r") as f:
        return yaml.safe_load(f)


def dungeon_item(item: str) -> Optional[tuple]:
    """A dungeon item's kind and the game's index of its dungeon, from LASR's `<kind>-D<number>` (D1 0 to D8 7, the
    Color Dungeon D0 9), as LASR's shuffler gives dungeon items; the module's AddItemID hook adds a dungeon item with
    an index to that dungeon. None for other items."""
    kind, _, number = item.rpartition("-D")
    if not number.isdigit() or not kind.startswith(DUNGEON_ITEM_PREFIXES):
        return None
    return kind, 9 if number == "0" else int(number) - 1


def index_lines(logic: dict, patch: dict) -> List[str]:
    """One line per seashell, heart piece, golden leaf, heart container, bottle and chamber stone location: its kind, its
    vanilla inventory bit and the local id of the location when it holds the placeholder, else -1. A location that keeps its
    vanilla item sets its bit when collected, so the module gives received items other bits; the Memories map marks
    bit i's vanilla place."""
    lines = []
    for loc, entry in logic.items():
        kind = INDEX_KINDS.get(entry.get("content"))
        if kind and entry.get("type") == "item" and "index" in entry:
            placeholder = patch["placements"].get(loc) == "ap-item"
            lines.append(f"{kind} {entry['index']} {int(patch['location_ids'][loc]) if placeholder else -1}")
    return lines


def start_lines(item_defs: dict, patch: dict) -> List[str]:
    """The start's lines: the patch's starting items, every dungeon's item of each Start With kind (the world leaves
    keys out of the starting items for them, and gives the other kinds both ways), the rupees and the flags. A dungeon
    item counts for its own dungeon; the module gives indexed items the bits received ones take."""
    settings = patch["settings"]
    kinds = [kind for setting, kind in START_WITH_KINDS.items() if settings.get(setting) == "Start With"]
    kind_of = lambda item: (dungeon_item(item) or (None,))[0]  # noqa: E731
    items = [item for item in patch["starting_items"] if kind_of(item) not in kinds]
    items += [item for item, item_def in item_defs.items() if kind_of(item) in kinds
              for _ in range(item_def.get("quantity", 1))]
    lines, given = [], []
    for item in items:
        key = item_defs[item]["item-key"]
        if key in START_UPGRADES and key in given:
            key = START_UPGRADES[key]
        given.append(key)
        lines.append(f"I {key} {dungeon_item(item)[1] if dungeon_item(item) else -1}")
    if settings.get("Rupees", 0) > 0:
        lines.append(f"R {settings['Rupees']}")
    data = ap_assets.data()
    setting_flags = {**START_SETTING_FLAGS, "Boss Cutscenes": data["boss_flags"]}
    flags = [*data["start_flags"], *(flag for setting, names in setting_flags.items() if settings.get(setting)
                                     for flag in names)]
    # START_MARKER last: the module sets them in this order, and a save holds all of them once it is set
    lines += [f"F {flag}" for flag in flags if flag != START_MARKER] + [f"F {START_MARKER}"]
    return lines


def marker_lines(patch: dict) -> List[str]:
    """The markers' lines: the map markers of the doors of shuffled dungeons."""
    shuffled = patch["settings"].get("Shuffled Dungeons", False)
    return ap_entrances.markers(patch["dungeon_entrances"]) if shuffled else []


def jump_lines(patch: dict) -> List[str]:
    """The jumps' lines: the doors of shuffled dungeons and, on a seashell hunt, the egg's door."""
    shuffled = patch["settings"].get("Shuffled Dungeons", False)
    return ((ap_entrances.jumps(patch["dungeon_entrances"]) if shuffled else []) +
            ap_goal.jumps(ap_goal.settings_seashells(patch["settings"])))


def shop_lines(patch: dict) -> List[str]:
    """Shop slots show the item's name on the price tag and in the buy dialog (System.msbt): the module writes it there.
    Each line: the slot's two labels (LASR's, ap_assets.data's shop_text) and its location's local id."""
    return [f"{tag} {buy} {int(patch['location_ids'][loc])}"
            for loc, (tag, buy) in ap_assets.data()["shop_text"].items() if loc in patch["location_ids"]]


def kept_lines(logic: dict, item_defs: dict, patch: dict) -> List[str]:
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
    return lines


def model_token(local_id) -> str:
    """What a location's actors name as their model file and model name."""
    return f"{MODEL_TOKEN}{int(local_id)}"


def shown_models(patch: dict, item_defs: dict) -> dict:
    """The AP locations whose item shows its own model (`location_models`) -> that item's definition; the others show
    the Archipelago model."""
    shown = {}
    for loc, name in patch.get("location_models", {}).items():
        model = item_defs.get(name, {})
        if (loc in patch["location_ids"] and patch["placements"].get(loc) == "ap-item" and model.get("model-path")
                and not model["item-key"].startswith("$")):
            shown[loc] = model
    return shown


def ground_transform(loc: str, model_name: str) -> tuple:
    """How LASR places an item actor that shows `model_name` at `loc`, against how it places one that shows the token,
    which it treats as any other model: (scale factor, yaw in degrees or None, rise)."""
    data = ap_assets.data()
    heart_place = loc in data["heart_rooms"] or loc in SUNKEN_SHELLS
    sunken = loc in data["heart_sunken"] or loc in SUNKEN_SHELLS
    shrunk = 0.75 if sunken else 0.55  # LASR's scale at a heart piece place for a model that is not a heart
    factor, rise = 1.0, 0.0
    if heart_place and model_name in HEART_MODELS:
        factor, rise = 1 / shrunk, (-0.25 if sunken else 0.0)
    if model_name in data["model_sizes"]:
        # a sunken seashell's scale becomes the size, a heart piece's is multiplied by it, the others' start at 1
        factor = data["model_sizes"][model_name] / (shrunk if loc in SUNKEN_SHELLS else 1.0)
    return factor, data["model_rotations"].get(model_name), rise


def model_lines(patch: dict, item_defs: dict) -> List[str]:
    """The models' lines: `M <local id> <item id> <model file> <model name>` per location whose item shows its own
    model (item id -1 where the item-get has no model for it: it holds up the Archipelago model), `S <local id> <scale
    factor> <yaw or -> <rise>` where LASR would place its actors otherwise than the token's, `I <row key> <item id>
    <local id>` per stand-in row of an AP location, and `C <local id> <size> <chest file>` per chest location whose
    chest is not the plain one (`location_chests`: its item's type and chest-type). Items.gsheet's rows and LASR's
    chests come from the mod's build (ap_assets.data)."""
    data = ap_assets.data()
    item_rows, chests = data["item_rows"], data["chests"]
    ids = patch["location_ids"]
    lines = []
    for loc, (item_type, chest_type) in patch.get("location_chests", {}).items():
        if loc not in chests["rooms"] or loc not in ids or patch["placements"].get(loc) != "ap-item":
            continue
        size, model = chest_look(patch["settings"], item_type, chest_type, chests)
        if (size, model) != (1.0, chests["files"]["default"]):
            lines.append(f"C {int(ids[loc])} {size:.4f} {model}")
    for loc, model in shown_models(patch, item_defs).items():
        item_id, npc_key = item_rows.get(model["item-key"], (-1, ""))
        lines.append(f"M {int(ids[loc])} {item_id if npc_key else -1} {model['model-path']} {model['model-name']}")
        scale, yaw, rise = ground_transform(loc, model["model-name"])
        if (scale, yaw, rise) != (1.0, None, 0.0):
            lines.append(f"S {int(ids[loc])} {scale:.4f} {'-' if yaw is None else f'{yaw:.1f}'} {rise:.4f}")
    for key, loc in STAND_IN_ROWS.items():
        if key in item_rows and loc in ids and patch["placements"].get(loc) == "ap-item":
            lines.append(f"I {key} {item_rows[key][0]} {int(ids[loc])}")
    return lines


def chest_look(settings: dict, item_type: str, chest_type: Optional[str], chests: dict) -> tuple:
    """How LASR's "Chest Types" shows a chest holding an item of this type and chest-type: (size, model file).
    `chests`: LASR's chest sizes by item type ("sizes") and model files by chest-type ("files")."""
    mode = settings.get("Chest Types", "Default")
    if mode == "Default":
        return 1.0, chests["files"]["default"]
    sizes = dict(chests["sizes"])
    if not settings.get("Seashells Important", True):
        sizes["seashell"] = sizes["junk"]
    if not settings.get("Trade Important", True):
        sizes["trade"] = sizes["junk"]
    return sizes[item_type], chests["files"][(mode == "Texture + Size" and chest_type) or "default"]


def patch_shape(patch: dict) -> dict:
    """What the mod's files depend on in a patch: per location its local id (None for none) and what it holds, the
    placeholder ("ap-item") for an AP location without its vanilla item and for a removed one (removed_location_ids)."""
    shape = {loc: [None, item] for loc, item in patch["placements"].items()}
    for loc, local_id in {**patch.get("removed_location_ids", {}), **patch["location_ids"]}.items():
        shape.setdefault(loc, [None, "ap-item"])[0] = int(local_id)
    return shape


def config_text(settings: dict, seed_hash: str, slot_name: str) -> str:
    """config.ini: LASR's sections, with full_360 and a speed per kind of movement as the module reads them, then the
    module's own sections."""
    speed = MOVEMENT_SPEED if settings["Movement Speed"] else 1.0
    config = configparser.ConfigParser()
    config["movement"] = {"full_360": settings["360 Movement"], "walk_speed": speed, "swim_speed": speed}
    config["nice_items"] = {"bombs": settings["Nice Bombs"], "hookshot": settings["Nice Hookshot"],
                            "rod": settings["Nice Magic Rod"]}
    config["blur_removal"] = {"enabled": settings["Blur Removal"]}
    config["damage"] = {"mode": settings["Damage"]}
    config["randomizer"] = {"enabled": True, "free_book": settings["Free Book"], "stealing": settings["Stealing"],
                            "enemies": settings["Randomize Enemies"], "enemy_sizes": settings["Randomize Enemy Sizes"]}
    text = io.StringIO()
    config.write(text)
    # Settings LASR lists but never implemented; the module does them.
    text.write(f"\n[tweaks]\ninstant_text = {settings.get('Instant Text', False)}\n"
               f"no_low_health_beep = {settings.get('Disable Low Health Beep', False)}\n"
               f"no_guardian_acorn = {settings.get('Disable Guardian Acorn', False)}\n"
               f"no_piece_of_power = {settings.get('Disable Piece of Power', False)}\n"
               f"easy_fishing = {settings.get('Easy Fishing', False)}\n"
               f"no_30fps_drops = {NO_30FPS_DROPS[settings.get('No 30 FPS Drops', 'On emulators')]}\n"
               f"instant_pickups = {settings.get('Instant Pickups', False)}\n")
    text.write(ap_options.config_lines(settings))
    seashells = ap_goal.settings_seashells(settings)
    # The goal's numbers: what the egg takes and its sign says, all eight instruments and no seashells for patches
    # from before the options; save_name is a new save's name (DESIGN.md, Goal, In-game text).
    text.write(f"\n[archipelago]\nseed_hash = {seed_hash}\n"
               f"seashell_hunt = {seashells is not None}\nseashells_required = {seashells or 0}\n"
               f"instruments_required = {settings.get('Instruments Required', 8)}\n"
               f"save_name = {ap_save_name.save_name(slot_name)}\n")
    return text.getvalue()


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
    problem = romfs_check.problem(args.romfs, romfs_check.sources(ap_assets.manifest()))
    if problem:
        print(problem)
        return 1
    if not (EXL_DIR / "subsdk9").exists():
        print("patcher/exl/subsdk9 is missing; build the exlaunch module first (scripts/build_exl.sh)")
        return 1
    if patch_shape(patch) != ap_assets.data()["shape"]:
        print("The patch's locations are not those of the mod this patcher makes: patch it with the patcher of the "
              "world version that made it.")
        return 1

    item_defs = load_yaml("items.yml")["Item_Pool"]
    logic = load_yaml("logic.yml")
    seed = patch["seed_name"] + str(patch["player"])
    location_names = {patch["location_ids"][loc]: names for loc, names in patch.get("location_names", {}).items()
                      if loc in patch["location_ids"]}
    sections = {
        "names": name_lines(location_names),
        "indices": index_lines(logic, patch),
        "kept": kept_lines(logic, item_defs, patch),
        "models": model_lines(patch, item_defs),
        "start": start_lines(item_defs, patch),
        "shop": shop_lines(patch),
        "jumps": jump_lines(patch),
        "markers": marker_lines(patch),
        "shuffles": ap_shuffles.lines(patch["settings"], random.Random(f"{seed} shuffles")),
        "hints": ap_hints.lines(patch.get("hints", {})),
    }
    if patch["settings"].get("Map Tracker", False):  # off for patches from before the option
        # the activities the options leave out stay off the map, as in PopTracker
        optional = set(patch.get("optional_locations", ()))
        sections["map"] = ap_map.lines({loc: i for loc, i in patch["location_ids"].items() if loc not in optional},
                                       patch["dungeon_entrances"] if patch["settings"].get("Shuffled Dungeons") else None)
    game = args.out / GAME_DIR
    try:
        ap_assets.install_all(args.romfs, game / "romfs")
    except ap_assets.AssetError as e:
        print(e)
        return 1
    (game / "exefs").mkdir(parents=True, exist_ok=True)
    for name in ("main.npdm", "subsdk9"):
        shutil.copy(EXL_DIR / name, game / "exefs")
    config_dir = args.out / CONFIG_DIR
    config_dir.mkdir(parents=True, exist_ok=True)
    (config_dir / "config.ini").write_text(config_text(patch["settings"], patch["seed_hash"],
                                                       patch.get("slot_name", "")), encoding="utf-8")
    seed_file.write(config_dir, sections)
    print(f"Mod written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
