#pragma once
// A tree that drops an item when Link bumps it (ObjTree; its drop, 0xcb1f30): what of it the module reads.
#include "game/actor.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Actors {
    // Its parameters: its drop's launch
    struct TreeParams {
        u8 unknown0[0x68];
        float launchSpeed;         // 0x68
        float crownHeight;         // 0x6c: the crown above the tree's root
    };
    static_assert(offsetof(TreeParams, launchSpeed) == 0x68 && offsetof(TreeParams, crownHeight) == 0x6c);

    struct Tree {
        Actor actor;
        u8 unknown2a8[0x1d0];
        const TreeParams* params;  // 0x478
    };
    static_assert(offsetof(Tree, params) == 0x478);
}
