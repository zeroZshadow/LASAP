import json
import pkgutil
from typing import Dict

ITEM_BASE = 0x4C410000
LOCATION_BASE = 0x4C420000

_ids = json.loads(pkgutil.get_data(__name__, "data/ids.json").decode("utf-8"))
# LASR name -> stable offset. A location's offset is its local id, the index stored in-game.
ITEM_OFFSETS: Dict[str, int] = _ids["items"]
LOCATION_OFFSETS: Dict[str, int] = _ids["locations"]
