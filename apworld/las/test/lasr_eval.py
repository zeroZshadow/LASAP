"""Reference evaluator: LASR's reachability code (RandomizerCore/shuffler.py, MIT, Owen_Splat), copied verbatim.

The only changes: the class has no Qt base, ``eval`` results of parsed conditions are compiled once and cached
(same semantics), and ``reachable`` is ``canReachLocation`` without a target that returns the final access dict.
"""
import re


class LASREvaluator:
    def __init__(self, logic: str, logic_defs: dict, placements: dict):
        self.logic = logic
        self.logic_defs = logic_defs
        self.placements = placements
        self.thread_active = True
        self._code = {}

    def _eval(self, func, access):
        code = self._code.get(func)
        if code is None:
            code = self._code[func] = compile(func, "<lasr>", "eval")
        return eval(code, {}, {"self": self, "access": access})

    # --- verbatim from ItemShuffler ---

    def addAccess(self, access, new):
        if new in access:
            access[new] += 1
        else:
            access[new] = 1
        return access

    def removeAccess(self, access, toRemove):
        if toRemove in access:
            access[toRemove] -= 1
            if access[toRemove] == 0:
                access.pop(toRemove)
        return access

    def hasAccess(self, access, key, amount=1):
        return key in access and access[key] >= amount

    def checkAccess(self, newCheck, access):
        # get the name of the check without the parameter sometimes applied to enemy checks
        no_params = re.match('[a-zA-Z0-9-]+', newCheck).group(0)

        if self.logic == 'none': return True

        if self.logic_defs[no_params]['type'] == 'enemy':
            param = re.search('\\[([a-z]+)\\]', newCheck)
            if param:
                return self._eval(self.parseCondition(self.logic_defs[no_params]['condition-basic']), access) or self._eval(self.parseCondition(self.logic_defs[no_params]['condition-'+param.group(1)]), access)
            else:
                return self._eval(self.parseCondition(self.logic_defs[no_params]['condition-basic']), access)
        else:
            # For item and follower checks, see if you have access to the region. Otherwise, check on the conditions, if they exist
            region_access = self.hasAccess(access, self.logic_defs[newCheck]['region']) if (self.logic_defs[newCheck]['type'] in ('item', 'follower')) else True
            basic        = self._eval(self.parseCondition(self.logic_defs[newCheck]['condition-basic']), access)    if ('condition-basic' in self.logic_defs[newCheck]) else True
            advanced     = self._eval(self.parseCondition(self.logic_defs[newCheck]['condition-advanced']), access) if (('condition-advanced' in self.logic_defs[newCheck]) and (self.logic in ('advanced', 'glitched', 'hell'))) else False
            glitched     = self._eval(self.parseCondition(self.logic_defs[newCheck]['condition-glitched']), access) if (('condition-glitched' in self.logic_defs[newCheck]) and self.logic in ('glitched', 'hell')) else False
            hell        = self._eval(self.parseCondition(self.logic_defs[newCheck]['condition-hell']), access)    if (('condition-hell' in self.logic_defs[newCheck]) and self.logic == 'hell') else False
            return region_access and (basic or advanced or glitched or hell)

    def parseCondition(self, condition):
        func = condition
        func = re.sub('([a-zA-Z0-9\\-\\[\\]]+)(:(\\d+))?', lambda match: f'self.hasAccess(access, "{match.group(1)}", {match.group(3) or 1})', func)
        func = re.sub('\\|', 'or', func)
        func = re.sub('&', 'and', func)
        func = re.sub('!', 'not ', func)
        return func

    def reachable(self, starting_access):
        access = starting_access.copy()
        access_added = True

        while access_added and self.thread_active:
            access_added = False
            for key in self.logic_defs:
                if self.thread_active:
                    if key not in access:
                        if self.checkAccess(key, access):
                            access = self.addAccess(access, key)
                            access_added = True

                            # if we're looking at an item or follower location, at the item it holds, if it has one
                            if (self.logic_defs[key]['type'] in ['item', 'follower']) and self.placements[key] != None:
                                access = self.addAccess(access, self.placements[key])

                            # if we're looking at an enemy, and we CAN kill it, then we can also kill it with access to pits or heavy objects, so add those too
                            if self.logic_defs[key]['type'] == 'enemy':
                                access = self.addAccess(access, key+'[pit]')
                                access = self.addAccess(access, key+'[heavy]')

                        # if we can't do the thing, but it's an enemy, we might be able to use pits or heavy throwables, so check those cases independently
                        elif self.logic_defs[key]['type'] == 'enemy':
                            if 'condition-pit' in self.logic_defs[key] and not self.hasAccess(access, key+'[pit]'):
                                if self.checkAccess(key+'[pit]', access):
                                    access = self.addAccess(access, key+'[pit]')
                                    access_added = True
                            if 'condition-heavy' in self.logic_defs[key] and not self.hasAccess(access, key+'[heavy]'):
                                if self.checkAccess(key+'[heavy]', access):
                                    access = self.addAccess(access, key+'[heavy]')
                                    access_added = True
                else: break

        return access
