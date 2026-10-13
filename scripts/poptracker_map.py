"""Writes the Archipelago id tables of the LASR PopTracker pack (github.com/Br00ty/lasr_brooty).

Run with .venv-lasr: python scripts/poptracker_map.py <pack folder>
Writes <pack>/scripts/autotracking/item_mapping.lua and location_mapping.lua from apworld/las/data/ids.json and the
tables below, and checks every pack code against the pack's items.json and locations.
Locations: overworld checks match the pack's section of the same name (LASR's name without dashes); the rest are
listed in LOCATIONS by hand. Dungeon checks are named by their room on the pack's dungeon maps (DUNGEON_ROOMS), found from
LASR's room files and the pins' positions; each one updates both the dungeon map pin and the dungeon's overworld pin.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IDS = json.loads((ROOT / "apworld" / "las" / "data" / "ids.json").read_text(encoding="utf-8"))
ITEM_BASE = 0x4C410000  # apworld/las/ids.py
LOCATION_BASE = 0x4C420000

# LASR item -> (pack code, how it counts): toggle, progressive (one level per copy), consumable (one per copy),
# or a stage number (that stage at least).
ITEMS = {
    "sword": ("sword", "progressive"), "shield": ("shield", "progressive"), "bracelet": ("bracelet", "progressive"),
    "boots": ("boots", "toggle"), "flippers": ("flippers", "toggle"), "feather": ("feather", "toggle"),
    "shovel": ("shovel", "toggle"), "hookshot": ("hookshot", "toggle"), "bow": ("bow", "toggle"),
    "rod": ("rod", "toggle"), "boomerang": ("boomerang", "toggle"), "ocarina": ("ocarina", "toggle"),
    "song-ballad": ("ballad", "toggle"), "song-mambo": ("manbo", "toggle"), "song-soul": ("soul", "toggle"),
    "yoshi-doll": ("yoshi", "toggle"), "ribbon": ("ribbon", "toggle"), "dog-food": ("food", "toggle"),
    "bananas": ("bananas", "toggle"), "stick": ("stick", "toggle"), "honeycomb": ("honey", "toggle"),
    "pineapple": ("pineapple", "toggle"), "hibiscus": ("flower", "toggle"), "letter": ("letter", "toggle"),
    "broom": ("broom", "toggle"), "fishing-hook": ("hook", "toggle"), "necklace": ("necklace", "toggle"),
    "mermaid-scale": ("scale", "toggle"), "magnifying-lens": ("lens", "toggle"),
    "golden-leaf": ("goldleaf", "consumable"), "mushroom": ("mushroom", "toggle"),
    "tail-key": ("tail", "toggle"), "slime-key": ("slime", "toggle"), "angler-key": ("angler", "toggle"),
    "face-key": ("face", "toggle"), "bird-key": ("bird", "toggle"),
    "full-moon-cello": ("cello", "toggle"), "conch-horn": ("conch", "toggle"), "sea-lilys-bell": ("bell", "toggle"),
    "surf-harp": ("harp", "toggle"), "wind-marimba": ("marimba", "toggle"), "coral-triangle": ("triangle", "toggle"),
    "evening-calm-organ": ("organ", "toggle"), "thunder-drum": ("drum", "toggle"),
    "seashell": ("shells", "consumable"), "bottle": ("bottles", "consumable"),
    "heart-container": ("heartcontainer", "consumable"), "heart-piece": ("poh", "consumable"),
    "chamber-stone": ("stones", "consumable"), "shell-sensor": ("sensor", "toggle"),
    "medicine": ("medicine", "toggle"), "red-tunic": ("redmail", 1), "blue-tunic": ("bluemail", 2),
    "bomb": ("bomb", "toggle"), "powder": ("powder", "toggle"),
}
for n in range(9):
    ITEMS[f"key-D{n}"] = (f"d{n}sk", "consumable")
    ITEMS[f"nightmare-key-D{n}"] = (f"d{n}bk", "toggle")
    ITEMS[f"stone-beak-D{n}"] = (f"d{n}beak", "toggle")

# Overworld checks whose pack section has another name: LASR location -> pack location id/section.
LOCATIONS = {
    "sale": "Crocodile Sale/Trade Dog Food",
    "mermaid-cave": "Mermaid Statue/Move Statue",
    "christine-trade": "Christine's House/Trade Flower",
    "christine-grateful": "Christine's House/Christine Grateful (come back after trading letter and flower)",
    "invisible-zora": "Pond House/Invizible Zora",
    "chef-bear": "Chef Bear/Trade Honeycomb",
    "grandma-yahoo": "Pond House/Trade Broom to Grandma Outside",
    "goriya-trader": "Goryia Cave/Boomerang Trade",
    "doghouse-dig": "Doghouse/Seashell Dig",
    "fishing-orange": "Fishing Hole/Fishing Any",
    "marin": "Marin/Ocarina with Marin",
    "shop-slot3-1st": "Mabe Shop/Slot3 1st",
    "shop-slot3-2nd": "Mabe Shop/Slot3 2nd",
    "shop-slot5-1st": "Mabe Shop/Slot5 1st-2nd",
    "shop-slot5-2nd": "Mabe Shop/Slot5 1st-2nd",
    "shop-slot5-3rd": "Mabe Shop/Slot5 3rd",
    "shop-slot5-4th": "Mabe Shop/Slot5 4-7",
    "shop-slot5-5th": "Mabe Shop/Slot5 4-7",
    "shop-slot5-6th": "Mabe Shop/Slot5 4-7",
    "shop-slot5-7th": "Mabe Shop/Slot5 4-7",
    "shop-slot6": "Mabe Shop/Slot6",
    "mamasha": "Mamasha/Trade Yoshi Doll",
    "ciao-ciao": "Doghouse/Trade Ribbon to CiaoCiao",
    "mr-write": "Mr. Write/Trade Letter",
    "kiki": "Kiki the Monkey/Trade Bananas",
    "tarin-ukuku": "Ukuku Prairie/Tarin/Trade Stick",
    "5-seashell-reward": "Seashell Mansion/5 Seashells",
    "15-seashell-reward": "Seashell Mansion/15 Seashells",
    "30-seashell-reward": "Seashell Mansion/30 Seashells",
    "40-seashell-reward": "Seashell Mansion/40 Seashells",
    "50-seashell-reward": "Seashell Mansion/50 Seashells",
    "mermaid-martha": "Mermaid/Trade Necklace",
    "bay-fisherman": "Fisherman under the Bridge/Trade Hook",
    "papahl": "Papahl/Trade Pineapple",
    "trendy-prize-1": "Trendy Store/Trendy Prizes 1-3",
    "trendy-prize-2": "Trendy Store/Trendy Prizes 1-3",
    "trendy-prize-3": "Trendy Store/Trendy Prizes 1-3",
    "pothole-north": "Pothole Field Loose/Heart Piece",
    "pothole-final": "Pothole Field/Pothole Field/Slime Key",
    "woods-loose": "Toadstool/Toadstool Spot",
    "diamond-island": "Holes Everywhere!/Heart Piece of Shame",
    "syrup": "Witch's Hut/Witch's Hut",
    "owl-statue-moblin-cave": "Overworld Owls/Taltal Heights/Moblin Cave Owl Statue",
    "owl-statue-pothole": "Pothole Field/Pothole Field/Owl Statue",
    "owl-statue-above-cave": "Bay Plateau/Owl Statue",
    "owl-statue-south-bay": "South Bay/Owl Statue",
    "owl-statue-desert": "Overworld Owls/Yarna Desert/Owl Statue",
    "owl-statue-maze": "Overworld Owls/Ancient Ruins/Owl Statue",
    "owl-statue-rapids": "Rapids Middle/Owl Statue",
    "owl-statue-taltal-east": "Overworld Owls/Taltal Mountains/Owl Statue",
    "owl-statue-below-D8": "Overworld Owls/Goponga Swamp/Owl Statue below D8",
    "swamp-chest": "World/Goponga Swamp/Swamp Chest",
}

# Dungeon number -> (overworld pin, dungeon map folder)
DUNGEONS = {
    0: ("Color Dungeon", "D0 - Color Dungeon"), 1: ("Tail Cave", "D1 - Tail Cave"),
    2: ("Bottle Grotto", "D2 - Bottle Grotto"), 3: ("Key Cavern", "D3 - Key Cavern"),
    4: ("Angler Tunnel", "D4 - Angler Tunnel"), 5: ("Catfish Maw", "D5 - Catfish Maw"),
    6: ("Face Shrine", "D6 - Face Shrine"), 7: ("Eagle Tower", "D7 - Eagle Tower"),
    8: ("Turtle Rock", "D8 - Turtle Rock"),
}
# LASR dungeon location -> room on the pack's dungeon map. The overworld pin's section has the room's name, except
# for checks that share a room (owl statues), which name their section.
DUNGEON_ROOMS = {
    "D0-northern-chest": "After Blob Mini-Boss", "D0-zol-pots": "Zol Pot Room Chest",
    "D0-north-orbs": "Four Orb Enemies", "D0-east-color-puzzle": "Match More Colors",
    "D0-west-color-puzzle": "Match the Colors", "D0-south-orbs": "Orb Enemy Chest", "D0-putters": "South Loop Chest",
    "D0-fairy-1": "Fairy Queen", "D0-fairy-2": "Fairy Queen",
    "D0-owl-statue-nine-switches": "Owl (Nine Switches)", "D0-owl-statue-first-switches": "Owl (First Switches)",
    "D0-owl-statue-before-mini-boss": "Owl (Before Mini Boss)",
    **{f"D0-rupee-{i}": "Rupee Hype Room" for i in range(1, 29)},
    "D1-instrument": "Full Moon Cello", "D1-moldorm": "Moldorm", "D1-west-hallway": "Feather Chest",
    "D1-middle-ledge": "Nightmare Key", "D1-3-of-a-kind": "3 of a Kind", "D1-bomb-room": "Bombable Wall",
    "D1-middle-kill-chest": "Kill the Moldorms", "D1-spark-chest": "Spark Guarded Chest",
    "D1-button-chest": "The Button", "D1-stalfos-chest": "Map Chest", "D1-4-zols-chest": "Compass Chest",
    "D1-beetles": "Hardhat Beetles", "D1-owl-statue-long-hallway": "Owl (Long Hallway)",
    "D1-owl-statue-spinies": "Owl (Spinies)", "D1-owl-statue-3-of-a-kind": "Owl (3-of-a-Kind)",
    "D2-instrument": "Conch Horn", "D2-genie": "Mad Genie", "D2-boos": "Just a couple Boos",
    "D2-long-room-west": "The Lonely Chest", "D2-long-room-east": "Blocks Everywhere!",
    "D2-vacuum-mouth-room": "Map Chest in Vacuum Room", "D2-kill-puzzle": "Pols Voice, Keese, then the Hooded Stalfos",
    "D2-west-chest": "Chest across the gap", "D2-double-stalfos": "Kill the Stalfos",
    "D2-double-shy-guys": "Kill the Shyguys", "D2-entrance-chest": "Entrance Pot Chest",
    "D2-single-shy-guy": "Compass Chest", "D2-peg-circle": "Surrounded by Blocks",
    "D2-button-chest": "Button across the room", "D2-owl-statue-first-switch": "Owl (First Switch)",
    "D2-owl-statue-push-puzzle": "Owl (Push Puzzle)", "D2-owl-statue-past-hinox": "Owl (Past Hinox)",
    "D3-instrument": "Sea Lily's Bell", "D3-slime-eye": "Slime Eye",
    "D3-triple-bombites": "Bombwall Room with Bombites", "D3-north-chest": "After the Bombites",
    "D3-central-ledge": "Kill the Zols and Stalfos", "D3-central-chest": "Chest after Dodongos",
    "D3-east-ledge": "East Ledge", "D3-pairodds": "Pairodd Ceiling Key", "D3-basement-north": "North Basement Room",
    "D3-hallway-4": "Beak Chest around Blocks", "D3-five-zols": "Kill the Zols!", "D3-basement-west": "West Ceiling Key",
    "D3-hallway-3": "Kill the Stalfos & the Zol", "D3-basement-south": "South Ceiling Key",
    "D3-hallway-2": "Kill the Zols", "D3-hallway-side-room": "Map Chest East of the 4 Zols",
    "D3-hallway-1": "First Room North", "D3-vacuum-mouth": "East of Entrance", "D3-pre-boss": "Conveyor Keese Mayhem",
    "D3-owl-statue-basement-north": ("North Basement Room", "Owl (Basement North)"),
    "D3-owl-statue-arrow": "Owl (Arrow)", "D3-owl-statue-northwest": "Owl (Northwest)",
    "D4-instrument": "Surf Harp", "D4-angler": "Angler Fish", "D4-north-chest": "Flippers Chest",
    "D4-east-side-north": "Second Water Room Chest", "D4-east-of-puzzle": "Chest between the two tile rooms",
    "D4-sunken-item": "Key drop in the hole", "D4-ledge-north": "Map Chest", "D4-south-of-puzzle": "Chest guarded by Stalfos",
    "D4-central-room": "Chest surrounded by water", "D4-east-side-south": "First Water Room Chest",
    "D4-small-island": "East Chest on the Island", "D4-west-ledge": "Through the stairs in the tile room",
    "D4-lobby": "East Entrance Room", "D4-statues-chest": "Beak Chest", "D4-crystals": "Crystal Maze Chest",
    "D4-owl-statue": "Owl Statue",
    "D5-instrument": "Wind Marimba", "D5-slime-eel": "Slime Eel", "D5-master-stalfos": "Master Stalfos Round 4",
    "D5-crystal-blocks": "Ceiling Key near Round 4", "D5-past-master-stalfos-3": "After Round 3",
    "D5-water-tunnel": "Nightmare Key through the water", "D5-right-side-north": "Hookshot Chest 3",
    "D5-right-side-middle": "Hookshot Chest 1", "D5-right-side-east": "Hookshot Chest 2",
    "D5-past-master-stalfos-1": "Chest after Round 1", "D5-west-chest": "Chest across the gap",
    "D5-helmasaurs": "Stone Beak", "D5-west-stairs-chest": "Compass Chest", "D5-near-entrance": "First Chest",
    "D5-owl-statue-triple-stalfos": "Owl (Triple Stalfos)", "D5-owl-statue-before-slime-eel": "Owl (Before Slime Eel)",
    "D6-instrument": "Coral Triangle", "D6-facade": "Facade", "D6-far-northwest": "Northwest Horsehead",
    "D6-far-northeast": "Northeast Horsehead", "D6-statue-line-north": "Compass Chest",
    "D6-wizzrobe-pegs": "North Wizzrobe Blocks", "D6-pot-chest": "North Pot Chest", "D6-statue-line-south": "Beak Chest",
    "D6-canal": "Chest in the water", "D6-3-wizzrobes": "More Wizzrobe Killing", "D6-tile-room": "Tile Room Key Drop",
    "D6-gated-hallway-north": "Use the Elephant", "D6-southwest-chest": "Power Bracelet 2 Chest",
    "D6-gated-hallway-south": "Kill the Wizzrobes", "D6-wizzrobes-ledge": "Southeast Blade Trap",
    "D6-owl-statue-ledge": "Owl (Ledge)", "D6-owl-statue-southeast": "Owl (Southeast)",
    "D6-owl-statue-canal": "Owl (Canal)",
    "D7-instrument": "Organ of Evening Calm", "D7-eagle": "Evil Eagle", "D7-3ofakind-north": "Three-of-a-kind",
    "D7-2f-horseheads": "Horseheads", "D7-3f-horseheads": "Conveyor Horseheads",
    "D7-grim-creeper": "Grim Creeper (Not Sans Undertale)", "D7-blue-pegs-chest": "Mirror Shield Chest",
    "D7-hinox": "Hinox", "D7-3ofakind-south": "Three-of-a-kind Hookshot Chest", "D7-west-ledge": "West Ledge",
    "D7-east-ledge": "East Ledge", "D7-1f-west": "Stone Beak", "D7-like-likes": "Like Like Key Drop",
    "D7-owl-statue-ball": "Owl (Ball)", "D7-owl-statue-kirbys": "Owl (Kirbys)",
    "D7-owl-statue-hookshot-chest": "Owl (Hookshot Chest)",
    "D8-instrument": "Thunder Drum", "D8-hothead": "Hothead", "D8-far-northwest": "Northwest Roomba after Cueball",
    "D8-northern-ledge": "North Hookshot Ledge", "D8-far-northeast": "Rod Chest after Blaino",
    "D8-left-exit-chest": "Northwest Hookshot Ledge", "D8-dodongos": "Dodongos", "D8-gibdos": "Gibdos Key Drop",
    "D8-beamos-chest": "Beamos Chest", "D8-statue": "Arrow Statue", "D8-torches": "Light Torches",
    "D8-west-vire": "West Vire", "D8-west-roomba": "After Hinox Chest", "D8-surrounded-by-blocks": "Surrounded By Blocks",
    "D8-sparks-chest": "Sparks Chest", "D8-east-of-pots": "East of Pots", "D8-east-roomba": "East Roomba",
    "D8-far-southwest": "Southwest Vacuum Zol Chest", "D8-far-southeast": "Snakes Southeast",
    "D8-owl-statue-above-smasher": "Owl (Above Smasher)", "D8-owl-statue-below-gibdos": "Owl (Below Gibdos)",
    "D8-owl-statue-eye-statue": "Owl (Eye Statue)",
}


def load(path: Path):
    text = re.sub(r"^\s*//.*$", "", path.read_text(encoding="utf-8-sig"), flags=re.M)
    return json.loads(text)


def pack_sections(pack: Path) -> dict:
    """Full location id -> section names."""
    sections = {}

    def walk(locations, parent):
        for loc in locations:
            loc_id = f"{parent}/{loc['name']}" if parent else loc["name"]
            if loc.get("sections"):
                sections[loc_id] = [s.get("name", "") for s in loc["sections"]]
            walk(loc.get("children", []), loc_id)

    for name in ("overworld.json", "dungeons.json"):
        walk(load(pack / "locations" / name), "")
    return sections


def resolve(sections: dict, code: str) -> str:
    """The full '@<location id>/<section>' for a code naming the end of a location id, as PopTracker matches it."""
    loc, _, section = code.rpartition("/")
    found = [loc_id for loc_id in sections if (loc_id == loc or loc_id.endswith("/" + loc))
             and section in sections[loc_id]]
    if len(found) != 1:
        raise SystemExit(f"{code}: {len(found)} matches {found}")
    return f"@{found[0]}/{section}"


def location_codes(sections: dict) -> dict:
    """LASR location -> its pack section codes."""
    codes = {}
    by_section = {}
    for loc_id, secs in sections.items():
        if not loc_id.startswith("Dungeons/"):
            for sec in secs:
                by_section.setdefault(re.sub(r"[^a-z0-9]", "", sec.lower()), []).append(f"@{loc_id}/{sec}")
    for lasr in IDS["locations"]:
        m = re.match(r"D(\d)-", lasr)
        if m:
            room = DUNGEON_ROOMS[lasr]
            room, section = room if isinstance(room, tuple) else (room, None)
            overworld, folder = DUNGEONS[int(m.group(1))]
            dungeon_secs = sections[f"Dungeons/{folder}/{room}"]
            dungeon_sec = section or next(s for s in dungeon_secs if not s.startswith("Owl (") or len(dungeon_secs) == 1)
            codes[lasr] = [resolve(sections, f"World/{overworld}/{section or room}"),
                           resolve(sections, f"Dungeons/{folder}/{room}/{dungeon_sec}")]
        elif lasr in LOCATIONS:
            codes[lasr] = [resolve(sections, LOCATIONS[lasr])]
        else:
            found = by_section.get(re.sub(r"[^a-z0-9]", "", lasr.lower()), [])
            if len(found) != 1:
                raise SystemExit(f"{lasr}: {len(found)} sections {found}; add it to LOCATIONS")
            codes[lasr] = found
    return codes


def item_codes(pack: Path) -> None:
    known = set()
    for item in load(pack / "items" / "items.json"):
        for code in [item.get("codes", "")] + [s.get("codes", "") for s in item.get("stages", [])]:
            known.update(c.strip() for c in code.split(",") if c.strip())
    for lasr, (code, _) in ITEMS.items():
        if code not in known:
            raise SystemExit(f"item {lasr}: no pack item has code {code}")
        if lasr not in IDS["items"]:
            raise SystemExit(f"item {lasr}: not in ids.json")


def lua_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> None:
    pack = Path(sys.argv[1])
    item_codes(pack)
    codes = location_codes(pack_sections(pack))
    out = pack / "scripts" / "autotracking"
    out.mkdir(parents=True, exist_ok=True)
    header = "-- Generated by LASAP's scripts/poptracker_map.py from its apworld/las/data/ids.json; do not edit.\n"
    lines = [header, "-- Archipelago item id -> {pack code, \"toggle\" | \"progressive\" | \"consumable\" | stage}",
             "ITEM_MAPPING = {"]
    for lasr, (code, kind) in sorted(ITEMS.items(), key=lambda kv: IDS["items"][kv[0]]):
        value = kind if isinstance(kind, int) else lua_string(kind)
        lines.append(f"    [{ITEM_BASE + IDS['items'][lasr]}] = {{{lua_string(code)}, {value}}},  -- {lasr}")
    lines.append("}")
    (out / "item_mapping.lua").write_text("\n".join(lines) + "\n", encoding="utf-8")
    lines = [header, "-- Archipelago location id -> pack sections", "LOCATION_MAPPING = {"]
    for lasr, offset in sorted(IDS["locations"].items(), key=lambda kv: kv[1]):
        sections = ", ".join(lua_string(c) for c in codes[lasr])
        lines.append(f"    [{LOCATION_BASE + offset}] = {{{sections}}},  -- {lasr}")
    lines.append("}")
    (out / "location_mapping.lua").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(ITEMS)} items, {len(codes)} locations written to {out}")


if __name__ == "__main__":
    main()
