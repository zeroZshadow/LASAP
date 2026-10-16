from dataclasses import dataclass

from Options import (Choice, DeathLink, DefaultOnToggle, NamedRange, OptionGroup, PerGameCommonOptions, Range,
                     StartInventoryPool, Toggle)


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
    """Shuffle the rewards of the trading sequence. The Yoshi Doll that starts it stays in the Trendy Game, whose
    prizes are checks that are never shuffled."""
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
    """Shuffle the Rapids Ride: the race's rewards and the items on its west, middle and east islands (a heart piece,
    a chest and a seashell dig), and with owl statues on, its owl statue's gift."""
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
    default = 2


class Instruments(Choice):
    """Where the eight instruments of the Sirens are placed.
    vanilla: at the end of their dungeon. dungeon_rewards: at a dungeon end or with the Color Dungeon fairy.
    own_dungeon: inside their own dungeon. any_dungeon: inside any dungeon, the Color Dungeon included.
    anywhere: anywhere in the multiworld."""
    display_name = "Instruments"
    option_vanilla = 0
    option_dungeon_rewards = 1
    option_own_dungeon = 2
    option_anywhere = 3
    option_any_dungeon = 4
    default = 3


class StartingInstruments(Range):
    """Start with this many random instruments. With vanilla instruments, their dungeon ends hold random items."""
    display_name = "Starting Instruments"
    range_start = 0
    range_end = 8
    default = 0


class InstrumentsRequired(Range):
    """How many of the eight instruments open the Wind Fish's Egg. Starting instruments count toward it. The sign at
    the foot of the egg's stairs tells the number, so it can be left to chance: random, random-low, random-high or
    random-range-<min>-<max>."""
    display_name = "Instruments Required"
    range_start = 0
    range_end = 8
    default = 8


class ChestAppearance(Choice):
    """Whether a chest's look tells what it holds, as in the original randomizer.
    uniform: every chest looks the same. size: big chests hold important items. texture_and_size: also a texture
    for junk, keys, heart upgrades and seashells (blue). Other players' items count as important when they are
    progression."""
    display_name = "Chest Appearance"
    option_uniform = 0
    option_size = 1
    option_texture_and_size = 2
    default = 0


class StartingRupees(Range):
    """Rupees in Link's wallet when he gets out of bed."""
    display_name = "Starting Rupees"
    range_start = 0
    range_end = 999
    default = 100


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


class AmmoGhost(DefaultOnToggle):
    """A ghost in Tarin's house tops your bombs and arrows up to 15 when you talk to him. He shows up once you have
    bombs or the bow."""
    display_name = "Ammo Ghost"


class MapTracker(DefaultOnToggle):
    """The pause map shows where your unchecked locations are: an icon on the overworld map for each spot (hover it
    for the locations' names), and in dungeons the chests and a chest icon for each room with other checks. The
    whole overworld map and all dungeon rooms are revealed from the start."""
    display_name = "Map Tracker"


class InGameHints(Choice):
    """Owl statues and library books tell where your progression items are, in your world or another's.
    off: their original text. text: the hint text only. server: reading a hint also makes it a hint on the server,
    as !hint does, without costing hint points. Dungeon owl statues need their dungeon's stone beak."""
    display_name = "In-Game Hints"
    option_off = 0
    option_text = 1
    option_server = 2
    default = 2


class Stealing(Choice):
    """Whether logic may expect stealing from the shop.
    standard: only with a sword. always: with or without one. never: logic never expects it."""
    display_name = "Stealing"
    option_standard = 0
    option_always = 1
    option_never = 2
    default = 1


class Traps(NamedRange):
    """How many junk items (rupees and chamber stones, 50 rupees first) become traps.
    none: 0. few: 3. many: 17. trapsanity: every junk item. A number above the junk in the pool also turns all of it
    into traps. The 50 Rupees that take the place of Starting Gear stay rupees. Logic never needs rupee items: rupees
    for Mamu, the shop and the games come from cutting grass and defeating enemies, so with many traps a higher
    Starting Rupees saves grinding."""
    display_name = "Traps"
    range_start = 0
    range_end = 999
    default = 17
    special_range_names = {"none": 0, "few": 3, "many": 17, "trapsanity": 999}


class Damage(Choice):
    """How much damage Link takes. none: none at all, from enemies and traps. normal: as in the original game; a trap
    never takes the last quarter heart. ohko: any hit kills him."""
    display_name = "Damage"
    option_none = 0
    option_normal = 1
    option_ohko = 2
    default = 1


class MovementSpeed(DefaultOnToggle):
    """Link walks and swims 20% faster."""
    display_name = "Movement Speed"


class Movement360(Toggle):
    """Link walks in any direction instead of eight."""
    display_name = "360 Movement"


class SkipSongCutscenes(Toggle):
    """Skips the cutscenes of Marin's, Manbo's and Mamu's songs."""
    display_name = "Skip Song Cutscenes"


class InstantText(Toggle):
    """Each page of text shows at once instead of letter by letter; it still waits for a button."""
    display_name = "Instant Text"


class NiceHookshot(Toggle):
    """The hookshot grabs onto any surface. The logic does not count on it."""
    display_name = "Nice Hookshot"


class DisableGuardianAcorn(Toggle):
    """Enemies never drop a Guardian Acorn."""
    display_name = "Disable Guardian Acorn"


class DisablePieceOfPower(Toggle):
    """Enemies never drop a Piece of Power."""
    display_name = "Disable Piece of Power"


class EasyFishing(DefaultOnToggle):
    """At the fishing pond the line never breaks, and Ol' Baron bites more often."""
    display_name = "Easy Fishing"


class StartingGear(StartInventoryPool):
    """Start with the specified amount of these items and don't place them in the world; 50 Rupees take their place.
    The default is the original randomizer's starting gear. Each Heart Container adds a starting heart."""
    default = {"Progressive Sword": 1, "Progressive Shield": 1, "Ocarina": 1, "Manbo's Mambo": 1}


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
    instruments_required: InstrumentsRequired
    starting_rupees: StartingRupees
    open_mabe: OpenMabe
    open_kanalet: OpenKanalet
    open_mamu: OpenMamu
    completed_bridge: CompletedBridge
    classic_d2: ClassicD2
    free_book: FreeBook
    fast_fishing: FastFishing
    fast_stalfos: FastStalfos
    shuffled_bombs: ShuffledBombs
    shuffled_powder: ShuffledPowder
    ammo_ghost: AmmoGhost
    map_tracker: MapTracker
    in_game_hints: InGameHints
    stealing: Stealing
    chest_appearance: ChestAppearance
    traps: Traps
    damage: Damage
    movement_speed: MovementSpeed
    movement_360: Movement360
    skip_song_cutscenes: SkipSongCutscenes
    instant_text: InstantText
    nice_hookshot: NiceHookshot
    disable_guardian_acorn: DisableGuardianAcorn
    disable_piece_of_power: DisablePieceOfPower
    easy_fishing: EasyFishing
    death_link: DeathLink
    start_inventory_from_pool: StartingGear


OPTION_GROUPS = [
    OptionGroup("Locations", [Chests, FreeGifts, TradeQuest, GoldenLeaves, HeartPieces, Seashells, Miscellaneous, Shop,
                              BossDrops, Dampe, Rapids, Fishing, BlueRupees, OwlStatues, SeashellMansion]),
    OptionGroup("Goal", [InstrumentsRequired]),
    OptionGroup("Dungeon Items", [Keys, DungeonItems, Instruments]),
    OptionGroup("Start", [StartingInstruments, StartingRupees]),
    OptionGroup("World", [OpenMabe, OpenKanalet, OpenMamu, CompletedBridge, ClassicD2, FreeBook, FastFishing,
                          FastStalfos, ShuffledBombs, ShuffledPowder, AmmoGhost, MapTracker, InGameHints, Stealing,
                          ChestAppearance]),
    OptionGroup("Gameplay", [Damage, MovementSpeed, Movement360, SkipSongCutscenes, InstantText, NiceHookshot,
                             DisableGuardianAcorn, DisablePieceOfPower, EasyFishing]),
]
