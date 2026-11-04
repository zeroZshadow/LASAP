#pragma once
// Archipelago bridge, the game side (ap_game.cpp): applies received items, reports collected locations and runs the
// other features once per input poll on the game thread.
#include "ap/location_id.hpp"
#include "game/data/item_id.hpp"
#include "lib.hpp"
#include "nn/hid.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace AP {
    using Game::Data::ItemId;

    // Inventory bits (ap_indices.cpp): kind 'S' seashells, 'H' heart pieces, 'L' golden leaves, 'C' heart containers,
    // 'B' bottles, 'P' chamber stones
    void LoadIndices();
    // The bits the seed's vanilla sources of a kind set when collected; received items take others.
    u64 VanillaBits(char kind);

    void Init(const std::string& seed_hash, int port);
    void Tick();
    void OnPlaceholderCollected(LocationId location);
    // The game adds an item (Inventory::AddItemID): reports the check of a location that keeps this vanilla item.
    void OnItemAdded(ItemId item, int index);
    void OnGoal();
    // The game shows the sign's text (SIGN_LABEL): kept in the save (SIGN_FLAG), reported as SIGN.
    void OnSignRead();
    // Link holds the seashells the seashell hunt takes (config.ini's [archipelago] seashells_required).
    bool GoalSeashellsHeld();
    // Checked in this game or, as the client reported, on the server.
    bool IsLocationChecked(LocationId location);
    // Marked as skipped on the map by the player (map tracker); saved per seed, never reported.
    bool IsLocationSkipped(LocationId location);
    void SetLocationSkipped(LocationId location, bool skipped);
    // In logic for the items received, as the client reported (INLOGIC); HasLogic once it has.
    bool HasLogic();
    bool IsInLogic(LocationId location);
    // A dungeon location whose dungeon's door is reachable for the items received, as the client reported (DOORS).
    bool IsDoorOpen(LocationId location);
    // Holds a server hint not yet found, as the client reported (HINTED).
    bool IsHinted(LocationId location);
    // The client is connected and logged into the slot (SERVER lines).
    bool IsServerConnected();
    // Where a level jump from level `from` to that level and locator leads instead (the seed file's jumps; ap_jumps.cpp): a
    // shuffled dungeon's door, exit or warp, or the seashell hunt's egg door. None: where it was going.
    std::optional<std::pair<std::string_view, std::string_view>> LeadJump(std::string_view from, std::string_view level,
                                                                          std::string_view locator);
    void ApplyInjectedInput(nn::hid::NpadBaseState* state);
    void Log(const std::string& text);
    std::string HelloLine();
}
