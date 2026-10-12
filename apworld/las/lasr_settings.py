"""Maps the world's options to LASR settings: the patch file's settings dict, logic tokens, and location status."""
import re
from typing import Any, Dict, Iterable, List, Optional, Set

from .lasr import CATEGORIES, ITEM_DEFS, ITEM_LOCATIONS, LOGIC
from .options import LASOptions

# Option attribute -> LASR location category it toggles.
CATEGORY_OPTIONS = {
    "chests": "Chest_Locations",
    "free_gifts": "Free_Gifts",
    "trade_quest": "Trade_Gifts",
    "golden_leaves": "Golden_Leaves",
    "heart_pieces": "Heart_Pieces",
    "seashells": "Seashells",
    "miscellaneous": "Misc_Items",
    "shop": "Shop_Items",
    "boss_drops": "Boss_Locations",
    "dampe": "Dampe_Rewards",
    "rapids": "Rapids_Rewards",
    "fishing": "Fishing_Rewards",
    "blue_rupees": "Blue_Rupees",
}
MANSION_THRESHOLDS = {f"{n}-seashell-reward": n for n in (5, 15, 30, 40, 50)}

DUNGEONS = ("tail-cave", "bottle-grotto", "key-cavern", "angler-tunnel", "catfish-maw", "face-shrine", "eagle-tower",
            "turtle-rock", "color-dungeon")
DUNGEON_ENTRANCES = {d: d for d in DUNGEONS}  # dungeon entrance shuffle is not supported

_PRESETS = {0: "Basic", 1: "Advanced", 2: "Glitched", 3: "Hell"}
_DUNGEON_ITEMS = {0: "Own Dungeon", 1: "Anywhere", 2: "Start With"}
_INSTRUMENTS = {0: "Vanilla", 1: "Dungeon Rewards", 2: "Own Dungeon", 3: "Anywhere"}
_OWLS = {0: "None", 1: "Overworld", 2: "Dungeons", 3: "All"}
_STEALING = {0: "Standard", 1: "Always", 2: "Never"}
_TRAPS = {0: "None", 1: "Few", 2: "Many"}
_CHEST_TYPES = {0: "Default", 1: "Size", 2: "Texture + Size"}


def chest_look(options: LASOptions, lasr_item: Optional[str], progression: bool = False, useful: bool = False,
               trap: bool = False) -> List[Optional[str]]:
    """How a chest holding this item looks: [LASR item type, which sets the chest size; chest texture, None for the
    plain chest]. lasr_item is the LASR item for this player's items, None for another player's item, which looks
    by its classification. The types follow LASR's shuffler and its chest code for the "Size" setting."""
    if lasr_item is None:
        if trap or not (progression or useful):
            return ["junk", "junk"]
        return ["important" if progression else "good", None]
    item_type = ITEM_DEFS[lasr_item]["type"]
    if (lasr_item == "bomb" and options.shuffled_bombs) or (lasr_item == "powder" and options.shuffled_powder):
        item_type = "important"
    if lasr_item.startswith("key-") and options.keys.value == options.keys.option_anywhere:
        item_type = "important"
    if lasr_item.startswith("stone-beak-") and _OWLS[options.owl_statues.value] in ("Dungeons", "All"):
        item_type = "important"
    if options.chest_appearance.value == options.chest_appearance.option_size:
        if lasr_item in ("heart-container", "red-tunic", "blue-tunic"):
            item_type = "junk"
        elif lasr_item.startswith("key-") or lasr_item.endswith("-capacity"):
            item_type = "important"
    return [item_type, ITEM_DEFS[lasr_item].get("chest-type")]


def removed_locations(options: LASOptions) -> Set[str]:
    """Locations LASR deletes from its logic: owl statues that give no gift."""
    removed = set()
    if options.owl_statues.value not in (options.owl_statues.option_overworld, options.owl_statues.option_all):
        removed |= set(CATEGORIES["Overworld_Owl_Statues"])
    if options.owl_statues.value not in (options.owl_statues.option_dungeons, options.owl_statues.option_all):
        removed |= set(CATEGORIES["Dungeon_Owl_Statues"])
    return removed


def vanilla_locations(options: LASOptions) -> Set[str]:
    """Locations that keep their vanilla content (instruments are decided by the world)."""
    vanilla: Set[str] = set(CATEGORIES["Trendy_Rewards"])
    # LASR only shuffles the shop slots listed in Shop_Items
    vanilla |= {k for k in ITEM_LOCATIONS if LOGIC[k]["subtype"] == "shop" and k not in CATEGORIES["Shop_Items"]}
    for attribute, category in CATEGORY_OPTIONS.items():
        if not getattr(options, attribute).value:
            vanilla |= set(CATEGORIES[category])
    vanilla |= {loc for loc, n in MANSION_THRESHOLDS.items() if n > options.seashell_mansion.value}
    if not options.rapids:
        vanilla.add("owl-statue-rapids")  # LASR excludes it without the Rapids category
    return vanilla


def lasr_settings(options: LASOptions, vanilla: Iterable[str] = ()) -> Dict[str, Any]:
    """LASR's SettingsManager.fetch()["Settings"] for these options; unsupported settings keep LASR's defaults."""
    vanilla = set(vanilla)
    o = options
    settings: Dict[str, Any] = {
        "Preset": _PRESETS[o.logic.value],
        "Chests": bool(o.chests),
        "Free Gifts": bool(o.free_gifts),
        "Trade Quest": bool(o.trade_quest),
        "Golden Leaves": bool(o.golden_leaves),
        "Heart Pieces": bool(o.heart_pieces),
        "Seashells": bool(o.seashells),
        "Miscellaneous": bool(o.miscellaneous),
        "Seashell Mansion": o.seashell_mansion.value,
        "Shop": bool(o.shop),
        "Boss Drops": bool(o.boss_drops),
        "Companions": False,
        "Dampe": bool(o.dampe),
        "Rapids": bool(o.rapids),
        "Fishing": bool(o.fishing),
        "Trendy Game": False,
        "Blue Rupees": bool(o.blue_rupees),
        "Owl Gifts": _OWLS[o.owl_statues.value],
        "Dungeon Maps": _DUNGEON_ITEMS[o.dungeon_items.value],
        "Compasses": _DUNGEON_ITEMS[o.dungeon_items.value],
        "Stone Beaks": _DUNGEON_ITEMS[o.dungeon_items.value],
        "Small Keys": _DUNGEON_ITEMS[o.keys.value],
        "Nightmare Keys": _DUNGEON_ITEMS[o.keys.value],
        "Shuffle Instruments": _INSTRUMENTS[o.instruments.value],
        "Starting Instruments": o.starting_instruments.value,
        "Race Mode": False,
        "Required Dungeons": 5,
        "Create Spoiler Log": False,
        "Unrandomizer Mode": False,
        "Randomize Enemies": False,
        "Randomize Enemy Sizes": False,
        "Chest Types": _CHEST_TYPES[o.chest_appearance.value],
        "Open Kanalet": bool(o.open_kanalet),
        "Open Mabe": bool(o.open_mabe),
        "Open Mamu": bool(o.open_mamu),
        "Completed Bridge": bool(o.completed_bridge),
        "Classic D2": bool(o.classic_d2),
        "Shuffled Dungeons": False,
        "Consumable Drops": True,
        "Bad Pets": bool(o.bad_pets),
        "Fast Fishing": bool(o.fast_fishing),
        "Shuffled Bombs": bool(o.shuffled_bombs),
        "Free Book": bool(o.free_book),
        "Fast Stalfos": bool(o.fast_stalfos),
        "Shuffled Powder": bool(o.shuffled_powder),
        "Ammo Ghost": bool(o.ammo_ghost),  # read by the AP patcher (ap_ghost.py), not by LASR
        "Stealing": _STEALING[o.stealing.value],
        "Boss Cutscenes": True,
        "Song Cutscenes": False,
        "Movement Speed": True,
        "Chest Animations": True,
        "Key Animations": True,
        "Item Get Animations": False,
        "Item Pool": "Standard",
        "Traps": _TRAPS[o.traps.value],
        "Nice Bombs": True,
        "Nice Hookshot": False,
        "Nice Magic Rod": True,
        "Damage": "Normal",
        "Music": "Vanilla",
        "Randomize Sound Effects": False,
        "Randomize Text": False,
        "Randomize Environments": False,
        "Disable Low Health Beep": False,
        "360 Movement": False,
        "Blur Removal": False,
        "Instant Text": False,
        "Disable Guardian Acorn": False,
        "Disable Piece of Power": False,
        "Rupees": o.starting_rupees.value,
        "Containers": 0,
        "Pieces": 0,
    }
    # set by LASR's main window before shuffling
    settings["Seashells Important"] = any(loc not in vanilla for loc in CATEGORIES["Mansion"])
    settings["Trade Important"] = any(loc not in vanilla for loc in CATEGORIES["Trade_Gifts"])
    return settings


def logic_tokens(options: LASOptions) -> Dict[str, bool]:
    """Settings tokens as LASR's shuffler derives them: every enabled boolean setting, lower-cased with dashes.

    Tokens that are also logic node or item names are dropped: in LASR, enabling "Rapids" puts the rapids region
    into the starting access by accident.
    """
    settings = lasr_settings(options)
    tokens = {re.sub(" ", "-", k).lower(): True for k, v in settings.items() if v is True}
    if settings["Stealing"] in ("Always", "Standard"):
        tokens["can-steal"] = True
    if settings["Stealing"] == "Standard":
        tokens["stealing-needs-sword"] = True
    return {k: v for k, v in tokens.items() if k not in LOGIC and k not in ITEM_DEFS}
