import RandomizerCore.Tools.event_tools as event_tools


class SeashellMansionRandomizer:
    """Places items on the presents"""

    def __init__(self, mod_generator) -> None:
        self.parent = mod_generator
        self.seashellMansionChanges()


    def seashellMansionChanges(self):
        """Adds the new item key and index to each present"""

        flow = self.parent.file_manager.readFile('ShellMansionMaster.bfevfl')

        item_key, item_index = self.parent.item_info_manager.getItemInfo('5-seashell-reward')
        event_tools.findEvent(flow.flowchart, 'Event36').data.params.data =\
            {'pointIndex': 0, 'itemKey': item_key, 'itemIndex': item_index, 'flag': 'GetSeashell10'}

        item_key, item_index = self.parent.item_info_manager.getItemInfo('15-seashell-reward')
        event_tools.findEvent(flow.flowchart, 'Event10').data.params.data =\
            {'pointIndex': 0, 'itemKey': item_key, 'itemIndex': item_index, 'flag': 'GetSeashell20'}

        item_key, item_index = self.parent.item_info_manager.getItemInfo('30-seashell-reward')
        event_tools.findEvent(flow.flowchart, 'Event11').data.params.data =\
            {'pointIndex': 0, 'itemKey': item_key, 'itemIndex': item_index, 'flag': 'GetSeashell30'}

        item_key, item_index = self.parent.item_info_manager.getItemInfo('50-seashell-reward')
        event_tools.findEvent(flow.flowchart, 'Event13').data.params.data =\
            {'pointIndex': 0, 'itemKey': item_key, 'itemIndex': item_index, 'flag': 'GetSeashell50'}

        self.parent.item_get_manager.get(flow.flowchart, '40-seashell-reward', 'Event91', 'Event79')

        # event fixes
        # 40 shells, doesn't use a present box
        event_tools.findEvent(flow.flowchart, 'Event65').data.forks.pop(0)

        event_tools.insertEventAfter(flow.flowchart, 'Event64', 'Event65')

        # Remove the thing to show Link's sword because it will show L1 sword if he has none. 
        sword_check1 = event_tools.createSwitchEvent(flow.flowchart, 'EventFlags', 'CheckFlag', {'symbol': "SwordFoundFlag"}, {0: 'Event65', 1: 'Event64'})
        event_tools.insertEventAfter(flow.flowchart, 'Event80', sword_check1)

        # and the 2nd time
        sword_check2 = event_tools.createSwitchEvent(flow.flowchart, 'EventFlags', 'CheckFlag', {'symbol': "SwordFoundFlag"}, {0: 'Event48', 1: 'Event47'})
        event_tools.insertEventAfter(flow.flowchart, 'Event54', sword_check2)
        event_tools.insertEventAfter(flow.flowchart, 'Event74', 'Event19')

        self.parent.file_manager.writeFile('ShellMansionMaster.bfevfl', flow)
