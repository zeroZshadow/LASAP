"""Maps the world's options to LASR settings: the patch file's settings dict, logic tokens, and location status."""
import re
from typing import Any, Dict, Iterable, List, Optional, Set

from .lasr import CATEGORIES, COMPANIONS, ITEM_DEFS, ITEM_LOCATIONS, LOGIC
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
# The categories of activities: left out, their locations are optional (optional_locations), not unrandomized.
OPTIONAL_CATEGORIES = {attribute: CATEGORY_OPTIONS[attribute] for attribute in ("trade_quest", "dampe", "rapids",
                                                                                "fishing")}
MANSION_THRESHOLDS = {f"{n}-seashell-reward": n for n in (5, 15, 30, 40, 50)}

_PRESETS = {0: "Basic", 1: "Advanced", 2: "Glitched", 3: "Hell"}
# own_world and different_world place them like anywhere does, as far as the patcher and the trackers are concerned
_DUNGEON_ITEMS = {0: "Own Dungeon", 1: "Anywhere", 2: "Start With", 3: "Any Dungeon", 4: "Anywhere", 5: "Anywhere"}
_INSTRUMENTS = {0: "Vanilla", 1: "Dungeon Rewards", 2: "Own Dungeon", 3: "Anywhere", 4: "Any Dungeon"}
_OWLS = {0: "None", 1: "Overworld", 2: "Dungeons", 3: "All"}
_STEALING = {0: "Standard", 1: "Always", 2: "Never"}
_TRAPS = {0: "None", 3: "Few", 999: "Trapsanity"}  # other counts are "Many"; LASR only tells "None" from the rest
_DAMAGE = {0: "None", 1: "Normal", 2: "OHKO"}
_CHEST_TYPES = {0: "Default", 1: "Size", 2: "Texture + Size"}
SEASHELL_CHEST = "seashell"  # the patcher's blue chest texture for seashells (ap_chest.CHEST_TYPE)


def chest_look(options: LASOptions, lasr_item: Optional[str], progression: bool = False, useful: bool = False,
               trap: bool = False) -> List[Optional[str]]:
    """How a chest holding this item looks: [LASR item type, which sets the chest size; chest texture, None for the
    plain chest]. lasr_item is the LASR item for this player's items, None for another player's item, which looks
    by its classification. The types follow LASR's shuffler and its chest code for the "Size" setting; seashells
    get the patcher's seashell texture."""
    if lasr_item is None:
        if trap or not (progression or useful):
            return ["junk", "junk"]
        return ["important" if progression else "good", None]
    item_type = ITEM_DEFS[lasr_item]["type"]
    if (lasr_item == "bomb" and options.shuffled_bombs) or (lasr_item == "powder" and options.shuffled_powder):
        item_type = "important"
    if lasr_item.startswith("key-") and _DUNGEON_ITEMS[options.small_keys.value] in ("Any Dungeon", "Anywhere"):
        item_type = "important"
    if lasr_item.startswith("stone-beak-") and _OWLS[options.owl_statues.value] in ("Dungeons", "All"):
        item_type = "important"
    if options.chest_appearance.value == options.chest_appearance.option_size:
        if lasr_item in ("heart-container", "red-tunic", "blue-tunic"):
            item_type = "junk"
        elif lasr_item.startswith("key-") or lasr_item.endswith("-capacity"):
            item_type = "important"
    return [item_type, SEASHELL_CHEST if lasr_item == "seashell" else ITEM_DEFS[lasr_item].get("chest-type")]


def removed_locations(options: LASOptions) -> Set[str]:
    """Locations LASR deletes from its logic: owl statues that give no gift."""
    removed = set()
    if options.owl_statues.value not in (options.owl_statues.option_overworld, options.owl_statues.option_all):
        removed |= set(CATEGORIES["Overworld_Owl_Statues"])
    if options.owl_statues.value not in (options.owl_statues.option_dungeons, options.owl_statues.option_all):
        removed |= set(CATEGORIES["Dungeon_Owl_Statues"])
    return removed


def fixed_locations(options: LASOptions) -> Set[str]:
    """Locations that keep their vanilla content and are no AP locations: the shop slots LASR does not shuffle (those
    not in Shop_Items), and the Color Dungeon's blue rupees with Blue Rupees off (off means vanilla, not locked checks:
    LASR leaves them plain rupees)."""
    fixed = {k for k in ITEM_LOCATIONS if LOGIC[k]["subtype"] == "shop" and k not in CATEGORIES["Shop_Items"]}
    if not options.blue_rupees:
        fixed |= set(CATEGORIES["Blue_Rupees"])
    return fixed


def kept_locations() -> Set[str]:
    """Locations the patcher cannot put the placeholder at: the Trendy Game's prizes and the companions. They are never
    randomized; the module reports them when the game adds their item or sets their flag."""
    return set(CATEGORIES["Trendy_Rewards"]) | set(COMPANIONS)


def optional_locations(options: LASOptions) -> Set[str]:
    """The locations of the activities the options leave out (OPTIONAL_CATEGORIES, the Seashell Mansion's rewards above
    seashell_mansion, the Rapids Ride's owl statue): checks the logic reaches only after the goal, excluded (junk and
    traps of any player), so no seed needs the activity."""
    optional: Set[str] = set()
    for attribute, category in OPTIONAL_CATEGORIES.items():
        if not getattr(options, attribute).value:
            optional |= set(CATEGORIES[category])
    optional |= {loc for loc, n in MANSION_THRESHOLDS.items() if n > options.seashell_mansion.value}
    if not options.rapids:
        optional.add("owl-statue-rapids")  # LASR excludes it without the Rapids category
    return optional - removed_locations(options)


def unrandomized_locations(options: LASOptions) -> Set[str]:
    """Locations whose default item stays, as the options do not randomize them (instruments are decided by the
    world). The world makes them checks holding that item, locked."""
    unrandomized: Set[str] = set()
    for attribute, category in CATEGORY_OPTIONS.items():
        if attribute not in OPTIONAL_CATEGORIES and not getattr(options, attribute).value:
            unrandomized |= set(CATEGORIES[category])
    return (unrandomized | kept_locations()) - fixed_locations(options)


# The settings trackers get in the slot data ("settings"): those that decide which checks exist and what reaches them.
TRACKER_SETTINGS = ("Preset", "Chests", "Free Gifts", "Trade Quest", "Golden Leaves", "Heart Pieces", "Seashells",
                    "Miscellaneous", "Seashell Mansion", "Shop", "Boss Drops", "Dampe", "Rapids", "Fishing",
                    "Trendy Game", "Blue Rupees", "Owl Gifts", "Dungeon Maps", "Compasses", "Stone Beaks", "Small Keys",
                    "Nightmare Keys", "Shuffle Instruments", "Open Kanalet", "Open Mabe", "Open Mamu",
                    "Completed Bridge", "Classic D2", "Shuffled Dungeons", "Fast Fishing", "Shuffled Bombs",
                    "Free Book", "Fast Stalfos", "Shuffled Powder", "Stealing")


def lasr_settings(options: LASOptions, vanilla: Iterable[str] = ()) -> Dict[str, Any]:
    """LASR's SettingsManager.fetch()["Settings"] for these options; unsupported settings keep LASR's defaults.
    The player settings (host_settings.PLAYER_SETTINGS, the visual and audio ones) carry their defaults: the client
    sets them when patching."""
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
        "Small Keys": _DUNGEON_ITEMS[o.small_keys.value],
        "Nightmare Keys": _DUNGEON_ITEMS[o.nightmare_keys.value],
        "Shuffle Instruments": _INSTRUMENTS[o.instruments.value],
        "Starting Instruments": o.starting_instruments.value,
        "Race Mode": False,
        "Required Dungeons": 5,
        "Create Spoiler Log": False,
        "Unrandomizer Mode": False,
        "Randomize Enemies": bool(o.randomize_enemies),
        "Randomize Enemy Sizes": bool(o.randomize_enemy_sizes),
        "Bouncy Items": bool(o.bouncy_items),  # read by the AP patcher (ap_drop.py) and the module, not by LASR
        "Chest Types": _CHEST_TYPES[o.chest_appearance.value],
        "Open Kanalet": bool(o.open_kanalet),
        "Open Mabe": bool(o.open_mabe),
        "Open Mamu": bool(o.open_mamu),
        "Completed Bridge": bool(o.completed_bridge),
        "Classic D2": bool(o.classic_d2),
        "Shuffled Dungeons": bool(o.shuffled_dungeons),
        "Consumable Drops": True,
        "Bad Pets": False,  # not supported
        "Fast Fishing": bool(o.fast_fishing),
        "Shuffled Bombs": bool(o.shuffled_bombs),
        "Free Book": bool(o.free_book),
        "Fast Stalfos": bool(o.fast_stalfos),
        "Shuffled Powder": bool(o.shuffled_powder),
        "Ammo Ghost": bool(o.ammo_ghost),  # read by the AP patcher (ap_ghost.py), not by LASR
        "Map Tracker": bool(o.map_tracker),  # read by the AP patcher (ap_map.py), not by LASR
        "Instruments Required": o.instruments_required.value,  # read by the AP patcher (ap_goal.py), not by LASR
        "Stealing": _STEALING[o.stealing.value],
        "Boss Cutscenes": True,
        "Song Cutscenes": bool(o.skip_song_cutscenes),  # on skips them
        "Chest Animations": True,
        "Key Animations": True,
        "Item Get Animations": False,  # not implemented by LASR
        "Item Pool": "Standard",
        "Traps": _TRAPS.get(o.traps.value, "Many"),
        "Nice Bombs": True,
        "Nice Hookshot": bool(o.nice_hookshot),
        "Nice Magic Rod": True,
        "Damage": _DAMAGE[o.damage.value],
        "Movement Speed": bool(o.movement_speed),
        "360 Movement": bool(o.movement_360),
        "Instant Text": bool(o.instant_text),  # done by the module; LASR never implemented it
        "Disable Guardian Acorn": bool(o.disable_guardian_acorn),
        "Disable Piece of Power": bool(o.disable_piece_of_power),
        "Easy Fishing": bool(o.easy_fishing),  # done by the module and the fishing spots; not a LASR setting
        # player settings
        "Music": "Vanilla",
        "Randomize Text": False,
        "Randomize Environments": False,
        "Blur Removal": False,
        "Disable Low Health Beep": False,  # done by the module; LASR never implemented it
        "No 30 FPS Drops": "On emulators",  # done by the module; not a LASR setting
        "Instant Pickups": False,  # done by the module; not a LASR setting
        # not implemented by LASR
        "Randomize Sound Effects": False,
        "Rupees": o.starting_rupees.value,
        "Containers": 0,
        "Pieces": 0,
    }
    # set by LASR's main window before shuffling
    settings["Seashells Important"] = any(loc not in vanilla for loc in CATEGORIES["Mansion"])
    # Off, the trade quest's rewards are optional AP locations, and no seed needs the trade items: LASR's chest code
    # gives the chests holding them the small size.
    settings["Trade Important"] = bool(o.trade_quest)
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
