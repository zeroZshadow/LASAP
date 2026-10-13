#pragma once
// Archipelago bridge: a TCP line protocol (see DESIGN.md) between the game and the AP client.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>
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
    // Common.bfevfl entry the patcher adds: Link's item-get of the placeholder with RECEIVED_LABEL as its message
    // entry. Received items play as this event; the module swaps in the received item and its text.
    constexpr const char* ITEM_EVENT_ENTRY = "APItemGet";
    // Common.bfevfl entries the patcher adds for received traps: AP<trap key> plays LASR's effect of that trap.
    constexpr const char* TRAP_EVENT_PREFIX = "AP";
    // Common.bfevfl entry the patcher adds for the warp menu: a choice between warping to Tarin's house and going on.
    constexpr const char* WARP_MENU_ENTRY = "APWarpMenu";
    // Index the ghost in Tarin's house adds bombs and arrows with (patcher ap_ghost.py): the count is what Link's
    // bombs or arrows are topped up to, not an amount to add.
    constexpr int AMMO_REFILL_INDEX = 1000;
    // Location names the patcher writes next to config.ini.
    constexpr const char* NAMES_PATH = "sd:/config/lasr-exl/ap_names.txt";
    // The map tracker's icons, written by the patcher when the option is on (ap_map.cpp).
    constexpr const char* MAP_PATH = "sd:/config/lasr-exl/ap_map.txt";
    // Event flag and Place.msbt label of an overworld icon: the prefix and the icon's group. Must match the patcher.
    constexpr const char* MAP_FLAG_PREFIX = "APMap";

    // Network thread (ap_net.cpp)
    void StartNetwork(int port);
    bool IsClientConnected();
    void Send(const std::string& line);
    // Pops one received line into out. Returns false when the queue is empty.
    bool PopLine(std::string& out);
    // Puts a line back at the front so it is retried on a later frame.
    void PushFront(const std::string& line);
    // True once after each new client connection.
    bool TakeNewConnection();

    // Game side (ap_game.cpp)
    void Init(const std::string& seed_hash, int port);
    void Tick();
    void OnPlaceholderCollected(int local_id);
    void OnGoal();
    // Checked in this game or, as the client reported, on the server.
    bool IsLocationChecked(int local_id);
    void ApplyInjectedInput(nn::hid::NpadBaseState* state);
    void Log(const std::string& text);
    std::string HelloLine();

    // In-game text (ap_text.cpp)
    void InstallTextHooks();
    // What a location holds: item name and, for another player's item, that player's name.
    void SetLocationName(int local_id, const std::string& item, const std::string& player);
    bool HasLocationName(int local_id);
    std::string TextState();
    // The location of a placeholder just added; it names the found text until the player is free again.
    void SetFoundLocation(int local_id);
    void ExpireFoundLocation();
    void SetReceivedText(const std::string& item, const std::string& sender);
    void LoadLocationNames();
    // A short message in the autosave popup ("Now Saving"), which does not stop the game; '\n' starts a new line.
    void QueueToast(const std::string& text);
    // Shows the next toast once the popup is free; only in a level.
    void TickToasts(bool inLevel);

    // Map tracker (ap_map.cpp)
    void LoadMap();
    // Answers the map tracker's event flags; false for any other flag.
    bool MapFlag(std::string_view name, bool& value);
    // The icon group a Place.msbt label names, or -1; the hover text for a group.
    int MapLabelGroup(std::string_view label);
    std::string MapLabelText(int group);
    std::string MapState();
    void SetMapEnabled(bool enabled);
    // Every room of the current dungeon counts as visited; once per tick.
    void TickMap();
}
