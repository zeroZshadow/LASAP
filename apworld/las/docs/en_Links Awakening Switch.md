# Link's Awakening (Switch)

## Where is the options page?

The [player options page for this game](../player-options) contains all the options you need to configure and export a
config file.

## What does randomization do to this game?

Items Link normally finds on Koholint Island are shuffled into the multiworld. The logic is that of the
[LAS Randomizer](https://github.com/Owen-Splat/LAS-Randomizer), so the game can always be finished, though areas may be
needed in a different order than in the original game. The world needs the 2019 Switch release, game version 1.0.0.

## What is the goal?

Wake the Wind Fish.

## What items and locations get shuffled?

Chests, free gifts, golden leaves, heart pieces, seashells, the shop, boss drops and other item locations, each category
with its own option. The trade quest, Dampé, the rapids, fishing, blue rupees, owl statues and the Seashell Mansion
rewards can be added. Small keys, nightmare keys, maps, compasses, stone beaks and instruments can stay in their own
dungeon or go anywhere.

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
randomizer. Another player's item counts as important when it is progression.

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
revealed from the start, and an icon marks each spot with checks. Its look tells whether the items you have reach
all of its checks you have not done, some of them, none yet, or whether they are all done; hover it to see their
names. In dungeons the map shows every room and the chests (open once checked, also when a server command checked
them), and a chest icon in each room with other checks (key drops, owl statues, the boss) opens once they are done. The dungeon map, compass and stone beak are still items to
find: the side panel shows the ones you have.

## How do I get back to the start?

Hold Minus for a second: a menu asks "Return to Tarin's house?". Warp home takes Link into Tarin's house in Mabe
Village; Continue (or B) closes the menu. A short press of Minus still opens the map. The menu does not open during a
cutscene, while Link holds something, or in the Wind Fish's Egg, the Chamber Dungeons, the fishing pond and the trendy
game.
