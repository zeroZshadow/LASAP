import hashlib
import logging
import os
import random
import secrets
import threading
import weakref
from collections import Counter
from typing import Any, ClassVar, Dict, Iterable, List, Mapping, Optional, Set, TextIO, Tuple

from BaseClasses import (CollectionState, Entrance, EntranceType, Item, ItemClassification, Location,
                         LocationProgressType, Region, Tutorial)
from entrance_rando import EntranceRandomizationError, randomize_entrances
from Options import Accessibility, OptionError, PerGameCommonOptions
from Fill import fill_restrictive
from worlds.AutoWorld import WebWorld, World
from worlds.generic.Rules import add_item_rule
from worlds.LauncherComponents import Component, SuffixIdentifier, Type, components, launch as launch_component

from .container import LASContainer
from .hints import choose_hints, source_name
from .host_settings import LASSettings

from .ids import LOCATION_OFFSETS
from .items import FILLER_ITEM, GAME, ITEM_GAME_INFO, ITEM_NAME_GROUPS, ITEM_TABLE, LASR_TO_ITEM, LASItem
from .lasr import (CATEGORIES, COUNTED, DOORS, DUNGEON_POOL, DUNGEON_REWARD_LOCATIONS, DUNGEONS, ENTRANCE_POOL,
                   HOME_DOOR, HOUSE_DOOR, HOUSE_EXIT, INSTRUMENT_LOCATIONS, INSTRUMENTS, ITEM_DEFS, ITEM_LOCATIONS,
                   LOGIC, SHOP_LOCATIONS, START_DOORS, START_POCKETS, TRAPS, WARP_POINTS, PlacementError,
                   closed_dungeons, door_entrance,
                   door_label, door_node, door_of_label, door_region, dungeon_item_kind, dungeon_name, dungeon_region, entered_by_door,
                   goal_logic, item_dungeon, location_dungeon, shuffle_dungeons, slot_logic, start_pairs)

from .locations import LASR_LOCATIONS, LOCATION_NAME_GROUPS, LOCATION_NAME_TO_ID, LOCATION_NAMES, LASLocation
from .logic import PLACES, LogicCompiler, make_rule, referenced_regions
from .models import is_decoy, model_for
from .options import OPTION_GROUPS, DungeonItemPlacement, Goal, LASOptions, ShuffledDungeons, goal_instruments, goal_seashells
from .tracker import DOORS_KEY, ENTRANCES_KEY, SIGN_KEY, LogicTracker, tracked_instruments, tracked_seashells
from .ut_map import DOOR_STEPS, JUMP_LABELS, MAMBO, TRACKER_WORLD, door_out, held_warps, hidden_checks, warp_step
from . import lasr_settings


__all__ = ["LASWorld", "ITEM_GAME_INFO"]


def launch_client(*args: str) -> None:
    from .client import launch
    launch_component(launch, name="LAS Client", args=args)


components.append(Component("Links Awakening Switch Client", func=launch_client, game_name=GAME,
                            component_type=Type.CLIENT, supports_uri=True,
                            file_identifier=SuffixIdentifier(LASContainer.patch_file_ending)))


class LASWeb(WebWorld):
    theme = "grassFlowers"
    option_groups = OPTION_GROUPS
    tutorials = [Tutorial(
        "Multiworld Setup Guide",
        "Setting up Link's Awakening (Switch) for Archipelago.",
        "English",
        "setup_en.md",
        "setup/en",
        ["LASAP"],
    )]


GOAL_LOCATION = "Wind Fish"
GOAL_EVENT = "Victory"
PATCH_FORMAT = 1
EARLY_LOCATIONS_WANTED = 10
# Locations the opener's item must add for the fill to get going. With 2 or 3 the fill can be left a chain of single
# items in single spots, which it fails to build.
OPENER_GAIN = 4
PRE_FILL_ATTEMPTS = 10
PROGRESSION_CHECKS = 1  # fills per pre-fill try (_progression_fits): more catch little and cost a fill each
DOOR_MAP_ATTEMPTS = 100
START_ATTEMPTS = 10  # Random start: the starts tried before generation stops
DOOR_PLACEMENT_ATTEMPTS = 40  # Entrances: the generic entrance randomizer's runs, shared by the starts tried
OUTSIDE_DOORS, INSIDE_DOORS = 1, 2  # the randomizer's groups: an overworld door leads to an inside door and back
DOOR_GROUPS = {OUTSIDE_DOORS: [INSIDE_DOORS], INSIDE_DOORS: [OUTSIDE_DOORS]}
# the items traps replace, in this order; logic never needs them (rupees it needs come from farming)
JUNK = ("rupee-50", "rupee-20", "rupee-5", "rupee-100", "chamber-stone", "rupee-300")
# the options in the slot data ("options"), from which Universal Tracker regenerates the slot
SLOT_DATA_OPTIONS = tuple(name for name in LASOptions.type_hints if name not in PerGameCommonOptions.type_hints
                          and name not in ("start_inventory_from_pool", "plando_connections"))


def opens_up(start: int, best: int) -> bool:
    """`start`: locations reached at the start; `best`: with the pool's item that reaches the most."""
    return best >= start + OPENER_GAIN


class LASWorld(World):
    """
    The Legend of Zelda: Link's Awakening for Nintendo Switch. Shipwrecked on Koholint Island, Link must gather the
    eight instruments of the Sirens to wake the Wind Fish. Uses the logic of the LAS Randomizer.
    """
    game = GAME
    web = LASWeb()
    settings: ClassVar[LASSettings]
    options_dataclass = LASOptions
    options: LASOptions

    item_name_to_id = {name: data.code for name, data in ITEM_TABLE.items()}
    location_name_to_id = LOCATION_NAME_TO_ID
    item_name_groups = ITEM_NAME_GROUPS
    location_name_groups = LOCATION_NAME_GROUPS

    compiler: LogicCompiler
    goal_instruments: int  # the instruments the egg's rule takes (compile_logic)
    goal_seashells: Optional[int]  # the secret seashells the egg's rule takes, None for no hunt (compile_logic)
    shuffled: List[str]  # LASR item locations that are AP locations
    locked: List[str]  # AP locations the options leave unrandomized: they hold their default item, locked
    optional: List[str]  # AP locations of activities the options leave out: after the goal, excluded
    vanilla: List[str]  # LASR item locations that keep their vanilla content and are no AP locations
    starting_items: List[str]  # LASR item names given at the start by options
    pre_fill_items: List[Item]
    plando_doors: Dict[str, str] = {}  # the plando connections' doors and start (_plando)
    plando_start: Optional[str] = None
    opener: Optional[str] = None  # the item Archipelago's early fill places at the start (_ask_for_opener)
    _tracker_map: Optional[Tuple] = None  # the dungeon map and start the cached sweeps are for (_tracker)
    _trackers: Dict[Tuple, LogicTracker]
    replaced: Dict[str, str] = {}  # location -> the item it holds instead of its own (_replaced_by_starting_gear)
    restricted: Dict[str, Set[str]]  # AP item name -> LASR locations it must be placed in
    dungeon_entrances: Dict[str, str]  # each dungeon's door -> the dungeon it leads into (connect_entrances)
    entrances: Dict[str, str]  # Entrances: each shuffled overworld door -> its inside door (_randomize_doors)
    start: Optional[str]  # Random start: the door the house's exit leads out of (lasr.START_DOORS), None for Tarin's own
    door_logic: Dict[str, Dict[str, Any]]  # the logic with this slot's doors (lasr.door_logic)
    deferred: bool  # Universal Tracker: what the player finds in the game waits until it is found (generate_early)

    ut_can_gen_without_yaml = True  # Universal Tracker regenerates the slot from its slot data
    # Universal Tracker watches these data storage keys and passes their values to reconnect_found_entrances
    found_entrances_datastorage_key = (SIGN_KEY, DOORS_KEY, ENTRANCES_KEY)
    tracker_world: ClassVar[Dict[str, Any]] = TRACKER_WORLD  # Universal Tracker's map tab (ut_map.py)

    @staticmethod
    def interpret_slot_data(slot_data: Dict[str, Any]) -> Dict[str, Any]:
        """Universal Tracker passes the result back to generate_early as multiworld.re_gen_passthrough[GAME]."""
        return slot_data

    def generate_early(self) -> None:
        passthrough = getattr(self.multiworld, "re_gen_passthrough", {}).get(GAME)
        if passthrough:
            for name, value in passthrough["options"].items():
                setattr(self.options, name, LASOptions.type_hints[name].from_any(value))
        options = self.options
        if options.shuffled_dungeons == ShuffledDungeons.option_everything and not options.entrances:
            logging.warning(f"{self.player_name}: Shuffled Dungeons everything needs Entrances: shuffling the dungeons "
                            f"among themselves")
            options.shuffled_dungeons.value = ShuffledDungeons.option_dungeons
        if passthrough:
            self.dungeon_entrances = dict(passthrough["dungeon_entrances"])
        elif self.dungeon_map:
            self.dungeon_entrances = shuffle_dungeons(self.random)
        else:
            self.dungeon_entrances = {d: d for d in DUNGEONS}
        self.settings_tokens = lasr_settings.logic_tokens(options)
        # Universal Tracker defers what the player finds in the game, unless its enforce_deferred_entrances is off: a
        # count left to chance until the player reads the sign (tracked_instruments), the doors' entrances until the
        # player finds them (connect_entrances)
        mode = getattr(self.multiworld, "enforce_deferred_connections", "default")
        self.from_slot_data = bool(passthrough)
        self.deferred = bool(passthrough) and mode != "off"
        instruments, seashells = goal_instruments(options), goal_seashells(options)
        if passthrough:
            sign_read = not self.deferred
            instruments = tracked_instruments(instruments, passthrough["instruments_known"], sign_read)
            seashells = tracked_seashells(passthrough, sign_read)

        if passthrough:
            self.starting_instruments = list(passthrough["starting_instruments"])
        else:
            self.starting_instruments = self.random.sample(INSTRUMENTS, options.starting_instruments.value)
        self.starting_items = list(self.starting_instruments)
        self.different_world: Set[str] = set()
        for kind, placement in self.dungeon_item_placements().items():
            items = [item for item in ITEM_DEFS if dungeon_item_kind(item) == kind]
            if placement == placement.option_start_with:
                for item in items:
                    self.starting_items += [item] * ITEM_DEFS[item]["quantity"]
            elif placement == placement.option_own_world:
                options.local_items.value |= {LASR_TO_ITEM[item] for item in items}
            elif placement == placement.option_different_world:
                self.different_world |= {LASR_TO_ITEM[item] for item in items}
                options.non_local_items.value |= {LASR_TO_ITEM[item] for item in items}

        removed = lasr_settings.removed_locations(options)
        vanilla = lasr_settings.fixed_locations()
        unrandomized = lasr_settings.unrandomized_locations(options)
        if options.instruments == options.instruments.option_vanilla:
            started = {INSTRUMENT_LOCATIONS[INSTRUMENTS.index(i)] for i in self.starting_instruments}
            unrandomized |= set(INSTRUMENT_LOCATIONS) - started
        optional = lasr_settings.optional_locations(options)
        self.removed = [loc for loc in ITEM_LOCATIONS if loc in removed]
        self.vanilla = [loc for loc in ITEM_LOCATIONS if loc in vanilla and loc not in removed]
        self.shuffled = [loc for loc in ITEM_LOCATIONS if loc not in vanilla and loc not in removed]
        self.locked = [loc for loc in self.shuffled if loc in unrandomized]
        self.optional = [loc for loc in self.shuffled if loc in optional]
        self.pre_fill_items = []
        self.restricted = {}
        self.replaced = self._replaced_by_starting_gear()

        if passthrough:
            self.start = passthrough["start"]
            self.entrances = dict(passthrough["entrances"])
        else:
            self.plando_doors, self.plando_start = self._plando()
            self._place_start(instruments, seashells)
            self.entrances = {}  # with Entrances connect_entrances places the doors
        # with Entrances where the shuffled doors lead stays out of the rules (connect_entrances connects them)
        self.door_logic = slot_logic(self.dungeon_entrances, self.door_pool if self.options.entrances else (),
                                     self.start)
        self.compile_logic(instruments, seashells)

    @property
    def dungeon_map(self) -> bool:
        """Whether the dungeons' doors lead into each other at random (Shuffled dungeons dungeons)."""
        return self.options.shuffled_dungeons == ShuffledDungeons.option_dungeons

    @property
    def door_pool(self) -> List[str]:
        """The doors Entrances moves; with Shuffled dungeons everything the dungeons' doors too."""
        if self.options.shuffled_dungeons == ShuffledDungeons.option_everything:
            return DUNGEON_POOL
        return ENTRANCE_POOL

    def _plando(self) -> Tuple[Dict[str, str], Optional[str]]:
        """The plando connections: {overworld door: its inside door}, and the start door Tarin's House Exit leads out
        of. Connections the options do not allow are dropped with a warning; a door in two connections is an
        OptionError."""
        outside = set(DUNGEON_POOL)
        names = {name.lower(): name for name in [*outside, *(DOORS["pairs"][door] for door in outside), HOUSE_EXIT,
                                                 *START_DOORS]}
        doors: Dict[str, str] = {}
        start = None
        used: Set[str] = set()
        for connection in self.options.plando_connections.value:
            entrance, exit_ = (names[door_of_label(side).lower()] for side in (connection.entrance, connection.exit))
            if entrance == HOUSE_EXIT:
                if self.options.random_start:
                    start = exit_
                else:
                    logging.warning(f"{self.player_name}: Plando Connections: {HOUSE_EXIT} needs Random Start")
                continue
            if not self.options.entrances:
                logging.warning(f"{self.player_name}: Plando Connections: {entrance} needs Entrances")
                continue
            door, inside = (entrance, exit_) if entrance in outside else (exit_, entrance)
            if door == HOME_DOOR and not self.options.random_start:
                logging.warning(f"{self.player_name}: Plando Connections: {HOME_DOOR} needs Random Start")
                continue
            if door not in self.door_pool or inside not in {DOORS["pairs"][other] for other in self.door_pool}:
                logging.warning(f"{self.player_name}: Plando Connections: {door} with {inside} needs Shuffled Dungeons "
                                f"everything")
                continue
            for side in (door, inside):
                if side in used:
                    raise OptionError(f"{self.player_name}: Plando Connections: {side} is in two connections")
                used.add(side)
            doors[door] = inside
        if start in used:
            raise OptionError(f"{self.player_name}: Plando Connections: {start} is in two connections")
        return doors, start

    def _place_start(self, instruments: int, seashells: Optional[int]) -> None:
        """Rolls the Random start and the dungeon map: a start from which a map reaches every location and one item
        opens up (_start_opens_up). With Entrances connect_entrances picks the start with the doors."""
        self.start = None
        if not self.options.random_start or self.options.entrances:
            # with Entrances connect_entrances checks the map with the doors placed
            if not self._place_doors(instruments, seashells, self.dungeon_map and not self.options.entrances):
                raise PlacementError(f"{self.player_name}: Shuffled Dungeons: no dungeon map in {DOOR_MAP_ATTEMPTS} "
                                     f"tries reaches every location")
            return
        for start in self._starts():
            self.start = start
            # each start its share of the dungeon maps: a start no map suits wastes no more
            if self._place_doors(instruments, seashells, True, DOOR_MAP_ATTEMPTS // START_ATTEMPTS) and \
                    self._start_opens_up({}, instruments, seashells):
                return
        raise PlacementError(f"{self.player_name}: Random Start: no start in {START_ATTEMPTS} tries reaches every "
                             f"location and opens up with one item{self._plando_note()}")

    def _place_doors(self, instruments: int, seashells: Optional[int], must_reach: bool = False,
                     maps: int = DOOR_MAP_ATTEMPTS) -> bool:
        """Rolls the dungeon map until every location is reachable: a map can put items behind the doors they open.
        With Boss Drops and Free Gifts off, the Hookshot stays in Catfish's Maw and the Rooster at the weathervane, so
        Catfish's Maw must not be behind a door that needs one of them. False with must_reach when no map reaches every
        location."""
        for attempt in range(maps if self.dungeon_map else 1):
            if attempt:
                self.dungeon_entrances = shuffle_dungeons(self.random)
            if not (self.dungeon_map or must_reach) or self._reaches({}, instruments, seashells):
                return True
        return not must_reach

    def _reaches(self, entrances: Dict[str, str], instruments: int, seashells: Optional[int]) -> bool:
        """Whether every non-optional location is reachable with the whole pool and the starting items, the vanilla and
        locked locations giving theirs once reached: some items only exist where they stand (the rooster, the trade
        quest's)."""
        required = [loc for loc in self.shuffled if loc not in self.optional]
        tracker = self._tracker(required, entrances, instruments, seashells)
        held = self._pool()
        held.update(self.starting_items)
        items = [LASR_TO_ITEM[item] for item, count in held.items() if item in LASR_TO_ITEM for _ in range(count)]
        items += [item for item, count in self.options.start_inventory.value.items() for _ in range(count)]
        return len(tracker.in_logic(items)) == len(required)

    def _start_opens_up(self, entrances: Dict[str, str], instruments: int, seashells: Optional[int]) -> bool:
        """opens_up for these doors, over the progression items the opener can be: not a dungeon's own, not non-local.
        Only the world's own starting items count: start inventories are Main's to give, which WorldTestBase leaves
        out."""
        restricted = self._restricted_items()
        names = {LASR_TO_ITEM[item] for item, count in self._pool().items()
                 if count > 0 and item in LASR_TO_ITEM and self._is_progression(item)}
        names -= set(restricted) | set(self.options.non_local_items.value)
        starting = [LASR_TO_ITEM[item] for item in self.starting_items if item in LASR_TO_ITEM]
        start, reach = self._early_reach(entrances, instruments, seashells, starting, names, OPENER_GAIN)
        return start >= EARLY_LOCATIONS_WANTED or bool(start) and opens_up(start, max(reach.values(), default=0))

    def _early_reach(self, entrances: Dict[str, str], instruments: int, seashells: Optional[int],
                     starting: List[str], names: Iterable[str],
                     gain: Optional[int] = None) -> Tuple[int, Dict[str, int]]:
        """Fill locations (not optional, locked or excluded) reached with `starting` alone, and per item of `names`
        with that item besides; no items when the start reaches none or EARLY_LOCATIONS_WANTED, and with `gain` only up
        to the first that adds that many. The sweep uses the world's rules, so it works before the rules are set."""
        excluded = self.options.exclude_locations.value
        empty = [loc for loc in self.shuffled if loc not in self.optional and loc not in self.locked
                 and LOCATION_NAMES[loc] not in excluded]
        tracker = self._tracker(empty, entrances, instruments, seashells)
        start = len(tracker.in_logic(starting))
        if not start or start >= EARLY_LOCATIONS_WANTED:
            return start, {}
        reach = {}
        for name in sorted(names):
            reach[name] = len(tracker.in_logic(starting + [name]))
            if gain is not None and reach[name] >= start + gain:
                break
        return start, reach

    def _tracker(self, locations: List[str], entrances: Dict[str, str], instruments: int,
                 seashells: Optional[int]) -> LogicTracker:
        """The slot's sweep over these locations, the vanilla and locked locations giving their items once reached.
        Its rules depend on the dungeon map and the start, and on whether the overworld doors are shuffled, not on where
        they lead (LogicTracker.lead): one sweep per map and start serves every door placement's tries."""
        events = [loc for loc in self.unrandomized() if self._is_progression(self.content(loc))]
        slot = (tuple(sorted(self.dungeon_entrances.items())), self.start)
        if self._tracker_map != slot:
            self._tracker_map, self._trackers = slot, {}
        key = (tuple(locations), tuple(events), instruments, seashells, bool(entrances))
        tracker = self._trackers.get(key)
        if tracker is None:
            tracker = self._trackers[key] = LogicTracker(
                self.options.logic.current_key, self.settings_tokens, events, locations, self.dungeon_entrances, (),
                instruments, seashells, entrances, self.start)
        return tracker.lead(entrances)

    def _pool(self, events_only: bool = True) -> Counter:
        """The LASR items the pool holds before the starting items leave it: every item but those of the vanilla and
        locked locations (with `events_only` those whose items are events), and with Warp Points each warp point's item
        (item_pool takes junk out for them)."""
        held = Counter({item: definition["quantity"] for item, definition in ITEM_DEFS.items()})
        if self.options.warp_points:
            held.update(list(WARP_POINTS))
        for loc in self.unrandomized():
            if not events_only or self._is_progression(self.content(loc)):
                held[self.content(loc)] -= 1
        return held

    def dungeon_item_placements(self) -> Dict[str, DungeonItemPlacement]:
        """The option placing each kind of dungeon item, by the LASR item name without its dungeon (key-D1: key)."""
        options = self.options
        return {"key": options.small_keys, "nightmare-key": options.nightmare_keys, "map": options.dungeon_items,
                "compass": options.dungeon_items, "stone-beak": options.dungeon_items}

    def _replaced_by_starting_gear(self) -> Dict[str, str]:
        """Locked locations whose item is starting gear the pool would otherwise not hold: Archipelago takes starting
        gear out of the pool, so such a location holds 50 Rupees instead and its item joins the pool. The Trendy
        Game's prizes stay: the game gives them itself."""
        in_pool = self._pool(events_only=False)
        for item in self.starting_items:
            in_pool[item] -= 1
        kept = lasr_settings.kept_locations()
        replaced: Dict[str, str] = {}
        for name, count in sorted(self.options.start_inventory_from_pool.value.items()):
            if name not in ITEM_TABLE:
                continue
            lasr_item = ITEM_TABLE[name].lasr_name
            missing = count - max(0, in_pool[lasr_item])
            for loc in [loc for loc in self.locked if self.content(loc) == lasr_item and loc not in kept]:
                if missing <= 0:
                    break
                replaced[loc] = ITEM_TABLE[FILLER_ITEM].lasr_name
                missing -= 1
        return replaced

    # --- regions and locations ---

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        # LASR regions, rooms and the few nodes on reference cycles are AP regions. A place is entered from each region
        # that leads a way into it, by that way's rule (LogicCompiler.ways), and from Menu by the ways no region leads;
        # other nodes from Menu by their condition. The regions entered through a dungeon's door (lasr.door_logic) have
        # only that door's entrance, which connect_entrances connects.
        logic = self.compiler.logic
        roots = self.shuffled + self.vanilla + ["kill-nightmare"] + \
            [k for k, v in logic.items() if v["type"] == "region"]
        needed = set(self.compiler.dependencies(roots)) & self.compiler.region_nodes
        self.node_regions: Dict[str, Region] = {}
        for name in logic:
            if name in needed:
                self.node_regions[name] = Region(name, self.player, self.multiworld)
                self.multiworld.regions.append(self.node_regions[name])
        self.way_rules: Dict[str, Any] = {}  # entrance name -> its rule's expression (set_rules)
        entered = entered_by_door(self.door_pool)
        for name, region in self.node_regions.items():
            if name in entered:
                continue
            ways = self.compiler.ways(name) if logic[name]["type"] in PLACES else {}
            if not ways:
                ways = {None: self.compiler.node_expr(name)}
            for source, expr in ways.items():
                parent = menu if source is None else self.node_regions[source]
                entrance = parent.connect(region, f"To {name}" if source is None else f"{source} -> {name}")
                self.way_rules[entrance.name] = expr
        for dungeon in closed_dungeons(self.door_pool):
            self.node_regions[door_node(dungeon)].create_exit(door_entrance(dungeon))
        if self.options.entrances:  # each shuffled door's way through, by the door's name (connect_entrances)
            for door in self.door_pool:
                for side, group in ((door, OUTSIDE_DOORS), (DOORS["pairs"][door], INSIDE_DOORS)):
                    exit_ = self.node_regions[door_region(side)].create_exit(side)
                    exit_.randomization_type, exit_.randomization_group = EntranceType.TWO_WAY, group

        for loc in self.shuffled:
            region = self.node_regions[LOGIC[loc]["region"]]
            region.locations.append(LASLocation(self.player, LOCATION_NAMES[loc],
                                                self.location_name_to_id[LOCATION_NAMES[loc]], region))
        for loc in self.vanilla:
            if self._is_progression(self.content(loc)):
                region = self.node_regions[LOGIC[loc]["region"]]
                region.locations.append(LASLocation(self.player, LOCATION_NAMES[loc], None, region))
        menu.locations.append(LASLocation(self.player, GOAL_LOCATION, None, menu))

    def content(self, loc: str) -> str:
        return self.replaced.get(loc) or LOGIC[loc]["content"]

    def _is_progression(self, lasr_item: str) -> bool:
        return bool(self.classification(LASR_TO_ITEM[lasr_item]) & ItemClassification.progression)

    def classification(self, name: str) -> ItemClassification:
        return ITEM_TABLE[name].classification

    # --- items ---

    def create_item(self, name: str) -> LASItem:
        return LASItem(name, self.classification(name), ITEM_TABLE[name].code, self.player)

    def create_event(self, name: str, classification=ItemClassification.progression) -> LASItem:
        return LASItem(name, classification, None, self.player)

    def get_filler_item_name(self) -> str:
        return FILLER_ITEM

    def item_pool(self) -> Counter:
        """LASR item counts for the shuffled locations, following LASR's pool adjustments."""
        options = self.options
        pool = self._pool(events_only=False)
        for category in ("Overworld_Owl_Statues", "Dungeon_Owl_Statues"):
            pool["rupee-20"] += sum(1 for loc in CATEGORIES[category] if loc not in self.removed)
        for item in self.starting_items:
            if pool[item] > 0:
                pool[item] -= 1
                pool["rupee-50"] += 1
        # The original game's chests hold four Zol Traps. LASR trades three for 500 rupees when it adds traps; with no
        # traps the fourth goes too, for 50 rupees. Zol Traps in chests the options leave unrandomized stay there.
        trades = ("rupee-100", "rupee-100", "rupee-300") + (() if options.traps.value else ("rupee-50",))
        for replacement in trades[:pool["zol-trap"]]:
            pool["zol-trap"] -= 1
            pool[replacement] += 1
        if options.warp_points:
            # each warp point's item (in _pool) takes a junk item's place, before traps can take all of it
            for _ in WARP_POINTS:
                pool[next(junk for junk in JUNK if pool[junk] > 0)] -= 1
        if options.traps.value:
            # then purple rupees and other junk become traps
            count = options.traps.value
            for junk in JUNK:
                taken = min(count, pool[junk])
                pool[junk] -= taken
                count -= taken
                for _ in range(taken):
                    pool[self.random.choice(TRAPS)] += 1
        assert all(n >= 0 for n in pool.values()), pool
        filled = len(self.shuffled) - len(self.locked)
        assert sum(pool.values()) == filled, (sum(pool.values()), filled)
        return pool

    def create_items(self) -> None:
        for loc in self.vanilla:
            content = self.content(loc)
            if self._is_progression(content):
                item = self.create_event(LASR_TO_ITEM[content], ITEM_TABLE[LASR_TO_ITEM[content]].classification)
                self.get_location(LOCATION_NAMES[loc]).place_locked_item(item)
        for loc in self.locked:
            self.get_location(LOCATION_NAMES[loc]).place_locked_item(self.create_item(LASR_TO_ITEM[self.content(loc)]))
        self.get_location(GOAL_LOCATION).place_locked_item(self.create_event(GOAL_EVENT))

        for item in self.starting_items:
            self.multiworld.push_precollected(self.create_item(LASR_TO_ITEM[item]))

        self.restricted = self._restricted_items()
        # starting gear stays in the item pool, where Archipelago takes it out, also where the pre-fill would place it
        starting_gear = Counter(self.options.start_inventory_from_pool.value)
        for lasr_item, count in sorted(self.item_pool().items(), key=lambda kv: list(ITEM_DEFS).index(kv[0])):
            for _ in range(count):
                item = self.create_item(LASR_TO_ITEM[lasr_item])
                if starting_gear[item.name] > 0:
                    starting_gear[item.name] -= 1
                    self.multiworld.itempool.append(item)
                elif item.name in self.restricted and not self.from_slot_data:
                    self.pre_fill_items.append(item)
                else:
                    self.multiworld.itempool.append(item)
        if self.from_slot_data:
            # Universal Tracker never fills, and its item panel shows only the item pool's items: the pre-fill's
            # stay in it (above), and the locked locations' items and the starting items join them
            for lasr_item in [self.content(loc) for loc in self.locked] + list(self.starting_items):
                self.multiworld.itempool.append(self.create_item(LASR_TO_ITEM[lasr_item]))

    def _ask_for_opener(self) -> None:
        """Very few locations are reachable with nothing, so, like LASR's Tarin rule, one of them gets an item that
        opens up the island: one that makes EARLY_LOCATIONS_WANTED locations reachable (else one that opens the most),
        each item as likely however many copies the pool holds, not only starting gear and not non-local. Archipelago's
        early fill places it, outside the shop slots (only stealing gets those) when another starting location is
        free. Runs once the doors are placed, as they decide what the start reaches."""
        ours = [item for item in self.multiworld.itempool if item.player == self.player and item.advancement]
        gear = Counter(self.options.start_inventory_from_pool.value)
        counts = Counter(item.name for item in ours)
        names = [name for name, count in counts.items()
                 if count > gear[name] and name not in self.options.non_local_items.value]
        held = [item.name for item in self.multiworld.precollected_items[self.player]]  # what the slot starts with
        start, reach = self._early_reach(self.entrances, self.goal_instruments, self.goal_seashells, held, names)
        wanted = min(EARLY_LOCATIONS_WANTED, max(reach.values(), default=0))
        if wanted <= start:
            return
        self.opener = self.random.choice(sorted(name for name, count in reach.items() if count >= wanted))
        early = self.multiworld.local_early_items[self.player]
        early[self.opener] = max(early.get(self.opener, 0), 1)
        state = CollectionState(self.multiworld)
        state.sweep_for_advancements(locations=self.get_locations())
        spots = [loc for loc in self.get_locations() if loc.item is None and loc.address is not None
                 and loc.progress_type != LocationProgressType.EXCLUDED and loc.can_reach(state)]
        shop = [loc for loc in spots if LASR_LOCATIONS[loc.name] in SHOP_LOCATIONS]
        if len(shop) < len(spots):
            for loc in shop:
                add_item_rule(loc, lambda item, name=self.opener: item.player != self.player or item.name != name)

    def _restricted_items(self) -> Dict[str, Set[str]]:
        options = self.options
        restricted: Dict[str, Set[str]] = {}
        shuffled = set(self.shuffled) - set(self.locked)

        def in_dungeon(number: int) -> Set[str]:
            return {loc for loc in shuffled if location_dungeon(loc) == number}

        any_dungeon = {loc for loc in shuffled if location_dungeon(loc) >= 0}
        placements = self.dungeon_item_placements()
        for lasr_item in ITEM_DEFS:
            number = item_dungeon(lasr_item)
            if number < 0:
                continue
            placement = placements[dungeon_item_kind(lasr_item)]
            if placement == placement.option_own_dungeon:
                restricted[LASR_TO_ITEM[lasr_item]] = in_dungeon(number)
            elif placement == placement.option_any_dungeon:
                restricted[LASR_TO_ITEM[lasr_item]] = any_dungeon
        if options.instruments == options.instruments.option_dungeon_rewards:
            for instrument in INSTRUMENTS:
                restricted[LASR_TO_ITEM[instrument]] = shuffled & set(DUNGEON_REWARD_LOCATIONS)
        elif options.instruments == options.instruments.option_own_dungeon:
            for number, instrument in enumerate(INSTRUMENTS, 1):
                restricted[LASR_TO_ITEM[instrument]] = in_dungeon(number)
        elif options.instruments == options.instruments.option_any_dungeon:
            for instrument in INSTRUMENTS:
                restricted[LASR_TO_ITEM[instrument]] = any_dungeon
        return restricted

    def get_pre_fill_items(self) -> List[Item]:
        return self.pre_fill_items

    def connect_entrances(self) -> None:
        """Connects the dungeon doors and, with Entrances, the shuffled doors (_randomize_doors, or the slot data's
        pairs for Universal Tracker, which connects a door only once the player has found it when deferred). Then
        the opener."""
        if not (self.deferred and self.dungeon_map):
            self.connect_doors(DUNGEONS)
        if self.from_slot_data:  # the shuffled doors once the player has gone through them, when deferred
            if self.options.entrances:  # the house's at once: the player leaves it first
                self._connect_pairs([self.start or HOME_DOOR])
            if not self.deferred:
                self._connect_pairs(self.entrances)
            return
        if self.options.entrances:
            self.entrances = self._randomize_doors()
        self._ask_for_opener()

    def _connect_pairs(self, doors: Iterable[str]) -> None:
        """Connects these shuffled doors' entrances to the inside doors self.entrances pairs them with, and those
        inside doors' back; others and those connected already are left alone."""
        for door in doors:
            if door in self.entrances and self.get_entrance(door).connected_region is None:
                self._connect_pair(door, self.entrances[door])

    def _door_exits(self, fixed: Dict[str, str]) -> List[Entrance]:
        """The entrances the generic entrance randomizer places: each shuffled overworld door's and its inside door's,
        in door_pool's order, but for the fixed ones: the plando's (_plando) and the house's."""
        planned = set(fixed) | set(fixed.values())
        return [self.get_entrance(side) for door in self.door_pool for side in (door, DOORS["pairs"][door])
                if side not in planned]

    def _plando_note(self) -> str:
        return " with these plando connections" if self.plando_doors or self.plando_start else ""

    def _starts(self) -> List[Optional[str]]:
        """The start doors to try: Tarin's (None), or with Random start a random zone of START_DOORS but the pockets
        (START_POCKETS) each, then a random door of it that no plando connection names, up to START_ATTEMPTS; the
        plando's start alone (_plando)."""
        if not self.options.random_start:
            return [None]
        if self.plando_start:
            return [self.plando_start]
        planned = set(self.plando_doors)
        free = {door: zone for door, zone in START_DOORS.items() if door not in planned and zone not in START_POCKETS}
        zones = sorted(set(free.values()))
        self.random.shuffle(zones)
        return [self.random.choice(sorted(door for door, its in free.items() if its == zone))
                for zone in zones[:START_ATTEMPTS]]

    def _randomize_doors(self) -> Dict[str, str]:
        """{overworld door: its inside door}, placed coupled by Archipelago's generic entrance randomizer. Each door's
        target sits in its own door's region, as the randomizer pairs a way back by name. The randomizer stops checking
        on minimal accessibility once the goal is reachable and ignores what the start reaches, so a placement counts
        only when _reaches and, with Random start, _start_opens_up hold. Plando doors stay fixed; each start gets its
        share of the tries."""
        for door, inside in self.plando_doors.items():
            self._connect_pair(door, inside)
        pool = set(self.door_pool)
        starts = self._starts()
        problem = ""
        for start in starts:
            self.start = start
            house = {start or HOME_DOOR: HOUSE_DOOR}
            self._connect_pair(*next(iter(house.items())))
            exits = self._door_exits({**self.plando_doors, **house})
            for _ in range(DOOR_PLACEMENT_ATTEMPTS // len(starts)):
                targets = []
                for exit_ in exits:
                    target = self.node_regions[door_region(exit_.name)].create_er_target(exit_.name)
                    target.randomization_type = EntranceType.TWO_WAY
                    target.randomization_group = exit_.randomization_group
                    targets.append(target)
                try:
                    state = randomize_entrances(self, True, DOOR_GROUPS, er_targets=targets, exits=exits)
                except EntranceRandomizationError as error:
                    problem = str(error).split("\n")[0]
                    self._unplace_doors(exits)
                    continue
                placed = {**self.plando_doors, **house,
                          **{exit_: target for exit_, target in state.pairings if exit_ in pool}}
                if not self._reaches(placed, self.goal_instruments, self.goal_seashells):
                    problem = "a location is out of reach"
                elif start is None or self._start_opens_up(placed, self.goal_instruments, self.goal_seashells):
                    return placed
                else:
                    problem = "the start does not open up with one item"
                self._unplace_doors(exits)
            self._unplace_doors([self.get_entrance(side) for pair in house.items() for side in pair])
        raise PlacementError(f"{self.player_name}: Entrances: no door placement in {DOOR_PLACEMENT_ATTEMPTS} tries"
                             f"{self._plando_note()}: {problem}")

    def _connect_pair(self, door: str, inside: str) -> None:
        """Leads an overworld door into an inside door's region, and that inside door back into the door's."""
        self.get_entrance(door).connect(self.node_regions[door_region(inside)])
        self.get_entrance(inside).connect(self.node_regions[door_region(door)])

    def _unplace_doors(self, exits: List[Entrance]) -> None:
        """Undoes a door placement: the entrances lead nowhere again and their regions keep no targets."""
        for exit_ in exits:
            if exit_.connected_region is not None:
                exit_.connected_region.entrances.remove(exit_)
                exit_.connected_region = None
        for exit_ in exits:
            region = self.node_regions[door_region(exit_.name)]
            region.entrances[:] = [entrance for entrance in region.entrances if entrance.parent_region is not None]

    def connect_doors(self, doors: Iterable[str]) -> None:
        """Connects these doors' entrances (dungeon_entrances' names; others are ignored), and the last door's once
        every other is connected: where it leads is then known. A dungeon whose door is in the door pool has none."""
        entrances = {door: self.get_entrance(door_entrance(door)) for door in closed_dungeons(self.door_pool)}
        for door in doors:
            if door in entrances and entrances[door].connected_region is None:
                entrances[door].connect(self.node_regions[dungeon_region(self.dungeon_entrances[door])])
        hanging = [door for door, entrance in entrances.items() if entrance.connected_region is None]
        if len(hanging) == 1:
            self.connect_doors(hanging)

    # --- rules ---

    def _fit_different_world(self) -> None:
        """Lets the items placed in a different world stay non-local only as far as other players' worlds have room for
        them, as they fit nowhere else; the others are placed anywhere. A slot with minimal accessibility has no room to
        count on: the fill may lock its locations away after it placed items there. Before Archipelago's locality rules,
        which read non_local_items. Alone, Archipelago has already emptied non_local_items."""
        if self.multiworld.players == 1:
            return
        count = Counter(item.name for item in self.multiworld.itempool
                        if item.player == self.player and item.name in self.different_world)
        room = sum(1 for player in self.multiworld.player_ids
                   if player != self.player
                   and self.multiworld.worlds[player].options.accessibility != Accessibility.option_minimal
                   for location in self.multiworld.get_unfilled_locations(player)
                   if location.progress_type != LocationProgressType.EXCLUDED)
        if sum(count.values()) <= room:
            return
        names = sorted(count)
        self.random.shuffle(names)
        for name in names:
            if count[name] <= room:
                room -= count[name]
            else:
                self.options.non_local_items.value.discard(name)

    def set_rules(self) -> None:
        self._fit_different_world()

        def rule(expr):
            return make_rule(expr, self.player, self.node_regions)

        for name, expr in self.way_rules.items():
            entrance = self.get_entrance(name)
            entrance.access_rule = rule(expr)
            for other in referenced_regions(expr) - {entrance.parent_region.name}:
                self.multiworld.register_indirect_condition(self.node_regions[other], entrance)
        for loc in self.shuffled + self.vanilla:
            if loc in self.shuffled or self._is_progression(self.content(loc)):
                self.get_location(LOCATION_NAMES[loc]).access_rule = rule(self.compiler.node_expr(loc))
        # Optional locations are excluded: filler and traps of any player, Location.can_fill turns away the rest. Those
        # beyond the slot's own filler and traps (most categories off) take the slot's own items but small keys instead,
        # so a slot alone does not run short of filler.
        filler = sum(1 for item in self.multiworld.itempool
                     if item.player == self.player and not (item.advancement or item.useful))
        optional = [self.get_location(LOCATION_NAMES[loc]) for loc in self.optional]
        own = set(self.random.sample(optional, len(optional) - filler)) if len(optional) > filler else set()
        for location in optional:
            if location in own:
                location.item_rule = lambda item: item.player == self.player and \
                    not ITEM_TABLE[item.name].lasr_name.startswith("key-")
            else:
                location.progress_type = LocationProgressType.EXCLUDED
            location.access_rule = lambda state, reach=location.access_rule: \
                state.has(GOAL_EVENT, self.player) and reach(state)
        self.get_location(GOAL_LOCATION).access_rule = rule(self.compiler.token("kill-nightmare", 1))
        self.multiworld.completion_condition[self.player] = lambda state: state.has(GOAL_EVENT, self.player)

    # --- fill ---

    def pre_fill(self) -> None:
        if not self.pre_fill_items:
            return
        allowed = {name: {self.get_location(LOCATION_NAMES[loc]) for loc in locs}
                   for name, locs in self.restricted.items()}
        candidates: Set = set().union(*allowed.values())
        for location in candidates:
            original = location.item_rule
            location.item_rule = lambda item, location=location, original=original: (
                (item.player != self.player or item.name not in allowed or location in allowed[item.name])
                and original(item))

        state = self._others_state(own=True)

        def priority(item: Item) -> int:
            # fill_restrictive places from the end of the list: instruments, nightmare keys, small keys first
            lasr = ITEM_TABLE[item.name].lasr_name
            if lasr in INSTRUMENTS:
                return 3
            if lasr.startswith("nightmare-key-"):
                return 2
            return 1 if lasr.startswith("key-") else 0

        others = self._others_state()
        for attempt in range(PRE_FILL_ATTEMPTS):
            items = sorted(self.pre_fill_items, key=priority)
            locations = sorted(candidates, key=lambda loc: loc.name)
            self.random.shuffle(locations)
            fill_restrictive(self.multiworld, state, locations, items, single_player_placement=True, lock=True,
                             allow_excluded=True, name="LAS Dungeon Items")
            if attempt == PRE_FILL_ATTEMPTS - 1 or self._progression_fits(others):
                break
            for location in candidates:  # try another placement
                if location.item is not None and location.item.player == self.player and \
                        location.item.name in self.restricted:
                    location.item.location = None
                    location.item = None
                    location.locked = False

    def fill_hook(self, progitempool: List[Item], usefulitempool: List[Item], filleritempool: List[Item],
                  fill_locations: List[Location]) -> None:
        """Gives the different_world items their place before other items fill it (_fit_different_world counted room
        for them, and a small world has little). The fill takes progression from the end of the pool, so these go last.
        The others would come after all progression: they are placed now, in locations of other players."""
        non_local = self.different_world & self.options.non_local_items.value
        if not non_local:
            return
        def ours(item: Item) -> bool:
            return item.player == self.player and item.name in non_local
        progitempool.sort(key=ours)
        spots = [location for location in fill_locations if location.player != self.player]
        self.random.shuffle(spots)
        state = self.multiworld.state
        for pool in (usefulitempool, filleritempool):  # useful items first, as they fit fewer locations
            for item in [item for item in pool if ours(item)]:
                location = next((spot for spot in spots if spot.can_fill(state, item, check_access=False)), None)
                if location is not None:
                    self.multiworld.push_item(location, item, collect=False)
                    spots.remove(location)
                    fill_locations.remove(location)
                    pool.remove(item)

    def start_opens_up(self) -> bool:
        """Whether the fill can get going from where the game starts: not a pocket that takes two items to leave."""
        return self._start_opens_up(self.entrances, self.goal_instruments, self.goal_seashells)

    def _others_state(self, own: bool = False) -> CollectionState:
        """The other players' items, collected: their locations as open as their own items make them. With `own` this
        world's item pool too."""
        state = CollectionState(self.multiworld)
        for item in self.multiworld.itempool:
            if own or item.player != self.player:
                state.collect(item, prevent_sweep=True)

        for player in self.multiworld.player_ids:
            if player != self.player:
                for item in self.multiworld.worlds[player].get_pre_fill_items():
                    state.collect(item, prevent_sweep=True)
        state.sweep_for_advancements()
        return state

    def _progression_fits(self, others: CollectionState) -> bool:
        """Whether this world's progression items fit into the empty locations (other players' included, as open as
        `others` makes them) in a few random fills. With few locations a placement of the dungeon items can leave no
        fill that works. The opener goes first into a location the start reaches, as Archipelago's early fill does."""
        empty = list(self.multiworld.get_unfilled_locations())
        start = CollectionState(self.multiworld)
        start.sweep_for_advancements(locations=[loc for loc in self.multiworld.get_filled_locations()
                                                if loc.address is None])
        for _ in range(PROGRESSION_CHECKS):
            items = [item for item in self.multiworld.itempool if item.player == self.player and item.advancement]
            locations = list(empty)
            self.random.shuffle(locations)
            opener = next((item for item in items if item.name == self.opener), None)
            spot = opener and next((loc for loc in locations if loc.player == self.player and loc.can_reach(start)
                                    and loc.can_fill(start, opener, check_access=False)), None)
            if spot:
                spot.item, opener.location = opener, spot
                items.remove(opener)
                locations.remove(spot)
            fill_restrictive(self.multiworld, others, locations, items, allow_partial=True,
                             name="LAS Dungeon Items check")
            for location in empty:
                if location.item is not None:
                    location.item.location = None
                    location.item = None
            if items:
                return False
        return True

    # --- output ---

    def unrandomized(self) -> List[str]:
        return self.vanilla + self.locked

    def seed_hash(self) -> str:
        return hashlib.sha256(f"{self.multiworld.seed_name}:{self.player}".encode()).hexdigest()[:8]

    def compile_logic(self, instruments_required: int, seashells_required: Optional[int]) -> None:
        self.goal_instruments = instruments_required
        self.goal_seashells = seashells_required
        logic = goal_logic(instruments_required, self.door_logic, seashells_required)
        self.compiler = LogicCompiler(logic, self.options.logic.current_key, self.settings_tokens, LASR_TO_ITEM,
                                      COUNTED)

    def explain_path(self, entrance: Entrance, state: CollectionState) -> Optional[List[Dict[str, str]]]:
        """Universal Tracker's /get_logical_path, per step of its way: only the doors Link goes through, each with the
        door it comes out of (ut_map.DOOR_STEPS), and the warps: onto the warp map by Manbo's Mambo or from a warp
        point, then ` -> ` the point Link comes out at, each point by its item's name. A slot without shuffled doors has
        every other step, as UT writes it."""
        warp = warp_step(entrance)
        if warp is not None:
            zone = entrance.parent_region if warp == "into" else entrance.connected_region
            if entrance.name == MAMBO:
                label = JUMP_LABELS[MAMBO]
            else:
                label = next((LASR_TO_ITEM[item] for item in held_warps(zone.name, state, self.player)), zone.name)
            if warp == "into":
                return [{"type": "color", "color": "blue", "text": label}]
            return [{"type": "text", "text": f" -> {label}"}]
        if not self.options.entrances and not closed_dungeons(self.door_pool):
            return []
        if entrance.name not in DOOR_STEPS:
            return None
        return [{"type": "color", "color": "blue", "text": door_label(entrance.name)},
                {"type": "text", "text": f" -> {door_label(door_out(entrance))}"}]

    def reconnect_found_entrances(self, key: str, value: Any) -> None:
        """Universal Tracker: a data storage key of found_entrances_datastorage_key changed. Once the player has read
        the sign (SIGN_KEY), the rules take the slot's own counts; the dungeon doors found (DOORS_KEY) and the
        overworld doors gone through (ENTRANCES_KEY) connect their entrances."""
        if not self.deferred:
            return
        if key.startswith(SIGN_KEY.partition("{")[0]):
            required = goal_instruments(self.options), goal_seashells(self.options)
            if value and (self.goal_instruments, self.goal_seashells) != required:
                self.compile_logic(*required)
                self.set_rules()
        elif key.startswith(DOORS_KEY.partition("{")[0]):
            self.connect_doors(value or {})
        elif key.startswith(ENTRANCES_KEY.partition("{")[0]):
            self._connect_pairs(value or {})

    # One random secret per multiworld, in every LAS slot's patch file: the ghost relay groups that multiworld's LAS
    # players by it (DESIGN.md, Ghosts). Not in the slot data, which anyone who can log into a slot reads.
    _ghost_secrets: ClassVar["weakref.WeakKeyDictionary[Any, str]"] = weakref.WeakKeyDictionary()
    _ghost_lock = threading.Lock()

    def ghost_secret(self) -> str:
        with self._ghost_lock:
            return self._ghost_secrets.setdefault(self.multiworld, secrets.token_hex(16))

    def fill_slot_data(self) -> Mapping[str, Any]:
        settings = lasr_settings.lasr_settings(self.options, self.unrandomized())
        return {
            "seed_hash": self.seed_hash(),
            "instruments_required": goal_instruments(self.options),
            # False for a count left to chance: trackers show it once the player read the sign (SIGN_KEY)
            "instruments_known": self.options.instruments_required.known or
                                 self.options.goal == Goal.option_seashell_hunt,
            "seashells_required": goal_seashells(self.options) or 0,
            "seashells_known": self.options.seashells_required.known or
                               self.options.goal != Goal.option_seashell_hunt,
            "death_link": bool(self.options.death_link.value),
            "settings": {key: settings[key] for key in lasr_settings.TRACKER_SETTINGS},
            # what Universal Tracker regenerates the slot from (interpret_slot_data)
            "options": self.options.as_dict(*SLOT_DATA_OPTIONS),
            "starting_instruments": self.starting_instruments,
            # each dungeon's door -> the dungeon it leads into, for trackers and Universal Tracker
            "dungeon_entrances": self.dungeon_entrances,
            # each shuffled overworld door -> the inside door it leads to (Entrances), for Universal Tracker
            "entrances": self.entrances,
            # the door the house's exit leads out of (Random start), None for Tarin's own
            "start": self.start,
            # what the slot's rules depend on, for the client's in-logic locations (tracker.py)
            "logic": {
                "preset": self.options.logic.current_key,
                "tokens": self.settings_tokens,
                "vanilla": [loc for loc in self.vanilla if self._is_progression(self.content(loc))],
                "optional": self.optional,  # reached only after the goal
                # the patch grants these, so the server does not send them (items_handling)
                "starting": [item.name for item in self.multiworld.precollected_items[self.player]],
            },
        }

    def patch_data(self) -> Dict[str, Any]:
        kept = lasr_settings.kept_locations()
        placements = {loc: self.content(loc) if loc in kept else "ap-item" for loc in self.shuffled}
        placements.update({loc: self.content(loc) for loc in self.vanilla})
        # Starting keys LASR's start event gives per dungeon (the Start With settings); as a starting item a key would
        # count for the level Link wakes up in.
        given = tuple(f"{kind}-D" for kind in ("key", "nightmare-key")
                      if self.dungeon_item_placements()[kind] == DungeonItemPlacement.option_start_with)
        starting = [ITEM_TABLE[item.name].lasr_name for item in self.multiworld.precollected_items[self.player]
                    if item.name in ITEM_TABLE and not (given and ITEM_TABLE[item.name].lasr_name.startswith(given))]
        return {
            "format": PATCH_FORMAT,
            "seed_name": self.multiworld.seed_name,
            "player": self.player,
            "slot_name": self.multiworld.player_name[self.player],
            "seed_hash": self.seed_hash(),
            "settings": lasr_settings.lasr_settings(self.options, self.unrandomized()),
            "starting_items": starting,
            "placements": {loc: placements[loc] for loc in ITEM_LOCATIONS if loc in placements},
            "location_ids": {loc: LOCATION_OFFSETS[loc] for loc in self.shuffled},
            # the owl statues the options leave out: the mod gives their gifts too, which the module turns off
            "removed_location_ids": {loc: LOCATION_OFFSETS[loc] for loc in self.removed},
            # the warp points only their items open: their WarpPoint.gsheet indices
            "warp_points": sorted(WARP_POINTS.values()) if self.options.warp_points else [],
            "optional_locations": self.optional,
            "map_hidden_locations": [loc for loc in hidden_checks(self) if loc in self.shuffled],
            "location_names": self.placed_names(),
            "location_models": self.placed_models(),
            "location_chests": self.placed_chests(),
            "location_traps": self.placed_traps(),
            "dungeon_entrances": dict(self.dungeon_entrances),
            # the overworld doors that lead elsewhere: the inside door each leads to, and the one it led to
            "entrances": self.moved_doors(),
            "entrance_pairs": {door: DOORS["pairs"][door] for door in self.moved_doors()},
            "start": self.start,  # the door the house's exit leads out of (Random start)
            "hints": self.in_game_hints(),
            "ghost_secret": self.ghost_secret(),
            "ghost_relay": str(self.settings.ghost_relay or ""),
        }

    def moved_doors(self) -> Dict[str, str]:
        """{overworld door: the inside door it leads to} for each door that leads elsewhere: as the door placement
        pairs them (Entrances), else the start door and the door outside the house (Random start, lasr.start_pairs)."""
        doors = dict(self.entrances) if self.options.entrances else start_pairs(self.start)
        return {door: inside for door, inside in sorted(doors.items()) if inside != DOORS["pairs"][door]}

    _hints: Optional[Dict[str, Dict[str, Any]]] = None
    _hints_lock = threading.Lock()  # output and spoiler run on several threads

    def in_game_hints(self) -> Dict[str, Dict[str, Any]]:
        """The owl statues' and books' hints (hints.py), picked once the multiworld is filled."""
        with self._hints_lock:
            if self._hints is None:
                self._hints = choose_hints(self)
            return self._hints

    def write_spoiler_header(self, spoiler_handle: TextIO) -> None:
        """The slot's doors go to the spoiler's own Entrances section, which Archipelago writes after the headers: the
        start (Random start), every dungeon door when one leads elsewhere (Shuffled dungeons), each overworld door
        that leads elsewhere (moved_doors). The ways out lead back."""
        spoiler = self.multiworld.spoiler
        if self.start:
            spoiler.set_entrance(HOUSE_EXIT, f"{door_label(self.start)} ({START_DOORS[self.start]})", "entrance",
                                 self.player)
        if any(door != dungeon for door, dungeon in self.dungeon_entrances.items()):
            for door, dungeon in self.dungeon_entrances.items():
                spoiler.set_entrance(door_entrance(door), dungeon_name(dungeon), "both", self.player)
        for door, inside in self.moved_doors().items():
            spoiler.set_entrance(door_label(door), door_label(inside), "both", self.player)

    def write_spoiler(self, spoiler_handle: TextIO) -> None:
        hints = self.in_game_hints()
        if hints:
            spoiler_handle.write(f"\n\nIn-game hints ({self.multiworld.player_name[self.player]}):\n\n")
            for source, hint in hints.items():
                spoiler_handle.write(f"{source_name(source)} ({hint['kind']}): {hint['text']}\n")

    def placed_names(self) -> Dict[str, List[str]]:
        """What each AP location holds: [item name, owner name], the owner empty for this player's items."""
        names = {}
        for loc in self.shuffled:
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            if item is not None:
                owner = "" if item.player == self.player else self.multiworld.player_name[item.player]
                names[loc] = [item.name, owner]
        return names

    def placed_traps(self) -> List[str]:
        """The AP locations that hold a trap, anyone's. Their item-get shows the trap's model (patcher TRAP_MODEL)."""
        return sorted(loc for loc in set(self.shuffled) - lasr_settings.kept_locations()
                      if (item := self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item) is not None
                      and item.trap)

    def trap_decoys(self) -> Dict[str, str]:
        """The LASR item each AP location holding a trap poses as on the ground and by its chest: one of this slot's
        items that a randomized location holds and a trap can pose as (models.is_decoy), at random. A trap thus never
        looks like an item the seed does not shuffle. The pick is the seed's and the location's, so it stays the same
        on every call."""
        pool = sorted(ITEM_TABLE[loc.item.name].lasr_name for loc in self.multiworld.get_filled_locations()
                      if loc.item.player == self.player and loc.address is not None and not loc.locked
                      and loc.item.name in ITEM_TABLE and is_decoy(ITEM_TABLE[loc.item.name].lasr_name))
        if not pool:
            return {}
        rng = random.Random(f"{self.multiworld.seed}-{self.player}-trap-decoys")
        return {loc: rng.choice(pool) for loc in self.placed_traps()}

    def placed_models(self) -> Dict[str, str]:
        """The LASR item whose model each AP location shows; locations left out show the Archipelago model."""
        models = {}
        decoys = self.trap_decoys()
        for loc in set(self.shuffled) - lasr_settings.kept_locations():
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            model = decoys.get(loc) or (model_for(item) if item is not None else "")
            if model:
                models[loc] = model
        return models

    def placed_chests(self) -> Dict[str, List[Any]]:
        """How the chest at each AP location looks (lasr_settings.chest_look); a trap's is its decoy's."""
        looks = {}
        decoys = self.trap_decoys()
        for loc in set(self.shuffled) - lasr_settings.kept_locations():
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            if item is None:
                continue
            if loc in decoys:
                looks[loc] = lasr_settings.chest_look(self.options, decoys[loc])
                continue
            own = item.player == self.player and item.name in ITEM_TABLE
            looks[loc] = lasr_settings.chest_look(self.options, ITEM_TABLE[item.name].lasr_name if own else None,
                                                  item.advancement, item.useful, item.trap)
        return looks

    def generate_output(self, output_directory: str) -> None:
        name = self.multiworld.get_out_file_name_base(self.player) + LASContainer.patch_file_ending
        LASContainer(os.path.join(output_directory, name), self.patch_data(), self.player,
                     self.multiworld.get_player_name(self.player)).write()

