#pragma once
#include "lib.hpp"

namespace Game {
    class Structure {
        public:
            uintptr_t addr;

            Structure(uintptr_t _addr) {
                addr = _addr;
            }
    };
}