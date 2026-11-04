import RandomizerCore.Tools.event_tools as event_tools


class CommonEventFixes:
    """Make Save&Quit after getting a GameOver send you back to Marin's house"""

    def __init__(self, mod_generator) -> None:
        flow = mod_generator.file_manager.readFile('Common.bfevfl')

        event_tools.setSwitchEventCase(flow.flowchart, 'Event64', 1,
            event_tools.createActionEvent(flow.flowchart, 'GameControl', 'RequestLevelJump',
                {'level': 'Field', 'locator': 'Field_11C', 'offsetX': 0.0, 'offsetZ': 0.0},
                'Event67'))

        mod_generator.file_manager.writeFile('Common.bfevfl', flow)
