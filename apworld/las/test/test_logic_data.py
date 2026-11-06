"""The logic's data: every name in it resolves, and the patcher's copies match the world's files."""
import unittest
from pathlib import Path

from test.general import setup_multiworld
from worlds.AutoWorld import AutoWorldRegister

from ..lasr import ITEM_DEFS, ITEM_LOCATIONS, LOGIC
from ..lasr_settings import lasr_settings, logic_tokens, setting_token
from ..logic import referenced_tokens

DATA = Path(__file__).resolve().parents[1] / "data"
PATCHER_DATA = Path(__file__).resolve().parents[3] / "patcher" / "data"
COPIES = ("items.yml", "locations.yml", "logic.yml")

# Tokens in logic.yml's conditions that resolve to nothing, so they never hold (logic.py). Removing or correcting one
# changes what the logic allows.
UNRESOLVED = {"dash", "fast-trendy", "free-shop", "pegasus-boots", "prairie", "reduced-farming", "tracey"}


def declared_settings() -> set:
    """The tokens LASR's settings can put into the logic: every boolean setting's, and those of Stealing."""
    world_type = AutoWorldRegister.world_types["Links Awakening Switch"]
    options = setup_multiworld(world_type, (), 1, {"stealing": "standard"}).worlds[1].options
    settings = {setting_token(key) for key, value in lasr_settings(options).items() if isinstance(value, bool)}
    return settings | set(logic_tokens(options))


class TestLogicNames(unittest.TestCase):
    def test_tokens_resolve(self) -> None:
        """As LogicCompiler.token resolves them: a setting, an item, a node that is no location, or an enemy's variant."""
        settings = declared_settings()
        unresolved = set()
        for token in referenced_tokens(LOGIC):
            base, _, variant = token.partition("[")
            if variant:
                resolved = LOGIC.get(base, {}).get("type") == "enemy"
            else:
                resolved = token in settings or token in ITEM_DEFS or LOGIC.get(token, {}).get("type", "item") != "item"
            if not resolved:
                unresolved.add(token)
        self.assertEqual(sorted(unresolved), sorted(UNRESOLVED))

    def test_locations(self) -> None:
        """A location holds an item of the pool and stands in a node."""
        for location in ITEM_LOCATIONS:
            with self.subTest(location=location):
                self.assertIn(LOGIC[location]["content"], ITEM_DEFS)
                self.assertIn(LOGIC[location]["region"], LOGIC)


@unittest.skipUnless(PATCHER_DATA.is_dir(), "patcher/data not found")
class TestPatcherCopies(unittest.TestCase):
    def test_copies_match(self) -> None:
        for name in COPIES:
            with self.subTest(name=name):
                self.assertEqual((DATA / name).read_bytes(), (PATCHER_DATA / name).read_bytes())
