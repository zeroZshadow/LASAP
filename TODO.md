# To do

- **Unverified locations.** Owl statue gifts' checks (with In-Game Hints off and on), the Kanalet bombed guard's golden leaf, Dampé, fishing, the rapids race and the Armos Knight's drop.
- **Collected check.** `0x148b10` kind `0x14` tests a bit in a global table; find which actors use it and whether received items can set it.
- **Linux and macOS.** The world, the client and the patcher are plain Python; untested on either system.
- **Dungeon entrance shuffle.**
- **Damage None against enemies.** LASR's `damageModifier` rewrites the health subtractions in TESTING.md ("Reverse engineering"); traps take nothing with None, but a Sea Urchin's touch and an Octorok's hit still take 2 quarter hearts. OHKO kills on the same touch, so the hit passes `0xd4c754` and its 2 quarter hearts come from another health write. Find that write; `damage_none` then gets an enemy hit.
