from RandomizerCore.Fixes.Events.skeletal_guard import SkeletalGuardEventFixes
from RandomizerCore.Fixes.Events.windfishs_egg import WindFishsEggEventFixes
from RandomizerCore.Fixes.Events.player_start import PlayerStartEventFixes
from RandomizerCore.Fixes.Events.prize_common import PrizeCommonEventFixes
from RandomizerCore.Fixes.Events.madam_meow import MadamMeowMeowEventFixes
from RandomizerCore.Fixes.Events.tunic_swap import TunicSwapper
from RandomizerCore.Fixes.Events.common import CommonEventFixes
from RandomizerCore.Fixes.Events.item import ItemEventFixes


class EventFixes:
    """Make changes to events that should be in every seed, regardless of item placements"""

    def __init__(self, mod_generator) -> None:
        PlayerStartEventFixes(mod_generator)
        ItemEventFixes(mod_generator)
        MadamMeowMeowEventFixes(mod_generator)
        WindFishsEggEventFixes(mod_generator)
        SkeletalGuardEventFixes(mod_generator)
        CommonEventFixes(mod_generator)
        PrizeCommonEventFixes(mod_generator)
        TunicSwapper(mod_generator)
