#pragma once
// The item popup (ap_popup.cpp): an item's model above Link's head while play goes on (Instant Pickups, received
// items).
#include "lib.hpp"
#include <string>
#include <vector>

namespace Game { struct Vec3; }

namespace AP::Popup {
    void InstallHooks();
    // Shows the item's model above Link's head for a moment; false when it cannot be made.
    bool Show(int itemId);
    // A model is loading or showing.
    bool Busy();
#ifdef LASAP_TEST
    // DBG popsway: whether the model turns from side to side (off: it faces the camera)
    void Sway(bool on);
    // DBG popframes: record the next `count` poses (time, frame counter, the parent's and the model's world
    // rotation), and the recorded ones as POPFRAME lines
    void RecordFrames(size_t count);
    std::vector<std::string> RecordedFrames();
#endif
    // The item model maker's task made this holder's model: the popup's, true.
    bool IsHolder(const void* holder);
    // The popup's model is an NPC model (HeldModelFix): that scale, that model point (model units) on the spot, and
    // that angle in the item-get's frame (degrees: x pitch, y yaw, z roll, applied roll first).
    void Fit(float scale, const Game::Vec3& center, const Game::Vec3& angle);
    // Once per tick.
    void Tick();
}
