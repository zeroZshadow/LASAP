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
    // (true: not for the AP client); any other line is mirrored to the debug clients as "AP <line>".
    bool Capture(const std::string& line);

    // Game thread (ap_game.cpp)
    void Tick();
    // Every input poll, before the game sees the state: the frame step, injected input, the trace.
    void ApplyInput(nn::hid::NpadBaseState* state);
    // DBG commands of this file; false when the command is not one of them.
    bool Command(const std::vector<std::string>& a);
    // " frame=<n> msg=<message window state>" for the STATE line.
    std::string StateFields();
    void InstallHooks();
}

namespace AP {
    // ap_game.cpp, for the test build
    bool HandleTestLine(const std::string& line);
    bool TestPlayerFree();
    bool TestItemDisplayPending();
    std::string TestScreenName();
}
#endif
