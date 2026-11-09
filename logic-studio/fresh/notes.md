# Logic rebuild notes

- `pothole-north`: the Basic rule `((rooster | feather) & (boomerang | rod)) | (feather & sword)` doesn't feel right. To review.

## Higher levels to bring over

From `logic-studio/workspace.yml`, for the overworld zones built so far.

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
