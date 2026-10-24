import hashlib
import os
import secrets
import threading
import weakref
from collections import Counter
from typing import Any, ClassVar, Dict, List, Mapping, Optional, Set, TextIO

from BaseClasses import CollectionState, Item, ItemClassification, LocationProgressType, Region, Tutorial
from Options import PerGameCommonOptions
from Fill import fill_restrictive
from worlds.AutoWorld import WebWorld, World
from worlds.LauncherComponents import Component, SuffixIdentifier, Type, components, launch as launch_component

from .container import LASContainer
from .hints import choose_hints, source_name
from .host_settings import LASSettings

from .ids import LOCATION_OFFSETS
from .items import FILLER_ITEM, GAME, ITEM_GAME_INFO, ITEM_NAME_GROUPS, ITEM_TABLE, LASR_TO_ITEM, LASItem
from .lasr import (CATEGORIES, COUNTED, DUNGEON_NAMES, DUNGEON_REWARD_LOCATIONS, DUNGEONS, INSTRUMENT_LOCATIONS,
                   INSTRUMENTS, ITEM_DEFS, ITEM_LOCATIONS, LOGIC, TRAPS, dungeon_logic, goal_logic, item_dungeon,
                   location_dungeon, shuffle_dungeons)
from .locations import LOCATION_NAME_GROUPS, LOCATION_NAME_TO_ID, LOCATION_NAMES, LASLocation
from .logic import LogicCompiler, make_rule, referenced_regions
from .models import model_for
from .options import OPTION_GROUPS, LASOptions
from .tracker import SIGN_KEY, tracked_instruments
from . import lasr_settings


__all__ = ["LASWorld", "ITEM_GAME_INFO"]


def launch_client(*args: str) -> None:
    # the client lives in client.py and is only imported when launched
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
PRE_FILL_ATTEMPTS = 10
PROGRESSION_CHECKS = 3
# the items traps replace, in this order; logic never needs them (rupees it needs come from farming)
JUNK = ("rupee-50", "rupee-20", "rupee-5", "rupee-100", "chamber-stone", "rupee-300")
# the options in the slot data ("options"), from which Universal Tracker regenerates the slot
SLOT_DATA_OPTIONS = tuple(name for name in LASOptions.type_hints
                          if name not in PerGameCommonOptions.type_hints and name != "start_inventory_from_pool")


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
    topology_present = False

    item_name_to_id = {name: data.code for name, data in ITEM_TABLE.items()}
    location_name_to_id = LOCATION_NAME_TO_ID
    item_name_groups = ITEM_NAME_GROUPS
    location_name_groups = LOCATION_NAME_GROUPS

    compiler: LogicCompiler
    goal_instruments: int  # the instruments the egg's rule takes (compile_logic)
    shuffled: List[str]  # LASR item locations that are AP locations
    locked: List[str]  # AP locations the options leave unrandomized: they hold their default item, locked
    optional: List[str]  # AP locations of activities the options leave out: after the goal, excluded
    vanilla: List[str]  # LASR item locations that keep their vanilla content and are no AP locations
    plain: List[str]  # unrandomized locations whose default item never matters: no AP locations, left out of logic
    starting_items: List[str]  # LASR item names given at the start by options
    pre_fill_items: List[Item]
    replaced: Dict[str, str] = {}  # location -> the item it holds instead of its own (_replaced_by_starting_gear)
    restricted: Dict[str, Set[str]]  # AP item name -> LASR locations it must be placed in
    dungeon_entrances: Dict[str, str]  # each dungeon's door -> the dungeon it leads into (lasr.dungeon_logic)

    ut_can_gen_without_yaml = True  # Universal Tracker regenerates the slot from its slot data
    # Universal Tracker watches this data storage key and passes its value to reconnect_found_entrances
    found_entrances_datastorage_key = SIGN_KEY

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
        if passthrough:
            self.dungeon_entrances = dict(passthrough.get("dungeon_entrances") or {d: d for d in DUNGEONS})
        elif options.shuffled_dungeons:
            self.dungeon_entrances = shuffle_dungeons(self.random)
        else:
            self.dungeon_entrances = {d: d for d in DUNGEONS}
        self.settings_tokens = lasr_settings.logic_tokens(options)
        # Universal Tracker keeps a count left to chance hidden until the player reads the sign (tracked_instruments)
        instruments = options.instruments_required.value
        if passthrough:
            instruments = tracked_instruments(instruments, passthrough.get("instruments_known", True), False)
        self.compile_logic(instruments)

        if passthrough:
            self.starting_instruments = list(passthrough["starting_instruments"])
        else:
            self.starting_instruments = self.random.sample(INSTRUMENTS, options.starting_instruments.value)
        self.starting_items = list(self.starting_instruments)
        if options.dungeon_items == options.dungeon_items.option_start_with:
            for number in range(9):
                self.starting_items += [f"map-D{number}", f"compass-D{number}", f"stone-beak-D{number}"]

        removed = lasr_settings.removed_locations(options)
        vanilla = lasr_settings.fixed_locations()
        unrandomized = lasr_settings.unrandomized_locations(options)
        if options.instruments == options.instruments.option_vanilla:
            started = {f"D{INSTRUMENTS.index(i) + 1}-instrument" for i in self.starting_instruments}
            unrandomized |= set(INSTRUMENT_LOCATIONS) - started
        optional = lasr_settings.optional_locations(options)
        plain = {loc for loc in unrandomized if not lasr_settings.default_item_matters(loc)}
        self.removed = [loc for loc in ITEM_LOCATIONS if loc in removed]
        self.vanilla = [loc for loc in ITEM_LOCATIONS if loc in vanilla and loc not in removed]
        self.plain = [loc for loc in ITEM_LOCATIONS if loc in plain and loc not in removed]
        self.shuffled = [loc for loc in ITEM_LOCATIONS if loc not in vanilla and loc not in plain and loc not in removed]
        self.locked = [loc for loc in self.shuffled if loc in unrandomized]
        self.optional = [loc for loc in self.shuffled if loc in optional]
        self.pre_fill_items = []
        self.restricted = {}
        self.replaced = self._replaced_by_starting_gear()

    def _replaced_by_starting_gear(self) -> Dict[str, str]:
        """Unrandomized locations (locked checks, or no AP location) whose item is starting gear
        (start_inventory_from_pool) that the pool would otherwise not hold: Archipelago takes starting gear out of the
        item pool, so such a location holds 50 Rupees instead (as StartingGear says) and its item joins the pool.
        The Trendy Game's prizes stay: the game gives them itself (kept_locations)."""
        in_pool = Counter({k: v["quantity"] for k, v in ITEM_DEFS.items()})
        for loc in self.vanilla + self.plain + self.locked:
            in_pool[self.content(loc)] -= 1
        for item in self.starting_items:
            in_pool[item] -= 1
        kept = lasr_settings.kept_locations()
        replaced: Dict[str, str] = {}
        for name, count in sorted(self.options.start_inventory_from_pool.value.items()):
            if name not in ITEM_TABLE:
                continue
            lasr_item = ITEM_TABLE[name].lasr_name
            missing = count - max(0, in_pool[lasr_item])
            for loc in [loc for loc in self.locked + self.plain if self.content(loc) == lasr_item and loc not in kept]:
                if missing <= 0:
                    break
                replaced[loc] = ITEM_TABLE[FILLER_ITEM].lasr_name
                missing -= 1
        return replaced

    # --- regions and locations ---

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        # LASR regions, rooms and the few nodes on reference cycles are AP regions entered from Menu
        roots = self.shuffled + self.vanilla + ["kill-nightmare"] +             [k for k, v in LOGIC.items() if v["type"] == "region"]
        needed = set(self.compiler.dependencies(roots)) & self.compiler.region_nodes
        self.node_regions: Dict[str, Region] = {}
        for name in LOGIC:
            if name in needed:
                region = self.node_regions[name] = Region(name, self.player, self.multiworld)
                self.multiworld.regions.append(region)
                menu.connect(region, f"To {name}")

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
        pool = Counter({k: v["quantity"] for k, v in ITEM_DEFS.items()})
        for category in ("Overworld_Owl_Statues", "Dungeon_Owl_Statues"):
            pool["rupee-20"] += sum(1 for loc in CATEGORIES[category] if loc not in self.removed)
        for loc in self.vanilla + self.plain + self.locked:
            pool[self.content(loc)] -= 1
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
                elif item.name in self.restricted:
                    self.pre_fill_items.append(item)
                else:
                    self.multiworld.itempool.append(item)

    def _restricted_items(self) -> Dict[str, Set[str]]:
        options = self.options
        restricted: Dict[str, Set[str]] = {}
        shuffled = set(self.shuffled) - set(self.locked)

        def in_dungeon(number: int) -> Set[str]:
            return {loc for loc in shuffled if location_dungeon(loc) == number}

        own_keys = options.keys == options.keys.option_own_dungeon
        own_items = options.dungeon_items == options.dungeon_items.option_own_dungeon
        for lasr_item in ITEM_DEFS:
            number = item_dungeon(lasr_item)
            if number < 0:
                continue
            kind = lasr_item[:-3]
            if (own_keys and kind in ("key", "nightmare-key")) or \
                    (own_items and kind in ("map", "compass", "stone-beak")):
                restricted[LASR_TO_ITEM[lasr_item]] = in_dungeon(number)
        if options.instruments == options.instruments.option_dungeon_rewards:
            for instrument in INSTRUMENTS:
                restricted[LASR_TO_ITEM[instrument]] = shuffled & set(DUNGEON_REWARD_LOCATIONS)
        elif options.instruments == options.instruments.option_own_dungeon:
            for number, instrument in enumerate(INSTRUMENTS, 1):
                restricted[LASR_TO_ITEM[instrument]] = in_dungeon(number)
        elif options.instruments == options.instruments.option_any_dungeon:
            any_dungeon = {loc for loc in shuffled if location_dungeon(loc) >= 0}
            for instrument in INSTRUMENTS:
                restricted[LASR_TO_ITEM[instrument]] = any_dungeon
        return restricted

    def get_pre_fill_items(self) -> List[Item]:
        return self.pre_fill_items

    # --- rules ---

    def set_rules(self) -> None:
        def rule(expr):
            return make_rule(expr, self.player, self.node_regions)

        for name, region in self.node_regions.items():
            entrance = self.get_entrance(f"To {name}")
            expr = self.compiler.node_expr(name)
            entrance.access_rule = rule(expr)
            for other in referenced_regions(expr):
                self.multiworld.register_indirect_condition(self.node_regions[other], entrance)
        for loc in self.shuffled + self.vanilla:
            if loc in self.shuffled or self._is_progression(self.content(loc)):
                self.get_location(LOCATION_NAMES[loc]).access_rule = rule(self.compiler.node_expr(loc))
        for loc in self.optional:  # filler and traps of any player: Location.can_fill turns away the rest
            location = self.get_location(LOCATION_NAMES[loc])
            location.progress_type = LocationProgressType.EXCLUDED
            location.access_rule = lambda state, reach=location.access_rule: \
                state.has(GOAL_EVENT, self.player) and reach(state)
        self.get_location(GOAL_LOCATION).access_rule = rule(self.compiler.token("kill-nightmare", 1))
        self.multiworld.completion_condition[self.player] = lambda state: state.has(GOAL_EVENT, self.player)

    # --- fill ---

    def generate_basic(self) -> None:
        # Very few locations are reachable with nothing (Tarin and a few loose items). Like LASR's Tarin rule,
        # make one item that opens up the island early so the fill does not run out of starting locations.
        events = [loc for loc in self.multiworld.get_locations(self.player) if loc.address is None]
        mine = [loc for loc in self.multiworld.get_locations(self.player) if loc.address is not None]
        state = CollectionState(self.multiworld)
        state.sweep_for_advancements(events)
        base = sum(loc.can_reach(state) for loc in mine)
        if base >= EARLY_LOCATIONS_WANTED:
            return
        gains = {}
        for name in sorted({item.name for item in self.multiworld.itempool
                            if item.player == self.player and item.advancement}):
            trial = state.copy()
            trial.collect(self.create_item(name), prevent_sweep=True)
            trial.sweep_for_advancements(events)
            gains[name] = sum(loc.can_reach(trial) for loc in mine) - base
        # the items that open enough locations, else those that open the most (with few locations shuffled)
        wanted = min(EARLY_LOCATIONS_WANTED, max(gains.values(), default=0))
        openers = [name for name, gain in gains.items() if gain >= wanted > 0]
        if openers:
            self.multiworld.local_early_items[self.player][self.random.choice(openers)] = 1

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

        state = CollectionState(self.multiworld)
        for item in self.multiworld.itempool:
            state.collect(item, prevent_sweep=True)
        for player in self.multiworld.player_ids:
            if player != self.player:
                for item in self.multiworld.worlds[player].get_pre_fill_items():
                    state.collect(item, prevent_sweep=True)
        state.sweep_for_advancements()

        def priority(item: Item) -> int:
            # fill_restrictive places from the end of the list: instruments, nightmare keys, small keys first
            lasr = ITEM_TABLE[item.name].lasr_name
            if lasr in INSTRUMENTS:
                return 3
            if lasr.startswith("nightmare-key-"):
                return 2
            return 1 if lasr.startswith("key-") else 0

        for attempt in range(PRE_FILL_ATTEMPTS):
            items = sorted(self.pre_fill_items, key=priority)
            locations = sorted(candidates, key=lambda loc: loc.name)
            self.random.shuffle(locations)
            fill_restrictive(self.multiworld, state, locations, items, single_player_placement=True, lock=True,
                             allow_excluded=True, name="LAS Dungeon Items")
            if attempt == PRE_FILL_ATTEMPTS - 1 or self._progression_fits():
                break
            for location in candidates:  # try another placement
                if location.item is not None and location.item.player == self.player and                         location.item.name in self.restricted:
                    location.item.location = None
                    location.item = None
                    location.locked = False

    def _progression_fits(self) -> bool:
        """Whether this world's progression items fit into its own empty locations, in each of a few random fills.
        With few locations (most categories off, dungeon items in their dungeons) a placement of the dungeon items can
        leave no placement that works, or few that the main fill finds."""
        empty = [loc for loc in self.multiworld.get_unfilled_locations(self.player)]
        for _ in range(PROGRESSION_CHECKS):
            items = [item for item in self.multiworld.itempool if item.player == self.player and item.advancement]
            locations = list(empty)
            self.random.shuffle(locations)
            fill_restrictive(self.multiworld, CollectionState(self.multiworld), locations, items,
                             single_player_placement=True, allow_partial=True, name="LAS Dungeon Items check")
            for location in empty:
                if location.item is not None:
                    location.item.location = None
                    location.item = None
            if items:
                return False
        return True

    # --- output ---

    def unrandomized(self) -> List[str]:
        return self.vanilla + self.plain + self.locked

    def seed_hash(self) -> str:
        return hashlib.sha256(f"{self.multiworld.seed_name}:{self.player}".encode()).hexdigest()[:8]

    def compile_logic(self, instruments_required: int) -> None:
        logic = goal_logic(instruments_required, dungeon_logic(LOGIC, self.dungeon_entrances))
        self.goal_instruments = instruments_required
        self.compiler = LogicCompiler(logic, self.options.logic.current_key, self.settings_tokens, LASR_TO_ITEM,
                                      COUNTED)

    def reconnect_found_entrances(self, key: str, value: Any) -> None:
        """Universal Tracker: the data storage key SIGN_KEY changed. Once the player has read the sign, the rules
        take the slot's own count."""
        required = self.options.instruments_required.value
        if value and self.goal_instruments != required:
            self.compile_logic(required)
            self.set_rules()

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
            "goal": "wind_fish",
            "instruments_required": self.options.instruments_required.value,
            # False for a count left to chance: trackers show it once the player read the sign (SIGN_KEY)
            "instruments_known": self.options.instruments_required.known,
            "death_link": bool(self.options.death_link.value),
            "location_count": len(self.shuffled),
            "settings": {key: settings[key] for key in lasr_settings.TRACKER_SETTINGS},
            # what Universal Tracker regenerates the slot from (interpret_slot_data)
            "options": self.options.as_dict(*SLOT_DATA_OPTIONS),
            "starting_instruments": self.starting_instruments,
            # each dungeon's door -> the dungeon it leads into, for trackers and Universal Tracker
            "dungeon_entrances": self.dungeon_entrances,
            # what the slot's rules depend on, for the client's in-logic locations (tracker.py)
            "logic": {
                "preset": self.options.logic.current_key,
                "tokens": self.settings_tokens,
                "vanilla": [loc for loc in self.vanilla if self._is_progression(self.content(loc))],
                "locked": [loc for loc in self.locked if self._is_progression(self.content(loc))],
                "optional": self.optional,  # reached only after the goal
                # the patch grants these, so the server does not send them (items_handling)
                "starting": [item.name for item in self.multiworld.precollected_items[self.player]],
            },
        }

    def patch_data(self) -> Dict[str, Any]:
        kept = lasr_settings.kept_locations()
        placements = {loc: self.content(loc) if loc in kept else "ap-item" for loc in self.shuffled}
        placements.update({loc: self.content(loc) for loc in self.vanilla + self.plain})
        starting = [ITEM_TABLE[item.name].lasr_name for item in self.multiworld.precollected_items[self.player]
                    if item.name in ITEM_TABLE]
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
            "optional_locations": self.optional,
            "location_names": self.placed_names(),
            "location_models": self.placed_models(),
            "location_chests": self.placed_chests(),
            "dungeon_entrances": dict(self.dungeon_entrances),
            "hints": self.in_game_hints(),
            "ghost_secret": self.ghost_secret(),
            "ghost_relay": str(self.settings.ghost_relay or ""),
        }

    _hints: Optional[Dict[str, Dict[str, Any]]] = None
    _hints_lock = threading.Lock()  # output and spoiler run on several threads

    def in_game_hints(self) -> Dict[str, Dict[str, Any]]:
        """The owl statues' and books' hints (hints.py), picked once the multiworld is filled."""
        with self._hints_lock:
            if self._hints is None:
                self._hints = choose_hints(self)
            return self._hints

    def write_spoiler(self, spoiler_handle: TextIO) -> None:
        if any(door != dungeon for door, dungeon in self.dungeon_entrances.items()):
            names = {d: DUNGEON_NAMES[(i + 1) % 9] for i, d in enumerate(DUNGEONS)}
            spoiler_handle.write(f"\n\nDungeon entrances ({self.multiworld.player_name[self.player]}):\n\n")
            for door, dungeon in self.dungeon_entrances.items():
                spoiler_handle.write(f"{names[door]} entrance -> {names[dungeon]}\n")
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

    def placed_models(self) -> Dict[str, str]:
        """The LASR item whose model each AP location shows; locations left out show the Archipelago model."""
        models = {}
        for loc in set(self.shuffled) - lasr_settings.kept_locations():
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            model = model_for(item) if item is not None else ""
            if model:
                models[loc] = model
        return models

    def placed_chests(self) -> Dict[str, List[Any]]:
        """How the chest at each AP location looks (lasr_settings.chest_look)."""
        looks = {}
        for loc in set(self.shuffled) - lasr_settings.kept_locations():
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            if item is None:
                continue
            own = item.player == self.player and item.name in ITEM_TABLE
            looks[loc] = lasr_settings.chest_look(self.options, ITEM_TABLE[item.name].lasr_name if own else None,
                                                  item.advancement, item.useful, item.trap)
        return looks

    def generate_output(self, output_directory: str) -> None:
        name = self.multiworld.get_out_file_name_base(self.player) + LASContainer.patch_file_ending
        LASContainer(os.path.join(output_directory, name), self.patch_data(), self.player,
                     self.multiworld.get_player_name(self.player)).write()

