#pragma once
#include "lib.hpp"
#include "Game/virtualstructure.hpp"

namespace Game {
    class Entity : public Game::VirtualStructure {
        public:
            unsigned char padding_18[0x18];
            unsigned char name[0x28];
            uint32_t flags;
    };
}