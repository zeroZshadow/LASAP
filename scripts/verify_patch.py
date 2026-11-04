"""Static checks on patcher output: every chest location holds the AP placeholder, the chests with a drop (Bouncy Items) the drop actor's
chest key, with its local id. The seed file's models name the model of every location `location_models` gives one, and the
item stand-in rows of the AP locations, and the rooms' item actors name the locations' tokens. An AP location's chests
name its chest token; with "Texture + Size" chests, the chests holding a seashell and only those are the seashell chest
(for an AP location, by its C line in the models). Every randomized heart piece and seashell actor has a unique index above
the received range (DESIGN.md, item model). With the map tracker on, every AP location but the optional ones has one icon in the seed file's map
and every icon its datasheet row and a name. The egg's event asks HasAllInstruments at its four
switches, and config.ini names the goal's numbers for the egg and its sign, and the save name. The shop lists the shop's AP
slots, and no flowchart asks CompareInt. Every in-game hint source shows its own message, a copy of the one it
replaces, and the hints hold the seed's hints, which fit the text box. With Shuffled Dungeons, the jumps lead
each dungeon door into the dungeon the patch's map names, and its ways out back out of that door, and the rooms keep
their vanilla targets; the markers swap the map's dungeon markers, whose rows stay vanilla.
The drop actor's flowchart reads and holds every drop's take entry, every drop has its model's bounds and a linked one
(a tree's, hole's, chest's, owl statue's or boss's) its link, and no small key or slime key stand-in is left. Each chest's launch flag (Bouncy Items) is
used by that chest and its drop alone, among all actors of its level (the RomFS's rooms, as the mod has them) and the
RomFS flowcharts' level flags; so is each owl statue's (Bouncy Items, Owl Gifts). The start holds the starting items,
the rupees and the start flags, which the start event neither gives nor sets.

python scripts/verify_patch.py <patch.aplas> <patcher output dir>
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher" / "modbuild"))
from local_paths import local_path  # noqa: E402
from mod import CHEST_FILES, SHOP_TEXT, START_FLAGS, chest_token  # noqa: E402  (LASR on the path)
import ap_chest  # noqa: E402
import ap_drop  # noqa: E402
import ap_model  # noqa: E402
import ap_goal  # noqa: E402
import evfl  # noqa: E402
from RandomizerCore.Randomizers.chests import CHEST_ROOMS  # noqa: E402
from RandomizerCore.Tools.leb import Room  # noqa: E402
from ap_save_name import save_name  # noqa: E402
import seed_file  # noqa: E402
from las_ap_patch import (COMPANION_FLAGS, INDEX_KINDS, PLACEHOLDER_KEY, START_MARKER, START_UPGRADES,  # noqa: E402
                          START_WITH_KINDS, MODEL_TOKEN, load_patch, load_yaml, marker_lines)

patch = load_patch(Path(sys.argv[1]))
seed = seed_file.read(Path(sys.argv[2]) / "config/lasr-exl")
item_defs = load_yaml("items.yml")["Item_Pool"]
models = patch.get("location_models", {})
level_dir = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/level"
ok = bad = 0
for loc, room_name in CHEST_ROOMS.items():
    item = patch["placements"].get(loc)
    if item != "ap-item":
        continue
    path = level_dir / room_name.split("_")[0] / f"{room_name}.leb"
    room = Room(path.read_bytes())
    chests = [a for a in room.actors if a.type == 0xF7]
    found = [(c.parameters[1], c.parameters[2]) for c in chests]
    key = PLACEHOLDER_KEY
    if loc in ap_drop.CHEST_LOCATIONS:  # Bouncy Items' chests, in every mod
        key = ap_drop.DROP_CHEST_KEY
    want = (key.encode(), patch["location_ids"][loc])
    if any(k == want[0] and v == want[1] for k, v in found):
        ok += 1
    else:
        bad += 1
        print(f"MISMATCH {loc} {room_name}: {found} want {want}")
print(f"chests ok={ok} bad={bad}")

# Models: one M line per location with a model of its own; the rooms' item actors name their location's token.
lines = [line.split() for line in seed.get("models", [])]
shown = {int(p[1]) for p in lines if p[0] == "M"}
want = {int(patch["location_ids"][loc]) for loc in models
        if loc in patch["location_ids"] and patch["placements"].get(loc) == "ap-item"
        and item_defs.get(models[loc], {}).get("model-path") and not item_defs[models[loc]]["item-key"].startswith("$")}
if shown != want:
    bad += 1
    print(f"MODELS for {sorted(shown ^ want)} differ from location_models")
stand_ins = {p[1] for p in lines if p[0] == "I"}
tokens = 0
for path in level_dir.rglob("*.leb"):
    for actor in Room(path.read_bytes()).actors:
        tokens += sum(1 for v in actor.parameters if isinstance(v, bytes) and v.startswith(MODEL_TOKEN.encode()))
print(f"models shown={len(shown)} stand-ins={len(stand_ins)} token parameters={tokens} bad={bad}")

# Chests: an AP location's chests name its chest token, the others their chest file. With "Texture + Size" every
# chest holding this slot's seashell, vanilla or a check, is the seashell chest, and no other chest is.
chest_files = {int(p[1]): p[3] for p in lines if p[0] == "C"}
looks = patch.get("location_chests", {})
textured = patch["settings"].get("Chest Types") == "Texture + Size"
blue = 0
for loc, room_name in CHEST_ROOMS.items():
    item = patch["placements"].get(loc)
    if item is None:
        continue
    path = level_dir / room_name.split("_")[0] / f"{room_name}.leb"
    chests = [a for a in Room(path.read_bytes()).actors if a.type == 0xF7]
    written = {c.parameters[6] for c in (chests[:5] if loc == "taltal-5-chest-puzzle" else chests[:1])}
    if item == "ap-item":
        local_id = int(patch["location_ids"][loc])
        if written != {chest_token(local_id).encode()}:
            bad += 1
            print(f"CHEST TOKEN {loc} {room_name}: {written}")
        shown = chest_files.get(local_id, CHEST_FILES["default"])
    else:
        shown = written.pop().decode() if len(written) == 1 else None
    want = textured and (item == "seashell" or (item == "ap-item" and looks.get(loc, [None, None])[1] == ap_chest.CHEST_TYPE))
    if (shown == ap_chest.FILE_NAME) != want:
        bad += 1
        print(f"CHEST TEXTURE {loc} {room_name}: {shown} for {item} {looks.get(loc)}")
    blue += want
print(f"chests shown={len(chest_files)} seashell={blue} bad={bad}")

# Map tracker: every AP location has one icon, and every icon its datasheet row.
if patch["settings"].get("Map Tracker"):
    import ap_map  # noqa: E402
    from RandomizerCore.Tools.oead_tools import readSheet  # noqa: E402
    out = Path(sys.argv[2])
    lines = [line.split() for line in seed.get("map", [])]
    overworld = {int(p[1]): [int(i) for i in p[2:]] for p in lines if p[0] == "O"}
    dots = {int(p[1]): (p[2], float(p[3]), float(p[4]), [int(i) for i in p[5:]]) for p in lines if p[0] == "M"}
    iconed = [i for ids in overworld.values() for i in ids] + [i for *_, ids in dots.values() for i in ids]
    positions = ap_map.load_positions()
    optional = set(patch.get("optional_locations", ()))
    shown = {loc: i for loc, i in patch["location_ids"].items() if loc not in optional}
    # overworld icons: overworld and dungeon checks (at the dungeon's entrance); dungeon dots: the dungeons' checks,
    # one per place (Eagle's Tower before and after its collapse)
    want = [int(i) for loc, i in shown.items() if loc in positions["overworld"]]
    want += [int(i) for loc, i in shown.items() for _ in range(1 + len(positions["dungeon"].get(loc, ())))
             if loc in positions["dungeon"]]
    if sorted(iconed) != sorted(want):
        bad += 1
        print(f"MAP ICONS cover {len(iconed)} locations, want {len(want)}")
    chests = [int(p[4]) for p in lines if p[0] == "C"]
    want = [int(i) for loc, i in patch["location_ids"].items() if CHEST_ROOMS.get(loc, "").startswith("Lv")
            for _ in {CHEST_ROOMS[loc]} | {room for room, *_ in positions["dungeon"].get(loc, ())}]
    if sorted(chests) != sorted(want):
        bad += 1
        print(f"MAP CHESTS found {len(chests)}, want {len(want)}")
    sheets = out / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/datasheets"
    flags = {str(row["mFirstShowFlagName"]) for row in readSheet(str(sheets / "UiFieldMapIcons.gsheet"))["values"]}
    missing = [g for g in overworld if any(f"{ap_map.FLAG_PREFIX}{mark}{g}" not in flags for mark in ap_map.LOOKS)]
    if missing:
        bad += 1
        print(f"MAP ROWS missing for groups {missing}")
    # every overworld icon has a name, which fits the hover box after the count of an icon of several checks
    names = {int(p[1]): " ".join(p[2:]) for p in lines if p[0] == "G"}
    unnamed = [g for g, ids in overworld.items()
               if not names.get(g) or (len(ids) > 1 and len(names[g]) > ap_map.NAME_WIDTH)]
    if unnamed:
        bad += 1
        print(f"MAP NAMES missing or too long for groups {[(g, names.get(g)) for g in unnamed]}")
    # every dungeon dot has a name; every location of an overworld icon or dungeon dot has its name for the check list
    dot_names = {int(p[1]) for p in lines if p[0] == "MG" and len(p) > 2}
    if sorted(dot_names) != sorted(dots):
        bad += 1
        print(f"MAP DOT NAMES missing for dots {sorted(set(dots) - dot_names)}")
    location_names = {int(p[1]) for p in lines if p[0] == "N" and len(p) > 2}
    nameless = [i for ids in overworld.values() for i in ids if i not in location_names]
    nameless += [i for *_, ids in dots.values() for i in ids if i not in location_names]
    if nameless:
        bad += 1
        print(f"MAP LOCATION NAMES missing for {nameless}")
    print(f"map icons overworld={len(overworld)} dungeon dots={len(dots)} locations={len(iconed)} bad={bad}")
# Inventory bits: every seashell, heart piece, golden leaf, heart container, bottle and chamber stone source once per kind
# and bit, with its local id when shuffled.
entries = [line.split() for line in seed.get("indices", [])]
ids = {int(i) for i in patch["location_ids"].values()}
counts = {}
for kind, index, local_id in entries:
    counts.setdefault(kind, set()).add(int(index))
    if int(local_id) >= 0 and int(local_id) not in ids:
        bad += 1
        print(f"INDICES {kind} {index}: {local_id} is not a location of the slot")
if len(entries) != sum(len(v) for v in counts.values()) or sorted(counts) != sorted(INDEX_KINDS.values()):
    bad += 1
    print(f"INDICES duplicate bits or missing kinds: {sorted(counts)}")
print("indices " + " ".join(f"{k}={len(v)}" for k, v in sorted(counts.items())) +
      f" vanilla={sum(1 for e in entries if e[2] == '-1')} bad={bad}")
# Kept locations: every AP location without the placeholder, with its vanilla item's key or its companion's flag.
kept = [line.split() for line in seed.get("kept", [])]
expected = {str(patch["location_ids"][loc]): COMPANION_FLAGS.get(loc) or item_defs[item]["item-key"]
            for loc, item in patch["placements"].items() if item != "ap-item" and loc in patch["location_ids"]}
found = {local_id: flag if key == "F" else key for key, flag, local_id in kept}
if found != expected or len(kept) != len(expected):
    bad += 1
    print(f"KEPT {kept} != {expected}")
print(f"kept locations={len(kept)} bad={bad}")

# In-game hints: in every patch each source shows its own message, a copy of the one it replaces, and no event sets
# its flag (the module does); the seed file's hints hold the seed's hints, each fitting the text box.
hints = patch.get("hints", {})
import ap_hints  # noqa: E402
from RandomizerCore.Tools.oead_tools import readSheet  # noqa: E402
from RandomizerCore.Tools.text_tools import TextFile  # noqa: E402
romfs = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs"
flows = {}
for name in (ap_hints.FIELD_FLOW, ap_hints.DUNGEON_FLOW, ap_hints.BOOK_FLOW):
    flow = evfl.EventFlow()
    flow.read((romfs / "region_common/event" / name).read_bytes())
    flows[name] = [dict(e.data.params.data) for e in flow.flowchart.events
                   if type(e.data).__name__ == "ActionEvent" and e.data.params is not None]
texts = {name: TextFile(romfs / "regionUS/USen/message" / name).getTextDict() for name in ("Hint.msbt", "System.msbt")}
sheet = readSheet(str(romfs / "region_common/datasheets/GlobalFlags.gsheet"))
global_flags = {str(row["symbol"]) for row in sheet["values"]}
copy_from = {**ap_hints.FIELD_OWLS, **ap_hints.DUNGEON_OWLS, **ap_hints.BOOKS}
for source in ap_hints.SOURCES:
    label = ap_hints.label(source)
    if source in ap_hints.BOOKS:
        flow, text_file = ap_hints.BOOK_FLOW, "System.msbt"
    else:
        flow, text_file = ap_hints.FIELD_FLOW if source in ap_hints.FIELD_OWLS else ap_hints.DUNGEON_FLOW, "Hint.msbt"
    shown = sum(1 for params in flows[flow] if params.get("message") == f"{text_file.split('.')[0]}:{label}")
    flagged = sum(1 for params in flows[flow] if params.get("symbol") == label)
    problems = []
    if shown != 1:
        problems.append(f"shown by {shown} events")
    if texts[text_file].get(label) != texts[text_file][copy_from[source]]:
        problems.append(f"text {texts[text_file].get(label)!r}, not {copy_from[source]}'s")
    if flagged or label not in global_flags:
        problems.append(f"flag set by {flagged} events, in GlobalFlags: {label in global_flags}")
    if source in hints and any(len(page) > ap_hints.PAGE_LINES or any(len(line) > ap_hints.LINE_WIDTH for line in page)
                               for page in ap_hints.wrap(hints[source]["text"])):
        problems.append(f"does not fit: {ap_hints.wrap(hints[source]['text'])}")
    if problems:
        bad += 1
        print(f"HINT {source} {label}: " + "; ".join(problems))
lines = seed.get("hints", [])
if lines != ap_hints.lines(hints):
    bad += 1
    print(f"HINTS FILE {lines} != {ap_hints.lines(hints)}")
# the statues that shared an entry point have their own
for source, (entry, room, _) in ap_hints.OWN_ENTRIES.items():
    if room:
        path = level_dir / room.split("_")[0] / f"{room}.leb"
        statues = [a for a in Room(path.read_bytes()).actors if a.parameters and a.parameters[0] == entry.encode()]
        if len(statues) != 1:
            bad += 1
            print(f"HINT {source}: {room} has {len(statues)} statues with {entry}")
print(f"hints={len(hints)} server={sum(1 for line in lines if line.startswith('S '))} bad={bad}")

# Shuffled Dungeons: the module leads each door into the dungeon behind it, and that dungeon's ways out out of the door
# (the seed file's jumps); the doors, the exits and the Fairy Queen's warps keep their vanilla targets, which the leads name.
import ap_entrances  # noqa: E402
from RandomizerCore.Randomizers.data import DUNGEON_ENTRANCES  # noqa: E402
region = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common"


def zones(room: str) -> list:
    """The room's AreaLevelOpen actors, from the patch or else the RomFS."""
    path = region / "level" / room.split("_")[0] / f"{room}.leb"
    if not path.exists():
        path = local_path("romfs") / "region_common" / "level" / room.split("_")[0] / f"{room}.leb"
    return [a for a in Room(path.read_bytes()).actors if a.type == 0x190]


def zone_target(room: str, index: int) -> str:
    return zones(room)[index].parameters[1].decode()


jumps = seed.get("jumps", [])
want = []
doors = 0
for door, dungeon in patch["dungeon_entrances"].items():
    field_room, index = DUNGEON_ENTRANCES[door][2], DUNGEON_ENTRANCES[door][4]
    found = (zone_target(field_room, index), zone_target(DUNGEON_ENTRANCES[door][0], 0))
    if found != (ap_entrances.entrance(door), ap_entrances.outside(door)):
        bad += 1
        print(f"DOOR {door}: door to {found[0]}, exit to {found[1]}, not vanilla")
    if patch["settings"].get("Shuffled Dungeons") and door != dungeon:
        inside, out = ap_entrances.entrance(dungeon), ap_entrances.outside(dungeon)
        want += [f"Field {ap_entrances.level_of(ap_entrances.entrance(door))} {ap_entrances.entrance(door)} "
                 f"{ap_entrances.level_of(inside)} {inside}",
                 f"{ap_entrances.level_of(inside)} Field {out} Field {ap_entrances.outside(door)}"]
    doors += 1
fairy = evfl.EventFlow()
fairy_flow = region / "event" / "FairyQueen.bfevfl"
fairy.read((fairy_flow if fairy_flow.exists() else local_path("romfs") / "region_common/event/FairyQueen.bfevfl").read_bytes())
fairy_warps = {e.data.params.data["locator"] for e in fairy.flowchart.events
               if getattr(e.data, "actor_action", None) is not None and str(e.data.actor_action.v) == "RequestLevelJump"}
if fairy_warps != {ap_entrances.outside("color-dungeon")}:
    bad += 1
    print(f"FAIRY QUEEN warps to {fairy_warps}, not vanilla")
hunt_lines = ap_goal.jumps(ap_goal.settings_seashells(patch["settings"]))
if sorted(jumps) != sorted(want + hunt_lines):
    bad += 1
    print(f"JUMPS {jumps}, want {want + hunt_lines}")
# The map's dungeon markers keep their rows (each its door's label and show flag); the seed file's markers swap them.
from RandomizerCore.Randomizers.data import DUNGEON_MAP_ICONS  # noqa: E402
from RandomizerCore.Tools.oead_tools import readSheet as read_sheet  # noqa: E402
icons_sheet = region / "datasheets/UiFieldMapIcons.gsheet"
if not icons_sheet.exists():
    icons_sheet = local_path("romfs") / "region_common/datasheets/UiFieldMapIcons.gsheet"
marker_rows = {(str(row["mNameLabel"]), str(row["mFirstShowFlagName"])) for row in read_sheet(str(icons_sheet))["values"]}
if not set(DUNGEON_MAP_ICONS.values()) <= marker_rows:
    bad += 1
    print(f"MARKERS rows {sorted(set(DUNGEON_MAP_ICONS.values()) - marker_rows)} are not vanilla")
markers = seed.get("markers", [])
if markers != marker_lines(patch):
    bad += 1
    print(f"MARKERS {markers}, want {marker_lines(patch)}")
print(f"dungeon doors={doors} jumps={len(jumps)} markers={len(markers)} bad={bad}")

# Drop actor: its flowchart reads (a duplicate entry name breaks the entry dictionary the game looks its entries up in)
# and holds the take entry (LEB parameter 3) of every drop actor; the model every drop actor shows (its token's M line,
# else the Archipelago model's) has its bounds in the module's drop_bounds.hpp, and a tree's or hole's drop a link; no
# small key or slime key stand-in is left (LASR's are drops).
drop_flow = evfl.EventFlow()
try:
    drop_flow.read((Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/event" /
                    ap_drop.EVENT_FILE).read_bytes())
    entries = {e.name for e in drop_flow.flowchart.entry_points}
except AssertionError:
    entries = set()
    bad += 1
    print(f"DROP {ap_drop.EVENT_FILE} does not read: its entry dictionary does not match its entries (a duplicate name)")
bounds_file = ROOT / "las-ap-exl/source/program/ap/drop_bounds.hpp"
bounds = set(re.findall(r'\{"([^"]+)", "([^"]+)",', bounds_file.read_text(encoding="utf-8")))
drop_models = {p[1]: (p[3], p[4]) for p in lines if p[0] == "M"}
placeholder = (ap_model.FILE_NAME, ap_model.MODEL_NAME)
takes = {}
keys = []
unlinked = []
unmeasured = []
launches = {}  # per level, its chest drops' launch flags
for path in level_dir.rglob("*.leb"):
    for actor in Room(path.read_bytes()).actors:
        if actor.type == ap_drop.DROP_ACTOR_ID:
            takes[actor.parameters[3].decode()] = path.stem
            token = actor.parameters[1].decode()
            model = drop_models.get(token[len(MODEL_TOKEN):], placeholder) if token.startswith(MODEL_TOKEN) else (
                token, actor.parameters[2].decode())
            if model not in bounds:
                unmeasured.append((path.stem, *model))
            if actor.parameters[0] in ap_drop.LAUNCHED:
                launches.setdefault(path.parent.name, []).append((path.stem, tuple(actor.switches[0])))
            if actor.parameters[0] in ap_drop.LINKED and actor.relationships.x == 0:
                unlinked.append(path.stem)
        elif actor.type == ap_drop.SMALL_KEY_ID or (
                actor.type == ap_drop.SLIME_KEY_ID and isinstance(actor.parameters[3], bytes) and
                actor.parameters[3].decode() in ap_drop.DIG_ROOMS):
            keys.append(path.stem)
missing = sorted(loc for loc in takes if loc not in entries)
if missing and entries:
    bad += 1
    print(f"DROP take entries missing: {[(loc, takes[loc]) for loc in missing]}")
if keys:
    bad += 1
    print(f"DROP small key or slime key stand-ins left in {keys}")
if unlinked:
    bad += 1
    print(f"DROP linked drops (tree, hole, chest, owl statue, boss) without a link in {unlinked}")
if unmeasured:
    bad += 1
    print(f"DROP drop models without bounds in drop_bounds.hpp (scripts/drop_bounds.py): {sorted(set(unmeasured))}")
for level, chests in launches.items():
    users = {}  # level flag: the actors that use it
    for room in (local_path("romfs") / "region_common/level" / level).glob("*.leb"):
        modded = level_dir / level / room.name
        for actor in Room((modded if modded.exists() else room).read_bytes()).actors:
            for flag in {index for kind, index in actor.switches if kind == ap_drop.LEVEL_FLAG}:
                users.setdefault(flag, []).append(actor.name.decode(errors="replace"))
    for room, (kind, flag) in chests:
        if kind != ap_drop.LEVEL_FLAG or flag in ap_drop.FLOW_LEVEL_FLAGS or len(users.get(flag, [])) != 2:
            bad += 1
            print(f"DROP {room}: the chest's or owl statue's launch flag {(kind, flag)} is not its own: "
                  f"{users.get(flag)}")
print(f"drops={len(takes)} take entries={len(entries)} bad={bad}")

# The goal: the egg's event asks HasAllInstruments at its switches in every patch, which the module answers from
# config.ini's numbers (below).
required = patch["settings"].get("Instruments Required", 8)
seashells = ap_goal.settings_seashells(patch["settings"])
hunt = seashells is not None
egg = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/event" / ap_goal.EVENT_FILE
flow = evfl.EventFlow()
flow.read((egg if egg.exists() else local_path("romfs") / "region_common/event" / ap_goal.EVENT_FILE).read_bytes())
queries = [e.data.actor_query.v.v for e in flow.flowchart.events if getattr(e.data, "actor_query", None) is not None]
if queries.count(ap_goal.ALL_QUERY) != ap_goal.SWITCHES:
    bad += 1
    print(f"EGG asks {ap_goal.ALL_QUERY} {queries.count(ap_goal.ALL_QUERY)} times, not {ap_goal.SWITCHES}")
# The egg's door keeps its target, which the module leads to the ending on a seashell hunt (the jumps above).
egg_doors = [a.parameters[1].decode() for a in zones("Field_01G") if a.parameters[0].decode() == ap_goal.EGG_LEVEL]
if egg_doors != [ap_goal.EGG_LOCATOR]:
    bad += 1
    print(f"EGG door leads to {egg_doors}, not into the egg")
# The module's numbers for the egg and its sign, and the save name, from config.ini; the shop's slots from the seed file's shop.
config = (Path(sys.argv[2]) / "config/lasr-exl/config.ini").read_text(encoding="utf-8")
archipelago = dict(line.split(" = ", 1) for line in config.split("[archipelago]")[1].strip().splitlines())
want = {"seashell_hunt": str(hunt), "seashells_required": str(seashells or 0), "instruments_required": str(required),
        "save_name": save_name(patch.get("slot_name", ""))}
if any(archipelago.get(key) != value for key, value in want.items()):
    bad += 1
    print(f"CONFIG [archipelago] {archipelago}, want {want}")
print(f"egg instruments={required} seashells={seashells} bad={bad}")
shop = [line.split() for line in seed.get("shop", [])]
want = [[tag, buy, str(patch["location_ids"][loc])] for loc, (tag, buy) in SHOP_TEXT.items()
        if loc in patch["location_ids"]]
if shop != want:
    bad += 1
    print(f"SHOP {shop}, want {want}")
# No flowchart asks CompareInt, which the module leaves as the game has it (only LASR's keysanity flows use it).
event_dir = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/event"
compare = []
for path in sorted(event_dir.glob("*.bfevfl")):
    chart = evfl.EventFlow()
    chart.read(path.read_bytes())
    if any(str(getattr(e.data, "actor_query", None) and e.data.actor_query.v.v) == "CompareInt"
           for e in chart.flowchart.events):
        compare.append(path.name)
if compare:
    bad += 1
    print(f"FLOWS asking CompareInt: {compare}")
print(f"shop slots={len(shop)} flows={len(list(event_dir.glob('*.bfevfl')))} bad={bad}")

# Start state: the seed file's start holds every starting item, the rupees and the start flags; the start event neither gives
# items nor sets flags (DESIGN.md, Start state).
start = [line.split() for line in seed.get("start", [])]
items = [(key, int(dungeon)) for _, key, dungeon in (s for s in start if s[0] == "I")]
flags = [s[1] for s in start if s[0] == "F"]
rupees = [int(s[1]) for s in start if s[0] == "R"]
for item in patch["starting_items"]:
    key = item_defs[item]["item-key"]
    if not any(k in (key, START_UPGRADES.get(key)) for k, _ in items):
        bad += 1
        print(f"START item {item} ({key}) missing")
for setting, kind in START_WITH_KINDS.items():
    if patch["settings"].get(setting) == "Start With":
        for n in range(9):
            want = item_defs[f"{kind}-D{n}"]
            got = items.count((want["item-key"], 9 if n == 0 else n - 1))
            if got != want.get("quantity", 1):
                bad += 1
                print(f"START {kind}-D{n}: {got} given, want {want.get('quantity', 1)}")
if rupees != ([patch["settings"]["Rupees"]] if patch["settings"].get("Rupees", 0) > 0 else []):
    bad += 1
    print(f"START rupees {rupees}, want {patch['settings'].get('Rupees')}")
if START_MARKER not in flags or not set(START_FLAGS) <= set(flags):
    bad += 1
    print(f"START flags {flags} lack {sorted(set(START_FLAGS) - set(flags))}")
player_start = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/event/PlayerStart.bfevfl"
if player_start.exists():
    chart = evfl.EventFlow()
    chart.read(player_start.read_bytes())
    # The stealing chain's placeholders stay (ShopRandomizer.makeStealingEventChanges)
    actions = [(str(e.data.actor_action.v), dict(e.data.params.data) if getattr(e.data, "params", None) else {})
               for e in chart.flowchart.events if getattr(e.data, "actor_action", None) is not None]
    given = [(action, params) for action, params in actions if action == "AddRupee" or
             (action == "AddItemByKey" and not params.get("itemKey", "").startswith(PLACEHOLDER_KEY)) or
             (action == "SetFlag" and params.get("symbol") in flags)]
    if given:
        bad += 1
        print(f"START the start event still gives or sets {given}")
print(f"start items={len(items)} rupees={sum(rupees)} flags={len(flags)} bad={bad}")
sys.exit(1 if bad else 0)
