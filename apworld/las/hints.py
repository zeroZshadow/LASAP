"""In-game hints (option in_game_hints): owl statues and library books tell where this slot's items are.

Each hint source gets one location, picked once the multiworld is filled: a location in any world holding one of this
slot's progression items, then useful items, then a joke line. The patch carries the texts (`hints`); in server mode
also the location, which the client hints on the server when the game reports the hint as read."""
from typing import TYPE_CHECKING, Dict, List, Optional

from BaseClasses import Location

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


def source_name(source: str) -> str:
    """The source as the spoiler names it: the owl statue's location, or the book."""
    return BOOK_NAMES.get(source) or LOCATION_NAMES[source]


def hint_text(world: "LASWorld", location: Location) -> str:
    item = location.item.name
    if location.player == world.player:
        return f"Your {item} is at {location.name}."
    return f"Your {item} is in {world.multiworld.player_name[location.player]}'s world at {location.name}."


def candidates(world: "LASWorld") -> List[Location]:
    """Locations of the multiworld holding this slot's items that a hint can name, in a stable order."""
    multiworld = world.multiworld
    player = world.player
    known = set(world.locked) | (lasr_settings.kept_locations() & set(world.shuffled))
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


def choose_hints(world: "LASWorld") -> Dict[str, Dict[str, object]]:
    """Per source its text and, in server mode, the location's player and id: {source: {text, player, location}}."""
    options = world.options
    if options.in_game_hints == options.in_game_hints.option_off:
        return {}
    rng = world.random
    pool = candidates(world)
    progression = [loc for loc in pool if loc.item.advancement]
    useful = [loc for loc in pool if loc.item.useful and not loc.item.advancement]
    rng.shuffle(progression)
    rng.shuffle(useful)
    sources = list(SOURCES)
    rng.shuffle(sources)
    jokes = list(JOKES)
    rng.shuffle(jokes)
    used = set()
    jokes_used = 0
    server = options.in_game_hints == options.in_game_hints.option_server
    hints = {}
    for source in sources:
        # an owl statue's own gift would be no news
        own = LOCATION_NAMES.get(source)
        target: Optional[Location] = next((loc for loc in progression + useful if loc not in used and
                                           not (loc.player == world.player and loc.name == own)), None)
        if target is None:
            hints[source] = {"text": jokes[jokes_used % len(jokes)]}
            jokes_used += 1
            continue
        used.add(target)
        hints[source] = {"text": hint_text(world, target)}
        if server:
            hints[source].update(player=target.player, location=target.address)
    return {source: hints[source] for source in SOURCES}
