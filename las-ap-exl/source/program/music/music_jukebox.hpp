#pragma once
#include "lib.hpp"
#include <string>
#include <vector>

// The Jukebox page (music_jukebox.cpp, DESIGN.md "Jukebox page"): the pause menu's fourth page as a mode of System's slot.
namespace AP::JukeboxPane {
    void InstallHooks();
    // Once a tick: the d-pad's left and right choose the track before and after while the player is free.
    void Tick(bool free);
#ifdef LASAP_TEST
    // STATE's " pane=<0-3|-> navi=<header state> jbox=<cursor>/<top>"
    std::string StateField(bool paused);
    // The header's state (1 Open, 2 WaitKey, ...), "-" with no header: the test condition navi=<state>
    std::string NaviState();
    // DBG jukebox
    std::string Info();
#endif
}
