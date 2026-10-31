#pragma once
// Ids, names and paths the patcher shares with the module: each must match the patcher's.
#include "game/data/item_id.hpp"
#include "lib.hpp"
#include <string_view>

namespace AP {
    using Game::Data::ItemId;

    // Items.gsheet itemID of the placeholder that every randomized location gives.
    // Its AddItemID index is the location's local id.
    constexpr ItemId PLACEHOLDER_ITEM_ID{71};
    constexpr const char* PLACEHOLDER_KEY = "ArchipelagoItem";
    // Model variants of the placeholder (ArchipelagoItem_<item key>) take item ids that neither the game nor LASR
    // uses (LASR's Dampe and shop dummies take 63-70, its traps 127-131 and its trade items 200-202); the item-get
    // picks its model by id. They count as the placeholder too. Must match VARIANT_ITEM_IDS in the patcher.
    constexpr bool IsPlaceholderItem(ItemId item) {
        int id = static_cast<int>(item);
        return (id >= static_cast<int>(PLACEHOLDER_ITEM_ID) && id <= 99) || id == 103 || id == 107 || id == 109 ||
               id == 118 || id == 119 || (id >= 132 && id <= 199) || (id >= 203 && id <= 255);
    }
    // The game's TCP port; config.ini can set another ([archipelago] port), e.g. for a second modded game on one PC.
    constexpr int PORT = 43384;
    constexpr int PROTOCOL_VERSION = 2;
    // Message labels the patcher adds: the found placeholder's text (Place.msbt) and the received-item text
    // (System.msbt). Their text is replaced at runtime (ap_text.cpp).
    constexpr const char* FOUND_LABEL = "ArchipelagoItem";
    constexpr const char* RECEIVED_LABEL = "APReceived";
    // Place.msbt label of the sign at the foot of the Wind Fish's Egg's stairs, which names the instruments the egg
    // needs (patcher ap_goal.py). Showing it sets SIGN_FLAG (patcher: ap_goal.SIGN_FLAG) in the save and is reported
    // as SIGN.
    constexpr const char* SIGN_LABEL = "SBMountTamaranch";
    constexpr const char* SIGN_FLAG = "APSignRead";
    // The egg's event checks this flag when seashells are required (patcher ap_goal.SEASHELL_FLAG). It is no save flag:
    // the module answers it set while Link holds config.ini's [archipelago] seashells_required seashells.
    constexpr std::string_view SEASHELL_GOAL_FLAG = "APSeashellGoal";
    // Common.bfevfl entry the patcher adds: Link's item-get of the placeholder with RECEIVED_LABEL as its message
    // entry. Received items play as this event; the module swaps in the received item and its text.
    constexpr const char* ITEM_EVENT_ENTRY = "APItemGet";
    // Common.bfevfl entries the patcher adds for received traps: AP<trap key> plays LASR's effect of that trap.
    constexpr const char* TRAP_EVENT_PREFIX = "AP";
    // An item-get whose message entry starts with this mark plays as the game's own also with Instant Pickups; the
    // mark is dropped from the entry. The patcher marks the instruments' pedestals (patcher: NATIVE_ITEM_GET_MARK).
    constexpr char NATIVE_ITEM_GET_MARK = '!';
    // DeathLink event flags (patcher): the module sets DEATHLINK_FLAG with a received death, and the game over flow
    // clears it; the flow sets DIED_FLAG for any other death that stands, which the module reports.
    constexpr const char* DEATHLINK_FLAG = "APDeathLink";
    constexpr const char* DIED_FLAG = "APDied";
    // Secret Medicines received while Link holds one (it is a single inventory bit): a count in the event flags
    // MEDICINE_FLAG<i> (patcher: MEDICINE_FLAG, MEDICINE_FLAG_BITS), given one at a time once he has none.
    constexpr const char* MEDICINE_FLAG = "APMedicine";
    constexpr int MEDICINE_FLAG_BITS = 2;
    // Common.bfevfl entry the patcher adds for the warp menu: a choice between warping to Tarin's house and going on.
    constexpr const char* WARP_MENU_ENTRY = "APWarpMenu";
    // Index the ghost in Tarin's house adds bombs and arrows with (patcher ap_ghost.py): the count is what Link's
    // bombs or arrows are topped up to, not an amount to add.
    constexpr int AMMO_REFILL_INDEX = 1000;
    // Location names the patcher writes next to config.ini.
    constexpr const char* NAMES_PATH = "sd:/config/lasr-exl/ap_names.txt";
    // Per seashell, heart piece, golden leaf and heart container source: its bit and its location (ap_indices.cpp).
    constexpr const char* INDICES_PATH = "sd:/config/lasr-exl/ap_indices.txt";
    // AP locations that keep their vanilla item (the Trendy Game's prizes): "<item key> <index> <local id>" lines.
    constexpr const char* VANILLA_PATH = "sd:/config/lasr-exl/ap_vanilla.txt";
    // Item ids that show another item's model (the placeholder's variants, LASR's Dampe rewards): "<item id> <the
    // item's id>" lines; the item-get holds them up in that item's pose (ap_item_pose.cpp).
    constexpr const char* POSES_PATH = "sd:/config/lasr-exl/ap_poses.txt";
    // Per drop actor, its model's bounds: "<LEB key> <centre x> <y> <z> <radius>" lines (patcher ap_drop.py,
    // write_drops; ap_drop.cpp); the patcher's checks want one for every drop actor.
    constexpr const char* DROPS_PATH = "sd:/config/lasr-exl/ap_drops.txt";
    // In-game hints that become server hints when read: "<n> <flag> <slot> <location id>" lines (patcher ap_hints.py).
    constexpr const char* HINTS_PATH = "sd:/config/lasr-exl/ap_hints.txt";
    // The map tracker's icons, written by the patcher when the option is on (map_tracker.cpp).
    constexpr const char* MAP_PATH = "sd:/config/lasr-exl/ap_map.txt";
    // Event flag and Place.msbt label of an overworld icon: the prefix and the icon's group. Must match the patcher.
    constexpr const char* MAP_FLAG_PREFIX = "APMap";
    // The Place.msbt entry the dungeon map's dots are named with (patcher ap_map.DOT_LABEL)
    constexpr std::string_view MAP_DOT_LABEL = "APMapDot";
    // After the prefix, the icon's look other than in logic: partly in logic, out of logic, collected.
    constexpr const char* MAP_LOOK_MARKS = "PXC";
    // The drop actor (ap_drop.cpp): its actor id, an empty one of the game's actor table, and its name, which is
    // also its Npc.gsheet row (patcher: ap_drop.DROP_ACTOR_ID, DROP_ACTOR). Its LEB parameter 0 is how it appears
    // (patcher ap_drop.Motion): hops up out of the ground (a bush's, rock's or pot's), on an arc from what its first LEB
    // link names (a bonked tree's crown, a bombed hole), falls onto its spawn from above (a dungeon key, a golden
    // leaf), so again whenever Link enters its zone, hops out when dug up where it is buried (a dig spot), or hops out
    // where its boss's BossItemDrop starts the item, in place of it (so without standing when its room loads: the item
    // falls into a pit), shows where it stands (the ghost's reward), or so from the start (the blue rupees: the module
    // turns its switch 0 on when it is made); with Bouncy Items, out of the lid of the chest or off the top of the owl
    // statue its first LEB link names.
    constexpr u16 DROP_ACTOR_ID = 0xa7;
    constexpr const char* DROP_ACTOR = "ItemAPDrop";
    enum DropMotion {
        DROP_HOPS = 0, DROP_TREE = 1, DROP_HOLE = 2, DROP_FALLS = 3, DROP_REFALLS = 4, DROP_BURIED = 5, DROP_CHEST = 6,
        DROP_OWL = 7, DROP_BOSS = 8, DROP_BOSS_PIT = 9, DROP_STANDS = 10, DROP_PLACED = 11
    };
    // A drop's switches (patcher: ap_drop, the pops; DROP_OPENED). Pop lets it appear: a chest's or owl statue's
    // launch flag, a falling drop's pop event's, the boss's global flag. Got is set by taking it, and its row lays out
    // no drop once it is on. Opened, a chest's drop's only, holds its chest's opened flag, which taking the drop sets.
    enum class DropSwitch { Pop = 0, Got = 1, Opened = 2 };
}
