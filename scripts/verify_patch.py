"""Static checks on patcher output: every chest location holds the AP placeholder (the model variant of
`location_models`), with Bouncy Items the drop actor's chest key, with its local id; with "Texture + Size" chests, the chests holding a seashell and only those are
the seashell chest. Every randomized heart piece and seashell actor has a unique index above
the received range (DESIGN.md, item model). With the map tracker on, every AP location but the optional ones has one icon in ap_map.txt
and every icon its datasheet row and a name. Below eight required instruments, the egg's event checks single instruments instead of
all of them. With in-game hints, every hint source shows its own message, which fits the text box, and a server hint
sets its flag, which ap_hints.txt names with the hinted location. With Shuffled Dungeons, each dungeon door leads into
the dungeon the patch's map names, whose exit and Fairy Queen warps (for the Color Dungeon) lead back out of that door.
The drop actor's flowchart reads and holds every drop's take entry, every drop has its model's bounds and a linked one
(a tree's, hole's, chest's, owl statue's or boss's) its link, and no small key or slime key stand-in is left. Each chest's launch flag (Bouncy Items) is
used by that chest and its drop alone, among all actors of its level (the RomFS's rooms, as the mod has them) and the
RomFS flowcharts' level flags; so is each owl statue's (Bouncy Items, Owl Gifts).

python scripts/verify_patch.py <patch.aplas> <patcher output dir>
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher"))
from local_paths import local_path  # noqa: E402
import ap_chest  # noqa: E402
import ap_drop  # noqa: E402
import ap_goal  # noqa: E402
import evfl  # noqa: E402
from RandomizerCore.Randomizers.chests import CHEST_ROOMS  # noqa: E402
from RandomizerCore.Tools.leb import Room  # noqa: E402
from las_ap_patch import (COMPANION_FLAGS, INDEX_KINDS, INDICES_FILE, PLACEHOLDER_KEY,  # noqa: E402
                          VANILLA_FILE, VARIANT_KEY_MAX, load_patch, load_yaml, variant_key)

patch = load_patch(Path(sys.argv[1]))
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
    item_key = item_defs[models[loc]]["item-key"] if loc in models else ""
    key = variant_key(item_key) if item_key else PLACEHOLDER_KEY
    if patch["settings"].get(ap_drop.BOUNCY_SETTING) and loc in ap_drop.CHEST_LOCATIONS:
        key = ap_drop.DROP_CHEST_KEY
    want = (key.encode(), patch["location_ids"][loc])
    if len(want[0]) > VARIANT_KEY_MAX:
        bad += 1
        print(f"KEY TOO LONG {loc} {room_name}: {want[0]} (a chest key over {VARIANT_KEY_MAX} characters crashes its room)")
    elif any(k == want[0] and v == want[1] for k, v in found):
        ok += 1
    else:
        bad += 1
        print(f"MISMATCH {loc} {room_name}: {found} want {want}")
print(f"chests ok={ok} bad={bad}")

# Chest textures ("Texture + Size"): every chest holding this slot's seashell, vanilla or a check, is the seashell
# chest, and no other chest is.
if patch["settings"].get("Chest Types") == "Texture + Size":
    looks = patch.get("location_chests", {})
    blue = 0
    for loc, room_name in CHEST_ROOMS.items():
        item = patch["placements"].get(loc)
        if item is None:
            continue
        want = item == "seashell" or (item == "ap-item" and looks.get(loc, [None, None])[1] == ap_chest.CHEST_TYPE)
        path = level_dir / room_name.split("_")[0] / f"{room_name}.leb"
        chests = [a for a in Room(path.read_bytes()).actors if a.type == 0xF7]
        written = chests[:5] if loc == "taltal-5-chest-puzzle" else chests[:1]  # LASR's setChestContent
        models = {c.parameters[6] for c in written}
        if (models == {ap_chest.FILE_NAME.encode()}) != want:
            bad += 1
            print(f"CHEST TEXTURE {loc} {room_name}: {models} for {item} {looks.get(loc)}")
        blue += want
    print(f"chest textures seashell={blue} bad={bad}")

# Map tracker: every AP location has one icon, and every icon its datasheet row.
if patch["settings"].get("Map Tracker"):
    import ap_map  # noqa: E402
    from RandomizerCore.Tools.oead_tools import readSheet  # noqa: E402
    out = Path(sys.argv[2])
    lines = [line.split() for line in (out / "config/lasr-exl" / ap_map.FILE_NAME).read_text(encoding="utf-8").splitlines()]
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
entries = [line.split() for line in (Path(sys.argv[2]) / "config/lasr-exl" / INDICES_FILE).read_text().splitlines()]
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
kept = [line.split() for line in (Path(sys.argv[2]) / "config/lasr-exl" / VANILLA_FILE).read_text().splitlines()
        if line.strip()]
expected = {str(patch["location_ids"][loc]): COMPANION_FLAGS.get(loc) or item_defs[item]["item-key"]
            for loc, item in patch["placements"].items() if item != "ap-item" and loc in patch["location_ids"]}
found = {local_id: flag if key == "F" else key for key, flag, local_id in kept}
if found != expected or len(kept) != len(expected):
    bad += 1
    print(f"KEPT {kept} != {expected}")
print(f"kept locations={len(kept)} bad={bad}")

# In-game hints: each source's message, its wrapping, its flag and ap_hints.txt.
hints = patch.get("hints", {})
if hints:
    import re  # noqa: E402
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
    want_lines = []
    for source, hint in hints.items():
        label = ap_hints.label(source)
        if source in ap_hints.BOOKS:
            flow, text_file = ap_hints.BOOK_FLOW, "System"
        else:
            flow, text_file = ap_hints.FIELD_FLOW if source in ap_hints.FIELD_OWLS else ap_hints.DUNGEON_FLOW, "Hint"
        shown = sum(1 for params in flows[flow] if params.get("message") == f"{text_file}:{label}")
        flagged = sum(1 for params in flows[flow] if params.get("symbol") == label)
        text = texts[f"{text_file}.msbt"].get(label, "")
        pages = [re.sub(r"\[[^]]*\]", "", page).split("\n") for page in text.split(ap_hints.PAGE_BREAK)]
        problems = []
        if shown != 1:
            problems.append(f"shown by {shown} events")
        if " ".join(" ".join(page) for page in pages) != ap_hints.clean(hint["text"]):
            problems.append(f"text {text!r}")
        if source in ap_hints.BOOK_PROMPTS and "Lost and Found" not in texts["System.msbt"][ap_hints.BOOK_PROMPTS[source]]:
            problems.append("the book keeps its title")
        if any(len(page) > ap_hints.PAGE_LINES or any(len(line) > ap_hints.LINE_WIDTH for line in page)
               for page in pages):
            problems.append(f"does not fit: {pages}")
        if "location" in hint:
            want_lines.append(f"{ap_hints.SOURCES.index(source)} {label} {hint['player']} {hint['location']}")
            if flagged != 1 or label not in global_flags:
                problems.append(f"flag set by {flagged} events, in GlobalFlags: {label in global_flags}")
        elif flagged:
            problems.append("flag set without a location")
        if problems:
            bad += 1
            print(f"HINT {source} {label}: " + "; ".join(problems))
    hints_file = Path(sys.argv[2]) / "config/lasr-exl" / ap_hints.FILE_NAME
    lines = hints_file.read_text(encoding="utf-8").splitlines() if hints_file.exists() else []
    if lines != want_lines:
        bad += 1
        print(f"HINTS FILE {lines} != {want_lines}")
    # the statues that shared an entry point have their own
    for source, (entry, room, _) in ap_hints.OWN_ENTRIES.items():
        if room and source in hints:
            path = level_dir / room.split("_")[0] / f"{room}.leb"
            statues = [a for a in Room(path.read_bytes()).actors if a.parameters and a.parameters[0] == entry.encode()]
            if len(statues) != 1:
                bad += 1
                print(f"HINT {source}: {room} has {len(statues)} statues with {entry}")
    print(f"hints={len(hints)} server={len(want_lines)} bad={bad}")

# Shuffled Dungeons: each door into the dungeon behind it, and that dungeon's exits out of the door.
if patch["settings"].get("Shuffled Dungeons"):
    import ap_entrances  # noqa: E402
    from RandomizerCore.Randomizers.data import DUNGEON_ENTRANCES  # noqa: E402
    romfs = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common"

    def zone_target(room: str, index: int) -> str:
        path = romfs / "level" / room.split("_")[0] / f"{room}.leb"
        return [a for a in Room(path.read_bytes()).actors if a.type == 0x190][index].parameters[1].decode()

    doors = 0
    for door, dungeon in patch["dungeon_entrances"].items():
        _, _, field_room, field_suffix, index = DUNGEON_ENTRANCES[door]
        inside = DUNGEON_ENTRANCES[dungeon][0] + DUNGEON_ENTRANCES[dungeon][1]
        outside = field_room + field_suffix
        found = (zone_target(field_room, index), zone_target(DUNGEON_ENTRANCES[dungeon][0], 0))
        if found != (inside, outside):
            bad += 1
            print(f"DOOR {door} -> {dungeon}: door to {found[0]}, exit to {found[1]}, want {inside}, {outside}")
        doors += 1
    locator = ap_entrances.color_dungeon_exit(patch["dungeon_entrances"])
    if locator is not None:
        flow = evfl.EventFlow()
        flow.read((romfs / "event" / ap_entrances.FAIRY_FLOW).read_bytes())
        warps = {e.name: e.data.params.data["locator"] for e in flow.flowchart.events if e.name in ap_entrances.FAIRY_WARPS}
        exits = [zone_target(ap_entrances.FAIRY_ROOM, 0)] + list(warps.values())
        if len(warps) != len(ap_entrances.FAIRY_WARPS) or any(target != locator for target in exits):
            bad += 1
            print(f"FAIRY QUEEN exits {exits}, want {locator}")
    print(f"dungeon doors={doors} bad={bad}")

# Drop actor: its flowchart reads (a duplicate entry name breaks the entry dictionary the game looks its entries up in)
# and holds the take entry (LEB parameter 3) of every drop actor; every drop actor has its model's bounds, and a tree's
# or hole's a link; no small key or slime key stand-in is left (LASR's are drops).
drop_flow = evfl.EventFlow()
try:
    drop_flow.read((Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/event" /
                    ap_drop.EVENT_FILE).read_bytes())
    entries = {e.name for e in drop_flow.flowchart.entry_points}
except AssertionError:
    entries = set()
    bad += 1
    print(f"DROP {ap_drop.EVENT_FILE} does not read: its entry dictionary does not match its entries (a duplicate name)")
takes = {}
keys = []
unlinked = []
drop_keys = set()
launches = {}  # per level, its chest drops' launch flags
for path in level_dir.rglob("*.leb"):
    for actor in Room(path.read_bytes()).actors:
        if actor.type == ap_drop.DROP_ACTOR_ID:
            takes[actor.parameters[3].decode()] = path.stem
            drop_keys.add(actor.key)
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
measured = {int(line.split()[0]) for line in
            (Path(sys.argv[2]) / "config/lasr-exl" / ap_drop.DROPS_FILE).read_text().splitlines()}
if drop_keys - measured:
    bad += 1
    print(f"DROP drop actors without their model's bounds: {sorted(f'{key:016X}' for key in drop_keys - measured)}")
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

# Instruments Required: below eight, the egg's event checks single instruments instead of all of them. Seashells
# Required: each of the four switches checks the seashell flag instead, and no instrument.
required = patch["settings"].get("Instruments Required", 8)
seashells = patch["settings"].get("Seashells Required", 0)
egg = Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/event" / ap_goal.EVENT_FILE
queries = []
flags = []
if egg.exists():
    flow = evfl.EventFlow()
    flow.read(egg.read_bytes())
    queries = [e.data.actor_query.v.v for e in flow.flowchart.events
               if getattr(e.data, "actor_query", None) is not None]
    flags = [e.data.params.data.get("symbol") for e in flow.flowchart.events
             if getattr(e.data, "actor_query", None) is not None and e.data.actor_query.v.v == ap_goal.FLAG_QUERY]
if seashells and flags.count(ap_goal.SEASHELL_FLAG) != ap_goal.SWITCHES:
    bad += 1
    print(f"EGG checks {ap_goal.SEASHELL_FLAG} {flags.count(ap_goal.SEASHELL_FLAG)} times with {seashells} seashells")
if not seashells and ap_goal.SEASHELL_FLAG in flags:
    bad += 1
    print(f"EGG checks {ap_goal.SEASHELL_FLAG} with no seashells required")
# With seashells the egg's door leads to the ending.
egg_room = egg.parents[1] / "level" / "Field" / f"{ap_goal.EGG_ROOM}.leb"
door = ap_goal.EGG_LEVEL
if egg_room.exists():
    door = ap_goal.egg_door(Room(egg_room.read_bytes())).parameters[0].decode()
if door != (ap_goal.ENDING_LEVEL if seashells else ap_goal.EGG_LEVEL):
    bad += 1
    print(f"EGG door leads to {door} with {seashells} seashells required")
if seashells and (ap_goal.ALL_QUERY in queries or ap_goal.ITEM_QUERY in queries):
    bad += 1
    print(f"EGG checks instruments with {seashells} seashells required")
if not seashells and required < 8 and (ap_goal.ALL_QUERY in queries or ap_goal.ITEM_QUERY not in queries):
    bad += 1
    print(f"EGG still checks {ap_goal.ALL_QUERY} with {required} instruments required")
# LASR writes the egg's event in every patch; with all eight required its switches stay as they are.
if not seashells and required >= 8 and egg.exists() and queries.count(ap_goal.ALL_QUERY) != ap_goal.SWITCHES:
    bad += 1
    print(f"EGG event lost its {ap_goal.ALL_QUERY} checks with all instruments required")
from RandomizerCore.Tools.text_tools import TextFile  # noqa: E402
sign = TextFile(Path(sys.argv[2]) / "atmosphere/contents/01006BB00C6F0000/romfs/regionUS/USen/message" /
                ap_goal.SIGN_FILE).getEntry(ap_goal.SIGN_KEY).message.text
if ap_goal.sign_text(required, seashells) not in sign:
    bad += 1
    print(f"EGG sign does not name {ap_goal.sign_text(required, seashells)!r}: {sign!r}")
print(f"egg instruments={required} seashells={seashells} item checks={queries.count(ap_goal.ITEM_QUERY)} bad={bad}")
sys.exit(1 if bad else 0)
