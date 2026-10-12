# To do

## Before the first real test run
- **Packaged world.** Build `las.apworld` and generate a seed from it in a clean Archipelago install, as a player would.
- **Fresh-seed run.** New two-slot seed, patched with the current module, played from a new save: Tarin's gift, the shop, a chest, a heart piece, items from the other slot, quit and resume.
- **Tail Cave.** A floor small key, the boss heart container and the Full Moon Cello as locations.
- **First-run guide.** README steps that match a real setup: installing the world, the YAML, patching, where the mod goes in Ryujinx, starting the client from the Launcher.

## Later
- **AP item model.** Build an Archipelago-coloured item model at patch time from an item model in the player's RomFS, the way LASR builds its chest textures (`bntx_tools.py`). Possibly one variant per item class (progression, useful, filler), which needs the class in the patch file.
- **Real models for own items.** Use the item's own model for this slot's items, as a player option.
- **Unverified locations.** Owl statues, tree seashells, golden leaves (small key actors), Dampé, fishing, the rapids race, the trendy game's final prize, trade quest NPCs, the other dungeons' boss drops and instruments.
- **Collected check.** `0x148b10` kind `0x14` tests a bit in a global table; find which actors use it and whether received items can set it.
- **Goal by play.** Reach the goal in-game instead of setting `ShadowClear`.
- **Release bundle.** The world, the patcher and a prebuilt module, so players need no Docker.
- **Dungeon entrance shuffle.**
