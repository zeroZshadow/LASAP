import RandomizerCore.Tools.oead_tools as oead_tools
import copy


class NpcDatasheetFixes:
    def __init__(self, mod_generator) -> None:
        sheet = mod_generator.file_manager.readFile('Npc.gsheet')
        for npc in sheet['values']:
            self.makeNpcChanges(npc)

        self.makeNewNpcs(sheet, mod_generator.placements, mod_generator.item_defs)
        mod_generator.file_manager.writeFile('Npc.gsheet', sheet)


    def makeNpcChanges(self, npc):
        """Makes lots of changes to the Npc datasheet to make this randomizer work, 
        ranging from event triggers to layout conditions to even graphics changes."""

        if npc['symbol'] == 'NpcMadBatter':
            npc['eventTriggers'][0]['entryPoint'] = '$2'
            del npc['layoutConditions'][1]
            return

        if npc['symbol'] == 'ItemGoldenLeaf': # Makes it so golden leaf actors will not spawn, and also removes event just in case
            npc['eventTriggers'] = []
            del npc['layoutConditions'][0]
            del npc['layoutConditions'][0]
            del npc['layoutConditions'][0]
            return

        if npc['symbol'] == 'ItemSmallKey':
            npc['graphics']['path'] = '$1'
            npc['graphics']['model'] = '$2'
            npc['eventTriggers'][2]['entryPoint'] = '$3'
            npc['shellSensor'].append({'category': 9, 'parameter': '$4'}) # make specific smallkey actors trigger the shell sensor
            return

        if npc['symbol'] == 'ObjClothBag': # Make it so Papahl's bag appears with him when you get the Pineapple
            npc['layoutConditions'][1] = {'category': 1, 'parameter': 'TradePineappleGet', 'layoutID': 0}
            return

        if npc['symbol'] == 'NpcGrandmaUlrira':
            npc['layoutConditions'][1] = {'category': 9, 'parameter': 'true', 'layoutID': 4}
            return

        if npc['symbol'] == 'ObjRoosterBones':
            del npc['layoutConditions'][0]
            return

        if npc['symbol'] == 'ObjTelephone': # since telephones swap tunics, make it so the Fairy Queen is talking
            npc['talk'] = {'personalSpace': 1.5, 'talkerLabel': 'NpcFairyQueen'}
            return

        # Adjustments to NPC layouts and shell sensor trigger conditions
        if npc['symbol'] == 'NpcBowWow':
            npc['layoutConditions'][2] = {'category': 3, 'parameter': 'BowWow', 'layoutID': -1}
            return

        if npc['symbol'] == 'NpcMadamMeowMeow':
            npc['layoutConditions'][2] = {'category': 1, 'parameter': 'BowWowJoin', 'layoutID': 3}
            del npc['layoutConditions'][1]
            return

        if npc['symbol'] == 'NpcChorusFrog':
            del npc['layoutConditions'][0]
            return

        if npc['symbol'] in ('NpcChristine', 'NpcSecretZora', 'NpcWalrus'):
            del npc['shellSensor'][0]
            return

        if npc['symbol'] == 'NpcTarin':
            npc['eventTriggers'][5]['additionalConditions'][0] = {'category': 1, 'parameter': '!ShieldGet'} # Make Tarin detain based on talking to him, not having Shield
            npc['eventTriggers'][1]['additionalConditions'][0] = {'category': 4, 'parameter': '3'} # Only the instance of Tarin-Ukuku should trigger the trade event
            npc['layoutConditions'][2] = {'category': 1, 'parameter': 'HoneycombDrop', 'layoutID': -1}
            npc['layoutConditions'][4] = {'category': 1, 'parameter': 'TradeStickGet', 'layoutID': 3} # Make Tarin-ukuku appear when you get the stick
            return

        if npc['symbol'] == 'NpcPapahl':
            npc['layoutConditions'][1] = {'category': 1, 'parameter': 'TradePineappleGet', 'layoutID': 2}
            return

        if npc['symbol'] == 'NpcMamu':
            del npc['layoutConditions'][0] # removes the frog's song layout condition so he's always there
            return

        if npc['symbol'] == 'NpcDanpei':
            npc['shellSensor'].append({'category': 9, 'parameter': '!DampeShellsComplete'})
            return

        if npc['symbol'] == 'NpcRaftShopMan':
            npc['shellSensor'].append({'category': 9, 'parameter': '!RapidsShellsComplete'})
            return

        if npc['symbol'] == 'NpcFisherman':
            npc['shellSensor'].append({'category': 9, 'parameter': '!FishingShellsComplete'})
            return

        if npc['symbol'] == 'NpcShellMansionMaster':
            npc['shellSensor'].append({'category': 9, 'parameter': '!MansionShellsComplete'})
            return



    def makeNewNpcs(self, npc_sheet, placements, item_defs):
        """We change the graphics for some items, so create new npcs to show the correct model when obtaining them"""

        dummy = copy.deepcopy(DUMMY_NPC)
        dummy['symbol'] = 'PatchSmallKey'
        dummy['graphics']['path'] = 'ItemSmallKey.bfres'
        dummy['graphics']['model'] = 'SmallKey'
        npc_sheet['values'].append(oead_tools.dictToStruct(dummy))

        item = placements['syrup']
        dummy['symbol'] = 'SyrupPowder'
        dummy['graphics']['path'] = item_defs[item]['model-path']
        dummy['graphics']['model'] = item_defs[item]['model-name']
        npc_sheet['values'].append(oead_tools.dictToStruct(dummy))

        item = placements['walrus']
        dummy['symbol'] = 'WalrusShell'
        dummy['graphics']['path'] = item_defs[item]['model-path']
        dummy['graphics']['model'] = item_defs[item]['model-name']
        npc_sheet['values'].append(oead_tools.dictToStruct(dummy))

        item = placements['bay-fisherman']
        dummy['symbol'] = 'FishNecklace'
        dummy['graphics']['path'] = item_defs[item]['model-path']
        dummy['graphics']['model'] = item_defs[item]['model-name']
        npc_sheet['values'].append(oead_tools.dictToStruct(dummy))


DUMMY_NPC = {
    'symbol': 'ItemOcarina',
    'graphics': {
        'path': 'ItemOcarina.bfres',
        'model': 'Ocarina',
        'animations': {'idle': '', 'talk': '', 'walk': '', 'run': ''},
        'rootAnimEnabled': False,
        'animSettings': [
            {'name': 'wait', 'blendTime': 4, 'lipsync': 0, 'cull': 2},
            {'name': 'talk', 'blendTime': 4, 'lipsync': 1, 'cull': 2},
            {'name': 'walk', 'blendTime': 4, 'lipsync': 0, 'cull': 2},
            {'name': 'run', 'blendTime': 4, 'lipsync': 0, 'cull': 2}
        ],
        'drsb': 'ItemOcarina.drsb',
        'waterChannel': {'enable': True, 'offsetY': 0.0, 'limitDepth': 0.5},
        'facial': '',
        'interestIK': '',
        'turn': {'enable': False, 'autoTurn': False, 'threshold': 30.0, 'reactionTime': 1.0, 'duration': 0.25},
        'cull': 1
    },
    'eventInfo': {'eventAsset': 'ItemCommon.bfevfl', 'actorName': 'Item'},
    'eventTriggers': [{'condition': 2, 'additionalConditions': [], 'entryPoint': 'Ocarina'}],
    'doAction': {'type': 7, 'yOffset': 0.0, 'xzDistance': 1.5, 'yDistance': 1.7999999523162842,
        'playerAngleRange': 45.0, 'reactionAngleRange': 180.0},
    'talk': {'personalSpace': 1.5, 'talkerLabel': ''},
    'actorArgs': {'layoutID': 0, 'identifier': 0},
    'popConditions': {
        'conditions': [],
        'depopEnable': False
    },
    'layoutConditions': [],
    'lookAtOffsetY': 0.0,
    'behavior': {
        'type': 2,
        'parameters': ['0', '0', '0', '0', '0']
    },
    'movement': {
        'walk': {'moveSpeed': 1.5, 'turnSpeed': 45.0},
        'run': {'moveSpeed': 2.5, 'turnSpeed': 120.0}
    },
    'collision': {
        'shape': 3,
        'traits': 'Ocarina',
        'component': 1,
        'isStatic': False,
        'filter': 5,
        'material': 0,
        'groundCheck': True,
        'offset': {'x': 0.0, 'y': 0.25, 'z': 0.0},
        'rotation': {'x': 0.0, 'y': 0.0, 'z': 0.0},
        'parameters': [1.7999999523162842, 0.5, 1.7999999523162842]
    },
    'damageCollision': {'enable': False, 'sizeDiff': 0.0},
    'ocarina': {'enable': False, 'distance': 0.0},
    'shellSensor': [],
    'attribute': 0
}
