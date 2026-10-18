"""In-game hints (options in_game_hints and hint_distribution): owl statues and library books give hints about this
slot's items.

The world picks them once the multiworld is filled (choose_hints). The kinds:
- always: a long check of this world (ALWAYS) holding a progression or useful item, anyone's.
- required: an area of this world, or a location of another, holding an item this slot cannot win without.
- barren: an area of this world whose locations hold no progression item, anyone's.
- area: the area of this world, or the world, holding one of this slot's progression items, then useful ones.
- precise: the location holding one of this slot's progression items, then useful ones.
- joke: for the sources left over.
hint_distribution sets how many of each (DISTRIBUTIONS). An area is the part of a location's name before " - ": an
overworld region or a dungeon. The patch carries each source's text and kind (`hints`); in server mode the hints that
name one location (always, precise) also carry it, which the client hints on the server when the game reports the
hint as read."""
from typing import TYPE_CHECKING, Dict, Iterable, Iterator, List, NamedTuple, Optional, Set

from BaseClasses import CollectionState, Location

from .items import ITEM_TABLE
from .lasr import item_dungeon, location_dungeon
from .locations import LASR_LOCATIONS, LOCATION_NAMES
from . import lasr_settings

if TYPE_CHECKING:
    from . import LASWorld

# The hint sources, by the LASR name of the owl statue's location, and the library's books by their actor's name.
# The patcher (patcher/ap_hints.py) knows each one's event and text.
FIELD_OWLS = ("owl-statue-below-D8", "owl-statue-pothole", "owl-statue-above-cave", "owl-statue-moblin-cave",
              "owl-statue-south-bay", "owl-statue-desert", "owl-statue-maze", "owl-statue-taltal-east",
              "owl-statue-rapids")
DUNGEON_OWLS = ("D1-owl-statue-spinies", "D1-owl-statue-3-of-a-kind", "D1-owl-statue-long-hallway",
                "D2-owl-statue-first-switch", "D2-owl-statue-push-puzzle", "D2-owl-statue-past-hinox",
                "D3-owl-statue-basement-north", "D3-owl-statue-arrow", "D3-owl-statue-northwest", "D4-owl-statue",
                "D5-owl-statue-triple-stalfos", "D5-owl-statue-before-slime-eel", "D6-owl-statue-ledge",
                "D6-owl-statue-southeast", "D6-owl-statue-canal", "D7-owl-statue-ball", "D7-owl-statue-kirbys",
                "D7-owl-statue-hookshot-chest", "D8-owl-statue-above-smasher", "D8-owl-statue-below-gibdos",
                "D8-owl-statue-eye-statue", "D0-owl-statue-nine-switches", "D0-owl-statue-first-switches",
                "D0-owl-statue-before-mini-boss")
BOOKS = ("book-Blue4", "book-Green1", "book-Orange2", "book-Red3", "book-Blue2", "book-Red1")
SOURCES = FIELD_OWLS + DUNGEON_OWLS + BOOKS
BOOK_NAMES = {"book-Blue4": "Library - Shield Book", "book-Green1": "Library - Item Book",
              "book-Orange2": "Library - Map Book", "book-Red3": "Library - Whirling Blade Book",
              "book-Blue2": "Library - Warp Book", "book-Red1": "Library - Bomb Book"}
# For sources left over once every progression and useful item is hinted.
JOKES = (
    "Your Wind Fish is in an egg on Mt. Tamaranch.",
    "Your Marin is in Mabe Village, singing.",
    "Your mermaid is in Martha's Bay. She lost a scale.",
    "Your sand is in Yarna Desert. All of it.",
    "Your nothing is in Tabahl Wasteland.",
    "Your animals are in Animal Village.",
    "Your Tarin is under a beehive somewhere.",
    "Your rooster is at the weather vane. Mostly.",
)
DUNGEON_ITEM_KINDS = ("key", "nightmare-key", "map", "compass", "stone-beak")
# Long checks the always hints name, when shuffled: the trade quest's end, the Seashell Mansion's last rewards, the
# rapids race's best time, Ol' Baron and Dampe's last challenge.
ALWAYS = ("mermaid-cave", "40-seashell-reward", "50-seashell-reward", "rapids-race-30", "fishing-ol-baron",
          "dampe-final")
KINDS = ("always", "required", "barren", "area", "precise", "joke")
REST = -1  # the kind that gets the sources the others leave
# hint_distribution: (kind, count) in the order they are picked; a kind that runs out leaves its sources to REST
DISTRIBUTIONS = {
    "precise": (("precise", REST),),
    "balanced": (("always", 6), ("required", 8), ("barren", 5), ("area", 8), ("precise", REST)),
    "vague": (("always", 6), ("required", 12), ("barren", 8), ("precise", 3), ("area", REST)),
}
FALLBACK = ("precise", "area")  # for the sources left once REST runs out too
SPARES = 3  # extra precise hints for owl statues whose hint would name their own gift
BARREN_MIN = 2  # the locations an area needs, whose item is not known anyway, for a barren hint
GOAL = "wake the Wind Fish"


class Hint(NamedTuple):
    kind: str
    text: str
    about: Optional[Location]  # the location it rests on
    server: bool  # it names `about` alone, so that it can be a server hint


def source_name(source: str) -> str:
    """The source as the spoiler names it: the owl statue's location, or the book."""
    return BOOK_NAMES.get(source) or LOCATION_NAMES[source]


def area(location: Location) -> str:
    """The location's area: its overworld region or dungeon (location_names.json's "<region or dungeon> - <spot>")."""
    return location.name.split(" - ", 1)[0]


def hint_text(world: "LASWorld", location: Location) -> str:
    item = location.item.name
    if location.player == world.player:
        return f"Your {item} is at {location.name}."
    return f"Your {item} is in {world.multiworld.player_name[location.player]}'s world at {location.name}."


def known_locations(world: "LASWorld") -> Set[str]:
    """This world's LASR locations whose item is known anyway: locked and kept ones."""
    return set(world.locked) | (lasr_settings.kept_locations() & set(world.shuffled))


def candidates(world: "LASWorld") -> List[Location]:
    """Locations of the multiworld holding this slot's items that a hint can name, in a stable order."""
    multiworld = world.multiworld
    player = world.player
    known = known_locations(world)
    found = []
    for location in multiworld.get_filled_locations():
        item = location.item
        if item.player != player or not isinstance(location.address, int) or item.code is None:
            continue
        if item.name in world.options.start_hints:
            continue
        if location.name in getattr(multiworld.worlds[location.player].options, "start_location_hints", ()):
            continue
        lasr_item = ITEM_TABLE[item.name].lasr_name if item.name in ITEM_TABLE else ""
        if lasr_item == "seashell":
            continue
        if location.player == player:
            lasr_location = LASR_LOCATIONS[location.name]
            if lasr_location in known:
                continue
            # a dungeon item in its own dungeon: where it has to be anyway
            if lasr_item[:-3] in DUNGEON_ITEM_KINDS and location_dungeon(lasr_location) == item_dungeon(lasr_item):
                continue
        found.append(location)
    return sorted(found, key=lambda loc: (loc.player, loc.name))


def always_locations(world: "LASWorld") -> List[Location]:
    """This world's ALWAYS locations that are shuffled and hold a progression or useful item a hint can name."""
    multiworld = world.multiworld
    known = known_locations(world)
    found = []
    for lasr_location in ALWAYS:
        if lasr_location not in world.shuffled or lasr_location in known or \
                LOCATION_NAMES[lasr_location] in world.options.start_location_hints:
            continue
        location = world.get_location(LOCATION_NAMES[lasr_location])
        item = location.item
        if item is None or not (item.advancement or item.useful):
            continue
        if item.name in getattr(multiworld.worlds[item.player].options, "start_hints", ()):
            continue
        if item.player == world.player and item.name in ITEM_TABLE and ITEM_TABLE[item.name].lasr_name == "seashell":
            continue
        found.append(location)
    return found


def barren_areas(world: "LASWorld") -> List[str]:
    """This world's areas holding no progression item, anyone's, with at least BARREN_MIN locations whose item is not
    known anyway, in a stable order."""
    known = known_locations(world)
    progression: Set[str] = set()
    unknown: Dict[str, int] = {}
    for location in world.multiworld.get_locations(world.player):
        if " - " not in location.name:  # the goal
            continue
        if location.item is not None and location.item.advancement:
            progression.add(area(location))
        if isinstance(location.address, int) and LASR_LOCATIONS[location.name] not in known:
            unknown[area(location)] = unknown.get(area(location), 0) + 1
    return sorted(name for name, count in unknown.items() if count >= BARREN_MIN and name not in progression)


def wins_without(world: "LASWorld", location: Optional[Location], locations: List[Location]) -> bool:
    """Whether this slot reaches its goal with every item of the multiworld but the location's."""
    state = CollectionState(world.multiworld)
    state.sweep_for_advancements([loc for loc in locations if loc is not location])
    return world.multiworld.completion_condition[world.player](state)


def _always(world: "LASWorld", locations: List[Location]) -> Iterator[Hint]:
    for location in locations:
        item = location.item
        if item.player == world.player:
            text = hint_text(world, location)
        else:
            text = f"{location.name} holds {world.multiworld.player_name[item.player]}'s {item.name}."
        yield Hint("always", text, location, True)


def _required(world: "LASWorld", order: List[Location]) -> Iterator[Hint]:
    """Tests the locations in their order, as hints are wanted, naming each area or other world's location once."""
    multiworld = world.multiworld
    locations = multiworld.get_filled_locations()
    if not wins_without(world, None, locations):  # not beatable at all: nothing is required more than the rest
        return
    named = set()
    for location in order:
        own = location.player == world.player
        key = area(location) if own else (location.player, location.name)
        if key in named or wins_without(world, location, locations):
            continue
        named.add(key)
        if own:
            text = f"Something in {area(location)} is needed to {GOAL}."
        else:
            text = f"Something in {multiworld.player_name[location.player]}'s world at {location.name} is " \
                   f"needed to {GOAL}."
        yield Hint("required", text, location, False)


def _barren(areas: Iterable[str]) -> Iterator[Hint]:
    for name in areas:
        yield Hint("barren", f"{name} holds nothing anyone needs.", None, False)


def _area(world: "LASWorld", order: List[Location], used: Set[Location]) -> Iterator[Hint]:
    texts = set()  # copies of an item in one area would give the same text
    for location in order:
        if location in used:
            continue
        if location.player == world.player:
            text = f"Your {location.item.name} is in {area(location)}."
        else:
            text = f"Your {location.item.name} is in {world.multiworld.player_name[location.player]}'s world."
        if text not in texts:
            texts.add(text)
            yield Hint("area", text, location, False)


def _precise(world: "LASWorld", order: List[Location], used: Set[Location]) -> Iterator[Hint]:
    for location in order:
        if location not in used:
            yield Hint("precise", hint_text(world, location), location, True)


def choose_hints(world: "LASWorld") -> Dict[str, Dict[str, object]]:
    """Per source its kind, text and, in server mode for a hint naming one location, the location's player and id:
    {source: {kind, text, player, location}}."""
    options = world.options
    if options.in_game_hints == options.in_game_hints.option_off:
        return {}
    rng = world.random
    pool = candidates(world)
    progression = [loc for loc in pool if loc.item.advancement]
    useful = [loc for loc in pool if loc.item.useful and not loc.item.advancement]
    rng.shuffle(progression)
    rng.shuffle(useful)
    by_item = progression + useful
    # area and required hints: this world's locations first, as another world's says little
    by_world = sorted(progression, key=lambda loc: loc.player != world.player) + \
        sorted(useful, key=lambda loc: loc.player != world.player)
    required_order = [loc for loc in by_world if loc.item.advancement]
    always = always_locations(world)
    rng.shuffle(always)
    barren = barren_areas(world)
    rng.shuffle(barren)
    sources = list(SOURCES)
    rng.shuffle(sources)
    jokes = list(JOKES)
    rng.shuffle(jokes)

    used: Set[Location] = set()  # the locations hints name, which area and precise hints leave out
    kinds = {
        "always": _always(world, always),
        "required": _required(world, required_order),
        "barren": _barren(barren),
        "area": _area(world, by_world, used),
        "precise": _precise(world, by_item, used),
    }
    picked: List[Hint] = []

    def take(kind: str, count: int) -> None:
        for _ in range(min(count, len(SOURCES) - len(picked))):
            hint = next(kinds[kind], None)
            if hint is None:
                return
            # a required hint of this world names an area: a precise one there says more
            if hint.about is not None and (hint.kind != "required" or hint.about.player != world.player):
                used.add(hint.about)
            picked.append(hint)

    plan = DISTRIBUTIONS[options.hint_distribution.current_key]
    for kind, count in plan:
        if count != REST:
            take(kind, count)
    for kind, count in plan:
        if count == REST:
            take(kind, len(SOURCES) - len(picked))
    for kind in FALLBACK:
        take(kind, len(SOURCES) - len(picked))
    picked += [hint for _, hint in zip(range(SPARES), kinds["precise"])]

    server = options.in_game_hints == options.in_game_hints.option_server
    hints = {}
    jokes_used = 0
    for source in sources:
        # an owl statue's own gift would be no news
        own = LOCATION_NAMES.get(source)
        index = next((i for i, h in enumerate(picked) if h.about is None or not (
            h.about.player == world.player and h.about.name == own)), None)
        if index is None:
            hints[source] = {"kind": "joke", "text": jokes[jokes_used % len(jokes)]}
            jokes_used += 1
            continue
        hint = picked.pop(index)
        hints[source] = {"kind": hint.kind, "text": hint.text}
        if server and hint.server and hint.about is not None:
            hints[source].update(player=hint.about.player, location=hint.about.address)
    return {source: hints[source] for source in SOURCES}
