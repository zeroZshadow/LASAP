#pragma once
// The current level (level.cpp): the name SetCurrentLevel last took, and warps.
#include "game/actor.hpp"
#include <string>

namespace LevelSystem {
    std::string GetCurrentLevel();
    // Counts SetCurrentLevel's calls: a level loading, from a warp or a save, has a number of its own
    u32 LoadCount();
    // Reloads the save in that level at that position (SetGameState 4), with OpTalked and ShieldGet set so that Link
    // can pause and collect rupees there.
    void LoadLevel(const char* level, Game::Vec3 pos);
}
