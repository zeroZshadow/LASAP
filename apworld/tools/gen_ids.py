"""Create or extend apworld/las/data/ids.json from the LASR data files.

Existing offsets never change; names missing from ids.json are appended after the current maximum offset, in the
order they appear in items.yml (Item_Pool) and logic.yml (nodes of type "item").

Usage: python apworld/tools/gen_ids.py [--check]
  --check  exit with status 1 if ids.json is missing names, without writing.
"""
import json
import pathlib
import sys

import yaml

DATA = pathlib.Path(__file__).resolve().parent.parent / "las" / "data"
IDS = DATA / "ids.json"
MAX_OFFSET = 0xFFFF  # local ids are stored in-game as a 16-bit index


def load_names():
    loader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)
    items = yaml.load((DATA / "items.yml").read_text(encoding="utf-8"), Loader=loader)["Item_Pool"]
    logic = yaml.load((DATA / "logic.yml").read_text(encoding="utf-8"), Loader=loader)
    return list(items), [name for name, node in logic.items() if node["type"] == "item"]


def extend(table: dict, names: list) -> list:
    added = []
    next_offset = max(table.values(), default=0) + 1
    for name in names:
        if name not in table:
            table[name] = next_offset
            added.append(name)
            next_offset += 1
    if table and max(table.values()) > MAX_OFFSET:
        raise ValueError("offset exceeds 16 bits")
    return added


def main() -> int:
    ids = json.loads(IDS.read_text(encoding="utf-8")) if IDS.exists() else {"items": {}, "locations": {}}
    item_names, location_names = load_names()
    added_items = extend(ids["items"], item_names)
    added_locations = extend(ids["locations"], location_names)
    if "--check" in sys.argv:
        if added_items or added_locations:
            print(f"ids.json is missing {len(added_items)} items and {len(added_locations)} locations")
            return 1
        return 0
    IDS.write_text(json.dumps(ids, indent=2) + "\n", encoding="utf-8")
    print(f"added {len(added_items)} items, {len(added_locations)} locations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
