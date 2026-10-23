#pragma once
// Test build only (LASAP_TEST, scripts/build_exl.sh): the debug port, the per-frame trace, frame-exact and state-gated
// input, and the DBG commands. Release builds compile none of it.
#ifdef LASAP_TEST
#include "lib.hpp"
#include "nn/hid.h"
#include <string>
#include <vector>

namespace AP::Test {
    // The debug port is the game port + this.
    constexpr int PORT_OFFSET = 100;

    // Net thread (test_port.cpp)
    void NetStart(int port);
    void NetPoll();
    // AP::Send hands every line here first: a reply to a debug command goes to the debug client that sent it
    // (true: not for the AP client). A line for the AP client (CHECK, RECV, ...) goes to the client also when a debug
    // command caused it (false); the debug client whose command it was gets it as its reply, the others as "AP <line>".
    bool Capture(const std::string& line);

    // Game thread: the debug clients' lines, once per tick (test_port.cpp).
    void Tick();
    // Every input poll, before the game sees the state: the frame step, injected input, the trace (test_input.cpp).
    void ApplyInput(nn::hid::NpadBaseState* state);
    // DBG <command> (test_commands.cpp), split on spaces. Returns false if the command must wait for the player.
    bool Command(const std::vector<std::string>& a);
    // Enemy randomizer (Hooks/Randomizer/enemies.cpp), from the thread loading a room: an enemy it looked at, its
    // LEB actor id before and after, and its scale (0 when kept).
    void OnEnemy(u16 vanilla, u16 now, float scale);
    void InstallHooks();
}
#endif
