#pragma once
#include "lib.hpp"
#include "Game/structure.hpp"

namespace Game {
    class VirtualStructure : public Game::Structure {
        public:
            uint64_t vtable() {
                uint64_t* newAddr = reinterpret_cast<uint64_t*>(addr);
                return *newAddr;
            }
    };
}