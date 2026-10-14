# To do

- **Unverified locations.** Owl statue gifts' checks (with In-Game Hints off and on), the Kanalet bombed guard's golden leaf, Dampé, fishing, the rapids race and the Armos Knight's drop.
- **Collected check.** `0x148b10` kind `0x14` tests a bit in a global table; find which actors use it and whether received items can set it.
- **Linux and macOS.** The world, the client and the patcher are plain Python; untested on either system.
- **Dungeon entrance shuffle.**
- **Player settings under Ryujinx's translation cache.** Ryujinx and Ryubing keep translated code between runs (PPTC, on by default), keyed by the code files; `config.ini` is not part of the key, and every seed of a release installs the same `subsdk9`. The module's code patches and hooks that depend on `config.ini` (`runCodePatches`: Damage, Blur Removal, nice items, free book, stealing; the hooks for 360° movement, Instant Text, the low-health beep and the buff drops) can then run as translated under an earlier seed's settings: after a run with Damage Normal, enemy hits took 2 quarter hearts under None and OHKO. Make the code the module writes the same under every setting, with the setting read when the code runs, or have the client clear the game's `cache/cpu` folder when it installs a patch.
