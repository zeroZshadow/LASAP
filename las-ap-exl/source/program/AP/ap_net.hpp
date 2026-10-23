#pragma once
// The network thread (ap_net.cpp): the game's end of the TCP line protocol (DESIGN.md) with the AP client.
#include "lib.hpp"
#include <string>

namespace AP {
    void StartNetwork(int port);
    bool IsClientConnected();
    void Send(const std::string& line);
    // Pops one received line into out. Returns false when the queue is empty.
    bool PopLine(std::string& out);
    // True once after each new client connection.
    bool TakeNewConnection();
}
