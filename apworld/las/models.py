"""Which LAS model a location shows for the item it holds. Other games' items are matched by name against
data/item_models.yml; LAS items show their own model."""
import re
from typing import List, Tuple

from BaseClasses import Item, ItemClassification

from .items import GAME, ITEM_TABLE
from .lasr import ITEM_DEFS, _load_yaml

MODEL_RULES: List[Tuple[re.Pattern, str]] = [
    (re.compile(pattern, re.IGNORECASE), entry["model"])
    for entry in _load_yaml("item_models.yml") for pattern in entry["names"]
]


def has_model(lasr_item: str) -> bool:
    item_def = ITEM_DEFS.get(lasr_item, {})
    return bool(item_def.get("model-path")) and not item_def["item-key"].endswith("Trap")


def model_for_name(name: str) -> str:
    """The LASR item whose model shows for an item called `name`, or "" for the Archipelago model."""
    for pattern, model in MODEL_RULES:
        if pattern.search(name):
            return model
    return ""


def model_for(item: Item) -> str:
    if item.classification & ItemClassification.trap:
        return ""
    if item.game == GAME:
        lasr_item = ITEM_TABLE[item.name].lasr_name if item.name in ITEM_TABLE else ""
        return lasr_item if has_model(lasr_item) else ""
    return model_for_name(item.name)
