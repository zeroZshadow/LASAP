#pragma once
// Test build only (LASAP_TEST, scripts/build_exl.sh): the debug port, the per-frame trace and frame-exact,
// state-gated input. Release builds compile none of it.
#ifdef LASAP_TEST
#include "lib.hpp"
#include "nn/hid.h"
#include <string>
#include <vector>

namespace AP::Test {
    // The debug port is the game port + this.
    constexpr int PORT_OFFSET = 100;

    // Net thread (ap_net.cpp)
    void NetStart(int port);
    void NetPoll();
    // AP::Send hands every line here first: a reply to a debug command goes to the debug client that sent it
    // (true: not for the AP client). A line for the AP client (CHECK, RECV, ...) goes to the client also when a debug
    // command caused it (false); the debug client whose command it was gets it as its reply, the others as "AP <line>".
    bool Capture(const std::string& line);

    // Game thread (ap_game.cpp)
    void Tick();
    // Every input poll, before the game sees the state: the frame step, injected input, the trace.
    void ApplyInput(nn::hid::NpadBaseState* state);
    // DBG commands of this file; false when the command is not one of them.
    bool Command(const std::vector<std::string>& a);
    // Enemy randomizer (Hooks/Randomizer/enemies.hpp), from the thread loading a room: an enemy it looked at, its
    // LEB actor id before and after, and its scale (0 when kept).
    void OnEnemy(u16 vanilla, u16 now, float scale);
    // " frame=<n> msg=<message window state>" for the STATE line.
    std::string StateFields();
    void InstallHooks();
}

namespace AP::Puppet {
    // DBG puppet (ap_puppet.cpp)
    bool Command(const std::vector<std::string>& a);
    // STATE puppets=<ghosts drawn, their model loaded>/<ghost slots with an actor>
    std::string StateField();
}

namespace AP {
    // ap_game.cpp, for the test build
    bool HandleTestLine(const std::string& line);
    bool TestPlayerFree();
    bool TestItemDisplayPending();
    bool TestPlayerCarrying();
    std::string TestScreenName();
}
#endif
