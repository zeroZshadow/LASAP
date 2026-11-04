import RandomizerCore.Tools.event_tools as event_tools


class PlayerStartEventFixes:
    """The start event's changes but its items and flags, which the exlaunch module gives and sets (the seed file's
    start: START_FLAGS, BOSS_FLAGS)"""

    def __init__(self, mod_generator) -> None:
        self.parent = mod_generator
        flow = self.parent.file_manager.readFile('PlayerStart.bfevfl')
        self.makeStartChanges(flow.flowchart)
        self.parent.file_manager.writeFile('PlayerStart.bfevfl', flow)


    def makeStartChanges(self, flowchart) -> None:
        # Remove the part that kills the rooster after D7 in Level7DungeonIn_FlyingCucco
        event_tools.insertEventAfter(flowchart, 'Level7DungeonIn_FlyingCucco', 'Event476')

        # Remove the 7 second timeOut wait on the companion when it gets blocked from a loading zone
        timeout_events = ('Event637', 'Event660', 'Event693', 'Event696', 'Event371', 'Event407', 'Event478')
        for e in timeout_events:
            event_tools.findEvent(flowchart, e).data.params.data['timeOut'] = 0.0


START_FLAGS = (
    'FirstClear',
    'SecondClear',
    'ThirdClear',
    'FourthClear',
    'FifthClear',
    'SixthClear',
    'SeventhClear',
    'NinthClear',
    'TenthClear',
    'EleventhClear',
    'TwelveClear',
    'ThirteenClear',
    'FourteenClear',
    'FiveteenClear',
    'WalrusAwaked',
    'MarinRescueClear',
    'SwordGet',
    'UI_FieldMapTraverse_MabeVillage', # mabe wont be cleared on the map if it starts in the bowwow-stolen phase
    # figured it out, this phase uses a different zoneID which means I need to make a new entry in UiFieldMapMask.gsheet
)

BOSS_FLAGS = (
    'Lv1BossDemoClear',
    'Lv2BossDemoClear',
    'Lv3BossDemoClear',
    'Lv4BossDemoClear',
    'Lv5BossDemoClear',
    'Lv05BrokeWall1',
    'Lv05BrokeWall2',
    'Lv05BrokeWall3',
    'Lv05BrokeWall4',
    'Lv05BrokeFloor',
    'Lv6BossDemoClear',
    'Lv7BossDemoClear',
    'Lv8BossDemoClear',
    'Lv9BossDemoClear',
    'ShadowBattle',
    'LanmolaDemoClear',
    'GrimCreeperDemoClear',
    'StoneHinoxDemoClear',
    'GiantBuzzBlobDemoClear',
    'EvilOrbDemoClear',
    'DeguArmosDemoClear',
    'LanemoraDemoClear'
)


