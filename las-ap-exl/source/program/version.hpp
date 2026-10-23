#pragma once

#include <common.hpp>

namespace exl::util {

    // The game's versions: 1.0.0 and its update 1.0.1. In the update, a function at 0xdfaf44 grew, so the code after
    // it sits 0xd0 further on, up to the next section (.rodata at 0x1434000); Game::Offset maps 1.0.0's offsets.
    enum class UserVersion {
        BASE,
        UPDATE,
    };

    namespace impl {
        // Decided at the module's init (game_version.cpp).
        UserVersion DetermineUserVersion();
    }
}
