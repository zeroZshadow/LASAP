# Link's Awakening for Logic Studio

Logic Studio, a tool in its own repository, shows and edits this world's logic on the game's maps, simulates it with the real world and exports `apworld/las/data/logic.yml`. This folder is what the game hands it: the manifest, the project, the game's data and the tooling that writes it.

```bash
uv run <Logic Studio>/run.py --project logic-studio
```

## What is here

| File | What | Written by |
|---|---|---|
| `game.yml` | The manifest: where each piece of data is and where an export goes (Logic Studio's DATA.md has its keys) | by hand |
| `project.yml`, `workspace.yml`, `zones.yml` | The project: `workspace.yml` is the logic `logic.yml` is exported from, `zones.yml` the painted tiles and gates | Logic Studio |
| `checks.yml` | Every check, a line each: its place, and its facts (content, index) | `tools/build.py` (places), by hand (facts) |
| `places.json`, `maps/` | Levels, rooms, teleports, warps, door marks and the pictures | `tools/build.py` |
| `apworld/las/data/doors.yml` | Which door leads to which | `tools/build.py` |
| `tools/` | The game's tooling | |

## Tools

- `build.py`: reads the RomFS (`romfs` in `scripts/local_paths.json`) and writes the places, the pictures, `doors.yml` and each check's place in `checks.yml`. It checks each check's index against the actor at its place where one carries it, and prints what it finds odd in the game's files. `--check` prints the totals and findings and writes nothing. In `.venv-ap`.
- `simulator.py`: Logic Studio's Simulator for this world: worlds from `setup_multiworld` with every location category on, rules from the world's `LogicCompiler`. Importing the world takes a few seconds, once; an answer takes milliseconds.
- `world_tests.py`: the world's tests the Tests panel runs.
- `venv_ap.py`: the Simulator and the world's tests run themselves in the main checkout's `.venv-ap`, whatever Python starts them.
- `test_data.py`: `pytest logic-studio/tools` in `.venv-ap`: the checked-in data is what `build.py` makes.

The Simulator and the world's tests need `vendor/Archipelago` with the world (`bash scripts/worktree.sh setup` in a worktree).

Logic Studio's own tests run against this project:

```bash
LOGIC_STUDIO_TEST_PROJECT=logic-studio uv run --with pytest --with pyyaml pytest <Logic Studio>/tests
```

## Not there yet

- The world's dungeon shuffle goes by zone name (`lasr.dungeon_logic`).
