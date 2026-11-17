#pragma once
// The overworld's warp points (scripts/re/UI.md, Warp points): the holes' event box and Manbo's platforms. Each holds
// its WarpPoint.gsheet index, -1 for none; bit index of Inventory +0xb8 is the point's opening.
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // AreaWarpHoleEventBox: its index from its LEB parameter 0 (an int), taken at its init
    struct AreaWarpHoleEventBox {
        u8 pad_0[0x478];
        s32 warpIndex;             // 0x478
    };
    static_assert(offsetof(AreaWarpHoleEventBox, warpIndex) == 0x478);

    // ObjWarpManbo: its index from its LEB parameter 0
    struct ObjWarpManbo {
        u8 pad_0[0x400];
        s32 warpIndex;             // 0x400
    };
    static_assert(offsetof(ObjWarpManbo, warpIndex) == 0x400);
}
