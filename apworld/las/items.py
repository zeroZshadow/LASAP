from typing import Dict, NamedTuple, Set, Tuple

from BaseClasses import Item, ItemClassification

from .lasr import DUNGEON_LEVEL, DUNGEON_NAMES, INSTRUMENTS, ITEM_DEFS, LOGIC, TRAPS, item_dungeon
from .logic import referenced_tokens
from .ids import ITEM_BASE, ITEM_OFFSETS

GAME = "Links Awakening Switch"


class LASItem(Item):
    game = GAME


_DISPLAY_NAMES: Dict[str, str] = {
    "sword": "Progressive Sword",
    "shield": "Progressive Shield",
    "bracelet": "Progressive Power Bracelet",
    "boots": "Pegasus Boots",
    "flippers": "Flippers",
    "feather": "Roc's Feather",
    "shovel": "Shovel",
    "hookshot": "Hookshot",
    "bow": "Bow",
    "rod": "Magic Rod",
    "boomerang": "Boomerang",
    "ocarina": "Ocarina",
    "song-ballad": "Ballad of the Wind Fish",
    "song-mambo": "Manbo's Mambo",
    "song-soul": "Frog's Song of Soul",
    "yoshi-doll": "Yoshi Doll",
    "ribbon": "Ribbon",
    "dog-food": "Dog Food",
    "bananas": "Bananas",
    "stick": "Stick",
    "honeycomb": "Honeycomb",
    "pineapple": "Pineapple",
    "hibiscus": "Hibiscus",
    "letter": "Goat's Letter",
    "broom": "Broom",
    "fishing-hook": "Fishing Hook",
    "necklace": "Mermaid's Necklace",
    "mermaid-scale": "Mermaid's Scale",
    "magnifying-lens": "Magnifying Lens",
    "golden-leaf": "Golden Leaf",
    "mushroom": "Sleepy Toadstool",
    "tail-key": "Tail Key",
    "slime-key": "Slime Key",
    "angler-key": "Angler Key",
    "face-key": "Face Key",
    "bird-key": "Bird Key",
    "full-moon-cello": "Full Moon Cello",
    "conch-horn": "Conch Horn",
    "sea-lilys-bell": "Sea Lily's Bell",
    "surf-harp": "Surf Harp",
    "wind-marimba": "Wind Marimba",
    "coral-triangle": "Coral Triangle",
    "evening-calm-organ": "Evening Calm Organ",
    "thunder-drum": "Thunder Drum",
    "seashell": "Secret Seashell",
    "bottle": "Bottle",
    "heart-container": "Heart Container",
    "bomb-capacity": "Bomb Capacity Upgrade",
    "arrow-capacity": "Arrow Capacity Upgrade",
    "powder-capacity": "Magic Powder Capacity Upgrade",
    "shell-sensor": "Seashell Sensor",
    "medicine": "Secret Medicine",
    "red-tunic": "Red Tunic",
    "blue-tunic": "Blue Tunic",
    "rupee-300": "300 Rupees",
    "heart-piece": "Piece of Heart",
    "rupee-50": "50 Rupees",
    "rupee-100": "100 Rupees",
    "chamber-stone": "Chamber Stone",
    "rupee-20": "20 Rupees",
    "rupee-5": "5 Rupees",
    "zol-trap": "Zol Trap",
    "zap-trap": "Zap Trap",
    "drown-trap": "Drown Trap",
    "squish-trap": "Squish Trap",
    "deathball-trap": "Deathball Trap",
    "quake-trap": "Quake Trap",
    "bomb": "Bombs",
    "powder": "Magic Powder",
    "stalfos-note": "Master Stalfos' Note",
}
_DUNGEON_ITEM_NAMES = {
    "key": "Small Key",
    "nightmare-key": "Nightmare Key",
    "stone-beak": "Stone Beak",
    "compass": "Compass",
    "map": "Dungeon Map",
}
_DISPLAY_NAMES.update({"bow-wow": "Bow-Wow", "rooster": "Flying Rooster"})


def display_name(lasr_item: str) -> str:
    if lasr_item in _DISPLAY_NAMES:
        return _DISPLAY_NAMES[lasr_item]
    dungeon = item_dungeon(lasr_item)
    kind = lasr_item[:-3]
    return f"{_DUNGEON_ITEM_NAMES[kind]} ({DUNGEON_NAMES[dungeon]})"


class ItemData(NamedTuple):
    lasr_name: str
    code: int
    classification: ItemClassification


# Everything the logic can ask for is progression, whatever its LASR pool type.
LOGIC_ITEMS: Set[str] = {t.split("[")[0] for t in referenced_tokens(LOGIC)} & set(ITEM_DEFS)


def _classify(lasr_item: str) -> ItemClassification:
    lasr_type = ITEM_DEFS[lasr_item]["type"]
    if lasr_item == "seashell":
        return ItemClassification.progression_skip_balancing
    if lasr_item in LOGIC_ITEMS or lasr_type in ("important", "trade"):
        return ItemClassification.progression
    if lasr_item.endswith("-trap"):
        return ItemClassification.trap
    if lasr_type in ("good", "important-health") or lasr_item == "heart-piece":
        return ItemClassification.useful
    return ItemClassification.filler


ITEM_TABLE: Dict[str, ItemData] = {
    display_name(k): ItemData(k, ITEM_BASE + ITEM_OFFSETS[k], _classify(k)) for k in ITEM_DEFS
}
LASR_TO_ITEM: Dict[str, str] = {data.lasr_name: name for name, data in ITEM_TABLE.items()}

# (game item key from items.yml, dungeon level for dungeon items or -1); the client sends these to the game.
ITEM_GAME_INFO: Dict[str, Tuple[str, int]] = {
    name: (ITEM_DEFS[data.lasr_name]["item-key"],
           DUNGEON_LEVEL[item_dungeon(data.lasr_name)] if item_dungeon(data.lasr_name) >= 0 else -1)
    for name, data in ITEM_TABLE.items()
}


def _group(predicate) -> Set[str]:
    return {name for name, data in ITEM_TABLE.items() if predicate(data.lasr_name)}


ITEM_NAME_GROUPS: Dict[str, Set[str]] = {
    "Instruments": _group(lambda i: i in INSTRUMENTS),
    "Songs": _group(lambda i: i.startswith("song-")),
    "Trade Items": _group(lambda i: ITEM_DEFS[i]["type"] == "trade"),
    "Dungeon Entrance Keys": _group(lambda i: i in ("tail-key", "slime-key", "angler-key", "face-key", "bird-key")),
    "Small Keys": _group(lambda i: i.startswith("key-")),
    "Nightmare Keys": _group(lambda i: i.startswith("nightmare-key-")),
    "Stone Beaks": _group(lambda i: i.startswith("stone-beak-")),
    "Compasses": _group(lambda i: i.startswith("compass-")),
    "Dungeon Maps": _group(lambda i: i.startswith("map-")),
    "Rupees": _group(lambda i: i.startswith("rupee-")),
    "Traps": _group(lambda i: i.endswith("-trap")),
    "Capacity Upgrades": _group(lambda i: i.endswith("-capacity")),
    "Tunics": _group(lambda i: i.endswith("-tunic")),
    "Sword": {"Progressive Sword"},
    "Shield": {"Progressive Shield"},
    "Power Bracelet": {"Progressive Power Bracelet"},
}
for _number, _dungeon in DUNGEON_NAMES.items():
    ITEM_NAME_GROUPS[f"{_dungeon} Items"] = _group(lambda i: item_dungeon(i) == _number)

FILLER_ITEM = "50 Rupees"
TRAP_ITEMS = tuple(LASR_TO_ITEM[t] for t in TRAPS)
