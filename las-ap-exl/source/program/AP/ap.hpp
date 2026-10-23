#pragma once
// Archipelago bridge, the game side (ap_game.cpp): applies received items, reports collected locations and runs the
// other features once per input poll on the game thread.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>

namespace AP {
    // Inventory bits (ap_memory.cpp): kind 'S' seashells, 'H' heart pieces, 'L' golden leaves, 'C' heart containers
    void LoadIndices();
    // The bits the seed's vanilla sources of a kind set when collected; received items take others.
    u64 VanillaBits(char kind);

    void Init(const std::string& seed_hash, int port);
    void Tick();
    void OnPlaceholderCollected(int local_id);
    // The game adds an item (Inventory::AddItemID): reports the check of a location that keeps this vanilla item.
    void OnItemAdded(int item_id, int index);
    void OnGoal();
    // The game shows the sign's text (SIGN_LABEL): kept in the save (SIGN_FLAG), reported as SIGN.
    void OnSignRead();
    // Checked in this game or, as the client reported, on the server.
    bool IsLocationChecked(int local_id);
    // Marked as skipped on the map by the player (map tracker); saved per seed, never reported.
    bool IsLocationSkipped(int local_id);
    void SetLocationSkipped(int local_id, bool skipped);
    // In logic for the items received, as the client reported (INLOGIC); HasLogic once it has.
    bool HasLogic();
    bool IsInLogic(int local_id);
    // The client is connected and logged into the slot (SERVER lines).
    bool IsServerConnected();
    void ApplyInjectedInput(nn::hid::NpadBaseState* state);
    void Log(const std::string& text);
    std::string HelloLine();
}
