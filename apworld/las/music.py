"""Jukebox (DESIGN.md, Music): the BGM the player picks with /music, which the game plays instead of the zone's music."""
from typing import List, Optional, Tuple

# The game's looping BGM tracks, as the patcher's music randomizer shuffles them (patcher music.py, BGM_TRACKS;
# test_music checks that they match). Any other BGM_ name goes to the game as typed, which refuses an unknown one.
TRACKS = (
    "BGM_DUNGEON_LV6_FACE", "BGM_DUNGEON_LV4_ANGLER", "BGM_CHICKEN_HURT", "BGM_GHOST_HOUSE", "BGM_ANIMAL_VILLAGE",
    "BGM_DUNGEON_LV1_TAIL", "BGM_DUNGEON_2D_SIDEVIEW", "BGM_DUGEON_CASTLE", "BGM_DUNGEON_LV3_KEY", "BGM_CAVE",
    "BGM_TARUTARU", "BGM_HOUSE", "BGM_MEVE", "BGM_FISHINGMAN", "BGM_DUNGEON_LV2_POT", "BGM_STRANGE_FOREST",
    "BGM_DUNGEON_HOLY_EGG", "BGM_SEASHELL_HOUSE", "BGM_TELEPHONE", "BGM_DANPEI", "BGM_SHOP", "BGM_GAME_SHOP",
    "BGM_GOAT_HOUSE", "BGM_DREAMSHRINE", "BGM_WRIGHT", "BGM_GAME_SHOP_FOR_POND", "BGM_RICHARD",
    "BGM_EVENT_RESCUE_BOWBOW", "BGM_DUNGEON_LV10_CLOTH", "BGM_PLACE_OF_FACE_KEY", "BGM_DUNGEON_LV5_CATFISH",
    "BGM_FAIRY", "BGM_DUNGEON_LV7_TOWER", "BGM_DUNGEON_LV8_TURTLE", "BGM_FIELD_NORMAL", "BGM_FIELD_MARINE_NORMAL",
    "BGM_LASTBOSS_APPEAR", "BGM_LASTBOSS_BATTLE", "BGM_MARINE_NAME", "BGM_MINIGAME_FISHING", "BGM_PANEL_DUNG_BEGINNER",
    "BGM_PANEL_DUNG_DIFFICULT", "BGM_PANEL_DUNG_MEDIUM", "BGM_PANEL_EDIT_MODE", "BGM_NAME_INPUT", "BGM_DUNGEON_BOSS",
    "BGM_DUNGEON_BOSS_MIDDLE", "BGM_DUNGEON_LV8_ENT_BATTLE", "BGM_EVENT_MARINE_IN_BEACH",
    "BGM_EVENT_RESCUE_BOWBOW_INTRO", "BGM_FIELD_FIRST", "BGM_FIELD_NORMAL_INTRO", "BGM_GAME_OF_RAFT",
    "BGM_PANEL_SHADOW_LINK", "BGM_RAFTING_TIMEATTACK", "BGM_RICHARD_230", "BGM_STRANGE_FOREST_MARINE",
    "BGM_TARUTARU2_AFTER_THE_RESCUE", "BGM_TARUTARU_MARINE", "BGM_TOTAKEKE_SONG", "BGM_ZELDA_NAME", "BGM_PANEL_RESULT",
    "BGM_DUNGEON_LV7_BOSS", "BGM_HOUSE_FIRST", "BGM_MADBATTER", "BGM_EVENT_DATE", "BGM_LASTBOSS_WIN", "BGM_OWL",
    "BGM_OWL_LAST", "BGM_DEMO_AFTER_LASTBOSS",
)
PREFIX = "BGM_"


def resolve(name: str) -> Tuple[Optional[str], List[str]]:
    """The track a /music argument names, or None and the tracks it could mean. Case does not matter and BGM_ may be
    left out; a part of a name picks the track it is part of when only one has it."""
    wanted = name.strip().upper()
    if not wanted:
        return None, []
    full = wanted if wanted.startswith(PREFIX) else PREFIX + wanted
    if full in TRACKS:
        return full, [full]
    matches = [t for t in TRACKS if wanted in t]
    if len(matches) == 1:
        return matches[0], matches
    if not matches and wanted.startswith(PREFIX):
        return wanted, []  # not a shuffled track: the game knows whether it is one
    return None, matches
