# To do

- **AP item model.** Build an Archipelago-coloured item model at patch time from an item model in the player's RomFS, the way LASR builds its chest textures (`bntx_tools.py`). Possibly one variant per item class (progression, useful, filler), which needs the class in the patch file.
- **Real models for own items.** Use the item's own model for this slot's items, as a player option.
- **Unverified locations.** Owl statues, tree seashells, golden leaves, dungeon floor small keys, Dampé, fishing, the rapids race, the trendy game's final prize, trade quest NPCs, boss drops, and the instruments after the Full Moon Cello.
- **Collected check.** `0x148b10` kind `0x14` tests a bit in a global table; find which actors use it and whether received items can set it.
- **Goal by play.** Reach the goal in-game instead of setting `ShadowClear`.
- **Release bundle.** The world, the patcher and a prebuilt module, so players need no Docker.
- **Dungeon entrance shuffle.**
