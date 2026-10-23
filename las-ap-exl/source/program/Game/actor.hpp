#pragma once
#include "lib.hpp"

namespace Game {
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
}
