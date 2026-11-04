"""The Wind Fish's egg opens with fewer instruments, or with secret seashells instead (the Goal, Instruments Required
and Seashells Required options), all through the module, from config.ini [archipelago] (instruments_required,
seashell_hunt, seashells_required; las_ap_patch.config_text).

The egg's song (`WindFishsEgg.bfevfl`, entry `orchestra`) asks `Inventory.HasAllInstruments` at SWITCHES switches:
whether the instruments play, which song plays, how the scene fades and whether the egg opens. The module answers the
query (ap_egg.cpp): whether Link holds instruments_required instruments or, on a seashell hunt, that many seashells.
The event stays as it is.

For the seashell hunt the module also opens the egg once Link holds the seashells, at once with none (ap_egg.cpp), and
leads the egg's door in Field_01G to the end instead of into the egg (`jumps`): where the stairs after the fight with
the Nightmare lead (Lv09WindFishsEgg.bfevfl, StairsUp's RequestLevelJump), the Wind Fish's ending.

The sign at the foot of the egg's stairs (`Place:SBMountTamaranch`, only used there) names the numbers, so a random
number can be read in the game: the module adds them to its text, and sets SIGN_FLAG in the save once the sign's text
shows, for the trackers."""
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
    """The secret seashells a patch's settings take on a seashell hunt, None for the instruments goal. A patch without
    the key Seashell Hunt is a hunt when it takes seashells."""
    seashells = settings.get("Seashells Required", 0)
    return seashells if settings.get("Seashell Hunt", seashells > 0) else None


def jumps(seashells: Optional[int] = None) -> List[str]:
    """The jumps' lines: on a hunt the egg's door leads to the ending (the module leads the jump)."""
    return [] if seashells is None else [f"Field {EGG_LEVEL} {EGG_LOCATOR} {ENDING_LEVEL} {ENDING_LOCATOR}"]
