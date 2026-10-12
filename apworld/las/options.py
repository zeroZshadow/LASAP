from dataclasses import dataclass

from Options import (Choice, DeathLink, DefaultOnToggle, OptionGroup, PerGameCommonOptions, Range, StartInventoryPool,
                     Toggle)


class Logic(Choice):
    """Which LASR logic preset the item placement must respect.
    basic: no tricks. advanced: some harder techniques. glitched / hell: glitches."""
    display_name = "Logic"
    option_basic = 0
    option_advanced = 1
    option_glitched = 2
    option_hell = 3
    default = 0


class Chests(DefaultOnToggle):
    """Shuffle the overworld chests. Dungeon chests are always shuffled."""
    display_name = "Chests"


class FreeGifts(DefaultOnToggle):
    """Shuffle the free gifts from NPCs (Marin, Manbo, Mamu, the Mad Batters, the walrus, the ghost, ...)."""
    display_name = "Free Gifts"


class TradeQuest(Toggle):
    """Shuffle the rewards of the trading sequence."""
    display_name = "Trade Quest"


class GoldenLeaves(DefaultOnToggle):
    """Shuffle the five golden leaves in Kanalet Castle."""
    display_name = "Golden Leaves"


class HeartPieces(DefaultOnToggle):
    """Shuffle the overworld pieces of heart."""
    display_name = "Heart Pieces"


class Seashells(DefaultOnToggle):
    """Shuffle the overworld secret seashells (dug up, in bushes, under rocks, ...)."""
    display_name = "Seashells"


class Miscellaneous(DefaultOnToggle):
    """Shuffle miscellaneous standing items: the washed-up sword, the ocarina, the bird key, the toadstool and the
    slime key."""
    display_name = "Miscellaneous"


class Shop(DefaultOnToggle):
    """Shuffle the shovel, bow and heart piece in the Town Tool Shop. The chamber stones stay vanilla."""
    display_name = "Shop"


class BossDrops(DefaultOnToggle):
    """Shuffle the items dropped by bosses (heart containers, the angler key, the face key, the hookshot)."""
    display_name = "Boss Drops"


class Dampe(Toggle):
    """Shuffle the rewards of Dampe's chamber challenges."""
    display_name = "Dampe"


class Rapids(Toggle):
    """Shuffle the rewards of the Rapids Ride."""
    display_name = "Rapids"


class Fishing(Toggle):
    """Shuffle the rewards of the fishing pond."""
    display_name = "Fishing"


class BlueRupees(Toggle):
    """Shuffle the 28 blue rupees of the Color Dungeon."""
    display_name = "Blue Rupees"


class OwlStatues(Choice):
    """Make owl statues give items. Each one adds a location and a 20 rupee item."""
    display_name = "Owl Statues"
    option_none = 0
    option_overworld = 1
    option_dungeons = 2
    option_all = 3
    default = 0


class SeashellMansion(Choice):
    """The Seashell Mansion rewards up to this seashell count are shuffled; the others stay vanilla."""
    display_name = "Seashell Mansion"
    option_0 = 0
    option_5 = 5
    option_15 = 15
    option_30 = 30
    option_40 = 40
    option_50 = 50
    default = 30


class DungeonItemPlacement(Choice):
    """own_dungeon: placed inside their own dungeon. anywhere: placed anywhere in the multiworld."""
    option_own_dungeon = 0
    option_anywhere = 1
    default = 0


class Keys(DungeonItemPlacement):
    """Where small keys and nightmare keys are placed.
    own_dungeon: inside their own dungeon. anywhere: anywhere in the multiworld."""
    display_name = "Keys"


class DungeonItems(DungeonItemPlacement):
    """Where dungeon maps, compasses and stone beaks are placed.
    own_dungeon: inside their own dungeon. anywhere: anywhere in the multiworld. start_with: given at the start."""
    display_name = "Maps, Compasses and Stone Beaks"
    option_start_with = 2


class Instruments(Choice):
    """Where the eight instruments of the Sirens are placed.
    vanilla: at the end of their dungeon. dungeon_rewards: at a dungeon end or with the Color Dungeon fairy.
    own_dungeon: inside their own dungeon. anywhere: anywhere in the multiworld."""
    display_name = "Instruments"
    option_vanilla = 0
    option_dungeon_rewards = 1
    option_own_dungeon = 2
    option_anywhere = 3
    default = 0


class StartingInstruments(Range):
    """Start with this many random instruments. With vanilla instruments, their dungeon ends hold random items."""
    display_name = "Starting Instruments"
    range_start = 0
    range_end = 8
    default = 0


class OpenMabe(DefaultOnToggle):
    """Opens up Mabe Village from the start of the game."""
    display_name = "Open Mabe"


class OpenKanalet(DefaultOnToggle):
    """The Kanalet Castle gate is always open."""
    display_name = "Open Kanalet"


class OpenMamu(DefaultOnToggle):
    """The Sign Maze is already completed."""
    display_name = "Open Mamu"


class CompletedBridge(DefaultOnToggle):
    """The bridge next to Kanalet Castle is already built."""
    display_name = "Completed Bridge"


class ClassicD2(Toggle):
    """Adds back the flower in front of Bottle Grotto that the remake replaced with a rock."""
    display_name = "Classic D2"


class BadPets(Toggle):
    """Companions can go inside dungeons with Link. Logic may then expect them to be used there."""
    display_name = "Bad Pets"


class FreeBook(DefaultOnToggle):
    """The book revealing the Wind Fish's Egg path can be read without the Magnifying Lens."""
    display_name = "Free Book"


class FastFishing(DefaultOnToggle):
    """Removes the instrument requirements for finding fish in the fishing pond."""
    display_name = "Fast Fishing"


class FastStalfos(DefaultOnToggle):
    """The first three Master Stalfos fights in Catfish's Maw are already completed."""
    display_name = "Fast Stalfos"


class ShuffledBombs(DefaultOnToggle):
    """Bombs can not be obtained until they are found as an item."""
    display_name = "Shuffled Bombs"


class ShuffledPowder(DefaultOnToggle):
    """Magic Powder can not be obtained until it is found as an item."""
    display_name = "Shuffled Powder"


class Stealing(Choice):
    """Whether logic may expect stealing from the shop.
    standard: only with a sword. always: with or without one. never: logic never expects it."""
    display_name = "Stealing"
    option_standard = 0
    option_always = 1
    option_never = 2
    default = 0


class Traps(Choice):
    """Replace some 50 rupee items with traps."""
    display_name = "Traps"
    option_none = 0
    option_few = 1
    option_many = 2
    default = 0


@dataclass
class LASOptions(PerGameCommonOptions):
    logic: Logic
    chests: Chests
    free_gifts: FreeGifts
    trade_quest: TradeQuest
    golden_leaves: GoldenLeaves
    heart_pieces: HeartPieces
    seashells: Seashells
    miscellaneous: Miscellaneous
    shop: Shop
    boss_drops: BossDrops
    dampe: Dampe
    rapids: Rapids
    fishing: Fishing
    blue_rupees: BlueRupees
    owl_statues: OwlStatues
    seashell_mansion: SeashellMansion
    keys: Keys
    dungeon_items: DungeonItems
    instruments: Instruments
    starting_instruments: StartingInstruments
    open_mabe: OpenMabe
    open_kanalet: OpenKanalet
    open_mamu: OpenMamu
    completed_bridge: CompletedBridge
    classic_d2: ClassicD2
    bad_pets: BadPets
    free_book: FreeBook
    fast_fishing: FastFishing
    fast_stalfos: FastStalfos
    shuffled_bombs: ShuffledBombs
    shuffled_powder: ShuffledPowder
    stealing: Stealing
    traps: Traps
    death_link: DeathLink
    start_inventory_from_pool: StartInventoryPool


OPTION_GROUPS = [
    OptionGroup("Locations", [Chests, FreeGifts, TradeQuest, GoldenLeaves, HeartPieces, Seashells, Miscellaneous, Shop,
                              BossDrops, Dampe, Rapids, Fishing, BlueRupees, OwlStatues, SeashellMansion]),
    OptionGroup("Dungeon Items", [Keys, DungeonItems, Instruments, StartingInstruments]),
    OptionGroup("World", [OpenMabe, OpenKanalet, OpenMamu, CompletedBridge, ClassicD2, BadPets, FreeBook, FastFishing,
                          FastStalfos, ShuffledBombs, ShuffledPowder, Stealing]),
]
