import importlib.util
import json
import os
import sys
from pathlib import Path

from BaseClasses import CollectionState, MultiWorld
from test.bases import WorldTestBase
from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister, call_all

from ..items import GAME, LASR_TO_ITEM
from ..lasr import INSTRUMENTS

ROOT = Path(os.path.realpath(__file__)).parents[3]
LOCAL_PATHS = ROOT / "scripts" / "local_paths.py"
PATCHER = ROOT / "patcher"
STEPS = ("generate_early", "create_regions", "create_items", "set_rules", "connect_entrances", "generate_basic")
OPTION_SETS = [
    {},
    {"logic": "advanced"},
    {"logic": "glitched"},
    {"logic": "hell"},
    {"open_mabe": False, "open_kanalet": False, "open_mamu": False, "completed_bridge": False,
     "classic_d2": True, "stealing": "never", "shuffled_bombs": False, "shuffled_powder": False},
    {"logic": "hell", "owl_statues": "all", "blue_rupees": True, "trade_quest": True, "dampe": True,
     "rapids": True, "fishing": True, "seashell_mansion": 50, "free_book": False, "fast_fishing": False,
     "fast_stalfos": False, "stealing": "always", "warp_points": True},
    {"chests": False, "free_gifts": False, "miscellaneous": False, "boss_drops": False, "instruments": "vanilla",
     "starting_instruments": 3, "seashell_mansion": 0, "dungeon_items": "start_with"},
    {"shuffled_dungeons": True},
    {"logic": "hell", "shuffled_dungeons": True, "owl_statues": "dungeons"},
]


class LASTestBase(WorldTestBase):
    game = "Links Awakening Switch"


def generate(options=None, seed=None, steps=STEPS) -> MultiWorld:
    """One LAS player with these options, through these generation steps."""
    return setup_multiworld(AutoWorldRegister.world_types[GAME], steps, seed, options or {})


def generate_world(options=None, seed=None, steps=STEPS):
    return generate(options, seed, steps).worlds[1]


def regenerate(slot_data: dict, seed: int, deferred: str = "default") -> MultiWorld:
    """What Universal Tracker does on connect: default options, another seed, the slot data passed through, its
    enforce_deferred_entrances setting (on, default, off)."""
    world_type = AutoWorldRegister.world_types[GAME]
    regen = setup_multiworld(world_type, (), seed)
    regen.generation_is_fake = True
    regen.enforce_deferred_connections = deferred
    regen.re_gen_passthrough = {GAME: world_type.interpret_slot_data(json.loads(json.dumps(slot_data)))}
    for step in STEPS:
        call_all(regen, step)
    return regen


def beatable(multiworld: MultiWorld, instruments) -> bool:
    """Whether the goal is reached with everything but the instruments, plus these."""
    world = multiworld.worlds[1]
    state = CollectionState(multiworld)
    for item in multiworld.itempool + world.get_pre_fill_items():
        if item.name not in {LASR_TO_ITEM[i] for i in INSTRUMENTS}:
            state.collect(item, prevent_sweep=True)
    for instrument in instruments:
        state.collect(world.create_item(LASR_TO_ITEM[instrument]), prevent_sweep=True)
    state.sweep_for_advancements()
    return multiworld.can_beat_game(state)


def patcher_import(*names, modbuild: bool = False):
    """Patcher modules by name (with `modbuild`, the asset build's too), without leaving them on sys.path."""
    saved = list(sys.path)
    sys.path.insert(0, str(PATCHER))
    if modbuild:
        sys.path.insert(0, str(PATCHER / "modbuild"))
    try:
        return [__import__(name, fromlist=["_"]) for name in names]
    finally:
        sys.path[:] = saved


def forget(*prefixes) -> None:
    for name in list(sys.modules):
        if name.split(".")[0] in prefixes:
            del sys.modules[name]


def client_context(game: bool = False, **attrs):
    """A LASContext without connections, with `attrs` set: (it, the messages it sends the server, the lines it writes
    to the game, with `game` a game connected)."""
    from ..client import LASContext
    ctx = LASContext(None, None, "127.0.0.1", 0)
    sent, lines = [], []

    async def send_msgs(msgs):
        sent.extend(msgs)

    class Writer:
        def write(self, data: bytes) -> None:
            lines.append(data.decode().rstrip("\n"))

    ctx.send_msgs = send_msgs
    if game:
        ctx.game_writer = Writer()
    for key, value in attrs.items():
        setattr(ctx, key, value)
    return ctx, sent, lines


def romfs() -> Path:
    """The RomFS dump the tests read: LASAP_ROMFS, else "romfs" in scripts/local_paths.json. Not configured, a folder
    that does not exist, so the tests that need the dump skip."""
    value = ""
    if LOCAL_PATHS.exists():
        spec = importlib.util.spec_from_file_location("local_paths", LOCAL_PATHS)
        local_paths = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(local_paths)
        value = local_paths.lookup("romfs")
    return Path(value) if value else LOCAL_PATHS.with_name("<no RomFS configured>")
