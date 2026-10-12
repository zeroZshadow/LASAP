#pragma once
// Archipelago bridge: a TCP line protocol (see DESIGN.md) between the game and the AP client.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>

namespace AP {
    // Items.gsheet itemID of the placeholder that every randomized location gives.
    // Its AddItemID index is the location's local id.
    constexpr int PLACEHOLDER_ITEM_ID = 71;
    constexpr int PORT = 43384;
    constexpr int PROTOCOL_VERSION = 1;

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
}
