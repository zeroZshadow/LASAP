#pragma once
// Multiplayer ghosts, the puppets (ghost_puppets.cpp, DESIGN.md "Ghosts"): each ghost slot shown as an NPC drawn with
// Link's model.

namespace AP::Puppets {
    void InstallHooks();
    // Once per tick, after GhostNet::Tick.
    void Tick();
}
