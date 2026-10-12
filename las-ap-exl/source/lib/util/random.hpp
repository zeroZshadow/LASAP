#pragma once

#include <common.hpp>

namespace exl::util {

    u64 GetRandomU64();
    f32 GetRandomF32(f32 min, f32 max);

    /* TODO: remove along with libnx. */
    extern "C" u64 exl_random();
}