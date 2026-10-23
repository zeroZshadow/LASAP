#pragma once
// The current level (level.cpp): the name SetCurrentLevel last took, and warps.
#include "Game/actor.hpp"
#include <string>

namespace LevelSystem {
    std::string GetCurrentLevel();
    // Reloads the save in that level at that position (SetGameState 4), with OpTalked and ShieldGet set so that Link
    // can pause and collect rupees there.
    void LoadLevel(const char* level, Game::Vec3 pos);
}
