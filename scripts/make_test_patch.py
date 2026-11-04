"""Writes a synthetic .aplas patch for patcher tests, its locations as the world gives them: every location an AP
location holding the placeholder, but for the Trendy Game's prizes and the companions, which keep their vanilla item,
and the shop slots LASR does not shuffle, which are no AP locations. The owl statues the Owl Gifts setting leaves out
are removed locations (removed_location_ids).

python make_test_patch.py <out.aplas> [--seed-hash <hash>] [--slot Tester] [--settings JSON] [--dungeons JSON]
                          [--models loc=item,...] [--names loc=item[|player],...] [--starting item,item,...]

--seed-hash defaults to a hash of the patch's contents, so the seed_hash of a mod made from it (config.ini) names the
patch: the same options and world data give the same hash (scripts/suite.py checks the installed mod by it).

--models gives AP locations the model of an item (items.yml's names), as the world does for items of this or another
game it knows, e.g. woods-north-cave-loose=heart-piece; the module shows them (the seed file's models).
--names names what AP locations hold, as the world's location_names do (the seed file's names; without a player, an
item of this slot), e.g. "beach-chest=Ballad of the Wind Fish".
--starting gives the slot starting items (items.yml's names, a name twice for two), e.g. sword,sword,heart-piece; as
the world's, without the keys of a Start With setting.
--settings replaces settings in the patch, e.g. '{"Instruments Required": 3, "Damage": "OHKO"}'.
--dungeons leads dungeon doors into other dungeons and turns Shuffled Dungeons on, e.g.
'{"tail-cave": "color-dungeon", "color-dungeon": "tail-cave"}' (door: the dungeon it leads into).
"""
import argparse
import hashlib
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

COMPANIONS = ("moblin-cave", "rooster-statue")  # LASR's followers, which the world makes item locations
DUNGEONS = ("tail-cave", "bottle-grotto", "key-cavern", "angler-tunnel", "catfish-maw", "face-shrine",
            "eagle-tower", "turtle-rock", "color-dungeon")


def make(seed_hash=None, slot: str = "Tester", settings: dict = None, dungeons: dict = None, models: dict = None,
         starting=(), names: dict = None) -> dict:
    """The patch, from the options as main takes them (parsed); scripts/build_assets.py builds the mod from make()."""
    models = models or {}
    logic = yaml.safe_load(open(DATA / "logic.yml"))
    categories = yaml.safe_load(open(DATA / "locations.yml"))
    ids = json.load(open(DATA / "ids.json"))["locations"]

    settings = {**SETTINGS, **(settings or {})}
    vanilla = {k for k, v in logic.items() if v.get("type") == "item" and v.get("subtype") == "shop"
               and k not in categories.get("Shop_Items", [])}
    kept = set(categories["Trendy_Rewards"]) | set(COMPANIONS)
    removed = set()
    if settings["Owl Gifts"] not in ("Overworld", "All"):
        removed |= set(categories["Overworld_Owl_Statues"])
    if settings["Owl Gifts"] not in ("Dungeons", "All"):
        removed |= set(categories["Dungeon_Owl_Statues"])

    placements, location_ids, removed_ids = {}, {}, {}
    for loc, d in logic.items():
        if d.get("type") != "item" and loc not in COMPANIONS:
            continue
        if loc in removed:
            removed_ids[loc] = ids[loc]
        elif loc in vanilla:
            placements[loc] = d["content"]
        else:
            placements[loc] = d["content"] if loc in kept else "ap-item"
            location_ids[loc] = ids[loc]
    names = names or {}
    not_ap = sorted(loc for loc in {*models, *names} if placements.get(loc) != "ap-item")
    assert not not_ap, f"not AP locations: {not_ap}"

    entrances = {d: d for d in DUNGEONS}
    entrances.update(dungeons or {})
    assert sorted(entrances.values()) == sorted(DUNGEONS), entrances
    if any(door != dungeon for door, dungeon in entrances.items()):
        settings["Shuffled Dungeons"] = True
    patch = {
        "format": 1, "seed_name": "test", "player": 1, "slot_name": slot, "seed_hash": seed_hash,
        "settings": settings, "starting_items": list(starting), "placements": placements,
        "location_ids": location_ids, "removed_location_ids": removed_ids,
        "dungeon_entrances": entrances, "location_models": models,
    }
    if names:
        patch["location_names"] = {loc: [*name.split("|", 1), ""][:2] for loc, name in names.items()}
    if patch["seed_hash"] is None:
        patch["seed_hash"] = hashlib.sha256(json.dumps(patch, sort_keys=True).encode()).hexdigest()[:8]
    return patch


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=Path)
    parser.add_argument("--seed-hash")
    parser.add_argument("--slot", default="Tester")
    parser.add_argument("--settings", default="{}")
    parser.add_argument("--dungeons", default="{}")
    parser.add_argument("--models", default="")
    parser.add_argument("--names", default="")
    parser.add_argument("--starting", default="")
    args = parser.parse_args()
    patch = make(args.seed_hash, args.slot, json.loads(args.settings), json.loads(args.dungeons),
                 dict(pair.split("=", 1) for pair in filter(None, args.models.split(","))),
                 filter(None, args.starting.split(",")),
                 dict(pair.split("=", 1) for pair in filter(None, args.names.split(","))))
    args.out.write_text(json.dumps(patch, indent=1))
    ids = patch["location_ids"]
    print(f"{args.out}: {len(ids)} AP locations, {len(patch['placements']) - len(ids)} vanilla, "
          f"{len(patch['removed_location_ids'])} removed")


if __name__ == "__main__":
    main()
