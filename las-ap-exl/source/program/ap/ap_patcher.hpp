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
    // The Archipelago model, which the placeholder shows (patcher ap_model.py: FILE_NAME, MODEL_NAME)
    constexpr const char* PLACEHOLDER_MODEL_FILE = "ItemArchipelago.bfres";
    constexpr const char* PLACEHOLDER_MODEL_NAME = "ShellRader";
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
    // Index the ghost in Tarin's house adds bombs, arrows and powder with (patcher ap_ghost.py): the count is what
    // Link's bombs, arrows or powder are topped up to, not an amount to add.
    constexpr int AMMO_REFILL_INDEX = 1000;
    // The seed file next to config.ini (patcher seed_file.py; config/seed_file.hpp): the per-seed tables, one section
    // each, whose lines the loaders name (DESIGN.md, Seed file).
    constexpr const char* SEED_PATH = "sd:/config/lasr-exl/ap_seed.txt";
    // A location's actors name this and its local id as their model file and model name (patcher MODEL_TOKEN).
    constexpr std::string_view MODEL_TOKEN = "ap:";
    // A location's chests name this and its local id as their model file (patcher CHEST_TOKEN); without a C line
    // they are the plain chest.
    constexpr std::string_view CHEST_TOKEN = "apchest:";
    constexpr const char* PLAIN_CHEST_FILE = "ObjTreasureBox.bfres";
    // Set once the starting items are given (patcher: START_ITEMS_FLAG). The start flags are set while START_MARKER is
    // unset, which is one of them (patcher: START_MARKER).
    constexpr const char* START_ITEMS_FLAG = "APStartItems";
    constexpr const char* START_MARKER = "FirstClear";
    // Message label (Hint.msbt for owl statues, System.msbt for books) and event flag of source n: the prefix and n.
    // Must match the patcher (ap_hints.FLAG_PREFIX).
    constexpr std::string_view HINT_PREFIX = "APHint";
    // Event flag and Place.msbt label of an overworld icon: the prefix and the icon's group. Must match the patcher.
    constexpr const char* MAP_FLAG_PREFIX = "APMap";
    // The Place.msbt entry the dungeon map's dots are named with (patcher ap_map.DOT_LABEL)
    constexpr std::string_view MAP_DOT_LABEL = "APMapDot";
    // After the prefix, the icon's look other than in logic: partly in logic, out of logic, collected.
    constexpr const char* MAP_LOOK_MARKS = "PXC";
    // The drop actor (ap_drop.cpp; DESIGN.md, Drop actor): its actor id, an empty one of the game's actor table, and
    // its name, which is also its Npc.gsheet row (patcher: ap_drop.DROP_ACTOR_ID, DROP_ACTOR).
    constexpr u16 DROP_ACTOR_ID = 0xa7;
    constexpr const char* DROP_ACTOR = "ItemAPDrop";
    // LEB parameter 0: how the drop appears (patcher ap_drop.Motion; DESIGN.md, Drop actor, Appearing).
    enum DropMotion {
        DROP_HOPS = 0,       // up out of the ground: a bush's, rock's or pot's
        DROP_TREE = 1,       // on an arc from the crown of the tree its first LEB link names
        DROP_HOLE = 2,       // so from the bombed hole its first LEB link names
        DROP_FALLS = 3,      // onto its spawn from above: a dungeon key, a golden leaf
        DROP_REFALLS = 4,    // so, and again whenever Link enters its zone
        DROP_BURIED = 5,     // hops out when dug up: a dig spot
        DROP_CHEST = 6,      // Bouncy Items: out of the lid of the chest its first LEB link names
        DROP_OWL = 7,        // Bouncy Items: off the top of the owl statue its first LEB link names
        DROP_BOSS = 8,       // hops out where its boss's BossItemDrop starts the item, in place of it
        DROP_BOSS_PIT = 9,   // so, but standing nowhere when its room loads: the item falls into a pit
        DROP_STANDS = 10,    // shows where it stands: the ghost's reward
        DROP_PLACED = 11     // so from the start, its switch 0 turned on by the module when it is made: the blue rupees
    };
    // A drop's switches (patcher: ap_drop, the pops; DROP_OPENED). Pop lets it appear: a chest's or owl statue's
    // launch flag, a falling drop's pop event's, the boss's global flag. Got is set by taking it, and its row lays out
    // no drop once it is on. Opened, a chest's drop's only, holds its chest's opened flag, which taking the drop sets.
    enum class DropSwitch { Pop = 0, Got = 1, Opened = 2 };
}
