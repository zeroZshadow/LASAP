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
2. The patcher reads the patch file plus the user's romfs dump and writes `atmosphere/contents/01006BB00C6F0000/{romfs,exefs}` and `config/lasr-exl/config.ini`.
3. The game runs with the mod. The module listens on TCP port **43384**.
4. The client connects to the AP server (websocket) and to the game (TCP), and relays checks and items.

## Item model: remote items

- Every randomized location in the patched game gives the **AP placeholder item**, with `index` = the location's **local id** (AP location id − `LOCATION_BASE`).
- The module's `Inventory::AddItemID` hook sees the placeholder item id, reports a check, and adds nothing to the inventory.
- Every real item, including the player's own, arrives from the server and is granted by the module. The client uses `items_handling = 0b011`: starting inventory is granted by the patch (LASR's starting gear), so the server does not send it again.
- Locations whose category is not shuffled keep their vanilla content. They are AP event locations (no id) with the vanilla item locked, and the patcher leaves them alone.

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

## Patch file (`.aplas`, UTF-8 JSON)

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
  "dungeon_entrances": { "<dungeon>": "<dungeon>" }
}
```

- `settings` uses LASR's `SettingsManager.fetch()["Settings"]` keys.
- `placements` covers every LASR item location: shuffled ones are `"ap-item"`, vanilla ones carry their vanilla item. Owl statues that give no gift are left out, as in LASR.
- `dungeon_entrances` is an identity map; entrance shuffle is not supported.

## slot_data

`{"seed_hash": str, "goal": str, "death_link": bool, "location_count": int}`

## Game ↔ client protocol (TCP 43384, game listens)

ASCII lines terminated by `\n`, fields separated by single spaces. The game accepts one client at a time. The client sends `PING` every 2 s, and the game drops a client that has been silent for 10 s.

**Client → game**

| Line | Meaning |
|---|---|
| `PING` | Keep-alive; the game answers `PONG` |
| `ITEM <n> <item-key> <count> <dungeon>` | The n-th received item (1-based AP index). `item-key` is the game's item symbol (`SwordLv1`, `SmallKey`, …). `dungeon` is the dungeon level for dungeon items, else `-1`. The game applies it only if `n` = stored count + 1, then answers `RECV <count>` |
| `SYNC` | Asks the game to send `RECV <count>` and `CHECKS ...` |
| `DEATH` | DeathLink: sets the player's health to zero |
| `MSG <text>` | Logged by the module |
| `DBG <cmd> [args]` | Test-only debug commands (see the module source) |

**Game → client**

| Line | Meaning |
|---|---|
| `HELLO <proto=1> <seed_hash>` | Sent on connect |
| `PONG` | Answer to `PING` |
| `RECV <count>` | Number of items applied, stored in the save as a flag-bit counter |
| `CHECK <local_id>` | A location was collected |
| `CHECKS <id> <id> ...` | Every collected location, sent on connect and on `SYNC` (possibly several lines) |
| `GOAL` | Goal completed: the `ShadowClear` flag was set (also resent on `SYNC`) |
| `DIED` | DeathLink: the player's health reached zero |
| `LOG <text>` | Debug text |

Lines that need a loaded save (`ITEM`, `SYNC`, `DEATH`, most `DBG`) wait until the player has been in the current level for 1.5 s; other lines are handled immediately.

**Items with special handling in the module**
- Progressive sword, shield and bracelet arrive as their Lv1 key; the module upgrades when LASR's `*FoundFlag` is set.
- Seashells, heart pieces, heart containers and golden leaves take the lowest unused in-game index.
- Traps (`*Trap`, `$ENEMY`) cost one heart and never kill. Unknown keys are logged and counted as applied.

**Persistence**
- **Received-item count:** event flags `APRecv0`..`APRecv15`, read as a 16-bit counter. The patcher adds them to `GlobalFlags.gsheet`.
- **Collected locations:** a bitset file `sd:/lasap/<seed_hash>.chk`, written on each check, so checks made while disconnected are reported later.
