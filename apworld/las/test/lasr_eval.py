"""Reference evaluator, independent of logic.LogicCompiler: LASR's reachability code (RandomizerCore/shuffler.py, MIT,
Owen_Splat) with these changes: no Qt base or thread flag, compiled conditions are cached, ``reachable`` is
``canReachLocation`` without a target returning the final access dict, tokens may contain dots (zone names like
``D1.b``), and an enemy without a condition needs nothing.
"""
import re

LEVELS = ("basic", "advanced", "glitched", "hell")


class LASREvaluator:
    def __init__(self, logic: str, logic_defs: dict, placements: dict):
        self.levels = LEVELS[:LEVELS.index(logic) + 1]
        self.logic_defs = logic_defs
        self.placements = placements
        self._code = {}

    def _eval(self, condition: str, access: dict) -> bool:
        code = self._code.get(condition)
        if code is None:
            func = re.sub('([a-zA-Z0-9\\-.\\[\\]]+)(:(\\d+))?',
                          lambda match: f'self.hasAccess(access, "{match.group(1)}", {match.group(3) or 1})', condition)
            func = func.replace('|', 'or').replace('&', 'and').replace('!', 'not ')
            code = self._code[condition] = compile(func, "<lasr>", "eval")
        return eval(code, {}, {"self": self, "access": access})

    @staticmethod
    def hasAccess(access: dict, key: str, amount: int = 1) -> bool:
        return access.get(key, 0) >= amount

    def checkAccess(self, check: str, access: dict) -> bool:
        node = self.logic_defs[re.match('[a-zA-Z0-9.-]+', check).group(0)]
        if node['type'] == 'enemy':
            if 'condition-basic' not in node:
                return True
            param = re.search('\\[([a-z]+)\\]', check)
            return self._eval(node['condition-basic'], access) or bool(
                param and self._eval(node['condition-' + param.group(1)], access))
        if node['type'] in ('item', 'follower') and not self.hasAccess(access, node['region']):
            return False
        if 'condition-basic' not in node:
            return True
        return any(self._eval(node[f'condition-{level}'], access) for level in self.levels if f'condition-{level}' in node)

    def reachable(self, starting_access: dict) -> dict:
        access = starting_access.copy()

        def add(key: str) -> None:
            access[key] = access.get(key, 0) + 1

        added = True
        while added:
            added = False
            for key, node in self.logic_defs.items():
                if key not in access:
                    if self.checkAccess(key, access):
                        add(key)
                        added = True
                        if node['type'] in ('item', 'follower') and self.placements[key] is not None:
                            add(self.placements[key])
                        if node['type'] == 'enemy':  # killed at all: killed with pits and heavy objects too
                            add(key + '[pit]')
                            add(key + '[heavy]')
                    elif node['type'] == 'enemy':
                        for way in ('pit', 'heavy'):
                            if f'condition-{way}' in node and not self.hasAccess(access, f'{key}[{way}]') \
                                    and self.checkAccess(f'{key}[{way}]', access):
                                add(f'{key}[{way}]')
                                added = True
        return access
