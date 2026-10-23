#pragma once
// Ids, names and paths the patcher shares with the module: each must match the patcher's.
#include "lib.hpp"
#include <string_view>

namespace AP {
    // Items.gsheet itemID of the placeholder that every randomized location gives.
    // Its AddItemID index is the location's local id.
    constexpr int PLACEHOLDER_ITEM_ID = 71;
    constexpr const char* PLACEHOLDER_KEY = "ArchipelagoItem";
    // Model variants of the placeholder (ArchipelagoItem_<item key>) take item ids that neither the game nor LASR
    // uses (LASR's Dampe and shop dummies take 63-70, its traps 127-131 and its trade items 200-202); the item-get
    // picks its model by id. They count as the placeholder too. Must match VARIANT_ITEM_IDS in the patcher.
    constexpr bool IsPlaceholderItem(int id) {
        return (id >= PLACEHOLDER_ITEM_ID && id <= 99) || id == 103 || id == 107 || id == 109 || id == 118 ||
               id == 119 || (id >= 132 && id <= 199) || (id >= 203 && id <= 255);
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
    // Common.bfevfl entry the patcher adds: Link's item-get of the placeholder with RECEIVED_LABEL as its message
    // entry. Received items play as this event; the module swaps in the received item and its text.
    constexpr const char* ITEM_EVENT_ENTRY = "APItemGet";
    // Common.bfevfl entries the patcher adds for received traps: AP<trap key> plays LASR's effect of that trap.
    constexpr const char* TRAP_EVENT_PREFIX = "AP";
    // An item-get whose message entry starts with this mark plays as the game's own also with Instant Pickups; the
    // mark is dropped from the entry. The patcher marks the instruments' pedestals (patcher: NATIVE_ITEM_GET_MARK).
    constexpr char NATIVE_ITEM_GET_MARK = '!';
    // DeathLink event flags (patcher): the module sets DEATHLINK_FLAG with a received death, so the game over flow
    // skips the Secret Medicine and clears it; the flow sets DIED_FLAG for any other death, which the module reports.
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
    // Per seashell, heart piece, golden leaf and heart container source: its bit and its location (ap_memory.cpp).
    constexpr const char* INDICES_PATH = "sd:/config/lasr-exl/ap_indices.txt";
    // AP locations that keep their vanilla item (the Trendy Game's prizes): "<item key> <index> <local id>" lines.
    constexpr const char* VANILLA_PATH = "sd:/config/lasr-exl/ap_vanilla.txt";
    // In-game hints that become server hints when read: "<n> <flag> <slot> <location id>" lines (patcher ap_hints.py).
    constexpr const char* HINTS_PATH = "sd:/config/lasr-exl/ap_hints.txt";
    // The map tracker's icons, written by the patcher when the option is on (ap_map.cpp).
    constexpr const char* MAP_PATH = "sd:/config/lasr-exl/ap_map.txt";
    // Event flag and Place.msbt label of an overworld icon: the prefix and the icon's group. Must match the patcher.
    constexpr const char* MAP_FLAG_PREFIX = "APMap";
    // The Place.msbt entry the dungeon map's dots are named with (patcher ap_map.DOT_LABEL)
    constexpr std::string_view MAP_DOT_LABEL = "APMapDot";
    // After the prefix, the icon's look other than in logic: partly in logic, out of logic, collected.
    constexpr const char* MAP_LOOK_MARKS = "PXC";
}
