#pragma once
// Archipelago bridge: a TCP line protocol (see DESIGN.md) between the game and the AP client.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>

namespace AP {
    // Items.gsheet itemID of the placeholder that every randomized location gives.
    // Its AddItemID index is the location's local id.
    constexpr int PLACEHOLDER_ITEM_ID = 71;
    constexpr const char* PLACEHOLDER_KEY = "ArchipelagoItem";
    // Model variants of the placeholder (ArchipelagoItem_<item key>) take item ids that neither the game nor LASR
    // uses (LASR's Dampe and shop dummies take 63-70); the item-get picks its model by id. They count as the
    // placeholder too. Must match VARIANT_ITEM_IDS in the patcher.
    constexpr bool IsPlaceholderItem(int id) {
        return (id >= PLACEHOLDER_ITEM_ID && id <= 99) || id == 103 || id == 107 || id == 109 || id == 118 || id == 119;
    }
    constexpr int PORT = 43384;
    constexpr int PROTOCOL_VERSION = 2;
    // Message labels the patcher adds: the found placeholder's text (Place.msbt) and the received-item text
    // (System.msbt). Their text is replaced at runtime (ap_text.cpp).
    constexpr const char* FOUND_LABEL = "ArchipelagoItem";
    constexpr const char* RECEIVED_LABEL = "APReceived";

    // Network thread (ap_net.cpp)
    void StartNetwork();
    bool IsClientConnected();
    void Send(const std::string& line);
    // Pops one received line into out. Returns false when the queue is empty.
    bool PopLine(std::string& out);
    // Puts a line back at the front so it is retried on a later frame.
    void PushFront(const std::string& line);
    // True once after each new client connection.
    bool TakeNewConnection();

    // Game side (ap_game.cpp)
    void Init(const std::string& seed_hash);
    void Tick();
    void OnPlaceholderCollected(int local_id);
    void OnGoal();
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
}
