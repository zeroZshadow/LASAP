"""Writes a synthetic .aplas patch (every shuffleable location gives the AP placeholder) for patcher tests.

python make_test_patch.py <out.aplas> [--seed-hash test0001] [--slot Tester] [--vanilla loc,loc,...] [--settings JSON]
                          [--dungeons JSON] [--items loc=item,...] [--kept loc,loc,...] [--models loc=item,...]

--items gives locations an item of this slot (logic.yml's content names, e.g. beach-chest=song-ballad); they are no
AP locations.
--kept makes locations AP locations that keep their vanilla item, as the world makes the Trendy Game's prizes
(DESIGN.md, Item model), e.g. trendy-prize-1,trendy-prize-3.
--models gives AP locations the model of an item (items.yml's names), as the world does for items of this or another
game it knows, e.g. woods-north-cave-loose=heart-piece; the patcher makes them placeholder variants.
--settings replaces settings in the patch, e.g. '{"Instruments Required": 3, "Damage": "OHKO"}'.
--dungeons leads dungeon doors into other dungeons and turns Shuffled Dungeons on, e.g.
'{"tail-cave": "color-dungeon", "color-dungeon": "tail-cave"}' (door: the dungeon it leads into).
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
    "Randomize Environments": False, "Disable Low Health Beep": False, "Easy Fishing": True, "No 30 FPS Drops": "On emulators", "Instant Pickups": False, "360 Movement": False,
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
    parser.add_argument("--slot", default="Tester")
    parser.add_argument("--vanilla", default="")
    parser.add_argument("--settings", default="{}")
    parser.add_argument("--dungeons", default="{}")
    parser.add_argument("--items", default="")
    parser.add_argument("--kept", default="")
    parser.add_argument("--models", default="")
    args = parser.parse_args()
    items = dict(pair.split("=", 1) for pair in filter(None, args.items.split(",")))
    models = dict(pair.split("=", 1) for pair in filter(None, args.models.split(",")))

    logic = yaml.safe_load(open(DATA / "logic.yml"))
    categories = yaml.safe_load(open(DATA / "locations.yml"))
    ids = json.load(open(DATA / "ids.json"))["locations"]

    vanilla = set(categories.get("Trendy_Rewards", []))
    vanilla |= {k for k, v in logic.items() if v.get("type") == "item" and v.get("subtype") == "shop"
                and k not in categories.get("Shop_Items", [])}
    vanilla |= {k for k, v in logic.items() if v.get("type") == "item" and "owl" in v.get("subtype", "")}
    vanilla |= set(filter(None, args.vanilla.split(",")))
    kept = set(filter(None, args.kept.split(",")))

    placements, location_ids = {}, {}
    for loc, d in logic.items():
        if d.get("type") != "item":
            continue
        if loc in items:
            placements[loc] = items.pop(loc)
        elif loc in kept:
            placements[loc] = d["content"]
            location_ids[loc] = ids[loc]
        elif loc in vanilla:
            placements[loc] = d["content"]
        else:
            placements[loc] = "ap-item"
            location_ids[loc] = ids[loc]
    assert not items, f"unknown locations: {sorted(items)}"
    assert all(loc in location_ids for loc in models), f"not AP locations: {sorted(set(models) - set(location_ids))}"

    dungeons = {d: d for d in DUNGEONS}
    dungeons.update(json.loads(args.dungeons))
    assert sorted(dungeons.values()) == sorted(DUNGEONS), dungeons
    settings = {**SETTINGS, **json.loads(args.settings)}
    if any(door != dungeon for door, dungeon in dungeons.items()):
        settings["Shuffled Dungeons"] = True
    patch = {
        "format": 1, "seed_name": "test", "player": 1, "slot_name": args.slot, "seed_hash": args.seed_hash,
        "settings": settings, "starting_items": [], "placements": placements, "location_ids": location_ids,
        "dungeon_entrances": dungeons, "location_models": models,
    }
    args.out.write_text(json.dumps(patch, indent=1))
    print(f"{args.out}: {len(location_ids)} AP locations, {len(placements) - len(location_ids)} vanilla")


if __name__ == "__main__":
    main()
