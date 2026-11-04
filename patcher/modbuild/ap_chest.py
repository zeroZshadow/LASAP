"""The seashell chest: LASR's chest textures ("Texture + Size") with a blue one for chests holding a seashell. Its
model, ObjTreasureBoxSeashell.bfres, the patcher makes from the player's ObjTreasureBox.bfres
(patcher/ap_chest_model.py).
"""
from ap_chest_model import FILE_NAME  # noqa: F401  (the chest-type's model file, mod.py CHEST_FILES)

CHEST_TYPE = "seashell"  # chest-type of the seashell in LASR's item definitions
