# To do

- **Unverified locations.** Dampé.
- **Linux and macOS.** The world, the client and the patcher are plain Python; untested on either system.
- **Optional activities hold only junk.** An optional location (`lasr_settings.optional_locations`) is excluded, so it takes filler and traps only, never a useful item. Open: whether such locations should take useful leftovers too (a rule keeping only progression out).
- **One choice per location group.** Each group option (Chests, Heart Pieces, Boss Drops, ...) a choice of vanilla (locked to its vanilla item), randomized or junk (the optional activities' handling), in place of the toggles, each of which means one of those depending on the group (Blue Rupees off is vanilla without checks: plain rupees). Precedents in Archipelago's worlds (surveyed 2026-10-29): Pokémon Red and Blue's `randomize_hidden_items: off | on | exclude` and Super Mario 64's `enable_coin_stars: off | on | vanilla`; `vanilla` and `exclude` are the usual value names. `exclude_locations` already takes location group names, so `location_name_groups` would let a player exclude a group without an option.
