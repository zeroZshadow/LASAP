import RandomizerCore.Tools.event_tools as event_tools
from RandomizerCore.Randomizers import heart_pieces
from RandomizerCore.Randomizers.data import MODEL_SIZES, MODEL_ROTATIONS

SUNKEN_SHELLS = ("kanalet-moat-north", "taltal-sunken")  # shown as heart pieces, so placed as sunken ones


class SeashellRandomizer:
    """The sunken seashells, as heart piece actors (heart piece actors are bigger and easier to see). ap_drop makes the
    other seashells' drops."""

    def __init__(self, mod_generator) -> None:
        self.parent = mod_generator
        flow = self.parent.file_manager.readFile('SinkingSword.bfevfl')

        for room in SUNKEN_SHELLS:

            item_key, item_index, model_path, model_name = self.parent.item_info_manager.getItemInfoWithModel(room)
            room_data = self.parent.file_manager.readFile(f"{SHELL_ROOMS[room]}.leb")
            shell = [a for a in room_data.actors if a.type == 0x87][0]
            shell.type = 0xB0

            get_anim = self.parent.item_get_manager.get(flow.flowchart, room)

            event_tools.addEntryPoint(flow.flowchart, room)
            event_tools.createActionChain(flow.flowchart, room, [
                ('SinkingSword', 'Destroy', {}),
                ('EventFlags', 'SetFlag', {'symbol': SHELL_FLAGS[room], 'value': True})
            ], get_anim)

            if model_name not in ("HeartPiece", "HeartContainer"):
                shell.posY += heart_pieces.SUNKEN_RISE
                shell.scaleX = shell.scaleY = shell.scaleZ = heart_pieces.SUNKEN_SHRUNK

            # parameter[0] is the heart piece's index
            shell.parameters[1] = bytes(model_path, 'utf-8')
            shell.parameters[2] = bytes(model_name, 'utf-8')
            shell.parameters[3] = bytes(room, 'utf-8') # entry point
            shell.parameters[4] = bytes(SHELL_FLAGS[room], 'utf-8') # flag which controls if the shell appears or not

            if item_key == 'Seashell':
                shell.parameters[5] = bytes('true', 'utf-8')
            else:
                shell.parameters[5] = bytes('false', 'utf-8')

            if model_name in MODEL_SIZES:
                size = MODEL_SIZES[model_name]
                shell.scaleX = size
                shell.scaleY = size
                shell.scaleZ = size
            if model_name in MODEL_ROTATIONS:
                shell.rotY = MODEL_ROTATIONS[model_name]

            self.parent.file_manager.writeFile(f"{SHELL_ROOMS[room]}.leb", room_data)

        self.parent.file_manager.writeFile('SinkingSword.bfevfl', flow)


GRASS_ROOMS = {
    "mabe-bushes":                  "Field_11D",
    "pond-island":                  "Field_11G",
    "southwest-bay-bush":           "Field_15J",
    "beside-seashell-mansion":      "Field_09L",
    "small-coast-island":           "Field_16I"
}
GRASS_FLAGS = {
    "mabe-bushes":                  "MabeBushGet",
    "pond-island":                  "PondIslandBushGet",
    "southwest-bay-bush":           "BayBushGet",
    "beside-seashell-mansion":      "MansionBushGet",
    "small-coast-island":           "CoastIslandBushGet"
}

ROCK_ROOMS = {
    "north-of-moblin-cave":         "Field_03F",
    "desert-south":                 "Field_16P",
    "ghost-house-pot":              "GhostHouse_01A",
    "rock-maze":                    "Field_09P",
    "plains-rock-maze":             "Field_12J",
    "taltal-east-bridge":           "Field_01M",
    "taltal-west-rock":             "Field_02F"
}
ROCK_FLAGS = {
    "north-of-moblin-cave":         "MoblinCaveRockGet",
    "desert-south":                 "DesertSouthRockGet",
    "ghost-house-pot":              "GhostHousePotGet",
    "rock-maze":                    "RockMazeRockGet",
    "plains-rock-maze":             "PlainsRockGet",
    "taltal-east-bridge":           "TaltalEastRockGet",
    "taltal-west-rock":             "TaltalWestRockGet"
}

TREE_ROOMS = {
    "tail-cave-bonk-tree":          "Field_14C",
    "beach-bonk-tree":              "Field_15A",
    "ukuku-bonk-tree":              "Field_11E"
}
TREE_FLAGS = {
    "tail-cave-bonk-tree":          "TailCaveBonkTreeGet",
    "beach-bonk-tree":              "BeachBonkTreeGet",
    "ukuku-bonk-tree":              "UkukuBonkTreeGet"
}
TREE_DROP_POSITIONS = {
    "tail-cave-bonk-tree":          (36.0, 8.25, 167.0),
    "beach-bonk-tree":              (11.25, 6.0, 177.75),
    "ukuku-bonk-tree":              (70.5, 9.0, 129.0)
}

HOLE_ROOMS = {
    "taltal-heights-hole":          "Field_03N",
    "taltal-bomb-hole":             "Field_02J"
}
HOLE_FLAGS = {
    "taltal-heights-hole":          "HeightsHoleGet",
    "taltal-bomb-hole":             "BridgeHoleGet"
}
HOLE_DROP_POSITONS = {
    "taltal-heights-hole":          (192.75, 12.75, 32.25),
    "taltal-bomb-hole":             (150.75, 25.5, 12.75)
}

SHELL_ROOMS = {
    "kanalet-moat-north":           "Field_04I",
    "taltal-sunken":                "Field_02M",
    "south-bay-dig":                "Field_14K",
    "beach-dig":                    "Field_16F",
    "wasteland-dig":                "Field_05H",
    "desert-dig":                   "Field_13P",
    "ghost-grave-dig":              "Field_08E",
    "doghouse-dig":                 "DogHouse_01A",
    "goponga-west-dig":             "Field_03B",
    "above-cave-dig":               "Field_11I",
    "above-d3":                     "Field_11F",
    "under-skull-rock":             "Field_10H",
    "rapids-east-island":           "Field_06O",
    "taltal-west-dig":              "Field_02C",
    "woods-west-dig":               "Field_05A",
    "woods-east-dig":               "Field_06C",
    "pothole-final":                "Field_13G" # we treat the slime key spot as a seashell
}
SHELL_FLAGS = {
    "kanalet-moat-north":           "KanaletSunkenShellGet",
    "taltal-sunken":                "TaltalSunkenShellGet",
    "south-bay-dig":                "SouthBayDigShellGet",
    "beach-dig":                    "BeachDigShellGet",
    "wasteland-dig":                "WastelandDigShellGet",
    "desert-dig":                   "DesertDigShellGet",
    "ghost-grave-dig":              "GhostGraveDigShellGet",
    "doghouse-dig":                 "DoghouseDigShellGet",
    "goponga-west-dig":             "GopongaDigShellGet",
    "above-cave-dig":               "AboveCaveDigShellGet",
    "above-d3":                     "AboveD3DigShellGet",
    "under-skull-rock":             "UnderSkullDigShellGet",
    "rapids-east-island":           "RapidsEastDigShellGet",
    "taltal-west-dig":              "TaltalWestDigShellGet",
    "woods-west-dig":               "WoodsWestDigShellGet",
    "woods-east-dig":               "WoodsEastDigShellGet",
    "pothole-final":                "PotholeItemGetFlag"
}