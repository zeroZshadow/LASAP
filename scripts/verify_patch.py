"""Static checks on patcher output: every chest location holds the AP placeholder (the model variant of
`location_models`) with its local id, and every randomized heart piece and seashell actor has a unique index above
the received range (DESIGN.md, item model). With the map tracker on, every AP location has one icon in ap_map.txt and
every icon its datasheet row. With in-game hints, every hint source shows its own message, which fits the text box,
and a server hint sets its flag, which ap_hints.txt names with the hinted location.

python scripts/verify_patch.py <patch.aplas> <patcher output dir>
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher"))
from RandomizerCore.Randomizers.chests import CHEST_ROOMS  # noqa: E402
from RandomizerCore.Tools.leb import Room  # noqa: E402
from las_ap_patch import (ACTOR_INDICES, COMPANION_FLAGS, INDEX_KINDS, INDICES_FILE, PLACEHOLDER_KEY,  # noqa: E402
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

# Item actors hide while the inventory bit of their index is set, so each one needs its own bit above the received range.
seen = {}
for path in sorted(level_dir.rglob("*.leb")):
    for actor in Room(path.read_bytes()).actors:
        if actor.type not in ACTOR_INDICES or actor.parameters[4] in (b"", 0) or not isinstance(actor.parameters[0], int):
            continue
        index = actor.parameters[0]
        if index < min(ACTOR_INDICES[actor.type]) or (actor.type, index) in seen:
            bad += 1
            print(f"ACTOR INDEX {path.stem} {actor.name.decode()} index {index} (also {seen.get((actor.type, index))})")
        seen[(actor.type, index)] = path.stem
print(f"item actors checked={len(seen)} bad={bad}")

# Map tracker: every AP location has one icon, and every icon its datasheet row.
if patch["settings"].get("Map Tracker"):
    import ap_map  # noqa: E402
    from RandomizerCore.Tools.oead_tools import readSheet  # noqa: E402
    out = Path(sys.argv[2])
    lines = [line.split() for line in (out / "config/lasr-exl" / ap_map.FILE_NAME).read_text(encoding="utf-8").splitlines()]
    overworld = {int(p[1]): [int(i) for i in p[2:]] for p in lines if p[0] == "O"}
    dungeon = {int(p[1]): (float(p[2]), float(p[3]), [int(i) for i in p[4:]]) for p in lines if p[0] == "D"}
    iconed = [i for ids in overworld.values() for i in ids] + [i for *_, ids in dungeon.values() for i in ids]
    positions = ap_map.load_positions()
    # overworld icons: overworld and dungeon checks (at the dungeon's entrance); dungeon icons: the dungeons' checks
    # that are not chests
    want = [int(i) for loc, i in patch["location_ids"].items() if loc in positions["overworld"]]
    want += [int(i) for loc, i in patch["location_ids"].items() if loc in positions["dungeon"]]
    want += [int(i) for loc, i in patch["location_ids"].items()
             if loc in positions["dungeon"] and loc not in CHEST_ROOMS]
    if sorted(iconed) != sorted(want):
        bad += 1
        print(f"MAP ICONS cover {len(iconed)} locations, want {len(want)}")
    chests = [int(p[4]) for p in lines if p[0] == "C"]
    want = [int(i) for loc, i in patch["location_ids"].items() if CHEST_ROOMS.get(loc, "").startswith("Lv")]
    if sorted(chests) != sorted(want):
        bad += 1
        print(f"MAP CHESTS found {len(chests)}, want {len(want)}")
    sheets = out / "atmosphere/contents/01006BB00C6F0000/romfs/region_common/datasheets"
    flags = {str(row["mFirstShowFlagName"]) for row in readSheet(str(sheets / "UiFieldMapIcons.gsheet"))["values"]}
    missing = [g for g in overworld if any(f"{ap_map.FLAG_PREFIX}{mark}{g}" not in flags for mark in ap_map.LOOKS)]
    rows = {(round(float(row["posX"]), 2), round(float(row["posZ"]), 2))
            for row in readSheet(str(sheets / "UiDungeonIcon.gsheet"))["values"]}
    missing += [g for g, (x, z, _) in dungeon.items() if (round(x, 2), round(z, 2)) not in rows]
    if missing:
        bad += 1
        print(f"MAP ROWS missing for groups {missing}")
    print(f"map icons overworld={len(overworld)} dungeon={len(dungeon)} locations={len(iconed)} bad={bad}")
# Inventory bits: every seashell, heart piece, golden leaf and heart container source once per kind and bit, with
# its local id when shuffled.
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
kept = [line.split() for line in (Path(sys.argv[2]) / "config/lasr-exl" / VANILLA_FILE).read_text().splitlines()]
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
    import evfl  # noqa: E402
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
sys.exit(1 if bad else 0)
