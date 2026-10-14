"""Writes a synthetic .aplas patch (every shuffleable location gives the AP placeholder) for patcher tests.

python make_test_patch.py <out.aplas> [--seed-hash test0001] [--vanilla loc,loc,...] [--settings JSON]

--settings replaces settings in the patch, e.g. '{"Instruments Required": 3, "Damage": "OHKO"}'.
"""
import argparse
import json
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "apworld" / "las" / "data"

SETTINGS = {
    "Preset": "Basic", "Chests": True, "Free Gifts": True, "Trade Quest": True, "Golden Leaves": True,
    "Heart Pieces": True, "Seashells": True, "Miscellaneous": True, "Seashell Mansion": 50, "Shop": True,
    "Boss Drops": True, "Companions": False, "Dampe": True, "Rapids": True, "Fishing": True,
    "Trendy Game": False, "Blue Rupees": False, "Owl Gifts": "None", "Dungeon Maps": "Own Dungeon",
    "Compasses": "Own Dungeon", "Stone Beaks": "Own Dungeon", "Small Keys": "Own Dungeon",
    "Nightmare Keys": "Own Dungeon", "Shuffle Instruments": "Anywhere", "Starting Instruments": 0,
    "Race Mode": False, "Required Dungeons": 5, "Create Spoiler Log": False, "Unrandomizer Mode": False,
    "Randomize Enemies": False, "Randomize Enemy Sizes": False, "Chest Types": "Default", "Open Kanalet": False,
    "Open Mabe": False, "Open Mamu": False, "Completed Bridge": False, "Classic D2": False,
    "Shuffled Dungeons": False, "Consumable Drops": True, "Bad Pets": False, "Fast Fishing": True,
    "Shuffled Bombs": False, "Free Book": True, "Fast Stalfos": True, "Shuffled Powder": False, "Ammo Ghost": True, "Map Tracker": True,
    "Stealing": "Always", "Boss Cutscenes": True, "Song Cutscenes": False, "Movement Speed": True,
    "Chest Animations": True, "Key Animations": True, "Item Get Animations": False, "Item Pool": "Standard",
    "Traps": "None", "Nice Bombs": True, "Nice Hookshot": False, "Nice Magic Rod": True, "Damage": "Normal",
    "Music": "Vanilla", "Randomize Sound Effects": False, "Randomize Text": False,
    "Randomize Environments": False, "Disable Low Health Beep": False, "Easy Fishing": True, "360 Movement": False,
    "Blur Removal": False, "Instant Text": False, "Disable Guardian Acorn": False,
    "Disable Piece of Power": False, "Rupees": 100, "Containers": 0, "Pieces": 0,
    "Seashells Important": True, "Trade Important": True,
}

DUNGEONS = ("tail-cave", "bottle-grotto", "key-cavern", "angler-tunnel", "catfish-maw", "face-shrine",
            "eagle-tower", "turtle-rock", "color-dungeon")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=Path)
    parser.add_argument("--seed-hash", default="test0001")
    parser.add_argument("--vanilla", default="")
    parser.add_argument("--settings", default="{}")
    args = parser.parse_args()

    logic = yaml.safe_load(open(DATA / "logic.yml"))
    categories = yaml.safe_load(open(DATA / "locations.yml"))
    ids = json.load(open(DATA / "ids.json"))["locations"]

    vanilla = set(categories.get("Trendy_Rewards", [])) | set(categories.get("Blue_Rupees", []))
    vanilla |= {k for k, v in logic.items() if v.get("type") == "item" and v.get("subtype") == "shop"
                and k not in categories.get("Shop_Items", [])}
    vanilla |= {k for k, v in logic.items() if v.get("type") == "item" and "owl" in v.get("subtype", "")}
    vanilla |= set(filter(None, args.vanilla.split(",")))

    placements, location_ids = {}, {}
    for loc, d in logic.items():
        if d.get("type") != "item":
            continue
        if loc in vanilla:
            placements[loc] = d["content"]
        else:
            placements[loc] = "ap-item"
            location_ids[loc] = ids[loc]

    patch = {
        "format": 1, "seed_name": "test", "player": 1, "slot_name": "Tester", "seed_hash": args.seed_hash,
        "settings": {**SETTINGS, **json.loads(args.settings)}, "starting_items": [], "placements": placements,
        "location_ids": location_ids,
        "dungeon_entrances": {d: d for d in DUNGEONS},
    }
    args.out.write_text(json.dumps(patch, indent=1))
    print(f"{args.out}: {len(location_ids)} AP locations, {len(placements) - len(location_ids)} vanilla")


if __name__ == "__main__":
    main()
