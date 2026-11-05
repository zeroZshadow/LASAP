# Link's Awakening (Switch)

## Where is the options page?

The [player options page for this game](../player-options) contains all the options you need to configure and export a
config file.

## What does randomization do to this game?

The items Link finds on Koholint Island are shuffled into the multiworld, using the logic of the [LAS
Randomizer](https://github.com/Owen-Splat/LAS-Randomizer). The world needs the 2019 Switch release at version 1.0.0.

## What is the goal?

Wake the Wind Fish: play the Ballad at the egg on Mt. Tamaranch with `instruments_required` instruments (all eight by
default), then beat the Nightmare inside.

With `goal: seashell_hunt`, stand at the egg with `seashells_required` secret seashells (30 by default). It opens by
itself, and its door leads straight to the ending.

Either number can be left to chance (`random`, `random-range-3-6`). The sign at the foot of the egg's stairs tells it,
and trackers hide it until you have read the sign.

## What items and locations get shuffled?

Chests, free gifts, golden leaves, heart pieces, seashells, the shop, boss drops and a few standing items, each with its
own option. You can add the trade quest, Dampé, the Rapids Ride, fishing, the Color Dungeon's blue rupees, owl statues
and Seashell Mansion rewards.

An activity you leave out is optional: its rewards hold only junk and traps, and are in logic only after the goal. Any
other category you leave out keeps its items in place, but each spot is still a check. The Trendy Game's prizes are
never shuffled.

`small_keys`, `nightmare_keys`, `dungeon_items` (maps, compasses and stone beaks) and `instruments` set where those
items can be placed.

## Can dungeon entrances be shuffled?

Yes, with Shuffled Dungeons. Each entrance leads into a random dungeon, and its exit leads back out the same way.
Trackers show where a door leads only after you have been through it.

## What does the player start with?

By default, a sword, a shield, the ocarina with Manbo's Mambo, 100 rupees, and every dungeon's map, compass and stone
beak. Change this with `start_inventory_from_pool` and `starting_rupees`. `Heart Container: 2` starts you with five
hearts.

## Which items can be in another player's world?

Any shuffled item, including dungeon items and instruments when their options allow it.

## What does another world's item look like in Link's Awakening?

A matching Link's Awakening model if its name has one (any shield shows the shield), otherwise the Archipelago logo as
six colored balls. Picking it up names the item and its owner. With `chest_appearance`, a chest's size and texture show
what it holds.

## When the player receives an item, what happens?

Link holds it up and its text names the sender, once he is free. With Instant Pickups, items show as a popup while you
keep playing: `incoming` for received items, `everything` for your finds too.

A trap plays its effect and names its sender. On normal damage it never takes your last quarter heart.

## Where do I get more bombs and arrows?

With the Ammo Ghost option (on by default), a ghost in Tarin's house tops your bombs, arrows and powder up to 15. He
shows up once you have one of them.

## Where are my checks?

With the Map Tracker option (on by default), the pause map marks each spot with a dot:

- Green: your items reach all its remaining checks.
- Half green, half red: some of them.
- Red: none yet.
- Grey with a tick: done.
- Gold ring: a server hint names a check there.

| Button | Action |
|---|---|
| Cursor on a dot | Shows its name and, for several checks, how many are in logic ("3/7 Fishing Hole"), plus any hinted items |
| A on a dot | Skips its one check, or lists its several checks to skip from. Skips stay on your map only |
| X | Hides or shows red dots |

"Offline" next to X means the client is not connected, so the colors may be out of date. With Shuffled Dungeons, a
door's dot reads "Unknown dungeon" until you have been inside.

## How do I get back to the start?

Hold Minus for a second, then choose Warp home to return to Mabe Village. This does not work during a cutscene, while
Link holds something, or in the Wind Fish's Egg, Chamber Dungeons, fishing pond, Trendy Game or ending.

## How do the in-game hints work?

Owl statues and library books give hints about your items (option In-Game Hints). Dungeon owl statues need that
dungeon's stone beak. With `server` (the default), hints that name a location also show in the server's hint list, at no
hint point cost. Hint Distribution picks the mix:

- `precise`: the location of one of your progression items.
- `balanced` (default): long checks, areas you need, areas nobody needs, areas holding your items, and precise hints.
- `vague`: mostly areas, with few precise hints.

## Will I see the other players?

Yes. The other Link's Awakening players in your multiworld show as see-through Links when they are in your area. They
can't touch anything. No setup is needed.

## What is the Jukebox?

A pause menu page after System that plays the game's music in place of the area's own.

| Button | Action |
|---|---|
| A | Plays the track, or stops it if it is playing |
| X / Y | Shuffle / Repeat one |
| ZR | Mutes all music |
| D-pad Left / Right, in play | Changes the track |

Loading a save turns it off.

## Which options change how the game plays?

The group "Gameplay": damage (none, normal or one hit kills), 20% faster walking and swimming (on by default), 360°
movement, skipped song cutscenes, instant text, Instant Pickups, a hookshot that grabs any surface, no Guardian Acorns
or Pieces of Power, easy fishing (on by default), and random enemies and enemy sizes.

These are options so that a seed plays the same for everyone. How the game looks and sounds is set in the client.

## How does DeathLink work?

Link's deaths reach the other DeathLink players, and theirs kill Link once he is free. Tracy's Secret Medicine revives
him from either and is used up, and a death it saves you from sends nothing.
