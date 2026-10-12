#include "random.hpp"

#include <random>

namespace exl::util {

    u64 GetRandomU64() {
        std::mt19937_64 random { svcGetSystemTick() };
        return random();
    }

    f32 GetRandomF32(f32 min, f32 max) {
        std::mt19937_64 random { svcGetSystemTick() };
        std::uniform_real_distribution<f32> dis(min, max);
        return dis(random);
    }

    extern "C" u64 exl_random() {
        return GetRandomU64();
    }
}