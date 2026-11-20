import RandomizerCore.Tools.event_tools as event_tools


class ItemEventFixes:
    """Add and fix some entry points for the ItemGetSequence"""

    def __init__(self, mod_generator) -> None:
        self.parent = mod_generator
        flow = self.parent.file_manager.readFile('Item.bfevfl')
        self.fixEntryPoints(flow.flowchart)
        self.parent.file_manager.writeFile('Item.bfevfl', flow)


    def fixEntryPoints(self, flowchart) -> None:
        event_tools.addEntryPoint(flowchart, 'MagicPowder_MaxUp')
        event_tools.createActionChain(flowchart, 'MagicPowder_MaxUp', [
            ('Dialog', 'Show', {'message': 'SubEvent:ByebyeMadBatter'})
        ])
        event_tools.addEntryPoint(flowchart, 'Bomb_MaxUp')
        event_tools.createActionChain(flowchart, 'Bomb_MaxUp', [
            ('Dialog', 'Show', {'message': 'SubEvent:ByebyeMadBatter'})
        ])
        event_tools.addEntryPoint(flowchart, 'Arrow_MaxUp')
        event_tools.createActionChain(flowchart, 'Arrow_MaxUp', [
            ('Dialog', 'Show', {'message': 'SubEvent:ByebyeMadBatter'})
        ])

        # create message for obtaining the bow, for some weird reason the game doesnt have one when you buy it
        event_tools.addEntryPoint(flowchart, 'Bow')
        event_tools.createActionChain(flowchart, 'Bow', [
            ('Dialog', 'Show', {'message': 'UI:ItemName_Bow'})
        ])

        event_tools.findEntryPoint(flowchart, 'GreenClothes').name = 'ClothesGreen'
        event_tools.findEntryPoint(flowchart, 'RedClothes').name = 'ClothesRed'
        event_tools.findEntryPoint(flowchart, 'BlueClothes').name = 'ClothesBlue'
        event_tools.findEntryPoint(flowchart, 'Necklace').name = 'PinkBra'

        # Dampe's rewards (DampeRandomizer): each entry shows its location's item text
        for entry, location in (('Dampe1', 'dampe-page-1'), ('DampeHeart', 'dampe-heart-challenge'),
                                ('Dampe2', 'dampe-page-2'), ('DampeBottle', 'dampe-bottle-challenge'),
                                ('DampeFinal', 'dampe-final')):
            event_tools.addEntryPoint(flowchart, entry)
            item_key = self.parent.item_defs[self.parent.placements[location]]['item-key']
            event_tools.insertEventAfter(flowchart, entry, event_tools.createSubFlowEvent(flowchart, '', item_key, {}))
