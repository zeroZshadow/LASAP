# Link's Awakening (Switch)

## Where is the options page?

The [player options page for this game](../player-options) contains all the options you need to configure and export a
config file.

## What does randomization do to this game?

Items Link normally finds on Koholint Island are shuffled into the multiworld. The logic is that of the
[LAS Randomizer](https://github.com/Owen-Splat/LAS-Randomizer), so the game can always be finished, though areas may be
needed in a different order than in the original game. The world needs the 2019 Switch release, game version 1.0.0.

## What is the goal?

Wake the Wind Fish: play the Ballad of the Wind Fish at the egg on Mt. Tamaranch, then beat the Nightmare inside.
The egg opens with `instruments_required` of the eight instruments (all eight by default); instruments you start
with count toward it. The sign at the foot of the egg's stairs tells how many, so the number can be random
(`random`, or `random-range-3-6` for a number from 3 to 6).

## What items and locations get shuffled?

Chests, free gifts, golden leaves, heart pieces, seashells, the shop, boss drops and other item locations, each category
with its own option. The trade quest, Dampé, the Rapids Ride (its race and the items on its islands), fishing, blue
rupees, owl statues and the Seashell Mansion rewards can be added. A category left unshuffled keeps its items in their places. Where the logic may need such an
item (a song, a key, an instrument, a trade item), the spot is still a check: it can be hinted, and the item arrives
from the server when you pick it up. The rest (rupees, heart pieces, seashells while the Seashell Mansion is off) are
not checks and are never needed. The Trendy Game's prizes are never shuffled; its Yoshi Doll, which starts the trade
quest, is a check. Saving Bow-Wow and reviving the rooster are checks too. Small keys, nightmare keys, maps, compasses, stone beaks and instruments can stay in their own
dungeon or go anywhere.

## Can dungeon entrances be shuffled?

With the Shuffled Dungeons option, each dungeon's entrance leads into a random dungeon, the Color Dungeon included,
and that dungeon's exit leads back out of the same entrance. The dungeon markers on the overworld map move with them,
and the logic follows each door. The spoiler log lists where each entrance leads.

## What does the player start with?

By default, a sword, a shield, the ocarina with Manbo's Mambo, and 100 rupees, as in the original randomizer. The
`start_inventory_from_pool` option lists the starting items (they leave the item pool) and `starting_rupees` sets the
rupees. With `dungeon_items: start_with`, the player also starts with every dungeon's map, compass and stone beak.

## Which items can be in another player's world?

Any shuffled item, including dungeon items and instruments when their options allow it.

## What does another world's item look like in Link's Awakening?

A location shows the model of the item it holds. Items from other games show a Link's Awakening model when their name
matches one (any shield shows the shield, any bomb bag the bombs); other items show the Archipelago logo as six colored
balls. Picking one up names the item and whose it is.

With `chest_appearance`, a chest's size, and optionally its texture, shows what it holds, as in the original
randomizer; with textures, chests holding a seashell are blue. Another player's item counts as important when it is
progression.

## When the player receives an item, what happens?

Link holds the item up with its usual item text, naming the player who sent it. Items arriving during a cutscene or a
menu wait until Link is free. A trap plays its effect from the original randomizer (a shock, a squish, ...), and a small
popup at the bottom right names the trap and its sender. On normal damage a trap never takes the last quarter heart.

## Where do I get more bombs and arrows?

With the Ammo Ghost option (on by default), a ghost waits in Tarin's house once you have bombs or the bow. Talk to him
and he tops your bombs and arrows up to 15 (10 bombs become 15; more than 15 stay as they are), as often as you
like. Together with the warp menu below, this keeps you from getting stuck without ammo.

## Where are my checks?

With the Map Tracker option (on by default), the map in the pause menu shows them. The whole overworld map is
revealed from the start, and a dot marks each spot with checks, a larger one where there are several: green when
the items you have reach all of its checks you have not done, yellow for some of them, red for none yet, grey with a
tick once they are all done. A dot beside each dungeon's entrance does the same for the checks inside. Hover a dot to
see the checks' names. Only checks your options shuffle get a dot. X on the map hides or shows the red dots (it
replaces Memories while the Map Tracker option is on). "Offline" next to X means the client is not connected to the
server: the colours are then those of the last connection, or all green before the first one. In dungeons the map shows every room and the chests (open once checked, also when a server command checked
them), and a chest icon in each room with other checks (key drops, owl statues, the boss) opens once they are done. The dungeon map, compass and stone beak are still items to
find: the side panel shows the ones you have.

## How do I get back to the start?

Hold Minus for a second: a menu asks "Return to Tarin's house?". Warp home takes Link to Mabe Village, walking out
of Tarin's house; Continue (or B) closes the menu. A short press of Minus still opens the map. The menu does not open during a
cutscene, while Link holds something, or in the Wind Fish's Egg, the Chamber Dungeons, the fishing pond and the trendy
game.

## Which options change how the game plays?

The group "Gameplay": the damage Link takes (none, normal or one hit kills), walking and swimming 20% faster (on by
default), 360° movement, skipping the song cutscenes, text that shows a page at once, a hookshot that grabs any
surface (the logic does not count on it), no Guardian Acorns or Pieces of Power, easy fishing: the line never
breaks and Ol' Baron bites more often (on by default), and random enemies and enemy sizes, which change each time a
room loads (enemies you must defeat to go on keep their kind). They are options rather than client settings so that a seed
plays the same for everyone in it. How the game looks and sounds (blur, music, shuffled dialogue and lighting, the
low-health beep) is set in the client.

## How does DeathLink work?

With the DeathLink option on, Link's deaths reach the other DeathLink players, and theirs kill Link. A death that
Tracy's Secret Medicine saves sends nothing. A death from another player waits until Link is free (not in a cutscene, a
conversation, an item display or a menu), shows who caused it, and kills him even with the Secret Medicine, which he
keeps. Deaths that arrive before a save is loaded are ignored.
