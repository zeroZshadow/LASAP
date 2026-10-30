# Items and inventory (game 1.0.0)

What adding and owning an item does in the main module, the Chamber Dungeon's stones and panels, and the saved data the inventory is part of, from static analysis. Offsets are module offsets; `las.sym` names them at their runtime addresses (module offset + 0x8504000). Item ids are `Items.gsheet`'s `itemID`, as the module's `kItemNames` (`game/hooks/inventory.hpp`) lists them.

## Adding and owning

`Inventory_AddItemID(item, count, index)` (`0x8d08e0`, hooked by the module) and `Inventory_HasItem(item, count, index)` (`0x8d1a30`) switch on the item id, 0 to 0x7e. `count < 1` adds nothing and owns everything. An id from 0x80 throws (a bitset's out_of_range), so the module's hook passes on ids below 127 only.

After every add, also those that change nothing, AddItemID sets the item's bit in the obtained bits (inventory `+0x124`, a bit per item id), and for the Boomerang (7), Seashell (22), Piece of Heart (58), Bottle (113) and Chamber Stone (114) it sends a play report (`PlayReport_ItemCommon`: SaveId, TotalTimePlayed, Chapter, Warashibe, then ItemId and Index). The getting flags (`Item_CheckGettingFlag`, `Item_SetGettingFlag`, both hooked) are something else: the event flag the item's `Items.gsheet` row names (row `+0x70`).

Inventory fields by name are in the module's `game/data/inventory.hpp` (`InventoryItems`; Saved data below).

| Id | Item | AddItemID | HasItem |
|---|---|---|---|
| 0, 2 | SwordLv1, Shield | `+0xa8` bit 0 / 1 | the bit |
| 1, 3 | SwordLv2, MirrorShield | also `+0x9c` bit 0 / 1 | `+0x9c` bit |
| 4 | Bomb | `+0xa8` bit 2, then `Inventory_AddBombs(count)` | `min(+0x9e, cap) >= count`: cap 0 without the bit, 30, 60 with Bomb_MaxUp |
| 5–9, 12, 15, 16 | Bow, HookShot, Boomerang, MagicRod, Shovel, RocsFeather, PegasusBoots, Ocarina | `+0xa8` bits 3–7, 9, 11, 12 | the bit |
| 10 | SleepyMushroom | `+0xac` bit 15 and the time at `+0xb0` | the bit |
| 11 | MagicPowder | `+0xa8` bit 8, `Inventory_AddPowder(count)` | `min(+0xa0, cap)`: 20, 40 with MagicPowder_MaxUp |
| 13, 14 | PowerBraceletLv1, Lv2 | `+0xa8` bit 10; Lv2 also `+0x9c` bit 2 | the bit; Lv2 `+0x9c` bit 2 |
| 17, 18, 19 | ClothesGreen, Red, Blue | `+0x9c` bits 6 (red) and 7 (blue): green clears both, red sets 6 and clears 7, blue the other way | green always owned |
| 20, 21 | Flippers, SecretMedicine | `+0xac` bit 0 / 1 | the bit |
| 22 | Seashell | `+0x88` bit `index`, index 0–49 | index < 0: popcount, else the bit |
| 23 | GoldenLeaf | `+0xc2` bit `index`, index 0–4 | as seashells |
| 24–28 | TailKey … BirdKey | `+0xac` bits 2–6 | the bit |
| 29–40, 42, 43 | trade items | `+0x9a` = 1–14 (41 is not an item): the one held, replacing the last | `+0x9a` equal |
| 44, 45, 47, 49 | Compass, DungeonMap, StoneBeak, NightmareKey | the current dungeon's entry (`DungeonItems[LevelIndex]` `+1`) bits 0, 1, 2, 3; nothing outside dungeons (`LevelIndex` 0xff) | the same entry |
| 48 | SmallKey | entry `+0` += count, 0–255 | entry `+0 >= count` |
| 50–57 | instruments | `+0xac` bits 7–14, then `Inventory_RecordChapter(instrument)` | the bit |
| 58 | HeartPiece | `+0x90` bit `index`, index 0–31; health to the maximum | as seashells |
| 59 | HeartContainer | `+0x98` bit `index`, index 0–8; health to the maximum | as seashells |
| 60 | Arrow | `Inventory_AddArrows(count)` | `min(+0x9f, cap)`: 0 without the Bow, 30, 60 with Arrow_MaxUp |
| 61, 62, 100 | Apple, GreenApple, Heart | health + 4, + 12, + count × 4, up to the maximum | Heart: health ≥ count × 4 |
| 101–108 | Rupee1, 5, 20, 50, 100, 300 | `+0x80` + 1, 5, 20, 50, 100, 300 (not × count), 0–9999 | rupees ≥ count × the value |
| 110, 126 | Fairy, BottleFairy | bottle slot `index` 0–2: `+0xc0` bit set while the bottle (`+0xa8` bit 13 + index) is owned, else cleared; index -1 adds nothing | index < 0: popcount of `+0xc0` |
| 111, 112 | DefenceUp, PowerUp | the scene's buff `+0x640` = 2 / 1, `+0x644` = 30.0 (its time, guess) | that buff active |
| 113 | Bottle | `+0xa8` bit 13 + `index`, index 0–2 | index < 0: owned bottles |
| 114 | PanelDungeonPiece | the chamber stone bit `index`, index 0–50 (Chamber stones) | the bit |
| 115 | PanelDungeonPieceSet | the set bit `index`, index 0–12 | the bit |
| 116 | ShellRader | `+0xac` bit 16 | the bit |
| 120–122 | songs | `+0x9b` bits 0–2; the selected song (`+0xa1`) becomes this one while none is | the bit |
| 123–125 | MagicPowder, Bomb, Arrow _MaxUp | `+0x9c` bits 3–5, then the count's add with 0 | the bit |
| 41, 46, 63–99, 103, 107, 109, 117–119 | | only the obtained bit | false |

What this means for received items:

- An index out of its range adds nothing but the obtained bit; a received Chamber Stone with index -1 is one (TODO.md).
- Dungeon items go to the dungeon Link is in; the module's hook sets `LevelIndex` to the item's dungeon around the call.
- A trade item replaces the one held; the module's hook adds none of 29–40 and sets their flags instead.
- Secret Medicine is a single bit: a second one while Link holds one is lost. LASR's pool has three.
- Heart containers take 9 indices: the 8 bosses and the Chamber Dungeon's reward for 12 clears (`MapPieceClearReward` index 8).

## Item actors

The item actors placed in rooms (ItemSmallKey, ItemGoldenLeaf, ItemHeartPiece, ...) share an item base class: an actor and an NPC component, whose Npc.gsheet row gives the model, tailor, flowchart (`eventInfo`), triggers and collision. The table entries of ItemSmallKey (`0xa9`) and ItemGoldenLeaf (`0x88`) name no resources. The item base's actor vtable is `0x1bc9ac0` (`ItemActor_Ctor` sets it), its NPC component's `0x1bc9d50`; both are complete (a class overrides only what differs). The module's drop actor is an item class of its own on them (DESIGN.md, Drop actor).

- **Factory** (ItemSmallKey `0x8cabb0`, the table's `(out x8, owner sp*, params*)`): `ActorName_Make` (`0x23830`, the LEB name, else `<table name>:<counter>`), then `0x8cacf0`: null for a name of 0 or 32+ characters or one the owner has (`Entity_FindChildInMap` on the owner's `+0x10`, `+0x38`; one in `+0x10` must also be in `+0x60`); `0x8cb180` builds the `ActorArgs` (`{name, length, 1, owner sp (one reference), params copy at +0x30}`, `ActorParams_Copy` `0x31100`), constructs (`0x8cb220`) and destroys them (`ActorArgs_Dtor` `0x319f0` releases the owner and the params); unless destroying: Register (`+0x78`), Setup (`+0x20`), flags bit 3, `Entity_Start` (`0xfab470`), `Entity_AddChild(owner, sp)` (`0xfb3290`), else `Entity_Destroy`.
- **Constructor** (`0x8cb220`): `operator_new(0x340)`, a make_shared block: control block vtable `0x1bcaa38` (its zero-shared calls the actor's virtual destructor), the 0x320-byte actor at `+0x20`. `ItemActor_Ctor` (`0x8b6880 (actor, ActorArgs*, StringView* row key, u8 item id, u32 count, pose pair)`): `Actor_Ctor`; the row key at `+0x2a8` (char[0x20], length `+0x2c8`; 0x20 characters or more throw); item id `+0x2e8` (0x30 for the small key), count `+0x2ec`, NPC component sp `+0x2f0`, pose pair `+0x300` (the small key's `0x16fc744`, the side view's `+0x48` when `0x8b6710(position)` finds a side-view area), touch delay `+0x308` (0.5 s), classBits `|= 0x201`. Then the class vtable (ItemSmallKey `0x1bcaa70`) and the self pointer (`+0x8`, weak owners 1).
- **NPC component.** The item base's Setup (`0x8b6b70`) finds the Items row of `+0x2e8` and calls actor virtual `+0x128 (actor x0, name* x1) -> sp x8` with `"Npc"`. ItemSmallKey's (`0x8cb8a0` → `0x8cb8f0`, `0x8cbb90`, `0x8cbeb0`): null for a destroyed actor, a name of 0 or 32+ characters or one it has (`0xfa9490 (actor, name, length)`); `operator_new(0x360)`, `ItemNpcComponent_Ctor` (`0x8c3f10 (component, ComponentName*, row key*)`: `NpcComponent_Ctor`, which looks the row up, and the item base's fields), vtable `0x1bcabf0`, u16 `+0x358` 0 (body ready, fall pending), `0x8cbf40 (out, component)` (its shared_ptr, control block vtable `0x1bcb028`); Register, Setup, bit 3, the parent-active bit 1 from the actor (`+0x50` when the active state flips); `EntityManager_PostStart`; `Entity_AttachChild(actor, component)`. The row key is read only by `NpcComponent_Ctor`; the item base's own maker (`0x8b8420`) takes it from the actor's `+0x2a8`.
- **Actor overrides** (ItemSmallKey over the item base): `+0x18` the deleting destructor (the same code), `+0x68`/`+0x70` IsA (type id address) and TypeId (`0x8cb450`: the small key's `0x1cb2aa0`, then the item base's chain; the item base's IsA `0x8b7250`), `+0x78` the start task (typed), `+0x100` the zone enter (the item base's `0x8b8410` clears the component's `+0x2e4` and its `+0x30c`; the small key's then falls again, see Event actions), `+0x128` the component maker. The make_shared control block's zero-shared calls the actor's `+0x10` (`0x8cf710`).
- **Component overrides** (ItemSmallKey over the item base `0x1bc9d50`): `+0x18` the deleting destructor, `+0x68`/`+0x70` IsA and TypeId (`0x8cc140`: the small key's component `0x1cb2ab0`; the item base's IsA `0x8c4120`), `+0x78` the start task, `+0x98` `SmallKey_UpdateAppear` (enabled while switch 0 is on; casts its owner to ItemSmallKey without a null check; the item base's `0x948190` enables it), `+0x178` the event start (an entry named exactly `take` adds a small key (`Inventory_AddItemID(0x30, 1, -1)`), sets switch 1, autosaves and destroys the actor; any other entry: the item base's `0x8c4750`), `+0x210` MakeCollision, `+0x220` MakeEventChara (both below), `+0x388` BuildComponents (the item base's `0x8c5530`, then the compass chime `CompassAlarm`), `+0x3e0`..`+0x3f8` the inventory hide (off; the item base's on), impulses and freeze (on; off), the sleep check (off; on). Its own code casts by its type ids. The component's shared_ptr is a pointer block (`0x8cbf40`; its zero-shared calls the component's `+0x18`). The pop check `+0x88` (`NpcComponent_CheckPop` `0x947d70`, the row's popConditions) gates `+0x98 (component, bool quiet)`.
- **MakeCollision** (component `+0x210 (out x8, component, name*, x2, x3)`, as the other makers): ItemSmallKey's (`0x8cc910`) calls `NpcComponent_MakeCollision` (`0x94b350`), and for a collision with a body (IsA `0x1cab3a8`, not destroying) connects two slots (virtual `+0x20 (signal, std::function*)`): to `+0x1c8` (the body made: u8 `+0x358` 1, and a pending fall runs) and `+0x220` (lost: `+0x358` 0). Each slot is a libc++ `__func` `{vtable, component}` (vtables `0x1bcb128`, `0x1bcb170`; the game has no RTTI, so 7 slots: destructor, deleting destructor, clone, clone into, destroy, destroy and free, call), called with the body's shared_ptr moved in, which it releases.
- **Event actions.** Each EventChara dispatches by action name through the `std::function<bool(std::function* step, args)>` at `+0x400` (the args' first member points to the name's `{characters, length}`): generic actions (`0x1049a0`: Activate, Deactivate, Destroy, SetActorSwitch, PlayOneshotSE, GenericGimmickSequence, ...), wrapped by the item base (`0x8c4a30`, component `+0x220`: `PlayTailor*Channel`) and the class, each wrapper asking the one inside first. An action that takes time swaps a step callable into `*step` (`std::function` swap), which the event runs each frame until it returns `{0, 0}` (through x8). `Fall` is ItemSmallKey's (MakeEventChara `0x8cccb0`, wrapper `__func` vtable `0x1bcb098`, step `0x1bcb0e0`: `SmallKey_StartFall` `0x8ce2f0 (component)`, done at once) and ItemGoldenLeaf's (`0x8a7f60`). `SmallKey_StartFall` reads no type: without a body yet (`+0x358` 0) it sets `+0x359` and runs again once the body is made; else the body dynamic with gravity, its velocities zero, and the root 10 units above the spawn (`Node_SetWorldPos` `0xfb6d30`, guess); gravity drops it. ItemSmallKey's zone-enter (`+0x100`) runs it too when LEB parameter 0 is the string `ReFall` and switch 0 is on.
- **Buried items.** ItemSlimeKey (component vtable `0x1bc6b78`) overrides `+0x88`: with its item id `0x19`, when a dig spot of the scene's dig map (Framework `+0x4a8` → `+0x208` → `+0x228`) is at its root (`0x89fa90`), it pops once that spot is dug up (`0x89fcb0`); else the row's popConditions. Its `+0x98` (`0x89f770`) enables it and, unless quiet, sets its body's velocity to (0, 4, 0): it hops out. Neither helper reads the class.
- **Appearing.** `NpcComponent_BuildComponents` arms the row's trigger conditions 14 to 17 (switch 0 to 3 turns on, `+0x320`): the pop entry. Touch (condition 0) runs the take entry once the touch delay has run out outside events.
- **Physics.** The row's `collision.traits` names an `NpcPhysicsTraits` row (mass, drag, angularDamp, bounciness, friction, PhysX combine modes, axis locks), applied when the collision is made and again on landing after an impulse. Component virtual `+0x290 (component, const Vec3*)` sets the body's linear velocity (`PxRigidDynamic` vtable `+0x138`, waking it) unless it is kinematic. The item update (`0x8c6030`, component vtable `+0x3c8`) does not run during events; physics does. On landing it applies the traits row again (`0x963df0 (body, row)`: PhysX `+0x100` the mass, `+0x1c8` setLinearDamping the drag, `+0x1d8` setAngularDamping, then the axis locks); once the body's linear velocity has stayed near zero for 0.25 s it puts the actor to rest (`0x95c1430(actor, 1)`, guess). The body is PhysX 3.4's `PxRigidDynamic`: `+0x130` getLinearVelocity, `+0x140` getAngularVelocity (each returns a `PxVec3` through x8, its copy constructor not being trivial), `+0x148` setAngularVelocity, `+0x1e0` getAngularDamping. A ball meeting a wall at about 1 unit a second does not bounce, whatever its bounciness: its velocity into the wall drops to zero (the scene's bounce threshold).
- **Water.** The component's water event (shared_ptr at `+0x168`) holds the in-water flag (u8 `+0x151`) and the water surface height (f32 `+0x154`). While in water, the item update compares the water event's height with the surface: deeper than 0.3 below it, it adds the upward acceleration component virtual `+0x400` returns (the item base's 7.0, less than gravity: items sink) through `PxRigidBody` vtable `+0x150 (force, mode 3, wake)` (guess: `addForce` with `eACCELERATION`), within 0.3 none. Linear damping (`PxRigidDynamic` `+0x1c8`, guess) is component virtual `+0x410` in water (the item base's 1.8) and `+0x408` (`Npc_TraitsValue`, the traits row's drag) out of it, set by the water event's enter and exit closures (`0x8c6c40`, `0x8c7060`) and on landing.
- **Collision filters.** The row's `collision.filter` (1..7) picks a filter word from `NpcCollisionFilters` (`NpcComponent_CollisionFilter`, component virtual `+0x128`; anything else `0xd727ffe0`). A word's bits 0-4 are its layer and bits 5-31 the layers it collides with (bit 5 + n: layer n); the physics scenes' filter callback (`Physics_FilterPair` `0x829920`) lets two shapes collide only when each one's set holds the other's layer, and not when both carry the same nonzero group word. Filter 7 (the items: layer 9 with {1, 2, 6, 12, 13, 16, 19, 26}) passes through rocks, bushes, chests and Link; filter 4 (most NPCs: layer 7 with {1, 2, 5, 6, 7, 8, 11, 12, 13, 15, 16, 17, 19, 23, 25, 26}) collides with them. `SetTouchable` clears bit 11 (layer 6, guess: Link's pickup contact). A rock's collision (`ObjRock`) is a block well above its model: a ball with filter 4 bounced off its side about 2.9 above the floor.
- **Seashell drops.** A bonked tree (`ObjTree_DropItem` `0xcb1f30`, the tree) and a bombed hole's AreaShellDropper (`AreaShellDropper_Drop` `0x855a0`) drop their seashell through `Enemy_DropItems` with callbacks that place and launch each item. Both aim at the point of their first LEB rail link (actor `railLinks`, `+0x118`: a link's `+0x20` rail, whose virtual `+0x48 (link +0x1c, the point's index)` gives the point's position). The tree starts the item at its root plus its parameters' (`+0x478`) crown height (`+0x6c`) and 0.5, launched at their speed (`+0x68`) toward the point (without one, toward Link). The dropper starts it 2.25 above itself (virtual `+0x138`), launched at 10.8 toward the point or itself (`+0x140`). `Drop_LaunchVelocity` (`0x89d880 (speed, Vec3* from, Vec3* to)`) solves the launch angle for that speed under gravity -9.81 (the higher of the two arcs; 45 degrees plus half the slope's angle when out of reach) and returns the velocity. A cut bush, a lifted rock and a pot (`0xba63e0`, `0xc49b10`, `0xb2ae50`; owners by address order, guess) drop the item their LEB parameter names (else a random drop, guess) through `Enemy_DropItemsDefault` (`0x89b7e0`), whose default callbacks (`0x899d10`) start each item 0.5 above the object's root (`0x899f40 (actor, id, index, count)`) and launch it at (0, 4, 0) (`0x89a190`); with 2 or more items each also goes out on a fixed fan (0.5 out, and 1 outward in the launch). `Enemy_DropItems` passes seashells (22), golden leaves (23), heart pieces (58) and heart containers (59) one at a time (index 0, count 1): a bush's seashell hops straight up.

## Item-get poses

The item-get holds its item's model up in a pose picked by item id, not from a datasheet: `ItemPose_Pair` (`0x8d2b80`) switches on the id (0–126; any other id and the ids without a case take a default) to a pair of 9-float poses (position, rotation in degrees about x, y, z, scale), a held pose and the item-get pose, with the side view's pair 0x48 further. `ItemPose_ItemGet` (`0x8d2f50`) returns the item-get pose alone. The item-get's item node (`ItemGetHolder_Make`, item id `+0x1c1`) reads it through its vtable `+0xa8` and the game writes it to the model component's node once the item pops up. The Npc row (`npcKey`) gives the model only.

| Item-get pose | Items |
|---|---|
| scale 1.7, tipped 20° | 58 HeartPiece, 59 HeartContainer |
| scale 1.6 | 0, 1 swords; 114, 115, 117 chamber stones |
| scale 1.5 | shields, Shovel, Letter, FishingHook, Fairy, the five nightmare-door keys, Compass, Dungeon Map, NightmareKey, the instruments |
| scale 1.17, rolled about 21° | 16 Ocarina, 120–122 songs |
| scale 0.9 | 22 Seashell |
| scale 0.8, tipped forward 70° | 17–19 tunics |
| the default: scale 1, tipped 20° | 41, 46, 63–99, 101–107, 109, 118, 119, 127–255 (the rupees but 300, the placeholder and its variants, LASR's Dampe and shop rows) |

The other items lie between 0.94 and 1.4, each with its own angle. `ItemPose_Pair`'s held pose is also read by the item actors (with their own item id) and by one more holder (`+0x168`, `0x091c68b0`), not traced.

## Chamber stones

**Tables** (each `{u8* bits, s32 bytes, s32 bits}`): `PanelPieceBits` the stones owned (51 bits), `PanelPieceSetBits` the panel sets owned (13), and copies `PanelPieceBits2`, `PanelPieceSetBits2`: those Dampé has made into panels. `PanelNotifyBits` tells Dampé there is something new: AddItemID sets bit 0 for a new stone, bit 2 for a further stone or a set, bit 3 for a special stone (7, 13, 18, 19, 24, 32, the `SpecialAddition` panels).

**Stones and panels.** A `MapPieceList` row (a chamber panel; `PanelSheets` `+0x0`, rows 0x98 bytes) names its source in `mPanelItem` (`+0x18`): below 13 the set of that index (0–7 the dungeons, 9 the Color Dungeon), else the stone `mPanelItem - 13`. A dungeon's set holds its panels; each of the 33 stones (`PanelStone.gsheet`) gives one panel. The Items page counts the owned stones whose `PanelStone` row has `mItemMenuCount` (`GmItem_ChamberStoneCount`): 3, 4, 8, 10–13 and 20–26, the 14 found outside the Chamber Dungeon.

| Stone | Vanilla source (LASR location) | Panel |
|---|---|---|
| 3 | Trendy Game (`trendy-prize-4`) | `PanelLv02BottleGrotto_04H` |
| 4 | Seashell Mansion, 30 shells (`30-seashell-reward`) | `PanelLv03KeyCavern_04D` |
| 8 | Rapids race (`rapids-race-30`) | `PanelLv04AnglersTunnel_03F` |
| 10 | Town Tool Shop after the Coral Triangle (`shop-slot5-3rd`, `ShopPanelItem` 0) | `PanelLv06FaceShrine_02C` |
| 11, 12 | Fishing (`fishing-150`, `fishing-ol-baron`) | `PanelLv06FaceShrine_02D`, `PanelLv05CatfishsMaw_05C` |
| 13 | Seashell Mansion, 50 shells (`50-seashell-reward`) | `PanelSpecialAddition_02E` |
| 20–23 | Town Tool Shop after the Thunder Drum (`shop-slot5-4th` to `7th`) | `PanelLv08TurtleRock_04H`, `_05H`, `_04A`, `_05A` |
| 24 | Trendy Game (`trendy-prize-final`) | `PanelSpecialAddition_05E` |
| 25, 26 | Town Tool Shop after the Color Dungeon (`shop-slot5-1st`, `2nd`) | `PanelLv10ClothesDungeon_04D`, `_04A` |
| 0–2, 5, 6, 9, 14–17 | Chamber Dungeon clear rewards (`MapPieceClearReward`) | |
| 7, 18, 19, 27–32 | not traced | `SpecialAddition` and `Lv11OnlyDungeon` panels |

**Dampé** (`Danpei.bfevfl`): his talk checks `CheckNewDungeonSet` and `CheckNewPanelStone` (`PanelDungeon_NewStone`: notify bit 0), plays `RupeePanelGetSequence` and `ExEnemyPanelGetSequence` behind `CheckPanelAddStone` with the indices 13 and 24 (not decompiled), then `ProcessDungeonAndPanelStone` (`PanelDungeon_ProcessStones`): it adds each dungeon's set (`AddItemID(115, 1, dungeon)`) whose instrument Link owns, and set 9 once `Lv10DungeonClear` is set, copies every owned set and stone into the second tables and clears notify bits 0, 2 and 3. A stone added with its index thus becomes a panel at Dampé's next talk, as a found one does; a received instrument brings its dungeon's set the same way.

## Saved data

The save is the static `GlobalSave` (`0x1cbb200`). `GlobalSave_Reset` (`0xdfab90`, a new game; also on `$QUIT`) zeroes `+0x20`..`+0x5f00` (the file image) and fills its sections through pointer slots:

| Slot | Section |
|---|---|
| `+0x5f00` | the file header at `+0x20`, 0xe0 bytes: version 0xe `+0x0`, the body's CRC32 `+0x4`, a 16-byte `SaveId` `+0x8`, `+0x32` 1, `+0x34` 4, `+0x88` u16 1970, `+0x8a`/`+0x8b` 1 |
| `+0x5f20` | the body at `+0x100`, 0x5e00 bytes, which the CRC32 covers; `EventFlags_ptr` is this slot, the start of the slot block |
| `+0x5f80` | the level state at `+0x5f40` (0x30 bytes, not saved): `+0x0` the 256 level flags (`LevelFlags`, bit table at slot `+0x5f88`), `+0x20` a position, `+0x2c` it is set |
| `+0x5fb0` | `DungeonItems_ptr`: a vector (begin, end, capacity) of 10 pointers, one per `LevelIndex`, to `DungeonItemsEntry` (map.hpp) |
| `+0x5fc8` | the Chamber Dungeon section at `+0x240`, 0x5160 bytes (below) |
| `+0x5fd0`..`+0x6138` | ten bit tables of the Chamber Dungeon section (below) |
| `+0x6160` | `SaveHeader_ptr`: the save header (`SaveHeader`, 0x140 bytes) at GlobalSave `+0x53a0` (`0x1cc05a0`) |
| `+0x6168` | `Inventory_ptr`: the current data (`SaveCurrent`, `InventoryItems`, 0x160 bytes) at GlobalSave `+0x54e0` (`0x1cc06e0`) |
| `+0x6170` | `MapPinSection_ptr`: the map pins' section (0x690 bytes zeroed, then up to `+0x763`; UI.md, Map markers) |
| `+0x6178` | bit table of the saved event flags (`GlobalFlags.gsheet`; 2048 bits at `+0x5de0`); "EventFlags `+0x268`" is its bit pointer |
| `+0x61a0` | bit table of 256 bits at `+0x5dc0` (not traced) |
| `+0x61c8` | u8 `LevelIndex`: the current dungeon (0-9), 0xff outside dungeons |

A bit table is 0x28 bytes, `{u64, u64, u8* bits, s32 bytes, s32 bits}`; the `las.sym` names of the tables point at
their `+0x10`. `SaveId` is what the play reports send as `SaveId` (formatted as a UUID); `GlobalSave_Reset` fills it
from `0x14324c0` (guess: `nn::util::GenerateUuid`). The level state's position is the iron ball's (likely: ObjIronball
stores and reads it, the room manager clears it with the level flags on a level load). `GlobalSave_Reset` also zeroes
the body's `+0x5de4` (u32).

**Chamber Dungeon section** (GlobalSave `+0x240`): `+0x0` 80 records of 0xe0 bytes (byte 0 reset to 3;
`PanelDungeon_ItemLock` `0xce1a94` reads byte `+0x2` bit 1 and u32 `+0xcc` bit 0), `+0x4600` 80 records of 0x20
(reset to u16 0x100, byte `+0x2` 1), `+0x5000`..`+0x502f` bytes, `+0x5050` u32, `+0x5093` u8 the amiibo touch count
(`PanelQuery_Stone4` needs 5 or more; the play report's `TouchedIdCount` adds `PanelNotifyBits` bit 5),
`+0x50b0`/`+0x50b2`/`+0x50b4`/`+0x50b6` u16 amiibo counters (play report keys `WrittenAnyDataCount`,
`WrittenOtherAppCount`, `WriteDungeonCount`, `ReadDungeonCount`). Its bit tables (bytes / bits):

| Bits at | | |
|---|---|---|
| `+0x5054`, `+0x5056` | 2 / 13, 7 / 51 | `PanelPieceSetBits`, `PanelPieceBits` (Chamber stones) |
| `+0x5060`, `+0x5062` | 2 / 13, 7 / 51 | `PanelPieceSetBits2`, `PanelPieceBits2` |
| `+0x5092` | 1 / 8 | `PanelNotifyBits` |
| `+0x506c` | 0x26 / 304 | not traced |
| `+0x5094`, `+0x509a`, `+0x50a8`, `+0x50a0` | 6 / 48 (three), 7 / 49 | not traced |

The save files are `save:\savedata0.bin` to `3` (a table at `0x1cc1950`, 0x28 bytes per file), read and written by asynchronous file tasks (`0xdfd420`, `0xe00a80`, `0xe02730`, `0xe072b0`, `0xe07930`).

**SaveHeader** (0x140 bytes, the size `GlobalSave_Reset` clears):

- `knockCount`: the enemies defeated; each enemy's death code adds 1 (beside `KillCount` and `KillCountNoDamage`).
  Bit 0 is what the event query `TestEnemyKnockCountOddEven` tests (lambda `0x96c680`, registered by `0x96c5a0`).
- `deathCount`: one more, up to 0xffff, by Link's event action `AddDeadCounter` (`EventAction_AddDeadCounter`
  `0xd6e650`); not 0, it changes a choice in `0x82c870`.
- `totalTimePlayed`: microseconds; the actor update phase (`0x15be8`) adds the frame time × 1e6 while no event runs,
  saturating.
- `chapterCount` and `chapters` (`ChapterRecord`, 0x20 bytes): `Inventory_RecordChapter` (`0x8d1830`) records each
  instrument: its index (0–7), `bits` (a `std::bitset<16>`: bit 0 the Shovel obtained, bit 1 the Bow obtained, bit 2
  `HeartPieces` bit 24, bit 3 + i Chamber Dungeon set i owned) and the rupees; it writes nothing else of the record.
- No access found to `+0x6`, `+0x29`..`+0x3f` and the records' `+0x8`..`+0x1f`: probably unused space.

**InventoryItems** (`SaveCurrent`, 0x160 bytes): `GlobalSave_Reset` clears 0x160 bytes, and `SaveCurrent_LoadKeepLocation` (`0xdf8850`) copies 0x160 bytes from a loaded save, keeping `LevelName`, `Locator`, `RestartPos` and bits 0–2 of `RestartDirection`. A new game sets `LevelName` `MarinTarinHouse`, `Locator` `Opening`, `Health` 12, `EquipY` and `EquipX` 0xff, and picks `GridPuzzle` and `TilePuzzle` at random.

| Field | |
|---|---|
| `GridPuzzle` `+0x86` | seven 2-bit direction codes and bit 15. A grid puzzle (strings `grid`, `gridConfig`; `GridPuzzle_Step` `0x8f4430`) compares each step's direction with code `(index & 0xf) * 2` and plays `BGM_NAZOTOKI_SEIKAI` once done; `0x909c60` shows code + 1 as an arrow glyph. Guess: the Signpost Maze |
| `TilePuzzle` `+0xa6` | five 3-bit values, one of 16 patterns; the ShiningTile / `Panel4_ShiningPanel` puzzle (`0xc5c130`, `0xc58f50`) compares its tile index with the field at `index * 3` |
| `MainItems` `+0xa8` | read and written as a u32 with `pad_aa`; no bit above 15 is used |
| `ExtraItems` `+0xac` | read and written as a u32 with `pad_af` (`0x8d0ea8`); bit 16 (ShellRader) is the highest used |
| `SleepyMushroomTime` `+0xb0` | `totalTimePlayed` when the Sleepy Toadstool was picked (`0x8d0ea8`); `0x8ca460` compares it plus 900 s |
| `WarpPoints` `+0xb8` | a bit per `WarpPoint.gsheet` `mIndex` (UI.md, Warp map) |
| `FishingLure` `+0xc1` | an event action sets its `type` − 1 (`0x993720`), `0xe1bbd0` sets 0, 1 or 2; the FishingLure actor (`0xb73330`) and the fishing play report's `RureType` (`0xdf42d0`) read it |
| `KeptItemKind` `+0xc3` | `KeptItem_Kind` (`0x9c5680`), the event actions TakeItem and ReturnItem; the table `0x16b3d70` maps it (`0x9c5d80`) |
| `FieldPosition` `+0xc4`, `IndoorPosition` `+0xd0` | `Inventory_StoreLinkPosition` (`0x8652b0`, from `LevelManager_LevelJump`) stores Link's position in the first when he leaves a level of type 3 and up (the overworld), else in the second (not while Player `sideView` is set); the overworld and warp maps place Link with the first, the dungeon map reads the second (`0xe74870`) |
| `Figurines` `+0xe8` | bits; `CranePrize_Has` reads `+0xe9` |
| `ZoneId` `+0xec` | `Scene_UpdateZone` (`0x836fa8`) writes the entered zone's id; the warp UI's fallback (`0xf8ea50`) reads it |
| `RapidsBestTime` `+0xf0` | milliseconds, written at `0xdf045c`; a condition tests it for 0 (`0xc689e0`), a message formats it (`0xc69120`), `0xdf2360` compares it × 1000 with a microsecond timer. Guess: the rapids race |
| `FishSize`, `FishCaught` | per fish kind; the record message takes the largest size over all kinds but 5 (`0xc68cf0`), a condition sums the catches over all but 5 (`0xc68920`) |
| `RestartDirection` `+0x121` | bits 0–2: a direction passed with `RestartPos` into level jumps (`<< 29`, `0x83a470`) |
| `HintIndex` `+0x123` | `Hint_NextRow` (`0xc93310`) goes round a datasheet's eligible rows with it (default `Level2SwordComplete`); guess: the hints |
| `Obtained` `+0x124` | `Inventory_HasObtained` (`0xdf8740`); AddItemID sets the bits (Adding and owning) |
| `LevelEnterTime` `+0x138` | `totalTimePlayed` at the current level's start (`SetCurrentLevel` `0xdfa450`); the event query `CompareCurrentLevelTime` (`EventQuery_CompareCurrentLevelTime` `0x800bd0`) holds while (`totalTimePlayed` − it) / 1e6 ≥ its `time` |
| `PreviousLevelTime` `+0x140` | microseconds spent in the level before this one: `SetCurrentLevel` sets it to `totalTimePlayed` − `LevelEnterTime` before it moves `LevelEnterTime`; the query `ComparePreviousLevelTime` (`EventQuery_ComparePreviousLevelTime` `0x800c50`) holds while it / 1e6 ≥ `time` |

No access found to `pad_aa` and `pad_af` but through their words, and none to `+0x148`..`+0x15f`.
