#pragma once
// The resource manager's loads (ResourceManager_ptr; ACTORS.md, Creation): what Resource_Load gives.
#include "game/stl.hpp"
#include <cstddef>

namespace Game {
    // Resource_Load's result (0x20 bytes, two shared_ptrs): a load in progress, whose entry holds the resource once
    // Resource_Wait has waited for it (+0x38), or the resource when it was loaded already. The Resource_Take*
    // functions take the resource from either. The module's own, released with it.
    struct ResourceHandle {
        Shared<> loading;
        Shared<> resource;         // 0x10
    };
    static_assert(offsetof(ResourceHandle, resource) == 0x10 && sizeof(ResourceHandle) == 0x20);
}
