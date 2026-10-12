# LAS Archipelago: design

How the pieces fit and the contracts between them. FEASIBILITY.md covers the background; TESTING.md covers the harness.

## Components

| Dir | What | Language / runtime |
|---|---|---|
| `apworld/las/` | Archipelago world (`Links Awakening Switch`) plus PC client | Python, AP's venv (`.venv-ap`, Py 3.12) |
| `patcher/` | Builds the romfs/exefs mod from an AP patch file; derived from LASR (MIT) | Python, `.venv-lasr` (oead, evfl, PyLibMS) |
| `las-ap-exl/` | exlaunch module (subsdk9) derived from Owen-Splat/exlaunch (GPL-2.0) | C++, Docker `devkitpro/devkita64` |

Flow:
1. AP generates a multiworld. The world writes `AP_<seed>_P<n>_<slot>.aplas` (the patch file).
2. The player opens the patch file with the Launcher, which starts the client with it. The client runs the patcher on the patch file plus the user's romfs dump, and installs its output (see Client install).
3. The game runs with the mod. The module listens on TCP port **43384**.
4. The client connects to the AP server (websocket) and to the game (TCP), and relays checks and items.

## Client install

- The client's paths live in `host.yaml` under `las_options` (`host_settings.py`): `romfs_folder`, `sd_card_folder`, `mod_folder`, `patcher_command`, `game_start`, `game_host`. A missing RomFS or SD card folder opens a folder browser.
- The client runs `patcher_command <patch> --romfs <romfs_folder> --out <temp dir>` as a subprocess and logs its output (`install.py`). The patcher needs its own Python 3.12 venv (oead, evfl), so it does not run inside Archipelago.
- It then replaces `romfs` and `exefs` in `mod_folder` (default `<sd_card_folder>/atmosphere/contents/01006BB00C6F0000`) and copies `config/lasr-exl/config.ini` to `sd_card_folder`, config last.
- The mod is current when the installed `config.ini`'s `[archipelago] seed_hash` matches the patch and both mod folders exist; then the client skips patching. `/patch` forces it.
- After installing, the client runs `game_start` if set, and connects to the patch's `server` as its `player_name`.

## Item model: remote items

- Every randomized location in the patched game gives the **AP placeholder item**, with `index` = the location's **local id** (AP location id − `LOCATION_BASE`).
- The module's `Inventory::AddItemID` hook sees the placeholder item id, reports a check, and adds nothing to the inventory.
- Every real item, including the player's own, arrives from the server and is granted by the module. The client uses `items_handling = 0b011`: starting inventory is granted by the patch (LASR's starting gear), so the server does not send it again.
- Locations whose category is not shuffled keep their vanilla content. They are AP event locations (no id) with the vanilla item locked, and the patcher leaves them alone.
- Each location shows a model for the item it holds. The world picks it (`models.py`):
  - LAS items show their own model.
  - Other games' items are matched by name against `data/item_models.yml` (e.g. any shield shows the shield).
  - Traps and unmatched names show the Archipelago model.
- The patch file's `location_models` carries these picks.
  - The patcher gives each such location a variant of the placeholder, `ArchipelagoItem_<item key>`. It shows the item's model on the ground and in shops.
  - Each variant gets an `Items.gsheet` row with its own item ID, because the item-get picks its model by ID. IDs come from 72–99 and 103, 107, 109, 118, 119, which neither the game nor LASR uses; the most used models get IDs first, and variants past the free IDs fall back to the placeholder's ID 71.
  - The module counts all these IDs as the placeholder (`AP::IsPlaceholderItem`).
- The Archipelago model is six balls in the logo's colors. The patcher builds it from the player's Seashell Sensor model (`patcher/ap_model.py`) and writes it as `ItemArchipelago.bfres`, with its own Npc entry, `ItemArchipelago`.
- Indexed items (heart pieces, seashells, golden leaves, heart containers) are granted at the lowest free index of their inventory bitfield. Received heart pieces stay below index 32 and seashells below 50, the game's totals.
- Item actors hide themselves while the inventory bit of their index (`parameters[0]`) is set: `ItemHeartPiece` checks the heart piece bits, `ItemSlimeKey` (LASR's static seashells) the seashell bits. The index must be unique within a level. The patcher gives every randomized heart piece and seashell actor its own index above the received range (LASR's converted misc items keep heart piece 51–54), so their visibility depends only on LASR's flag.

## IDs

- `ITEM_BASE = 0x4C410000` and `LOCATION_BASE = 0x4C420000`. The AP id is the base plus a stable offset.
- **Stable offsets.** They live in `apworld/las/data/ids.json` (`{"items": {lasr_name: offset}, "locations": {lasr_location: offset}}`).
  - The file is generated once from LASR's data in file order and committed.
  - New names are appended; existing offsets never change.
- **Local id.** A location's local id is its offset, which is the `index` stored in-game. It must stay below 65536.

## Names

- **AP display names** are human-readable, e.g. `Progressive Sword`, `Small Key (Tail Cave)`, `Tail Cave - West Hallway Chest`.
- **LASR names** (`sword`, `key-D1`, `D1-west-hallway`) are used in the patch file and in `ids.json`.
- The world keeps the mapping between the two.

## Patch file (`.aplas`)

An Archipelago player container (`container.py`, `APPlayerContainer`): a zip with `archipelago.json` (game, player, player name, server; WebHost fills in the room's server) and `patch.json`. The patcher also accepts a bare `patch.json`. `patch.json` is UTF-8 JSON:

```json
{
  "format": 1,
  "seed_name": "<AP seed name>",
  "player": 1,
  "slot_name": "Link",
  "seed_hash": "<8 hex chars; also in slot_data; the game reports it in HELLO>",
  "settings": { "<LASR setting key>": "<value>" },
  "starting_items": ["<lasr item name>"],
  "placements": { "<lasr location name>": "<lasr item name> | \"ap-item\"" },
  "location_ids": { "<lasr location name>": "<local id (int)>" },
  "location_names": { "<lasr location name>": ["<AP item name>", "<owner's name, empty for this slot>"] },
  "location_models": { "<lasr location name>": "<lasr item name whose model it shows>" },
  "dungeon_entrances": { "<dungeon>": "<dungeon>" }
}
```

- `settings` uses LASR's `SettingsManager.fetch()["Settings"]` keys.
- `placements` covers every LASR item location: shuffled ones are `"ap-item"`, vanilla ones carry their vanilla item. Owl statues that give no gift are left out, as in LASR.
- `location_names` covers the AP locations. The patcher uses it for the shop's price tags; in-game text gets the same names at runtime from the client.
- `location_models` leaves out locations that show the Archipelago model. It is optional: older patches show the Archipelago model everywhere.
- `dungeon_entrances` is an identity map; entrance shuffle is not supported.

## slot_data

`{"seed_hash": str, "goal": str, "death_link": bool, "location_count": int}`

## Game ↔ client protocol (TCP 43384, game listens)

ASCII lines terminated by `\n`, fields separated by single spaces. The game accepts one client at a time. The client sends `PING` every 2 s, and the game drops a client that has been silent for 10 s.

**Client → game**

| Line | Meaning |
|---|---|
| `PING` | Keep-alive; the game answers `PONG` |
| `ITEM <n> <item-key> <count> <dungeon> <location> <item name>\|<sender>` | The n-th received item (1-based AP index). `item-key` is the game's item symbol (`SwordLv1`, `SmallKey`, …). `dungeon` is the dungeon level for dungeon items, else `-1`. `location` is the local id the item came from when it is this slot's own, else `-1`. `sender` is empty for own items and items from the server. The game applies it only if `n` = stored count + 1, then answers `RECV <count>` |
| `NAME <local id> <item name>\|<owner>` | What a location holds, from `LocationScouts`; `owner` is empty for this slot's items. Sent for every location on connect |
| `SYNC` | Asks the game to send `RECV <count>` and `CHECKS ...` |
| `DEATH` | DeathLink: sets the player's health to zero |
| `MSG <text>` | Logged by the module |
| `DBG <cmd> [args]` | Test-only debug commands (see the module source) |

**Game → client**

| Line | Meaning |
|---|---|
| `HELLO <proto=2> <seed_hash>` | Sent on connect |
| `PONG` | Answer to `PING` |
| `RECV <count>` | Number of items applied, stored in the save as a flag-bit counter |
| `CHECK <local_id>` | A location was collected |
| `CHECKS <id> <id> ...` | Every collected location, sent on connect and on `SYNC` (possibly several lines) |
| `GOAL` | Goal completed: the `ShadowClear` flag was set (also resent on `SYNC`) |
| `DIED` | DeathLink: the player's health reached zero |
| `LOG <text>` | Debug text; `LOG level <name>` on every level change |

Lines that need a loaded save (`ITEM`, `SYNC`, `DEATH`, most `DBG`) wait until the player has been in the current level for 1.5 s; other lines are handled immediately.

**Items with special handling in the module**
- Progressive sword, shield and bracelet arrive as their Lv1 key; the module upgrades when LASR's `*FoundFlag` is set.
- Seashells, heart pieces, heart containers and golden leaves take the lowest unused in-game index.
- Traps (`*Trap`, `$ENEMY`) cost one heart and never kill. Unknown keys are logged and counted as applied.

**In-game text**
- A found placeholder says what it is: "You found <item> for <player>!", or "You found your <item>!" for this slot's own items. Without names (client not connected yet), it says "You found an Archipelago item!".
- A received item from another player says "You got <item> from <player>!". Own items found here are not shown again; other own items and server items show the item's native text, except items whose native text names their vanilla source (the Boomerang, the capacity upgrades), which say "You got <item>!".
- The patcher adds the messages `Place:ArchipelagoItem` and `System:APReceived` (with the native item-text window style) and the Item event entry point `APReceived`. The module hooks the game's MSBT label lookup and text getter and hands out the text for these two messages (`ap_text.cpp`).
- The module learns the location from the placeholder's `AddItemID`, so placeholder flows add the item before playing the item-get (LASR's item-get chains and a TreasureBox branch); other items keep LASR's order. That location names the found text until the player is free again.
- The Seashell Mansion's presents add their item after the item-get. The module reads the location from `ShellMansionMaster.PopBox` when the present appears.

**Showing received items**
- The module grants an item the moment its `ITEM` line is handled, and queues a display of it.
- A display is the game's own item-get (Link holds the item up, the item's text shows). The module calls the code behind the event action `Link.GenericItemGetSequenceByKey` directly.
- A display starts once the player has been free for 0.6 s: no talk, event, cutscene, pause menu or level load. Displays run one at a time.
- During a display only A and B reach the game, and A arrives as B, so Link cannot walk away and the closing press does not start a talk.
- With three or more displays waiting, filler (rupees, ammo, drops) is granted without a display. The queue is not saved; a restart drops pending displays, not items.
- Traps show nothing.

**Persistence**
- **Received-item count:** event flags `APRecv0`..`APRecv15`, read as a 16-bit counter. The patcher adds them to `GlobalFlags.gsheet`.
- **Collected locations:** a bitset file `sd:/lasap/<seed_hash>.chk`, written on each check, so checks made while disconnected are reported later.
