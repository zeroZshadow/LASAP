import RandomizerCore.Tools.event_tools as event_tools
import ap_option_edits


class OwlStatueRandomizer:
    def __init__(self, mod_generator):
        self.parent = mod_generator
        ap_option_edits.owl_changes(self)


    def makeFieldChanges(self, flowchart):
        '''Places items on the field owls. Hint system not implemented'''

        field_owls = {
            'owl-statue-below-D8': 'Event20',
            'owl-statue-pothole': 'Event22',
            'owl-statue-above-cave': 'Event28',
            'owl-statue-moblin-cave': 'Event30',
            'owl-statue-south-bay': 'Event32',
            'owl-statue-desert': 'Event34',
            'owl-statue-maze': 'Event36',
            'owl-statue-taltal-east': 'Event38',
            'owl-statue-rapids': 'Event40'
        }
        for k,v in field_owls.items():
            gift_event = self.parent.item_get_manager.get(flowchart, k, None, None, True)

            flag_set = event_tools.createActionEvent(flowchart, 'EventFlags', 'SetFlag',
                {'symbol': k, 'value': True}, gift_event)

            flag_check = event_tools.createSwitchEvent(flowchart, 'EventFlags', 'CheckFlag',
                {'symbol': k}, {0: flag_set, 1: None})

            event_tools.insertEventAfter(flowchart, v, flag_check)

        # Now lets prevent Link from backing up. This is what causes the get item animation to sometimes not play
        dist_evs = (
            'Event17',
            'Event41',
            'Event42',
            'Event43',
            'Event44',
            'Event45',
            'Event46',
            'Event47',
            'Event48'
        )
        for ev in dist_evs:
            dist_ev = event_tools.findEvent(flowchart, ev)
            dist_ev.data.params.data['keepPersonalSpace'] = False
