#pragma once
#include "lib.hpp"
#include "Game/actor.hpp"

namespace Game::Actors {
    class CharCtrlComp : public Game::RootComp  {
        public:
            unsigned char padding_180[0x180 - 0xe1];
            Game::Vec3 vecA;
            Game::Vec3 vel;
            Game::Vec3 gravity;
            Game::Vec3 vecC;
    };

    class Player {//} : public Game::Actor {
        public:

            unsigned char padding_2a8[0x2a8];// - 0x290];
            Game::RootComp* skeletalModelComp; // 0x2a8
            unsigned char padding_2d8[0x28];
            Game::Actors::CharCtrlComp* playerCollision; // 0x2d8
            unsigned char padding_4ed[0x20d];
            uint8_t state; // 0x4ed
            unsigned char padding_1b40[0x1652];
            Game::Coords respawnCoords; // 0x1B40
    };
}