from typing import Dict

from .lasr import load_json

ITEM_BASE = 0x4C410000
LOCATION_BASE = 0x4C420000

_ids = load_json("ids.json")
# LASR name -> stable offset. A location's offset is its local id, the index stored in-game.
ITEM_OFFSETS: Dict[str, int] = _ids["items"]
LOCATION_OFFSETS: Dict[str, int] = _ids["locations"]
