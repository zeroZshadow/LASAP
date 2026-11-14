"""Entrances: which overworld doors the door placement moves, and doors.yml with the placed doors (DESIGN.md,
Entrances). The world places the doors with Archipelago's generic entrance randomizer (LASWorld._randomize_doors)."""
from typing import Any, Dict, List, Mapping

from .logic import SHUFFLE_GROUPS as GROUPS  # doors.yml's groups the placement draws from


class PlacementError(Exception):
    """No placement in every try: a generation failure, which fuzzers count, not an option error they set aside."""


def shuffle_pool(doors: Mapping[str, Any]) -> List[str]:
    """The overworld doors the placement moves: doors.yml's GROUPS, the door outside the house among them. The world
    pairs that door's inside, the house's, with the start door before it places the others (LASWorld._randomize_doors)."""
    return [door for group in GROUPS for door in doors["groups"][group]]


def shuffled_doors(doors: Mapping[str, Any], entrances: Mapping[str, str]) -> Dict[str, Any]:
    """doors.yml with the shuffled doors leading as entrances says."""
    return {**doors, "pairs": {**doors["pairs"], **entrances}}
