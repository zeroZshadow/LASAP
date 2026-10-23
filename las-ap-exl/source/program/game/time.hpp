#pragma once
// Time from the system counter (19.2 MHz). The game polls input several times per frame, so timing does not count
// frames.
#include "lib.hpp"

namespace Game {
    inline u64 Ticks() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks;
    }

    constexpr u64 kTicksPerMs = 19200;

    inline u64 NowMs() { return Ticks() / kTicksPerMs; }
    inline double NowMsFine() { return static_cast<double>(Ticks()) / kTicksPerMs; }
    constexpr u64 TicksToUs(u64 ticks) { return ticks * 10 / 192; }
}
