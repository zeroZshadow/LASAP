# Link's Awakening for Logic Studio

Logic Studio, a tool in its own repository, shows and edits this world's logic on the game's maps, simulates it with the real world and exports `apworld/las/data/logic.yml`. This folder is what the game hands it: the manifest, the project, the game's data and the tooling that writes it.

```bash
uv run <Logic Studio>/run.py --project logic-studio
```

## What is here

| File | What | Written by |
|---|---|---|
| `game.yml` | The manifest: where each piece of data is and where an export goes (Logic Studio's DATA.md has its keys) | by hand |
| `project.yml`, `workspace.yml`, `zones.yml` | The project: `workspace.yml` is the logic `logic.yml` is exported from, `zones.yml` the painted tiles and the zones of each lock | Logic Studio |
| `notes.md` | Open questions about the logic and the rules still to bring over | by hand |
| `checks.yml` | Every check, a line each: its place, and its facts (content, index) | `tools/build.py` (places), by hand (facts) |
| `places.json`, `maps/` | Levels, rooms, teleports, warps, door marks and the pictures | `tools/build.py` |
| `apworld/las/data/doors.yml` | Which door leads to which, and the overworld pairs by group (`dungeon`, `dungeon-other`, `connector`, `single`) | `tools/build.py` |
| `tools/` | The game's tooling | |

## Tools

- `build.py`: reads the RomFS (`romfs` in `scripts/local_paths.json`) and writes the places, the pictures, `doors.yml` and each check's place in `checks.yml`. A door mark is a door actor on a room's edge. A doorway with no door is a passage, drawn as ground with no mark: solid tiles across a seam that both carry the doorway bit, or two door arches of a dungeon's room models facing each other across a seam where the tiles are solid (`render.ARCHES`). A dungeon's raised floor, solid in the tiles, is drawn as ground with an edge where it drops: a solid tile low in the grid whose room collision (`scripts/cidb.py`) has a floor on it a step above the room's (`render.raised`), in a stretch a doorway opens onto or Link arrives on (`render.walkable_raised`); the rest, mostly the tops of low fences, stays wall. A teleport is a door, stairs or hole actor, which names where it leads; Lanmola's sand pit (`AreaAntlion`) names nothing, as its code starts the Field flow's `FallOutAnt`, whose jump `extract.ACTOR_FLOW_JUMPS` follows. It checks each check's index against the actor at its place where one carries it, and prints what it finds odd in the game's files. `--check` prints the totals and findings and writes nothing. In `.venv-ap`.
- `simulator.py`: Logic Studio's Simulator for this world: worlds from `setup_multiworld` with every location category on, rules from the world's `LogicCompiler`. Importing the world takes a few seconds, once; an answer takes milliseconds.
- `world_tests.py`: the world's tests the Tests panel runs, on the project's logic (`--logic`).
- `logic_override.py`: another `logic.yml` in place of the world's, for the Simulator and, as a pytest plugin, the tests.
- `venv_ap.py`: the Simulator and the world's tests run themselves in the main checkout's `.venv-ap`, whatever Python starts them.
- `test_data.py`: `pytest logic-studio/tools` in `.venv-ap`: the checked-in data is what `build.py` makes.

The Simulator and the world's tests need `vendor/Archipelago` with the world (`bash scripts/worktree.sh setup` in a worktree).

Logic Studio's own tests run against this project:

```bash
LOGIC_STUDIO_TEST_PROJECT=logic-studio uv run --with pytest --with pyyaml pytest <Logic Studio>/tests
```

## Not there yet

- The world's dungeon shuffle goes by zone name (`lasr.dungeon_logic`).
