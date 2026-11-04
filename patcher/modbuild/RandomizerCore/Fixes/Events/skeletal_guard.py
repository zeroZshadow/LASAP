import RandomizerCore.Tools.event_tools as event_tools
import ap_option_edits


class SkeletalGuardEventFixes:
    """Make the blue guard sell 20 bombs in addition to the 20 powder"""

    def __init__(self, mod_generator) -> None:
        flow = mod_generator.file_manager.readFile('SkeletalGuardBlue.bfevfl')

        # edit Magic Powder amount from 20 to 40 so that it'll max even with the capacity upgrade
        event_tools.findEvent(flow.flowchart, 'Event19').data.params.data['count'] = 40

        ap_option_edits.powder_guard(flow.flowchart)
        mod_generator.file_manager.writeFile('SkeletalGuardBlue.bfevfl', flow)
