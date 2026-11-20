import RandomizerCore.Tools.oead_tools as oead_tools


class CraneGameDatasheetFixes:
    def __init__(self, mod_generator) -> None:
        sheet = mod_generator.file_manager.readFile('CranePrize.gsheet')
        self.makeDatasheetChanges(sheet)
        mod_generator.file_manager.writeFile('CranePrize.gsheet', sheet)

        group1 = mod_generator.file_manager.readFile('CranePrizeFeaturedPrizeGroup1.gsheet')
        self.changePrizeGroups(group1)
        mod_generator.file_manager.writeFile('CranePrizeFeaturedPrizeGroup1.gsheet', group1)


    def makeDatasheetChanges(self, sheet):
        """Edits conditions in the Trendy prizes datasheet"""
        
        # PRIZE PLACEMENTS
        # type 1 is the lower level, index 0 on the left and index 2 on the right
        # type 2 is upper level, index 0 on the left and index 1 on the right

        for prize in sheet['values']:

            # Shield should not be obtainable until you find your first shield
            if prize['symbol'] == 'Shield':
                prize['layouts'][0]['conditions'].append({'category': 1, 'parameter': "ShieldFoundFlag"})
                continue

            if prize['symbol'] == 'RupeeRed':
                # Now because the 2 non featured prizes in this slot have conditions, nothing will be in this slot
                # Lets fix this by adding RupeeRed prizes to the slot that will be met automatically
                prize['layouts'].append(oead_tools.dictToStruct({
                    'itemIndex': -1,
                    'conditions': [],
                    'place': {'type': 2, 'index': 1}
                }))

                prize['layouts'][2]['conditions'].append({'category': 2, 'parameter': '!SurfHarp'})
                
                prize['layouts'][2]['conditions'].append({'category': 1, 'parameter': f'!ShieldFoundFlag'})

                # We are removing Yoshi from being considered as a featured prize
                # So we want to make the red rupee that would otherwise replace it require the yoshi gotten flag
                prize['layouts'][0]['conditions'].append({'category': 1, 'parameter': 'TradeYoshiDollGet'})
                continue
            
            # Make the YoshiDoll prize go away once you get it since we don't actually keep the item
            if prize['symbol'] == 'YoshiDoll':
                prize['layouts'][0]['conditions'].append({'category': 1, 'parameter': '!TradeYoshiDollGet'})
                prize['options']['isFeatured'] = False
                continue
            
            # SmallBowWow (Ciao Ciao): Remove the condition of HintYosshi. It's unnecessary and can lead to a softlock
            if prize['symbol'] == 'SmallBowWow':
                prize['layouts'][0]['conditions'].pop(0)
                continue

            # BowWow: Remove the ShadowClear condition. This was stupid in vanilla and it's even worse for rando.
            if prize['symbol'] == 'BowWow':
                prize['layouts'][0]['conditions'].pop(0)
                continue


    def changePrizeGroups(self, sheet1):
        sheet1['values'].pop(0) # remove yoshi doll
