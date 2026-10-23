#pragma once
// The test build's parts, shared by the test_*.cpp files, and the probes of the features whose state they read.
#ifdef LASAP_TEST
#include "test/test.hpp"
#include <atomic>
#include <string>
#include <vector>

namespace AP::Test {
    // ---- The debug port (test_port.cpp) ----

    // A line to one debug client, or to all.
    void Push(int client, const std::string& line);
    void Broadcast(const std::string& line);
    // The debug client whose command the game thread is handling, -1 for none.
    int ReplyTo();

    // ---- Game state and the trace (test_trace.cpp) ----

    std::string Hex(u64 v);
    // Game thread only: whether memory there can be read.
    bool Readable(uintptr_t addr, size_t len);
    extern u32 g_FrameNo;          // frames counted here (the framework counter stalls while the game is paused)
    extern std::atomic<u32> g_Passes;  // passes of the framework's main loop, one a frame
    // A "T <ms> <frame> <what>" line to the debug clients while DBG trace is on.
    void Trace(const std::string& what);
    // A traced field's value (screen, level, event, ...; TESTING.md), "" for an unknown one; all as " key=value".
    const std::string& FieldValue(const char* key);
    std::string AllFields();
    // Each frame, before the ops step: the fields, the UI state machines and the watches.
    void Sample();
    // Conditions (TESTING.md, Conditions), joined with '+', each negated by a leading '!'.
    bool Holds(const std::string& cond);
    // A UI machine of that label or hook name in that state (by name).
    bool MachineIn(const std::string& label, const std::string& state);
    // The open message waits for a button and takes one now; a choice is open and takes buttons now; its cursor.
    bool TextReady();
    bool ChoiceReady();
    int ChoiceCursor();
    std::string MessageState();
    // STATE's " frame=<n> msg=<message window state> ..."
    std::string TraceStateFields();
    // DBG trace, ui, watch, holds, frame, memq and mem; false for any other command.
    bool TraceCommand(const std::vector<std::string>& a);

    // ---- Input ops (test_input.cpp) ----

    // DBG input, press, advance, hold, choose, tapuntil, release, interact, guard and await; false for any other.
    bool InputCommand(const std::vector<std::string>& a);
}

namespace AP::Puppets {
    // ---- DBG puppet (test_puppets.cpp) ----

    // STATE puppets=<ghosts drawn, their model loaded>/<ghost slots with an actor>
    std::string StateField();
    // DBG puppet; false for any other command.
    bool Command(const std::vector<std::string>& a);
}

namespace AP::MapTracker {
    // ---- DBG map, maplist, mapdot (test_map.cpp) ----

    // DBG map: the tracker's state.
    std::string State();
    // The overworld icon group the map's cursor was on the last frame the overworld map took buttons, or -1.
    int HoverGroup();
    // STATE's " mlist=<group>/<cursor>/<first row>[/closing]" while the map's check list is there, else " mlist=-";
    // DBG maplist; DBG mapdot (the dungeon page's dots).
    std::string ListField();
    std::string ListInfo(const std::vector<std::string>& args);
    std::string DotInfo(const std::vector<std::string>& args);
}
#endif
