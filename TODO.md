# To do

- **Unverified locations.** Owl statue gifts' checks (with In-Game Hints off and on), the Kanalet bombed guard's golden leaf, Dampé, fishing, the rapids race and the Armos Knight's drop.
- **Received chamber stones.** `GrantItem` adds a Chamber Stone (item 114) with index -1, which `Inventory_AddItemID` ignores: it sets no chamber stone bit (`PanelPieceBits`, 51 bits) and opens no chamber. Give it the lowest clear index among 3, 4, 8, 10–13 and 20–26 that no vanilla source of the seed gives (always 3 and 24, the Trendy Game, and 10, 20–23, 25 and 26, the shop's panel slot; when unshuffled 4 and 13 the Seashell Mansion, 8 the rapids race, 11 and 12 fishing), listed in `ap_indices.txt` as the heart pieces' and seashells' are.
- **Linux and macOS.** The world, the client and the patcher are plain Python; untested on either system.
