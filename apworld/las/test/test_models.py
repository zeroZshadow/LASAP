import unittest

from ..models import MODEL_RULES, has_model, model_for_name

# Item names from other games and the model they should show ("" = the Archipelago model).
SAMPLES = {
    "Progressive Sword": "sword",
    "Kokiri Sword": "sword",
    "Hylian Shield": "shield",
    "Mirror Shield": "shield",
    "Progressive Shield": "shield",
    "Hookshot": "hookshot",
    "Longshot": "hookshot",
    "Progressive Bow": "bow",
    "Fairy Slingshot": "bow",
    "Arrows (10)": "arrow-capacity",
    "Bombs (10)": "bomb",
    "Bombchus (5)": "bomb",
    "Map Bombs": "bomb",
    "Map Width": "",
    "Fire Rod": "rod",
    "Pegasus Boots": "boots",
    "Hover Boots": "boots",
    "Roc's Feather": "feather",
    "Power Glove": "bracelet",
    "Progressive Glove": "bracelet",
    "Zora's Flippers": "flippers",
    "Ocarina of Time": "ocarina",
    "Song of Storms": "ocarina",
    "Magic Powder": "powder",
    "Heart Container": "heart-container",
    "Energy Tank": "heart-container",
    "Piece of Heart": "heart-piece",
    "Mask Shard": "heart-piece",
    "Boss Key (Forest Temple)": "nightmare-key-D1",
    "Big Key (Eastern Palace)": "nightmare-key-D1",
    "Small Key (Fire Temple)": "key-D1",
    "Small Key (Tail Cave)": "key-D1",
    "Map (Eastern Palace)": "map-D1",
    "Dungeon Map (Tail Cave)": "map-D1",
    "Compass (Water Temple)": "compass-D1",
    "Monkey": "",
    "Rupees (50)": "rupee-50",
    "Red Rupee": "rupee-20",
    "Rupees (300)": "rupee-300",
    "Rupee (1)": "rupee-5",
    "Money x128": "rupee-100",
    "Money x25": "rupee-20",
    "Money x48": "rupee-20",
    "Golden Coin": "rupee-5",
    "Geo Rock (420)": "rupee-300",
    "Bottle with Red Potion": "bottle",
    "Bottled Fairy": "bottle",
    "Blue Mail": "red-tunic",
    "Goron Tunic": "red-tunic",
    "Bombos Medallion": "",
    "Triforce Piece": "",
    "Missile": "",
    "Full Moon Cello": "full-moon-cello",
}


class TestModels(unittest.TestCase):
    def test_rules_name_items_with_models(self) -> None:
        self.assertEqual({model for _, model in MODEL_RULES if not has_model(model)}, set())

    def test_sample_names(self) -> None:
        self.assertEqual({name: model_for_name(name) for name in SAMPLES}, SAMPLES)
