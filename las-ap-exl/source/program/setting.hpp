#pragma once

#include "common.hpp"

#define EXL_MODULE_NAME "exlaunch"

#define EXL_DEBUG
#define EXL_USE_FAKEHEAP

/*
#define EXL_SUPPORTS_REBOOTPAYLOAD
*/

namespace exl::setting {
    /* How large the fake .bss heap will be. The module never gives heap back; a failed allocation hangs the game.
       0x50000 held 323440 bytes in free play with Map Tracker on, too few with the map open (DBG heap, 2026-10-22). */
    constexpr size_t HeapSize = 0xA0000;

    /* How large the JIT area will be for hooks: 200 bytes per trampoline hook installed (0x4000 held 81, too few). */
    constexpr size_t JitSize = 0x8000;

    /* How large the area will be inline hook pool. */
    constexpr size_t InlinePoolSize = 0x4000;

    /* How large the formatting buffer should be for logging. The buffer will be on the stack. */
    constexpr size_t LogBufferSize = 512;

    /* Sanity checks. */
    static_assert(ALIGN_UP(JitSize, PAGE_SIZE) == JitSize, "");
    static_assert(ALIGN_UP(InlinePoolSize, PAGE_SIZE) == InlinePoolSize, "");
}
