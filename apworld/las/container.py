"""The .aplas patch file: an Archipelago player container (zip) holding archipelago.json and patch.json."""
import json
import zipfile
from typing import Any, Dict, Optional

from worlds.Files import APPlayerContainer

from .items import GAME

PATCH_JSON = "patch.json"


class LASContainer(APPlayerContainer):
    game = GAME
    patch_file_ending = ".aplas"

    def __init__(self, path: Optional[str] = None, patch: Optional[Dict[str, Any]] = None,
                 player: Optional[int] = None, player_name: str = "", server: str = ""):
        super().__init__(path, player, player_name, server)
        self.patch = patch or {}

    def write_contents(self, opened_zipfile: zipfile.ZipFile) -> None:
        opened_zipfile.writestr(PATCH_JSON, json.dumps(self.patch, indent=1))
        super().write_contents(opened_zipfile)

    def read_contents(self, opened_zipfile: zipfile.ZipFile) -> Dict[str, Any]:
        manifest = super().read_contents(opened_zipfile)
        self.patch = json.loads(opened_zipfile.read(PATCH_JSON).decode("utf-8"))
        return manifest
