from RandomizerCore.Fixes.Datasheets.conditions import ConditionsDatasheetFixes
from RandomizerCore.Fixes.Datasheets.crane_game import CraneGameDatasheetFixes
from RandomizerCore.Fixes.Datasheets.item_drop import ItemDropDatasheetFixes
from RandomizerCore.Fixes.Datasheets.items import ItemsDatasheetFixes
from RandomizerCore.Fixes.Datasheets.npc import NpcDatasheetFixes


class DatasheetFixes:
    """Make changes to some datasheets that are general in nature and not tied to specific item placements"""

    def __init__(self, mod_generator):
        NpcDatasheetFixes(mod_generator)
        ItemDropDatasheetFixes(mod_generator)
        ItemsDatasheetFixes(mod_generator)
        ConditionsDatasheetFixes(mod_generator)
        CraneGameDatasheetFixes(mod_generator)
