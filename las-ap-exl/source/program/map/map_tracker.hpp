#pragma once
// Map tracker (map_tracker.cpp, DESIGN.md "Map tracker"): the pause map shows the slot's unchecked locations, and the
// whole overworld and every dungeon room are revealed.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>
#include <string_view>

namespace AP::MapTracker {
    // Reads the patcher's map file (MAP_PATH) and, when there is one, installs the tracker's hooks.
    void Load();
    // Answers the map tracker's event flags; false for any other flag.
    bool Flag(std::string_view name, bool& value);
    // The icon group a Place.msbt label names, or -1; the hover text for a group.
    int LabelGroup(std::string_view label);
    std::string LabelText(int group);
    // Each input poll: while the map's check list is open the game sees no buttons and no sticks (true then); buttons
    // held as it closes stay hidden from the game until released.
    bool ListTakesPad(nn::hid::NpadBaseState* state);
    // The text of the map's X button caption (UI:MapCaptionMemory), or "" for the game's own.
    std::string CaptionText();
    // 0 off, 1 on, 2 maps revealed without overworld icons (DBG map, for tests and screenshots)
    void SetMode(int mode);
    // A logic (INLOGIC) or checks (CHECKED) update arrived: the open map refreshes (Tick).
    void DataChanged();
    // Every room of the current dungeon counts as visited, and the open map refreshes once a frame passes after an
    // update; once per tick.
    void Tick();
#ifdef LASAP_TEST
    // The text handed to the map cursor for one of our labels (DBG map's name=).
    void NoteText(const std::string& text);
#endif
}
