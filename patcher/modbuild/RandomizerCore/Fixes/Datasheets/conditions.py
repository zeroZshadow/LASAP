import RandomizerCore.Tools.oead_tools as oead_tools


class ConditionsDatasheetFixes:
    def __init__(self, mod_generator) -> None:
        sheet = mod_generator.file_manager.readFile('Conditions.gsheet')

        for condition in sheet['values']:
            self.editConditions(condition)

        self.makeConditions(sheet)
        mod_generator.file_manager.writeFile('Conditions.gsheet', sheet)


    def makeConditions(self, sheet):
        """Condition sets that always hold, so the shell sensor stays quiet at Dampe, the rapids, the fishing pond and
        the Seashell Mansion (NpcDatasheetFixes)"""

        for symbol in ('DampeShellsComplete', 'RapidsShellsComplete', 'FishingShellsComplete', 'MansionShellsComplete'):
            sheet['values'].append(oead_tools.createCondition(symbol, [(9, 'true')]))


    def editConditions(self, condition):
        """Makes needed changes to conditions, such as making Marin staying in Mabe and the shop not sell shields until you find one"""

        # Make sure Marin always stays in the village even if you trade for the pineapple
        if condition['symbol'] == 'MarinVillageStay':
            condition['conditions'].pop(1)
            return

        # Make the animals in Animal village not be in the ring, which they would because of WalrusAwaked getting set
        if condition['symbol'] == 'AnimalPop':
            condition['conditions'][0] = {'category': 9, 'parameter': 'false'}
            return

        # Make Grandma Yahoo's broom invisible until you give her the broom
        if condition['symbol'] == 'BroomInvisible':
            condition['conditions'].pop(0)
            return

        if condition['symbol'] == 'BowWowMissionStart': # Remove BoyA condition
            condition['conditions'].pop(0)
            return

        if condition['symbol'] == 'BowWowMissionEnd': # Remove BoyA condition
            condition['conditions'].pop(0)
            return

        # Make the shop not sell shields until you find one
        if condition['symbol'] == 'ShopShieldCondition':
            condition['conditions'][0] = {'category': 1, 'parameter': "ShieldFoundFlag"}
            return

        # Edit the shop conditions for the shovel, bow, and heart
        if condition['symbol'] == 'ShopShovelCondition':
            condition['conditions'].pop(0)
            condition['conditions'][0] = {'category': 1, 'parameter': '!ScoopGet'}
            return

        if condition['symbol'] == 'ShopBowCondition':
            condition['conditions'][0] = {'category': 1, 'parameter': 'ScoopGet'}
            condition['conditions'][1] = {'category': 1, 'parameter': '!BowGet'}
            return

        if condition['symbol'] == 'ShopHeartPieceCondition':
            condition['conditions'][0] = {'category': 1, 'parameter': '!ShopHeartGet'}
