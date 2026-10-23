#pragma once
// Items added to the inventory (inventory.cpp): the module's own handling of each item id.
#include <string>
#include <unordered_map>

namespace Inventory {
    // The items' Items.gsheet names by item id
    extern const std::unordered_map<int, std::string> kItemNames;
    // Adds an item as the game's AddItemID does, through the module's handling.
    void AddItemID(int id, int count, int index);
}
