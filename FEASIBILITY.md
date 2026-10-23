# LAS Randomizer → Archipelago: feasibility

Investigated 2026-10-12. Reference repos are cloned in `reference/`. They were read only; nothing was built or run.

## Verdict

**Feasible.** Most of the hard groundwork already exists across three open-source projects. No single piece is a research problem. The work is integration, plus a list of game-specific edge cases.

| Piece | What exists | Remaining work |
|---|---|---|
| Logic, items, locations | LASR `RandomizerCore/Data/{logic,items,locations}.yml`: 344 item locations, ~115 item types, small boolean grammar | Compile into AP regions and rules: mechanical, medium effort |
| Static patcher (romfs) | LASR `mod_generator.py` writes event flows, datasheets, rooms and text from a `{location: item}` dict | Feed it AP placements; add an "AP item" placeholder and per-location check flags |
| In-game code (exefs) | `Owen-Splat/exlaunch` (GPL-2.0, public): hooks `Inventory::AddItemID`, `EventFlags::Set/CheckFlag`, level load; offsets for both 1.0.0 and the 1.0.1 update | Add a socket thread, check reporting, a received-item queue and a save-persisted index |
| Switch networking | No reusable library. Proven patterns: Randovania Dread `open-dread-rando-exlaunch` `remote_api.cpp` (exlaunch, GPL-2.0, TCP listen on console + Ryujinx), SMO-AP `SocketClient.cpp` (Starlight, MIT) | Port Dread's socket thread into the LAS exlaunch module |
| PC client | SMO-AP `World/smo/Connector/Client.py`: `CommonContext` + asyncio TCP bridge | Write an LAS client on the same pattern (pure Python) |
| Prior LAS AP work | None found. LASR `roadmap.txt` ends with "time to work on AP support" | Coordinate with Owen-Splat before forking (see Recommendations) |

## Key findings

### The SMO mod is a pattern, not a library

- **Architecture.** The Switch mod connects over raw TCP to a PC "connector". The connector is a `CommonContext` subclass that does all AP websocket/JSON work and serves on port 1027. This split is the right model for LAS.
- **What can't be reused.**
  - **Engine-bound code.** The C++ is built on SMO's own engine types (`al::AsyncFunctorThread`, `sead::MessageQueue`, `sead::ExpHeap`, SMO layouts). Much of it is SMO Online leftovers (puppets, hide-and-seek, `PlayerInf` packets).
  - **Toolchain.** It uses Starlight with BL-patch `.slpatch` files and depends on SMO 1.0.0 exporting its symbols. LAS's main NSO doesn't export symbols, so the build approach doesn't carry over either.
- **What carries over.**
  - **Switch-side flow.** nifm init, `nn::socket` TCP, a fixed header + body framing, recv/send threads, reconnect-by-reinit. Game-state changes are deferred to the game thread.
  - **Connector flow.** Received items are replayed through `Sync` on every reconnect. Checked locations are resent from `checked_locations`.
- **Bugs to avoid if copying the connector.**
  - It uses `reader.read(n)` instead of `readexactly`.
  - `on_package` echoes server packets back to the server.

### Randovania's Dread module is the closer template

`randovania/open-dread-rando-exlaunch` uses the same framework as LAS (exlaunch) and the same license (GPL-2.0).

- **Socket thread.** A `StaticThread` runs `nn::socket::Initialize`, then `Bind`/`Listen`/`Accept` on a port, and polls `Recv` with `MSG_DONTWAIT`.
- **Game-thread handoff.** Received work is flagged for the game thread and executed from a per-frame hook. Replies go into a mutex-guarded send buffer.
- **Ryujinx workarounds** documented in its comments:
  - one blocking socket blocks all sockets
  - `fcntl` non-blocking mode fails, but `MSG_DONTWAIT` works
  - errno is unreliable, so a keep-alive is needed
- **The game listens, the PC connects.** So no on-console IP entry or software keyboard is needed.

### The LAS exlaunch module already has the primitives AP needs

From `reference/LAS-exlaunch/source/program`:

- **`Hooks/Common/inventory.hpp`, `Inventory__AddItemID(ID, count, index)`.** Every item grant flows through this hook. It already special-cases dungeon items: it uses `index` as the dungeon by temporarily overriding the inventory level byte. It also handles extra flags for instruments, ammo capacity and similar.
  - This is both the place to **detect** a check (placeholder item → report the location) and the function to **grant** received items.
  - Trade items (IDs 29–42 apart from the lens) are deliberately not added to the inventory; LASR tracks them with flags. Receiving trade items remotely needs its own path.
- **`Hooks/Common/eventflags.hpp`.** Callable `EventFlags::SetFlag/CheckFlag(name)` and `Set/CheckGettingFlag(itemID)`.
- **Game versions (`version.hpp`, `Game::Offset`).** Base and update builds are told apart by a byte read; the update's offsets are shifted inside a known range. Both code versions (1.0.0, 1.0.1) are covered. 1.1.0 (Switch 2) reportedly changes only `main.npdm` and is unverified.
- **NPDM permissions.** The NPDM shipped with LASR already grants `bsd:u`, `nifm:u`, `sfdnsres` and `ssl`, so sockets are allowed without permission changes.
- **Source vs. shipped binary.** The public repo's `main.cpp` currently installs only the common hooks; the randomizer hooks are commented out while practice-mod work is in progress. The binary shipped in `DEV-BUILD-LASR/exl/subsdk9` contains the randomizer hooks (from its strings). Building from source matches the shipped binary only after re-enabling the randomizer hooks.

### LASR logic ports cleanly

- **Node types in `logic.yml`.** It is a flat dict of nodes typed `item` (locations), `region`, `room`, `enemy`, `interaction`, `trick`, `follower`.
- **Conditions.** Each location has per-preset conditions (`condition-basic|advanced|glitched|hell`).
  - Grammar: names, `&`, `|`, `!`, parentheses, and counts written `name:N`.
  - Settings appear as tokens (`open-mabe`, `classic-d2`, …).
  - `shuffler.py` evaluates them by regex-rewriting each condition to Python and calling `eval`.
- **Port to AP.** Compile each condition into a `CollectionState` lambda. Model `region`/`room`/`enemy`/`interaction`/`trick` nodes as either regions with entrances or event items/helper rules.
- **Gotchas.**
  - `kill-x[pit]` / `[heavy]` enemy variants
  - `starting-item*` keys
  - LASR's vanilla-only location subtypes
  - `verifySeashellsAttainable` and other fill constraints that AP must express as rules or `pre_fill`
- **Why not reuse LADX's logic.** The AP core `ladx` world bundles LADXR logic keyed to GBC room IDs and ROM checks. LAS changes several checks (seashell count, Dampé, Trendy, chamber dungeons), so LASR's logic is the right source.

### Item placement already supports flag-only items

- **How placements are baked in.**
  - **Chests:** the room actor's itemKey/index parameters, plus a `CompareString` chain in `TreasureBox.bfevfl`.
  - **Heart pieces, seashells, instruments:** actor parameters plus a per-location entry point.
  - **NPCs:** event-flow edits through `ItemGetManager` (`Inventory.AddItemByKey` + `Link.GenericItemGetSequenceByKey`).
- **Existing flag-only items.** The Dampé and shop rows are `Items.gsheet` clones whose only job is to set a getting flag. Traps use fake IDs 127–131 that never reach the inventory; LASR notes that IDs above 127 crash if actually added.
  - An "Archipelago item" row can be made the same way.
- **Flag budget.**
  - The vanilla `GlobalFlags.gsheet` has ~1118 flags, 56 of them named "unused".
  - LASR already adds ~180 custom flags by renaming unused flags and appending past the max index.
  - AP needs one more flag per location plus a few for the received-item counter. The actual save-data capacity for event flags is not yet confirmed.

## Proposed architecture

```
Archipelago server ──websocket/JSON──> LAS client (Python, CommonContext, in the apworld)
                                          │ TCP (game listens; e.g. port 4321)
                                          v
                       LAS exlaunch subsdk9 (socket thread + game-thread queue)
                                          │ Inventory::AddItemID / EventFlags
                                          v
                       Link's Awakening with LASR-patched romfs (per seed)
```

1. **apworld `las`** (pure Python, no native deps).
   - **Generation-time data.** Items, locations and stable IDs come from LASR's YAML, with logic compiled from `logic.yml`; options are a subset of LASR's settings.
   - **`fill_slot_data`.** Carries what the client needs.
   - **`generate_output`.** Writes a small patch file (placements, settings, seed) for the patcher.
   - **Native deps stay out.** LASR's patcher needs `oead`, `evfl`, `PyLibMS` and the user's romfs. That's why the apworld stays pure Python and the patcher is a separate step.
2. **Patcher.** A fork or mode of LASR that loads the AP patch file instead of running `ItemShuffler`.
   - **Local items** are placed natively, as today.
   - **Items for other players** become an `ArchipelagoItem` placeholder row whose `index` is the AP location ID.
   - **Check flags.** Every location also sets a per-location `AP_<id>` flag. This makes resync possible: on connect, the game walks all `AP_*` flags and reports them.
3. **exlaunch module.**
   - **Socket thread.** Ported from Dread's `remote_api.cpp`.
   - **Check reporting.** In the `AddItemID` hook: a placeholder grant reports its `index` as a location check and isn't added; native grants report through the location flag.
   - **Received-item queue.**
     - Drained on the game thread only when Link is controllable: not during events, menus or warps.
     - The received-item count is persisted in the save as a flag-bit counter, so reloading an older save replays correctly.
   - **Goal and DeathLink.** The goal is detected on the Wind Fish ending flag. DeathLink: read player health, kill via the player actor.
4. **Protocol.** A small length-prefixed message format: `hello` with version and seed hash, `check`, `item(index, item_id, count, dungeon)`, `checked_flags_dump`, `death`, `ping`. Newline-delimited JSON (as in `mdietz94/smo_archipelago`) is acceptable for this volume.

**Fallback transport.** On Ryujinx only, the client can read and write a mailbox in emulator process memory, as `SSHD_APWorld` does with a MOD0 scan. This avoids sockets entirely but doesn't work on hardware.

## Risks and unknowns

| Risk | Severity | Notes |
|---|---|---|
| Showing received and foreign items in-game | Medium | A silent `AddItemID` works. Showing "You got X from Player" needs either dynamic message text (hook the message lookup) or an event-flow trigger from code; neither is RE'd yet. The client UI can carry notifications for an MVP |
| Safe timing for remote grants | Medium | Granting instruments, trade items or tunics mid-event can break flags. Needs a "player controllable" check and per-item side effects (the instrument flag handling in `AddItemID` is a start) |
| Event-flag capacity in save data | Medium | Must confirm how many flags the save stores before adding ~350 `AP_*` flags |
| Per-location flags for every location type | Medium | Chests in dungeons use room actor switches, not global flags; the placeholder/`AddItemID` path covers them, but each location type needs checking (shops, Trendy, Seashell Mansion, rapids, fishing) |
| Indexed items (seashells, heart pieces, leaves, chamber stones) | Low–Med | Remote grants need non-colliding indexes |
| Ryujinx socket quirks | Low | Already solved by Dread's code |
| Two game builds | Low | Already handled by `Game::Offset` |
| Moving upstream | Medium | DEV-BUILD-LASR is a public dev build (0.5.0) and the exlaunch repo is mid-refactor. A hard fork will drift |
| Licensing | Low | LAS exlaunch and Dread exlaunch are both GPL-2.0, so copying Dread code is fine. LASR is MIT (its bntx editor is GPLv3). SMO Mod is MIT; SMO `World/` has no license, so reference it, don't copy it |

## Effort estimate (one experienced developer, part-time)

| Phase | Deliverable | Estimate |
|---|---|---|
| 0 | Build LAS exlaunch from source; run LASR output on Ryujinx 1.0.0 and 1.0.1 | 2–5 days |
| 1 | Socket PoC: PC script connects and grants an item via `AddItemID`; game reports a flag set | 1–2 weeks |
| 2 | apworld: IDs, logic compiler, core options, slot data, patch output; passes AP's generic world tests | 3–5 weeks |
| 3 | Patcher: AP patch input, placeholder item, `AP_*` flags | 2–3 weeks |
| 4 | Client: checks, items with index and resync, goal, DeathLink | 1–2 weeks |
| 5 | Polish: in-game item text, shops, traps, dungeon shuffle, remaining settings | open-ended |

An MVP (phases 0–4, core settings, no in-game foreign-item text) is roughly 2–3 months of part-time work.

## Recommendations

1. **Contact Owen-Splat first** (LASR Discord, linked in the LASR README). AP support is on their roadmap and they own both the patcher and the exlaunch module. Upstreaming the hooks and the patch-file input avoids a fork that drifts.
2. **Start with phase 1 (socket PoC).** It retires the main technical risk, and the remaining phases are mostly mechanical.
3. **Use LASR's logic, not LADXR's.**
4. **Target Ryujinx for development and hardware for validation.** Dread's code shows the same socket path works on both.

## References

- LASR dev build: https://github.com/Owen-Splat/DEV-BUILD-LASR (`reference/DEV-BUILD-LASR`)
- LAS exlaunch module: https://github.com/Owen-Splat/exlaunch (`reference/LAS-exlaunch`)
- LAS Ghidra decomp project: https://github.com/Owen-Splat/las-decomp
- SMO Archipelago: https://github.com/Kgamer77/SuperMarioOdysseyArchipelago (`reference/SuperMarioOdysseyArchipelago`)
- Dread exlaunch remote API: https://github.com/randovania/open-dread-rando-exlaunch
- Dread PC executor: https://github.com/randovania/randovania (`randovania/game_connection/executor/mercury_executor.py`)
- Alternative SMO AP (JSON-lines protocol): https://github.com/mdietz94/smo_archipelago
- Ryujinx memory-mailbox approach: https://github.com/LonLon-Labs/SSHD_APWorld
- AP LADX world: https://github.com/ArchipelagoMW/Archipelago/tree/main/worlds/ladx
- AP network protocol: https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/network%20protocol.md
- AP apworld spec: https://github.com/ArchipelagoMW/Archipelago/blob/main/docs/apworld%20specification.md
- LAS RE notes: https://github.com/leoetlino/la-re-notes (`reference/la-re-notes`); wiki: https://zeldamods.org/las/Main_Page (versions and build IDs on its Versions page)
- Ghidra Switch loader: https://github.com/Adubbz/Ghidra-Switch-Loader
