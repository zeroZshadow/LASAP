"""Run with .venv-ap: pytest logic-studio/tools. The checked-in data is what build.py makes; reading the RomFS
needs "romfs" in scripts/local_paths.json."""
import json
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build  # noqa: E402
import checks  # noqa: E402
import doors  # noqa: E402

CHECKS, PLACES, MAPS, DOORS = build.paths()


@pytest.fixture(scope="module")
def extracted():
    sys.path.insert(0, str(HERE.parents[1] / "scripts"))
    from local_paths import lookup
    if not lookup("romfs"):
        pytest.skip('scripts/local_paths.json has no "romfs"')
    import extract
    return extract.build({name: fields for name, (fields, _) in checks.read(CHECKS).items()})


def test_doors_yml_is_what_the_teleports_give():
    teleports = json.loads(PLACES.read_text(encoding="utf-8"))["teleports"]
    assert DOORS.read_text(encoding="utf-8") == doors.text(doors.table(teleports))


def test_two_doors_with_one_name_stop_the_build():
    door = {"room": "Room", "level": "Cave", "entrance": True, "door": True, "companion": None}
    with pytest.raises(ValueError, match="Two doors named Room"):
        doors.table([{**door, "id": "Room/1", "to": "Field_A"}, {**door, "id": "Room/2", "to": "Field_B"}])


def test_every_picture_is_placed_and_there():
    pictures = json.loads(PLACES.read_text(encoding="utf-8"))["pictures"]
    assert {entry["file"] for entry in pictures.values()} == {path.name for path in MAPS.glob("*.png")}


def test_checks_yml_reads_back_as_it_is_written():
    assert checks.text(checks.read(CHECKS)) == CHECKS.read_text(encoding="utf-8")


def test_the_places_and_checks_are_what_the_romfs_gives(extracted):
    levels, index, _ = extracted
    places = json.loads(PLACES.read_text(encoding="utf-8"))
    found = {key: value for key, value in index.items() if key != "checks"}
    assert {key: places[key] for key in found} == json.loads(json.dumps(found))
    written = {name: {k: v for k, v in fields.items() if k in checks.PLACE} for name, (fields, _) in checks.read(CHECKS).items()}
    assert written == {name: {k: v for k, v in place.items() if k in checks.PLACE} for name, place in index["checks"].items()}


def test_every_index_an_actor_gives_is_the_checks(extracted):
    levels, index, _ = extracted
    entries = checks.read(CHECKS)
    items = build.item_pool()
    keys = {items[fields["content"]]["item-key"] for fields, _ in entries.values() if "index" in fields}
    assert checks.merged(entries, index["checks"], levels, keys)[1] == []


@pytest.mark.parametrize("name, room, kind, first", [("goponga-west-dig", "Field_03B", "ItemSecretSe", 27), ("wasteland-dig", "Field_05H", "ItemSecretSe", 28), ("taltal-west-escape", "Field_03B", "ItemHeartPiece", 21),
                                                     ("southwest-bay-bush", "Field_15J", "ObjGrass", "Seashell11"), ("south-bay-sunken", "Field_15J", "ItemHeartPiece", 17), ("pothole-final", "Field_13G", "ItemSlimeKey", None)])
def test_an_overworld_check_stands_on_its_own_actor(extracted, name, room, kind, first):
    levels, index, _ = extracted
    check = index["checks"][name]
    actors = next(r for r in levels["Field"] if r["name"] == room)["actors"]
    assert check["room"] == room
    assert any(a["kind"] == kind and (first is None or first in a["params"]) and (a["x"], a["z"]) == (check["x"], check["z"]) for a in actors)


def test_stairs_are_floor_flagged_solid_tiles_that_climb_steadily():
    import render
    solid_floor, wall, floor = render.SOLID | render.FLOOR_TYPE, render.SOLID, render.FLOOR_TYPE

    def column(flags, heights):
        return [{"name": "A", "x": 0, "z": 0, "w": 1, "h": len(flags), "flags": flags, "elevation": heights}]

    assert render.stairs(column([floor, solid_floor, solid_floor, floor], [10.5, 9.94, 8.81, 8.25])) == {("A", 1), ("A", 2)}
    assert not render.stairs(column([floor, wall, floor], [10.0, 9.5, 9.0]))  # a wall
    assert not render.stairs(column([floor, solid_floor, floor], [25.0, 22.5, 22.0]))  # a drop
    assert not render.stairs(column([floor, solid_floor, floor], [9.0, 11.0, 9.0]))  # no climb across it
