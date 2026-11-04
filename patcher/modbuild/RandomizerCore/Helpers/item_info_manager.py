class ItemInfoManager:
    def __init__(self, mod_generator):
        self.parent = mod_generator


    def getItemInfo(self, check: str) -> tuple[str, int]:
        item_key: str = self.parent.item_defs[self.parent.placements[check]]['item-key']
        item_index: int = self.parent.placements['indexes'][check] if check in self.parent.placements['indexes'] else -1
        return item_key, item_index


    def getItemInfoWithModel(self, check: str):
        item: str = self.parent.placements[check]
        item_key, item_index = self.getItemInfo(check)
        model_path: str = self.parent.item_defs[item]['model-path']
        model_name: str = self.parent.item_defs[item]['model-name']
        return item_key, item_index, model_path, model_name
