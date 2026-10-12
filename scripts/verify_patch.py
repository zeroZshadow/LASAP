"""Static checks on patcher output: every chest location holds the AP placeholder with its local id, and every
randomized heart piece and seashell actor has a unique index above the received range (DESIGN.md, item model).

python scripts/verify_patch.py <patch.aplas> <patcher output dir>
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "patcher"))
from RandomizerCore.Randomizers.chests import CHEST_ROOMS  # noqa: E402
from RandomizerCore.Tools.leb import Room  # noqa: E402
from las_ap_patch import ACTOR_INDICES  # noqa: E402

patch = json.loads(Path(sys.argv[1]).read_text())
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
    want = (b"ArchipelagoItem", patch["location_ids"][loc])
    if any(k == want[0] and v == want[1] for k, v in found):
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
sys.exit(1 if bad else 0)
