#pragma once
#include "lib.hpp"
#include "Game/entity.hpp"

namespace Game {
    struct Vec2 {
        float x;
        float y;
    };

    // Vec3 still pads to 0x10 bytes just like Vec4, we use both for clarity
    struct Vec3 {
        float x;
        float y;
        float z;
    };

    struct Vec4 {
        float x;
        float y;
        float z;
        float t;
    };

    struct Coords {
        Vec3 pos;
        float paddingPos;
        Vec4 rotate;
        Vec3 scale;
        float paddingScale;
    };

    struct RootComp {
        unsigned char padding_58[0x58];
        RootComp* otherComp; // 0x58
        unsigned char padding_80[0x20];
        Coords coordsNew; // 0x80
        Coords coords; // 0xb0
        bool needsCoordUpdate; // 0xe0
        // AttachInfo stuff that Im too lazy to write
    };

    class Actor : public Game::Entity {
        public:
            unsigned char padding_80[0x80 - 0x44]; // 0x44 = entity flags
            RootComp* rootComp; // 0x80
            unsigned char padding_98[0x10];
            uint64_t hash; // 0x98
            uint16_t id; // 0xa0
            unsigned char padding_260[0x260 - 0xa2];
            Coords spawnCoords; // 0x260
    };
}