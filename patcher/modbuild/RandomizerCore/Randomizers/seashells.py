SUNKEN_SHELLS = ("kanalet-moat-north", "taltal-sunken")  # standing from the start (ap_drop.STANDING), not dug up


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