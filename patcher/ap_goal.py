"""The Wind Fish's egg (DESIGN.md, Goal): the module answers the egg's HasAllInstruments query and names the counts on
the sign from config.ini [archipelago]; on a seashell hunt the egg's door leads to the ending (`jumps`)."""
from typing import List, Optional

EVENT_FILE = "WindFishsEgg.bfevfl"
ALL_QUERY = "HasAllInstruments"
SWITCHES = 4  # the switches on ALL_QUERY in the orchestra
SIGN_FLAG = "APSignRead"  # AP::SIGN_FLAG, added to GlobalFlags by modbuild/mod.py
EGG_LEVEL = "Lv09WindFishsEgg"
EGG_LOCATOR = "Lv09WindFishsEgg_08D"  # where the egg's door in Field_01G leads
ENDING_LEVEL = "Ending"
ENDING_LOCATOR = "Lv09WindFishsEgg_05H"  # in Ending_13O


def settings_seashells(settings) -> Optional[int]:
    """The seashells a seashell hunt takes, None for the instruments goal."""
    return settings["Seashells Required"] if settings["Seashell Hunt"] else None


def jumps(seashells: Optional[int] = None) -> List[str]:
    """The jumps' lines: on a hunt the egg's door leads to the ending (the module leads the jump)."""
    return [] if seashells is None else [f"Field {EGG_LEVEL} {EGG_LOCATOR} {ENDING_LEVEL} {ENDING_LOCATOR}"]
