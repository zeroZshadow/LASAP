import hashlib
import os
from collections import Counter
from typing import Any, ClassVar, Dict, List, Mapping, Set

from BaseClasses import CollectionState, Item, ItemClassification, Region, Tutorial
from Fill import fill_restrictive
from worlds.AutoWorld import WebWorld, World
from worlds.LauncherComponents import Component, SuffixIdentifier, Type, components, launch as launch_component

from .container import LASContainer
from .host_settings import LASSettings

from .ids import LOCATION_OFFSETS
from .items import FILLER_ITEM, GAME, ITEM_GAME_INFO, ITEM_NAME_GROUPS, ITEM_TABLE, LASR_TO_ITEM, LASItem
from .lasr import (CATEGORIES, DUNGEON_REWARD_LOCATIONS, FOLLOWERS, INSTRUMENT_LOCATIONS, INSTRUMENTS, ITEM_DEFS,
                   ITEM_LOCATIONS, LOGIC, TRAPS, item_dungeon, location_dungeon)
from .locations import LOCATION_NAME_GROUPS, LOCATION_NAME_TO_ID, LOCATION_NAMES, LASLocation
from .logic import LogicCompiler, make_rule, referenced_regions
from .models import model_for
from .options import OPTION_GROUPS, LASOptions
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
FOLLOWER_EVENTS = {"bow-wow": "bow-wow", "rooster": "rooster"}
PATCH_FORMAT = 1
EARLY_LOCATIONS_WANTED = 10


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
    shuffled: List[str]  # LASR item locations that are AP locations
    vanilla: List[str]  # LASR item locations that keep their vanilla content
    starting_items: List[str]  # LASR item names given at the start by options
    pre_fill_items: List[Item]
    restricted: Dict[str, Set[str]]  # AP item name -> LASR locations it must be placed in

    def generate_early(self) -> None:
        options = self.options
        self.settings_tokens = lasr_settings.logic_tokens(options)
        self.compiler = LogicCompiler(LOGIC, options.logic.current_key, self.settings_tokens,
                                      {**LASR_TO_ITEM, **FOLLOWER_EVENTS})

        self.starting_instruments = self.random.sample(INSTRUMENTS, options.starting_instruments.value)
        self.starting_items = list(self.starting_instruments)
        if options.dungeon_items == options.dungeon_items.option_start_with:
            for number in range(9):
                self.starting_items += [f"map-D{number}", f"compass-D{number}", f"stone-beak-D{number}"]

        removed = lasr_settings.removed_locations(options)
        vanilla = lasr_settings.vanilla_locations(options)
        if options.instruments == options.instruments.option_vanilla:
            started = {f"D{INSTRUMENTS.index(i) + 1}-instrument" for i in self.starting_instruments}
            vanilla |= set(INSTRUMENT_LOCATIONS) - started
        self.removed = [loc for loc in ITEM_LOCATIONS if loc in removed]
        self.vanilla = [loc for loc in ITEM_LOCATIONS if loc in vanilla and loc not in removed]
        self.shuffled = [loc for loc in ITEM_LOCATIONS if loc not in vanilla and loc not in removed]
        self.pre_fill_items = []
        self.restricted = {}

    # --- regions and locations ---

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        # LASR regions, rooms and the few nodes on reference cycles are AP regions entered from Menu
        roots = self.shuffled + self.vanilla + FOLLOWERS + ["kill-nightmare"] +             [k for k, v in LOGIC.items() if v["type"] == "region"]
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
        for follower in FOLLOWERS:
            region = self.node_regions[LOGIC[follower]["region"]]
            region.locations.append(LASLocation(self.player, follower, None, region))
        menu.locations.append(LASLocation(self.player, GOAL_LOCATION, None, menu))

    def content(self, loc: str) -> str:
        return LOGIC[loc]["content"]

    def _is_progression(self, lasr_item: str) -> bool:
        return bool(ITEM_TABLE[LASR_TO_ITEM[lasr_item]].classification & ItemClassification.progression)

    # --- items ---

    def create_item(self, name: str) -> LASItem:
        data = ITEM_TABLE[name]
        return LASItem(name, data.classification, data.code, self.player)

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
        for loc in self.vanilla:
            pool[self.content(loc)] -= 1
        for item in self.starting_items:
            if pool[item] > 0:
                pool[item] -= 1
                pool["rupee-50"] += 1
        if options.traps != options.traps.option_none:
            # LASR trades three zol traps for 500 rupees, then replaces purple rupees with traps
            for replacement in ("rupee-100", "rupee-100", "rupee-300")[:min(3, pool["zol-trap"])]:
                pool["zol-trap"] -= 1
                pool[replacement] += 1
            count = {options.traps.option_few: 3, options.traps.option_many: 17}[options.traps.value]
            for junk in ("rupee-50", "rupee-20", "rupee-5", "rupee-100", "chamber-stone"):
                taken = min(count, pool[junk])
                pool[junk] -= taken
                count -= taken
                for _ in range(taken):
                    pool[self.random.choice(TRAPS)] += 1
        assert all(n >= 0 for n in pool.values()), pool
        assert sum(pool.values()) == len(self.shuffled), (sum(pool.values()), len(self.shuffled))
        return pool

    def create_items(self) -> None:
        for loc in self.vanilla:
            content = self.content(loc)
            if self._is_progression(content):
                item = self.create_event(LASR_TO_ITEM[content], ITEM_TABLE[LASR_TO_ITEM[content]].classification)
                self.get_location(LOCATION_NAMES[loc]).place_locked_item(item)
        for follower in FOLLOWERS:
            content = FOLLOWER_EVENTS[LOGIC[follower]["content"]]
            self.get_location(follower).place_locked_item(self.create_event(content))
        self.get_location(GOAL_LOCATION).place_locked_item(self.create_event(GOAL_EVENT))

        for item in self.starting_items:
            self.multiworld.push_precollected(self.create_item(LASR_TO_ITEM[item]))

        self.restricted = self._restricted_items()
        for lasr_item, count in sorted(self.item_pool().items(), key=lambda kv: list(ITEM_DEFS).index(kv[0])):
            for _ in range(count):
                item = self.create_item(LASR_TO_ITEM[lasr_item])
                if item.name in self.restricted:
                    self.pre_fill_items.append(item)
                else:
                    self.multiworld.itempool.append(item)

    def _restricted_items(self) -> Dict[str, Set[str]]:
        options = self.options
        restricted: Dict[str, Set[str]] = {}
        shuffled = set(self.shuffled)

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
        for follower in FOLLOWERS:
            self.get_location(follower).access_rule = rule(self.compiler.node_expr(follower))
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
        openers = []
        for name in sorted({item.name for item in self.multiworld.itempool
                            if item.player == self.player and item.advancement}):
            trial = state.copy()
            trial.collect(self.create_item(name), prevent_sweep=True)
            trial.sweep_for_advancements(events)
            if sum(loc.can_reach(trial) for loc in mine) >= base + EARLY_LOCATIONS_WANTED:
                openers.append(name)
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

        items = sorted(self.pre_fill_items, key=priority)
        locations = sorted(candidates, key=lambda loc: loc.name)
        self.random.shuffle(locations)
        fill_restrictive(self.multiworld, state, locations, items, single_player_placement=True, lock=True,
                         allow_excluded=True, name="LAS Dungeon Items")

    # --- output ---

    def seed_hash(self) -> str:
        return hashlib.sha256(f"{self.multiworld.seed_name}:{self.player}".encode()).hexdigest()[:8]

    def fill_slot_data(self) -> Mapping[str, Any]:
        settings = lasr_settings.lasr_settings(self.options, self.vanilla)
        return {
            "seed_hash": self.seed_hash(),
            "goal": "wind_fish",
            "death_link": bool(self.options.death_link.value),
            "location_count": len(self.shuffled),
            "settings": {key: settings[key] for key in lasr_settings.TRACKER_SETTINGS},
        }

    def patch_data(self) -> Dict[str, Any]:
        placements = {loc: "ap-item" for loc in self.shuffled}
        placements.update({loc: self.content(loc) for loc in self.vanilla})
        starting = [ITEM_TABLE[item.name].lasr_name for item in self.multiworld.precollected_items[self.player]
                    if item.name in ITEM_TABLE]
        return {
            "format": PATCH_FORMAT,
            "seed_name": self.multiworld.seed_name,
            "player": self.player,
            "slot_name": self.multiworld.player_name[self.player],
            "seed_hash": self.seed_hash(),
            "settings": lasr_settings.lasr_settings(self.options, self.vanilla),
            "starting_items": starting,
            "placements": {loc: placements[loc] for loc in ITEM_LOCATIONS if loc in placements},
            "location_ids": {loc: LOCATION_OFFSETS[loc] for loc in self.shuffled},
            "location_names": self.placed_names(),
            "location_models": self.placed_models(),
            "location_chests": self.placed_chests(),
            "dungeon_entrances": dict(lasr_settings.DUNGEON_ENTRANCES),
        }

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
        for loc in self.shuffled:
            item = self.multiworld.get_location(LOCATION_NAMES[loc], self.player).item
            model = model_for(item) if item is not None else ""
            if model:
                models[loc] = model
        return models

    def placed_chests(self) -> Dict[str, List[Any]]:
        """How the chest at each AP location looks (lasr_settings.chest_look)."""
        looks = {}
        for loc in self.shuffled:
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

