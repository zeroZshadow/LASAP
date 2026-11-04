from RandomizerCore.Fixes.datasheets import DatasheetFixes
from RandomizerCore.Fixes.events import EventFixes
from RandomizerCore.Fixes.rooms import RoomFixes
from RandomizerCore.Helpers.item_info_manager import ItemInfoManager
from RandomizerCore.Helpers.item_get_manager import ItemGetManager
from RandomizerCore.Helpers.file_manager import FileManager
from RandomizerCore.Helpers.flag_manager import FlagManager
from RandomizerCore.Randomizers.seashell_mansion import SeashellMansionRandomizer
from RandomizerCore.Randomizers.heart_pieces import HeartPieceRandomizer
from RandomizerCore.Randomizers.instruments import InstrumentRandomizer
from RandomizerCore.Randomizers.trade_quest import TradeQuestRandomizer
from RandomizerCore.Randomizers.trendy_game import TrendyGameRandomizer
from RandomizerCore.Randomizers.free_gifts import FreeGiftsRandomizer
from RandomizerCore.Randomizers.miscellaneous import MiscRandomizer
from RandomizerCore.Randomizers.seashells import SeashellRandomizer
from RandomizerCore.Randomizers.fishing import FishingRandomizer
from RandomizerCore.Randomizers.owls import OwlStatueRandomizer
from RandomizerCore.Randomizers.small_keys import KeyRandomizer
from RandomizerCore.Randomizers.rapids import RapidsRandomizer
from RandomizerCore.Randomizers.chests import ChestRandomizer
from RandomizerCore.Randomizers.dampe import DampeRandomizer
from RandomizerCore.Randomizers.tarin import TarinRandomizer
from RandomizerCore.Randomizers.shop import ShopRandomizer
from pathlib import Path


class ModsProcess:
    def __init__(self, placements: dict, rom_path: Path, out_dir: Path, items: dict):
        self.item_defs = items
        self.placements = placements
        self.settings = self.placements.pop('settings')

        self.rom_path = rom_path
        self.romfs_dir = out_dir / "atmosphere" / "contents" / "01006BB00C6F0000" / "romfs"

        self.file_manager = FileManager(self)
        self.flag_manager = FlagManager(self)
        self.item_info_manager = ItemInfoManager(self)
        self.item_get_manager = ItemGetManager(self)


    def run(self):
        # Handle general fixes that are done regardless of item placements
        DatasheetFixes(self)
        EventFixes(self)
        RoomFixes(self)

        # Run all of our randomization classes
        TarinRandomizer(self)
        ChestRandomizer(self)
        FreeGiftsRandomizer(self)
        TradeQuestRandomizer(self)
        SeashellRandomizer(self)
        HeartPieceRandomizer(self)
        MiscRandomizer(self)
        SeashellMansionRandomizer(self)

        DampeRandomizer(self)
        RapidsRandomizer(self)
        FishingRandomizer(self)
        TrendyGameRandomizer(self)

        KeyRandomizer(self) # also handles the golden leaves
        InstrumentRandomizer(self)

        OwlStatueRandomizer(self)

        ShopRandomizer(self)
