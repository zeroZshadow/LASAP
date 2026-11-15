"""The logic's data: every name in it resolves."""
import unittest

from . import generate_world
from ..lasr import ITEM_DEFS, ITEM_LOCATIONS, LOGIC
from ..lasr_settings import lasr_settings, logic_tokens, setting_token
from ..logic import NEVER, referenced_tokens


def declared_settings() -> set:
    """The tokens LASR's settings can put into the logic: every boolean setting's, and those of Stealing."""
    options = generate_world({"stealing": "standard"}, 1, ()).options
    settings = {setting_token(key) for key, value in lasr_settings(options).items() if isinstance(value, bool)}
    return settings | set(logic_tokens(options))


class TestLogicNames(unittest.TestCase):
    def test_tokens_resolve(self) -> None:
        """As LogicCompiler.token resolves them: a setting, an item, a node that is no location, or an enemy's variant.
        NEVER, which lowering writes for a zone with no way in, never holds by design (test_routes checks the zones)."""
        settings = declared_settings()
        unresolved = set()
        for token in referenced_tokens(LOGIC):
            base, _, variant = token.partition("[")
            if token == NEVER:
                continue
            if variant:
                resolved = LOGIC.get(base, {}).get("type") == "enemy"
            else:
                resolved = token in settings or token in ITEM_DEFS or LOGIC.get(token, {}).get("type", "item") != "item"
            if not resolved:
                unresolved.add(token)
        self.assertEqual(sorted(unresolved), [])

    def test_locations(self) -> None:
        """A location holds an item of the pool and stands in a node."""
        for location in ITEM_LOCATIONS:
            with self.subTest(location=location):
                self.assertIn(LOGIC[location]["content"], ITEM_DEFS)
                self.assertIn(LOGIC[location]["region"], LOGIC)

