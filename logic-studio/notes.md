# Logic rebuild notes

- Entrance shuffle: the passage between `RichardHouse_01A` and `RichardCave_02A` (`richard-house` <> `richard-cave`, `golden-leaf:5`) cannot be shuffled, on either side.
- Entrance shuffle: `RichardCave_01B` <> `Field_13G` (`pothole-field`) cannot be shuffled: falling into the holes at `pothole-field` always lands in `RichardCave_01B` or `RichardCave_01C`.

## Higher levels to bring over

From the workspace this one replaced (`logic-studio/workspace.yml` in git history), for the zones built so far.

Dungeons: the Advanced and Glitched rules on dungeon checks, rooms and enemies move as they are.

D3's tricks were on rooms that the zones replace, so each needs a place:

- `D3-east-ledge` Advanced `bombs & hookshot`: `hookshot` instead of `dash-jump` on `D3.w` > `D3.x`.
- `D3-4C` > `D3-8G` Advanced `key-D3:8 & boots & kill-pairodd`: `boots` without `feather` through the side-view passage `D3.2d-a` > `D3.2d-b`.
- `D3-4C` > `D3-8G` Glitched `dash-jump & bombs & kill-pairodd`: the boss side with no key beyond the 4 for the central area.

D6's Glitched tricks, on the rooms the zones replace (03A is `D6.al`, 04H `D6.z`, 05D `D6.l`, 08D `D6.u`):

- `D6-far-northwest` Glitched `taltal-east-2 & rooster`: `D6.a` from the overworld above.
- `D6-pot-chest` and `D6-owl-statue-canal` Glitched `D6-4H & feather & bombs & bracelet` (the owl also `stone-beak-D6`): `D6.f` (03G) from `D6.z`.
- `D6-3A` <> `D6-5D` Glitched `feather & (bombs | (boots & sword) | (boots & rod) | (boots & shoot-arrows))`: `D6.al` <> `D6.l`.
- `D6-8D` > `D6-4H` Glitched `bracelet:2 & feather & bombs`: `D6.u` > `D6.z`.

Move as they are:

| Rule | Level | Goes on |
|---|---|---|
| `pond-island` | Advanced | same check |
| `swamp-chest` | Glitched, Hell | same check |
| `woods-south-chest` | Hell | same check |
| `plains` > `pothole` | Glitched | `prairie` > `pothole-field` |
| `plains` > `rapids` | Hell | `prairie` > `rapids` |
| `swamp` > `plains` | Hell | `swamp` > `prairie` |
| `mr-write-house` > `taltal-west-2` | Glitched | `goponga-west` > `taltal-west-2` |
| `swamp` > `bottle-grotto` | Hell | `swamp` > `bottle-grotto-porch` |
| `plains` > `key-cavern` | Glitched | door `Field_12F` |

One choice each:

- `wasteland` > `kanalet-outside` (Glitched) and `wasteland` > `taltal-east-0` (Advanced, Glitched): start from `goponga-central` or `graveyard-east`.
- `swamp` > `sign-maze` (Glitched, Hell): `frog-maze`, if that is the same place.
- `swamp` > `dream-shrine` (Hell): the porch or the interior.

Need a new place:

- `woods` > `swamp` Advanced `hookshot & break-bush & (sword | shovel | boots)`: no direct `woods` <> `swamp` route exists.
- `woods-north-chest` Advanced `hookshot & (sword | shovel | boots) & break-bush`: the chest is in `woods-racoon`, and a check's rule only counts once its zone is reached. With the route above it looks like one trick, possibly a single Advanced route `woods` > `woods-racoon`.
- `swamp` > `mr-write-house` Hell `mabe`: `goponga-west` connects to `woods-north`, not `swamp`.

Hell rules on `swamp` routes that name `mabe` may fit better as routes starting at `mabe`.
