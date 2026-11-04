import RandomizerCore.Tools.event_tools as event_tools


class ItemGetManager:
    def __init__(self, mod_generator) -> None:
        self.parent = mod_generator


    def get(self, flowchart, location, before=None, after=None, force_anim=False):
        """Inserts the needed itemGet event into the flowchart

        Skips over the animation if the item is a rupee or dungeon item"""

        item_key, item_index = self.parent.item_info_manager.getItemInfo(location)

        if self.checkItemNeedsAnimation(item_key) or force_anim:
            return self.getWithAnimation(flowchart, item_key, item_index, before, after)
        else:
            return event_tools.createActionChain(flowchart, before, [
                ('Inventory', 'AddItemByKey', {'itemKey': item_key, 'count': 1, 'index': item_index, 'autoEquip': False})
            ], after)


    def checkItemNeedsAnimation(self, item) -> bool:
        """Some items skip over the animation for the sake of speeding up gameplay

        Dungeon items in their own dungeon skip it too"""

        return item not in ("SmallKey", "NightmareKey", "DungeonMap", "Compass", "StoneBeak") \
            and not item.startswith("Rupee")


    # Inserts an AddItemByKey and a GenericItemGetSequenceByKey, or a progressive item switch (depending on the item).
    # It goes after 'before' and before 'after'. Return the name of the first event in the sequence.
    # we used to have a 'play_extra_anim' flag only used for doing a spin attack when you get sword, it has been removed
    # we used to have a 'can_hurt_player' flag for traps since dying in certain cases can softlock, this will be handled through code now
    def getWithAnimation(self, flowchart, item, index, before=None, after=None):
        """Inserts an itemGet event into the flowchart and returns the name of the first event in the sequence

        Parameters
        ----------
        flowchart: dict[str, any]
            The flowchart of the eventflow file
        item : str
            The key of the item
        index : int
            The index of the item
        before : str | None
            The event that comes before the returned ItemGetAnimation
        after : str | None
            The event that comes after the returned ItemGetAnimation"""

        # progressive items
        if item == 'PowerBraceletLv1':
            return event_tools.createProgressiveItemSwitch(flowchart, 'PowerBraceletLv1', 'PowerBraceletLv2',
                "BraceletFoundFlag", before, after)

        if item == 'SwordLv1':
            return event_tools.createProgressiveItemSwitch(flowchart, 'SwordLv1', 'SwordLv2',
                "SwordFoundFlag", before, after)

        if item == 'Shield':
            return event_tools.createProgressiveItemSwitch(flowchart, 'Shield', 'MirrorShield',
                "ShieldFoundFlag", before, after)    

        # traps
        if item == 'ZapTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            stop_event = event_tools.createActionEvent(flowchart, 'Link', 'StopTailorOtherChannel',
                {'channel': 'toolshopkeeper_dmg', 'index': 0}, autosave_event)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'ev_dmg_elec_lp'}),
                event_tools.createActionEvent(flowchart, 'Link', 'PlayTailorOtherChannelEx',
                    {'channel': 'toolshopkeeper_dmg', 'index': 0, 'restart': False, 'time': 1.0}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
            ]
            forks.append(event_tools.createActionEvent(flowchart, 'Link', 'Damage', {'amount': 6}))
            return event_tools.createForkEvent(flowchart, before, forks, stop_event)[0]

        if item == 'DrownTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'fall_water'}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True})
            ]
            forks.append(event_tools.createActionChain(flowchart, None, [
                ('Timer', 'Wait', {'time': 1.5}),
                ('Link', 'Damage', {'amount': 2})
            ]))
            return event_tools.createForkEvent(flowchart, before, forks, autosave_event)[0]

        if item == 'SquishTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'dmg_press'}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
                event_tools.createActionEvent(flowchart, 'Timer', 'Wait', {'time': 2.0})
            ]
            forks.append(event_tools.createActionEvent(flowchart, 'Link', 'Damage', {'amount': 4}))
            return event_tools.createForkEvent(flowchart, before, forks, autosave_event)[0]

        if item == 'DeathballTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayTailorOtherChannelEx',
                    {'channel': 'GreatFairy_Heal', 'index': 0, 'restart': False, 'time': 0.0}),
                event_tools.createActionChain(flowchart, None, [
                    ('Timer', 'Wait', {'time': 0.1}),
                    ('Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'fall_deathball'})
                ])
            ]
            forks.append(event_tools.createActionChain(flowchart, None, [
                ('Hud', 'SetHeartUpdateEnable', {'enable': True}),
                ('Timer', 'Wait', {'time': 1.5}),
                ('Link', 'Damage', {'amount': 2})
            ]))
            return event_tools.createForkEvent(flowchart, before, forks, autosave_event)[0]

        if item == 'QuakeTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'dmg_quake'}),
                event_tools.createActionEvent(flowchart, 'Timer', 'Wait', {'time': 1.5}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
                event_tools.createActionEvent(flowchart, 'Link', 'Damage', {'amount': 2})
            ]
            return event_tools.createForkEvent(flowchart, before, forks, autosave_event)[0]

        # tunics
        if item == 'ClothesRed':
            return event_tools.createActionChain(flowchart, before, [
                ('EventFlags', 'SetFlag', {'symbol': 'RedTunicFoundFlag', 'value': True}),
                ('Link', 'PlayTailorOtherChannelEx', {'channel': 'Change_Color_Red_00', 'index': 0, 'restart': False, 'time': 3.58}),
                ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False}),
                ('Link', 'GenericItemGetSequenceByKey', {'itemKey': item, 'keepCarry': False, 'messageEntry': 'ClothesRed'})
            ], after)

        if item == 'ClothesBlue':
            return event_tools.createActionChain(flowchart, before, [
                ('EventFlags', 'SetFlag', {'symbol': 'BlueTunicFoundFlag', 'value': True}),
                ('Link', 'PlayTailorOtherChannelEx', {'channel': 'Change_Color_Blue_00', 'index': 0, 'restart': False, 'time': 3.58}),
                ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False}),
                ('Link', 'GenericItemGetSequenceByKey', {'itemKey': item, 'keepCarry': False, 'messageEntry': 'ClothesBlue'})
            ], after)

        if item == 'ClothesGreen':
            return event_tools.createActionChain(flowchart, before, [
                ('Link', 'PlayTailorOtherChannelEx', {'channel': 'Change_Color_Green_00', 'index': 0, 'restart': False, 'time': 3.58}),
                ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False}),
                ('Link', 'GenericItemGetSequenceByKey', {'itemKey': item, 'keepCarry': False, 'messageEntry': 'ClothesGreen'})
            ], after)

        message_entry = item

        # EVERYTHING ELSE - play the get event before giving the item, otherwise it messes with index related messages
        # this is how the game normally does it, and so for the "you've collected them all" messages,
        # the game actually checks for 3 heart pieces and 4 golden leaves respectively
        return event_tools.createActionChain(flowchart, before, [
            ('Link', 'GenericItemGetSequenceByKey', {'itemKey': item, 'keepCarry': False, 'messageEntry': message_entry}),
            ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False})
        ], after)


    def getForDampe(self, flowchart, item, index, after=None):
        # progressive items
        if item == 'PowerBraceletLv1':
            give_bracelet2 = event_tools.createActionEvent(flowchart, 'Inventory', 'AddItemByKey',
                {'itemKey': 'PowerBraceletLv2', 'count': 1, 'index': -1, 'autoEquip': False}, after)
            give_bracelet1 = event_tools.createActionEvent(flowchart, 'Inventory', 'AddItemByKey',
                {'itemKey': item, 'count': 1, 'index': -1, 'autoEquip': False}, after)
            return event_tools.createSwitchEvent(flowchart, 'EventFlags', 'CheckFlag',
                {'symbol': "BraceletFoundFlag"}, {0: give_bracelet1, 1: give_bracelet2})

        if item == 'SwordLv1':
            give_sword2 = event_tools.createActionEvent(flowchart, 'Inventory', 'AddItemByKey',
                {'itemKey': 'SwordLv2', 'count': 1, 'index': -1, 'autoEquip': False}, after)
            give_sword1 = event_tools.createActionEvent(flowchart, 'Inventory', 'AddItemByKey',
                {'itemKey': item, 'count': 1, 'index': -1, 'autoEquip': False}, after)
            return event_tools.createSwitchEvent(flowchart, 'EventFlags', 'CheckFlag',
                {'symbol': "SwordFoundFlag"}, {0: give_sword1, 1: give_sword2})

        if item == 'Shield':
            give_shield2 = event_tools.createActionEvent(flowchart, 'Inventory', 'AddItemByKey',
                {'itemKey': 'MirrorShield', 'count': 1, 'index': -1, 'autoEquip': False}, after)
            give_shield1 = event_tools.createActionEvent(flowchart, 'Inventory', 'AddItemByKey',
                {'itemKey': item, 'count': 1, 'index': -1, 'autoEquip': False}, after)
            return event_tools.createSwitchEvent(flowchart, 'EventFlags', 'CheckFlag',
                {'symbol': "ShieldFoundFlag"}, {0: give_shield1, 1: give_shield2})    

        # traps
        if item == 'ZapTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            stop_event = event_tools.createActionEvent(flowchart, 'Link', 'StopTailorOtherChannel',
                {'channel': 'toolshopkeeper_dmg', 'index': 0}, autosave_event)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'ev_dmg_elec_lp'}),
                event_tools.createActionEvent(flowchart, 'Link', 'PlayTailorOtherChannelEx',
                    {'channel': 'toolshopkeeper_dmg', 'index': 0, 'restart': False, 'time': 1.0}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
                event_tools.createActionEvent(flowchart, 'Link', 'Damage', {'amount': 6})
            ]
            return event_tools.createForkEvent(flowchart, None, forks, stop_event)[0]

        if item == 'DrownTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'fall_water'}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
                event_tools.createActionChain(flowchart, None, [
                    ('Timer', 'Wait', {'time': 1.5}),
                    ('Link', 'Damage', {'amount': 2})
                ])
            ]
            return event_tools.createForkEvent(flowchart, None, forks, autosave_event)[0]

        if item == 'SquishTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'dmg_press'}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
                event_tools.createActionEvent(flowchart, 'Timer', 'Wait', {'time': 2.0}),
                event_tools.createActionEvent(flowchart, 'Link', 'Damage', {'amount': 4})
            ]
            return event_tools.createForkEvent(flowchart, None, forks, autosave_event)[0]

        if item == 'DeathballTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayTailorOtherChannelEx',
                    {'channel': 'GreatFairy_Heal', 'index': 0, 'restart': False, 'time': 0.0}),
                event_tools.createActionChain(flowchart, None, [
                    ('Timer', 'Wait', {'time': 0.1}),
                    ('Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'fall_deathball'})
                ]),
                event_tools.createActionChain(flowchart, None, [
                    ('Hud', 'SetHeartUpdateEnable', {'enable': True}),
                    ('Timer', 'Wait', {'time': 1.5}),
                    ('Link', 'Damage', {'amount': 2})
                ])
            ]
            return event_tools.createForkEvent(flowchart, None, forks, autosave_event)[0]

        if item == 'QuakeTrap':
            autosave_event = event_tools.createActionEvent(flowchart, 'GameControl', 'RequestAutoSave', {}, after)
            forks = [
                event_tools.createActionEvent(flowchart, 'Link', 'PlayAnimation', {'blendTime': 0.1, 'name': 'dmg_quake'}),
                event_tools.createActionEvent(flowchart, 'Timer', 'Wait', {'time': 1.5}),
                event_tools.createActionEvent(flowchart, 'Hud', 'SetHeartUpdateEnable', {'enable': True}),
                event_tools.createActionEvent(flowchart, 'Link', 'Damage', {'amount': 6})
            ]
            return event_tools.createForkEvent(flowchart, None, forks, autosave_event)[0]    

        # tunics
        if item == 'ClothesRed':
            return event_tools.createActionChain(flowchart, None, [
                ('EventFlags', 'SetFlag', {'symbol': 'RedTunicFoundFlag', 'value': True}),
                ('Link', 'PlayTailorOtherChannelEx', {'channel': 'Change_Color_Red_00', 'index': 0, 'restart': False, 'time': 3.58}),
                ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False}),
            ], after)

        if item == 'ClothesBlue':
            return event_tools.createActionChain(flowchart, None, [
                ('EventFlags', 'SetFlag', {'symbol': 'BlueTunicFoundFlag', 'value': True}),
                ('Link', 'PlayTailorOtherChannelEx', {'channel': 'Change_Color_Blue_00', 'index': 0, 'restart': False, 'time': 3.58}),
                ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False}),
            ], after)

        if item == 'ClothesGreen':
            return event_tools.createActionChain(flowchart, None, [
                ('Link', 'PlayTailorOtherChannelEx', {'channel': 'Change_Color_Green_00', 'index': 0, 'restart': False, 'time': 3.58}),
                ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False}),
            ], after)    

        # everything else
        return event_tools.createActionChain(flowchart, None, [
            ('Inventory', 'AddItemByKey', {'itemKey': item, 'count': 1, 'index': index, 'autoEquip': False})
        ], after)
