"""The drop actor (DESIGN.md, Drop actor): every location LASR shows as a stand-in item (a small key actor for the item
that drops out of a cut bush, a lifted rock or pot, a bonked tree or a bombed hole, a dungeon's falling key, a Kanalet
Castle golden leaf; a buried slime key for a dig spot) shows it as ItemAPDrop, an actor type the module adds to the
game's actor table (ap_drop.cpp), instead. No small key or slime key stand-in is left in the rooms, and the mod keeps the
game's small key flowchart (mod.py skips LASR's). The bosses' items, the ghost's reward, the Color Dungeon's blue
rupees and, with Bouncy Items, the chests' and owl statues' gifts are drop actors too.

The actor has an Npc.gsheet row of its own (the small key's data: the model, the take entry and the shell sensor from
LEB parameters 1 to 4) and its own flowchart, made here: the pops and one take entry per location. LEB parameter 0 picks
how it appears (Motion); a tree's or hole's first LEB link names the tree or the hole's dropper it leaves.
The module centres and sizes each drop's model by its bounds (scripts/drop_bounds.py).

With the Bouncy Items option the drop is a ball (its own collision and physics traits; the module launches it at random
and spins it, config.ini's bouncy_items), and each chest's item jumps out of it when it opens, instead of the item-get.
"""
import copy
import math
from enum import IntEnum
from pathlib import Path

import ap_option_edits
import evfl
from RandomizerCore.Randomizers.chests import CHEST_ROOMS
from RandomizerCore.Randomizers.seashells import GRASS_ROOMS, HOLE_ROOMS, ROCK_ROOMS, SHELL_FLAGS, SHELL_ROOMS, TREE_ROOMS
from RandomizerCore.Randomizers.small_keys import GOLDEN_LEAF_ROOMS, SMALL_KEY_ROOMS
from RandomizerCore.Tools import event_tools
from RandomizerCore.Tools.gsheet import FloatArray
from RandomizerCore.Tools.leb import Room
from RandomizerCore.Tools.oead_tools import dictToStruct, parseStruct

DROP_ACTOR = "ItemAPDrop"  # must match AP::DROP_ACTOR: the actor table's name and the Npc row
DROP_ACTOR_ID = 0xA7  # must match AP::DROP_ACTOR_ID: an empty id of the game's actor table
BOUNCY_SETTING = "Bouncy Items"  # the world's option (lasr_settings), config.ini's [options] bouncy_items
BALL_ROW = "ItemAPDropBall"  # the drop's Npc row with Bouncy Items, which the module swaps in (ap_options.cpp)


class Motion(IntEnum):
    """LEB parameter 0: how the drop appears (must match AP::DropMotion)."""
    HOPS = 0  # up out of the ground just above its spawn: a bush's, rock's or pot's
    TREE = 1  # on the arc from the crown of the tree its first LEB link names onto its rail link's point
    HOLE = 2  # so from above the hole's AreaShellDropper
    FALLS = 3  # onto its spawn from above: a dungeon key, a golden leaf
    REFALLS = 4  # so again whenever Link enters its zone (the small key's "ReFall")
    BURIED = 5  # hops out when dug up where it is buried: a dig spot
    CHEST = 6  # Bouncy Items: launched from the lid of the chest its first LEB link names onto a spot near its spawn
    OWL = 7  # Bouncy Items: so off the top of the owl statue its first LEB link names
    BOSS = 8  # up out of the ground where the boss its first LEB link names starts its item, in its place
    BOSS_PIT = 9  # so, but standing nowhere when its room loads: the item falls into a pit, and BOSS_PITS stands it below
    STANDS = 10  # shows where it stands, as a free-standing item: the ghost's reward at its grave
    PLACED = 11  # so from the start, its switch 0 turned on by the module when it is made: the blue rupees


SMALL_KEY_ID = 0xA9
SLIME_KEY_ID = 0x8A
SMALL_KEY_ROW = "ItemSmallKey"
SMALL_KEY_TRAITS = "SmallKey"
SMALL_KEY_EVENT = "SmallKey.bfevfl"  # LASR's edits are its stand-ins' entries: mod.py does not write it
REFALL = b"ReFall"
EVENT_FILE = "APDrop.bfevfl"
EVENT_ACTOR = "APDrop"
# The pops (switch 0 turns on while switch 1, the got flag, is off): a dungeon key or golden leaf falls as the small
# key with LASR's Key Animations, which the world always sets: the drop sound, the fall, and the player's control back
# after 2 seconds; any other comes out at once. A trigger's actor parameter condition (category 4) compares LEB
# parameter 0; the first trigger whose conditions hold runs.
POP_TRIGGER = 14
TAKE_TRIGGER = 0
SWITCH_CONDITION = 8
PARAMETER_CONDITION = 4
POP = "pop"
FALL = "fall"
FALL_SOUND = "SE_SY_NAZOKAGI_DROP"
FALL_WAIT = 2.0  # with Bouncy Items none: the ball's physics (ap_drop.cpp, ItemUpdate) do not run during events
TAKE_ENTRY = "$3"
SHELL_SENSOR = "$4"
# Not laid out once switch 1 (the got flag) is on
TAKEN_LAYOUT = {"category": SWITCH_CONDITION, "parameter": "1", "layoutID": -1}
LEVEL_FLAG = 0
GLOBAL_FLAG = 1
NO_SWITCH = (4, 0)

# Bouncy Items: a ball that bounces whatever it lands on (PhysX's combine mode max) and spins freely, little slowed by
# the air; the model turns with it. On the ground the module damps its spin (ap_drop.cpp, ItemUpdate)
BOUNCINESS = 0.7  # ap_drop.cpp's kWallBounce the same
BOUNCE_COMBINE_MAX = 3
DRAG = 0.05
ANGULAR_DAMP = 0.05
SPHERE_SHAPE = 1  # Npc row collision shape: parameters [radius] (ItemRecoveryHeart's)
SPHERE_RADIUS = 0.3  # resting on its bottom: the centre that high above the actor's root

ROOMS = {**GRASS_ROOMS, **ROCK_ROOMS, **TREE_ROOMS, **HOLE_ROOMS}
ROOM_MOTIONS = {**{location: Motion.TREE for location in TREE_ROOMS}, **{location: Motion.HOLE for location in HOLE_ROOMS}}
# the motions whose first LEB link names the actor they come from
LINKED = (Motion.TREE, Motion.HOLE, Motion.CHEST, Motion.OWL, Motion.BOSS, Motion.BOSS_PIT)
LAUNCHED = (Motion.CHEST, Motion.OWL)  # launched by a launch flag of their own (_launched_drop)
# Dungeon keys and golden leaves: LASR's stand-ins, which fall from above when their switch 0 (a global flag) turns
# on. pothole-final, which LASR lists with the small keys, is a dig. The sunken key stands in two rooms, LASR writing
# both.
KEY_ROOMS = {location: [room] for location, room in {**SMALL_KEY_ROOMS, **GOLDEN_LEAF_ROOMS}.items()
             if location not in SHELL_ROOMS}
KEY_ROOMS["D4-sunken-item"].append("Lv04AnglersTunnel_06A")
# Dig spots: LASR's buried slime keys (the sunken seashells are heart piece actors, which stay)
DIG_ROOMS = SHELL_ROOMS
# Boss drops: each boss drops its item (a heart container, the Angler Key, the Face Key, the hookshot) as in the game
# (its BossItemDrop event; the bosses' flowcharts and drop rows stay the game's), and the module makes the boss's drop actor, linked to the boss
# and holding the location's item (without Boss Drops its vanilla one), pop out of the ground where the item would
# start, in place of it (ap_drop.cpp), turning its switch 0 (BOSS_FLAG) on; the game drops the item again when the room
# loads while it is not had, and the drop stands there (Lanmola, not made again, drops nothing: BOSS_PITS). Switch 1 is
# a got flag of its own (GOT_FLAG).
# Per location: the boss's actor (Enemy<name>), its room (Master Stalfos's last fight's).
BOSS_DROPS = {
    "D1-moldorm": ("DeguTail", "Lv01TailCave_04G"),
    "D2-genie": ("PotDemonKing", "Lv02BottleGrotto_04G"),
    "D3-slime-eye": ("DeguZol", "Lv03KeyCavern_07G"),
    "D4-angler": ("Angler", "Lv04AnglersTunnel_04AL"),
    "D5-slime-eel": ("Hooker", "Lv05CatfishsMaw_02D"),
    "D6-facade": ("MatFace", "Lv06FaceShrine_04E"),
    "D7-eagle": ("Albatoss", "Lv07EagleTower_05HL"),
    "D8-hothead": ("DeguFlame", "Lv08TurtleRock_02D"),
    "lanmola": ("Lanmola", "Field_13O"),
    "armos-knight": ("DeguArmos", "ArmosShrine_02A"),
    "D5-master-stalfos": ("MasterStalfon", "Lv05CatfishsMaw_01B"),
}
# A boss's item that falls into a pit: Lanmola's, into the desert's pit at Lanmola's spot, below which the game stands
# the item in the cave (LASR removes it, RoomFixes). The boss's drop is a BOSS_PIT, and a drop stands where that item
# does, switch 0 BOSS_FLAG. Per location: the room, the item's actor (in the RomFS's room)
BOSS_PITS = {"lanmola": ("LanmolaCave_02A", "ItemAnglerKey")}
BOSS_FLAG = "APBossDrop_{}"
GOT_FLAG = "APDropGet_{}"
# The ghost's reward (ghost-reward): the game gives a bottle in the ghost's farewell at its grave (Field.bfevfl,
# Graveyard_talk, which turns Ghost4_Clear on), and LASR the location's item in the event after it (mod.py skips
# that). A drop stands where Link takes it, the spot of the farewell's trigger (TagAreaEvent) on the grave's floor,
# and shows once the farewell turned its switch 0 (GHOST_FLAG) on. Its room, the trigger's event entry, the grave
GHOST_DROP = "ghost-reward"
GHOST_ROOM, GHOST_TRIGGER, GHOST_GRAVE = "Field_07E", b"Graveyard_talk", "ObjGhostsGrave"
GHOST_FLAG = "Ghost4_Clear"
# The Color Dungeon's blue rupees (D0-rupee-1 to -28: the room's rupees in turn): each rupee made a drop actor where
# it stands. Its switch 1 is the rupee's own got flag (its switch 0, Lv10RupeeGet...); switch 0 a level flag of the
# drops' own, which the module turns on when one is made and not taken, so it stands there from the start.
BLUE_RUPEE_ROOM = "Lv10ClothesDungeon_08D"
RUPEE_ACTOR = 0xAB
BLUE_RUPEES = [f"D0-rupee-{n}" for n in range(1, 29)]
# Bouncy Items, chests: a drop actor in front of each AP chest, linked to it. The chest holds DROP_CHEST_KEY, for which
# TreasureBox.bfevfl plays no item-get but turns its switch LAUNCHED_SWITCH on (a level flag of its own, not saved),
# the drop's switch 0: the ball pops out. The chest's opened flag (its switch 1, saved), which opening it set, is the
# drop's switch DROP_OPENED: it stays on while the ball is out, and goes off when the chest's room loads with no ball
# out and the item not taken (ap_drop.cpp, DropStaysOut), so the chest is closed again. Each of the five chests of
# FIVE_CHESTS, which LASR all gives the location's item, has a drop, and their switch 1 is a got flag of their own
# (GOT_FLAG), which taking one sets: the puzzle closes the chests again when Link leaves.
# Left out: the two Eagle's Tower chests whose room is copied for the tower's collapse.
CHEST_ACTOR = 0xF7
CHEST_OPENED = 1
# Not switch 1, which the drop's row lays out no drop for once on: the opened flag is on when the ball pops.
# AP::DROP_OPENED_SWITCH in the module
DROP_OPENED = 2
DROP_CHEST_KEY = "APDropChest"
FIVE_CHESTS = "taltal-5-chest-puzzle"
CHEST_SKIPPED = {"D7-grim-creeper", "D7-3f-horseheads"}
CHEST_LOCATIONS = [location for location in CHEST_ROOMS if location not in CHEST_SKIPPED]
# added by mod.py
FLAGS = [flag.format(location) for location in BOSS_DROPS for flag in (BOSS_FLAG, GOT_FLAG)] + [
    GOT_FLAG.format(location) for location in (GHOST_DROP, FIVE_CHESTS)]
# Bouncy Items, owl statues: a drop actor in front of each owl statue that gives an item (LASR's Owl Gifts), linked to
# it. LASR's gift in the statue's event (FieldOwlStatue.bfevfl, DungeonOwlStatue.bfevfl), behind a check of the
# location's flag, turns the statue's LAUNCHED_SWITCH on instead, the drop's switch 0, and taking the drop sets the
# location's flag (its switch 1), so examining the statue launches it until it is taken. Statue actor: its flowchart
# and event actor; the rooms with owl statues.
OWL_FLOWS = {0xD2: "FieldOwlStatue", 0xF9: "DungeonOwlStatue"}
OWL_ROOMS = (
    "Field_02B", "Field_02O", "Field_04F", "Field_06N", "Field_09P", "Field_11I", "Field_13G", "Field_13P", "Field_14K",
    "Lv01TailCave_04B", "Lv01TailCave_04C", "Lv01TailCave_05F", "Lv02BottleGrotto_04C", "Lv02BottleGrotto_06G",
    "Lv02BottleGrotto_07E", "Lv03KeyCavern_01A", "Lv03KeyCavern_02D", "Lv03KeyCavern_03G", "Lv04AnglersTunnel_05E",
    "Lv05CatfishsMaw_03C", "Lv05CatfishsMaw_06F", "Lv06FaceShrine_03G", "Lv06FaceShrine_04D", "Lv06FaceShrine_08G",
    "Lv07EagleTower_02D", "Lv07EagleTower_04B", "Lv07EagleTower_05D", "Lv08TurtleRock_04C", "Lv08TurtleRock_04G",
    "Lv08TurtleRock_06H", "Lv10ClothesDungeon_05F", "Lv10ClothesDungeon_06C", "Lv10ClothesDungeon_07D",
)
OWL_MARK = "owl-statue"  # in every owl statue location's name
# The chest's or statue's switch that launches its drop: unused by both
LAUNCHED_SWITCH = 2
# A launch flag: a level flag that no actor of its level's rooms uses, nor a flowchart of the RomFS by its index
# (EventFlags SetLevelFlag and CheckLevelFlag), counted down from LEVEL_FLAG_TOP (dungeon rooms use 255)
FLOW_LEVEL_FLAGS = {5, 11, 61, 81}
LEVEL_FLAG_TOP = 254
# A launched drop's spawn, the way it is launched: the tile in front of the chest or statue, from which Link opens or
# examines it, free floor (a tile further can be a pot's, as in Bottle Grotto's entrance)
FRONT = 1.5
OPEN_EVENTS = ("Event32", "Event15")  # TreasureBox.bfevfl: BoxOpen in TreasureBox_Open and _ShockOpen


def add(proc, placeholder_key: str) -> None:
    """Bouncy Items' parts are in every mod, the module turning them on. It changes the AP chests' item, so it runs
    after ap_map, which finds them by it."""
    files = proc.file_manager
    _npc_row(files)
    _traits_row(files)
    flow = _flow()
    for location, room in ROOMS.items():
        _room_actor(files, room, location, ROOM_MOTIONS.get(location, Motion.HOPS))
        _take_entry(proc, flow.flowchart, location)
    for location, rooms in KEY_ROOMS.items():
        for room in rooms:
            _room_actor(files, room, location, Motion.FALLS)
        _take_entry(proc, flow.flowchart, location)
    for location, room in DIG_ROOMS.items():
        if _dig_actor(proc, room, location):
            _take_entry(proc, flow.flowchart, location)
    _bosses(proc, flow)
    _ghost(proc, flow)
    level_flags = {}
    _blue_rupees(proc, flow, level_flags)
    _chests(proc, flow, level_flags, placeholder_key)
    _owls(proc, flow, level_flags)
    files.writeFile(EVENT_FILE, flow)


def _npc_row(files) -> None:
    """The small key's row (its model, take entry and shell sensor from LEB parameters, its collision and physics
    traits) with the drop's flowchart, pops and layout; and BALL_ROW, the same with a ball's collision, which takes its
    place with Bouncy Items."""
    sheet = files.readFile("Npc.gsheet")
    row = copy.deepcopy(next(parseStruct(row) for row in sheet["values"] if row["symbol"] == SMALL_KEY_ROW))
    row["symbol"] = DROP_ACTOR
    row["eventInfo"] = {"eventAsset": EVENT_FILE, "actorName": EVENT_ACTOR}
    not_taken = {"category": SWITCH_CONDITION, "parameter": "!1"}
    row["eventTriggers"] = [
        *({"condition": POP_TRIGGER, "entryPoint": FALL, "additionalConditions": [
            not_taken, {"category": PARAMETER_CONDITION, "parameter": str(int(motion))}]}
          for motion in (Motion.FALLS, Motion.REFALLS)),
        {"condition": POP_TRIGGER, "entryPoint": POP, "additionalConditions": [not_taken]},
        {"condition": TAKE_TRIGGER, "entryPoint": TAKE_ENTRY, "additionalConditions": []},
    ]
    row["layoutConditions"] = [TAKEN_LAYOUT]
    row["shellSensor"] = [{"category": 9, "parameter": SHELL_SENSOR}]
    sheet["values"].append(dictToStruct(row))
    row["symbol"] = BALL_ROW
    row["collision"]["traits"] = DROP_ACTOR
    row["collision"]["shape"] = SPHERE_SHAPE
    row["collision"]["parameters"] = FloatArray([SPHERE_RADIUS])  # dictToStruct keeps only lists of structs
    row["collision"]["offset"] = {"x": 0.0, "y": SPHERE_RADIUS, "z": 0.0}
    sheet["values"].append(dictToStruct(row))
    files.writeFile("Npc.gsheet", sheet)


def _traits_row(files) -> None:
    sheet = files.readFile("NpcPhysicsTraits.gsheet")
    row = copy.deepcopy(next(parseStruct(r) for r in sheet["values"] if r["key"] == SMALL_KEY_TRAITS))
    row["key"] = DROP_ACTOR
    row["bounciness"] = BOUNCINESS
    row["bounceCombine"] = BOUNCE_COMBINE_MAX
    row["drag"] = DRAG
    row["angularDamp"] = ANGULAR_DAMP
    for axis in ("rotationX", "rotationY", "rotationZ"):
        row["axisLock"][axis] = False
    sheet["values"].append(dictToStruct(row))
    files.writeFile("NpcPhysicsTraits.gsheet", sheet)


def _flow():
    """The drop's flowchart, with its pops, a fall waiting unless it bounces; the take entries come per location
    (_take_entry)."""
    flow = evfl.EventFlow()
    flow.name = EVENT_ACTOR
    flow.flowchart = evfl.Flowchart()
    flow.flowchart.name = EVENT_ACTOR
    event_tools.addEntryPoint(flow.flowchart, POP)
    event_tools.createActionChain(flow.flowchart, POP, [
        (EVENT_ACTOR, "Activate", {}),
        (EVENT_ACTOR, "Fall", {}),
    ])
    event_tools.addEntryPoint(flow.flowchart, FALL)
    wait = event_tools.createActionEvent(flow.flowchart, "Timer", "Wait", {"time": FALL_WAIT})
    bounces = event_tools.createSwitchEvent(flow.flowchart, "EventFlags", "CheckFlag",
                                            {"symbol": ap_option_edits.flag("bouncy_items")}, {0: wait, 1: None})
    event_tools.createActionChain(flow.flowchart, FALL, [
        (EVENT_ACTOR, "Activate", {}),
        (EVENT_ACTOR, "PlayOneshotSE", {"label": FALL_SOUND, "volume": 1.0, "pitch": 1.0}),
        (EVENT_ACTOR, "Fall", {}),
    ], bounces)
    return flow


def _take_entry(proc, flowchart, location: str, got_switches: tuple = (1,)) -> None:
    """LASR's take entry for a small key stand-in (seashells.writeKeyEvent), for the drop actor, turning the switches
    on. A second entry of the same name would break the flowchart's entry dictionary, which the game looks every entry
    up in."""
    if event_tools.findEntryPoint(flowchart, location) is not None:
        raise ValueError(f"{EVENT_FILE} has an entry {location} already")
    item_event = proc.item_get_manager.get(flowchart, location)
    event_tools.addEntryPoint(flowchart, location)
    event_tools.createActionChain(flowchart, location, [
        (EVENT_ACTOR, "Deactivate", {}),
        *((EVENT_ACTOR, "SetActorSwitch", {"value": True, "switchIndex": switch}) for switch in got_switches),
        (EVENT_ACTOR, "Destroy", {}),
    ], item_event)


def _make_drop(actor) -> None:
    actor.type = DROP_ACTOR_ID
    actor.name = f"{DROP_ACTOR}-{actor.key:016X}".encode()


def _room_actor(files, room_name: str, location: str, motion: Motion) -> None:
    """LASR's small key stand-in made a drop actor. A tree's or hole's links to
    the tree or dropper, the actor that sets its switch 0 (LASR gives both the same)."""
    room_file = f"{room_name}.leb"
    room = files.readFile(room_file)
    # the small key whose take entry (LEB parameter 3) is the location
    drop = next(a for a in room.actors if a.type == SMALL_KEY_ID and a.parameters[3] == location.encode())
    _make_drop(drop)
    if motion == Motion.FALLS and drop.parameters[0] == REFALL:
        motion = Motion.REFALLS
    drop.parameters[0] = int(motion)
    if motion in LINKED:
        if tuple(drop.switches[0]) == NO_SWITCH:
            raise ValueError(f"{room_name}: {location}'s stand-in has no switch 0 to find its tree or dropper by")
        source = next(i for i, a in enumerate(room.actors) if a is not drop and a.switches[0] == drop.switches[0])
        drop.relationships.section_1 = [[[0, 0], source]]
        drop.relationships.x = 1
    files.writeFile(room_file, room)


def _dig_actor(proc, room_name: str, location: str) -> bool:
    """LASR's buried slime key made a buried drop actor (False for a sunken seashell): its model, take entry and shell
    sensor (the slime key's LEB parameters 1, 2, 3 and 5), switch 1 the location's got flag."""
    files = proc.file_manager
    room_file = f"{room_name}.leb"
    room = files.readFile(room_file)
    drop = next((a for a in room.actors if a.type == SLIME_KEY_ID and a.parameters[3] == location.encode()), None)
    if drop is None:
        return False
    _make_drop(drop)
    drop.rotY = 0.0
    drop.scaleX = drop.scaleY = drop.scaleZ = 1.0
    drop.parameters = [int(Motion.BURIED), *drop.parameters[1:4], drop.parameters[5], 0, 0, 0]
    drop.switches = [NO_SWITCH, (GLOBAL_FLAG, proc.flag_manager.flags[SHELL_FLAGS[location]]), NO_SWITCH, NO_SWITCH]
    files.writeFile(room_file, room)
    return True


def _model(proc, location: str) -> tuple:
    """The model file and model name of the location's item."""
    _, _, model_path, model_name = proc.item_info_manager.getItemInfoWithModel(location)
    return model_path, model_name


def _new_drop(room, source, location: str, motion: Motion, model: tuple):
    """A drop actor for the location at `source`'s place (a copy of its record, unturned, unscaled, without links),
    appended to the room."""
    drop = copy.deepcopy(source)
    keys = {a.key for a in room.actors}
    drop.key = source.key + 1
    while drop.key in keys:
        drop.key += 1
    _make_drop(drop)
    drop.rotY = 0.0
    drop.scaleX = drop.scaleY = drop.scaleZ = 1.0
    drop.parameters = [int(motion), model[0].encode(), model[1].encode(), location.encode(), b"false", 0, 0, 0]
    links = drop.relationships
    links.section_1, links.x = [], 0
    links.section_2, links.z = [], 0
    links.section_3, links.y = [], 0
    room.actors.append(drop)
    return drop


def _launched_drop(files, room_file: str, room, index: int, location: str, motion: Motion, model: tuple,
                   level_flags: dict):
    """A drop actor that the room's actor at `index` (a chest, an owl statue) launches: linked to it, on the tile in
    front of it, and the actor's LAUNCHED_SWITCH and the drop's switch 0 a launch flag of their own. Appended."""
    source = room.actors[index]
    if tuple(source.switches[LAUNCHED_SWITCH]) != NO_SWITCH:
        raise ValueError(f"{room_file}: {source.name.decode()}'s switch {LAUNCHED_SWITCH} is in use")
    drop = _new_drop(room, source, location, motion, model)
    turn = math.radians(source.rotY)
    drop.posX = source.posX + math.sin(turn) * FRONT
    drop.posZ = source.posZ + math.cos(turn) * FRONT
    launched = (LEVEL_FLAG, _free_level_flag(files, room_file, level_flags))
    source.switches[LAUNCHED_SWITCH] = launched
    drop.switches = [launched, NO_SWITCH, NO_SWITCH, NO_SWITCH]
    drop.relationships.section_1, drop.relationships.x = [[[0, 0], index]], 1
    return drop


def _bosses(proc, flow) -> None:
    files = proc.file_manager
    flags = proc.flag_manager.flags
    for location, (boss, room_name) in BOSS_DROPS.items():
        room_file = f"{room_name}.leb"
        room = files.readFile(room_file)
        # the boss's LEB actor, named by its actor table name, cut short (EnemyMasterS)
        name = f"Enemy{boss}"
        index, source = next((i, a) for i, a in enumerate(room.actors)
                             if len(short := a.name.split(b"-")[0].decode()) > len("Enemy") and name.startswith(short))
        model = _model(proc, location)
        switches = [(GLOBAL_FLAG, flags[BOSS_FLAG.format(location)]), (GLOBAL_FLAG, flags[GOT_FLAG.format(location)]),
                    NO_SWITCH, NO_SWITCH]
        drop = _new_drop(room, source, location, Motion.BOSS_PIT if location in BOSS_PITS else Motion.BOSS, model)
        drop.relationships.section_1, drop.relationships.x = [[[0, 0], index]], 1
        drop.switches = list(switches)
        files.writeFile(room_file, room)
        if location in BOSS_PITS:
            pit_file = f"{BOSS_PITS[location][0]}.leb"
            pit = files.readFile(pit_file)
            vanilla = Room((Path(files.parent.rom_path) / files.getRelativeDir(pit_file) / pit_file).read_bytes())
            item = next(a for a in vanilla.actors if a.name.split(b"-")[0].decode() == BOSS_PITS[location][1])
            drop = _new_drop(pit, item, location, Motion.HOPS, model)
            drop.switches = list(switches)
            files.writeFile(pit_file, pit)
        _take_entry(proc, flow.flowchart, location)


def _ghost(proc, flow) -> None:
    files = proc.file_manager
    flags = proc.flag_manager.flags
    room_file = f"{GHOST_ROOM}.leb"
    room = files.readFile(room_file)
    trigger = next(a for a in room.actors if a.parameters[0] == GHOST_TRIGGER)
    grave = next(a for a in room.actors if a.name.split(b"-")[0].decode() == GHOST_GRAVE)
    model = _model(proc, GHOST_DROP)
    drop = _new_drop(room, trigger, GHOST_DROP, Motion.STANDS, model)
    drop.posY = grave.posY
    drop.switches = [(GLOBAL_FLAG, flags[GHOST_FLAG]), (GLOBAL_FLAG, flags[GOT_FLAG.format(GHOST_DROP)]), NO_SWITCH,
                     NO_SWITCH]
    files.writeFile(room_file, room)
    _take_entry(proc, flow.flowchart, GHOST_DROP)


def _blue_rupees(proc, flow, level_flags: dict) -> None:
    files = proc.file_manager
    room_file = f"{BLUE_RUPEE_ROOM}.leb"
    room = files.readFile(room_file)
    rupees = [a for a in room.actors if a.type == RUPEE_ACTOR]
    if len(rupees) != len(BLUE_RUPEES):
        raise ValueError(f"{room_file}: {len(rupees)} blue rupees, want {len(BLUE_RUPEES)}")
    shown = (LEVEL_FLAG, _free_level_flag(files, room_file, level_flags))
    for location, drop in zip(BLUE_RUPEES, rupees):
        model = _model(proc, location)
        got = tuple(drop.switches[0])
        _make_drop(drop)
        drop.rotY = 0.0
        drop.scaleX = drop.scaleY = drop.scaleZ = 1.0
        drop.parameters = [int(Motion.PLACED), model[0].encode(), model[1].encode(), location.encode(), b"false", 0, 0,
                           0]
        drop.switches = [shown, got, NO_SWITCH, NO_SWITCH]
        _take_entry(proc, flow.flowchart, location)
    files.writeFile(room_file, room)


def _chests(proc, flow, level_flags: dict, placeholder_key: str) -> None:
    files = proc.file_manager
    for location in CHEST_LOCATIONS:
        room_file = f"{CHEST_ROOMS[location]}.leb"
        room = files.readFile(room_file)
        chests = [i for i, a in enumerate(room.actors) if a.type == CHEST_ACTOR]
        model = _model(proc, location)
        five = location == FIVE_CHESTS
        for index in chests if five else chests[:1]:
            chest = room.actors[index]
            drop = _launched_drop(files, room_file, room, index, location, Motion.CHEST, model, level_flags)
            drop.switches[DROP_OPENED] = chest.switches[CHEST_OPENED]
            if five:
                drop.switches[1] = (GLOBAL_FLAG, proc.flag_manager.flags[GOT_FLAG.format(location)])
            chest.parameters[1] = DROP_CHEST_KEY.encode()
        files.writeFile(room_file, room)
        _take_entry(proc, flow.flowchart, location, (DROP_OPENED, 1) if five else (DROP_OPENED,))
    _chest_skip(files, placeholder_key)


def _owls(proc, flow, level_flags: dict) -> None:
    files = proc.file_manager
    charts = {}
    changed = set()
    for room_name in OWL_ROOMS:
        room_file = f"{room_name}.leb"
        room = files.readFile(room_file)
        statues = [i for i, a in enumerate(room.actors) if a.type in OWL_FLOWS]
        launched = False
        for index in statues:
            actor = OWL_FLOWS[room.actors[index].type]
            if actor not in charts:
                charts[actor] = files.readFile(f"{actor}.bfevfl")
            chart = charts[actor].flowchart
            gift = _owl_gift(chart, room.actors[index].parameters[0].decode())
            if gift is None:
                continue
            location = gift.data.params.data["symbol"]
            model = _model(proc, location)
            drop = _launched_drop(files, room_file, room, index, location, Motion.OWL, model, level_flags)
            drop.switches[1] = (GLOBAL_FLAG, proc.flag_manager.flags[location])
            launch = event_tools.createActionEvent(chart, actor, "SetActorSwitch",
                                                   {"switchIndex": LAUNCHED_SWITCH, "value": True})
            with ap_option_edits.gated(chart, "bouncy_items"):
                event_tools.setSwitchEventCase(chart, gift.name, 0, launch)
            _take_entry(proc, flow.flowchart, location)
            launched = True
            changed.add(actor)
        if launched:
            files.writeFile(room_file, room)
    for actor in changed:
        files.writeFile(f"{actor}.bfevfl", charts[actor])


def _owl_gift(chart, entry: str):
    """LASR's gift in an owl statue's event: the first check of an owl location's flag the entry leads to (not into
    subflows), or None (no entry, or an owl without an item)."""
    start = event_tools.findEntryPoint(chart, entry)
    todo = [start.main_event.v] if start is not None else []
    seen = set()
    while todo:
        event = todo.pop()
        if event is None or event.name in seen:
            continue
        seen.add(event.name)
        data = event.data
        if (isinstance(data, evfl.event.SwitchEvent) and data.actor.v.identifier.name == "EventFlags" and
                data.actor_query.v.v == "CheckFlag" and OWL_MARK in data.params.data.get("symbol", "")):
            return event
        todo += [link.v for link in (getattr(data, "nxt", None), getattr(data, "join", None)) if link is not None]
        todo += [case.v for case in (getattr(data, "cases", None) or {}).values()]
        todo += [fork.v for fork in getattr(data, "forks", None) or []]
    return None


def _free_level_flag(files, room_file: str, taken: dict) -> int:
    """The next launch flag of the room's level; `taken` holds each level's flags in use, filled on its first room
    from every room of the level (the RomFS's list, each room as the mod has it so far)."""
    level = files.getRelativeDir(room_file)
    if level not in taken:
        taken[level] = level_flags_used(files, level)
    used = taken[level]
    flag = next(index for index in range(LEVEL_FLAG_TOP, 0, -1) if index not in used)
    used.add(flag)
    return flag


def level_flags_used(files, level: Path) -> set:
    """The level flags that the level's actors (every room of the RomFS's level folder `level`, as `files` reads it)
    and the RomFS flowcharts use."""
    used = set(FLOW_LEVEL_FLAGS)
    for path in (Path(files.parent.rom_path) / level).glob("*.leb"):
        used.update(index for actor in files.readFile(path.name).actors
                    for kind, index in actor.switches if kind == LEVEL_FLAG)
    return used


def _chest_skip(files, placeholder_key: str) -> None:
    """A chest holding DROP_CHEST_KEY plays no item-get once open, nor PopItem (DROP_CHEST_KEY has no Items.gsheet
    row), with Bouncy Items: it turns its launch flag on, and its drop actor pops out. Without, it gives the AP
    placeholder as the other AP chests do (mod._chest_placeholder_branch). CompareString's case 0 is a
    match."""
    flow = files.readFile("TreasureBox.bfevfl")
    chart = flow.flowchart
    launch = event_tools.createActionEvent(chart, "TreasureBox", "SetActorSwitch",
                                           {"switchIndex": LAUNCHED_SWITCH, "value": True})
    auto_save = event_tools.findEvent(chart, "Event40").data.nxt.v.name  # LASR's RequestAutoSave
    get = event_tools.createActionChain(chart, None, [
        ("Inventory", "AddItemByKey", {"itemKey": placeholder_key, "count": 1,
                                       "index": evfl.common.Argument("itemIndex"), "autoEquip": False}),
        ("Link", "GenericItemGetSequenceByKey", {"itemKey": placeholder_key, "keepCarry": False,
                                                 "messageEntry": placeholder_key}),
    ], auto_save)
    bounces = event_tools.createSwitchEvent(chart, "EventFlags", "CheckFlag",
                                            {"symbol": ap_option_edits.flag("bouncy_items")}, {0: get, 1: launch})
    for event in OPEN_EVENTS:
        rest = event_tools.findEvent(chart, event).data.nxt.v.name
        skip = event_tools.createSwitchEvent(chart, "FlowControl", "CompareString",
                                             {"value1": "itemKey", "value2": DROP_CHEST_KEY}, {0: bounces, 1: rest})
        event_tools.insertEventAfter(chart, event, skip)
    files.writeFile("TreasureBox.bfevfl", flow)
