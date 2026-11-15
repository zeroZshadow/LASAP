"""Where checks sit in the RomFS, shared by map_positions.py and Logic Studio's extract.py. Needs nothing but the
standard library."""
import ast
from pathlib import Path

RANDOMIZERS = Path(__file__).resolve().parent.parent / "patcher" / "modbuild" / "RandomizerCore" / "Randomizers"

# Checks in buildings without a room table: the level whose overworld door marks them.
DOOR_LEVELS = {
    "sale": "BananaHouse", "mermaid-cave": "MermaidStatue", "christine-trade": "ChristineHouse",
    "christine-grateful": "ChristineHouse", "invisible-zora": "HiddenZoraHouse", "chef-bear": "BearHouse",
    "goriya-trader": "BoomerangCave", "tarin": "MarinTarinHouse", "mamasha": "QuadupletHouse", "ciao-ciao": "DogHouse",
    "armos-knight": "ArmosShrine", "mr-write": "DrWrightHouse", "mad-batter-bay": "MadBattersWell01",
    "mad-batter-woods": "MadBattersWell02", "mad-batter-taltal": "MadBattersWell03", "mamu": "MamuCave",
    "manbo": "MamboCave", "syrup": "MagicPowderHouse", "dream-shrine-left": "BedRoom", "moblin-cave": "MoriblinCave",
    "rooster-statue": "FlyingCuccoCave",
    **{f"dampe-{part}": "DanpeiHouse" for part in ("page-1", "heart-challenge", "page-2", "bottle-challenge", "final")},
    **{loc: "Shop" for loc in ("shop-slot3-1st", "shop-slot3-2nd", "shop-slot5-1st", "shop-slot5-2nd", "shop-slot5-3rd",
                               "shop-slot5-4th", "shop-slot5-5th", "shop-slot5-6th", "shop-slot5-7th", "shop-slot6")},
    **{f"{n}-seashell-reward": "SeashellHouse" for n in (5, 15, 30, 40, 50)},
    **{f"rapids-race-{n}": "RapidsRideTicket" for n in (45, 35, 30)},
    **{f"trendy-prize-{n}": "CraneGame" for n in (1, 2, 3, 4, 5, 6, "final")},
}

# Actors that hold a check, by name prefix or by their first parameter (rocks, bushes and holes hiding an item).
ITEM_ACTORS = ("ObjTreasureBox", "ItemHeartPiece", "ItemSeashell", "ItemGoldenLeaf", "ItemSmallKey", "ItemHeartContainer")
ITEM_PARAMS = ("Seashell", "GoldenLeaf", "HeartPiece", "SmallKey")
# The instruments, each its own actor (a room's actor names keep 12 characters).
INSTRUMENT_ACTORS = ("ItemFullMoon", "ItemConchHorn", "ItemSeaLilys", "ItemSurfHarp", "ItemWindMari", "ItemCoralTri",
                     "ItemEveningC", "ItemThunderD")


def room_tables() -> dict:
    """LASR location -> room, from the *_ROOMS dicts in LASR's randomizers."""
    rooms = {}
    for path in sorted(RANDOMIZERS.glob("*.py")):
        for node in ast.parse(path.read_text(encoding="utf-8")).body:
            if (isinstance(node, ast.Assign) and isinstance(node.value, ast.Dict)
                    and isinstance(node.targets[0], ast.Name) and node.targets[0].id.endswith("_ROOMS")
                    and not node.targets[0].id.startswith("PANEL")):
                try:
                    table = ast.literal_eval(node.value)
                except ValueError:
                    continue
                for loc, room in table.items():
                    if isinstance(room, str):
                        rooms.setdefault(loc, room)
    return rooms
