#pragma once
// How the game's code passes values (AAPCS64 with the Itanium C++ ABI), for the module's types that cross into it.
#include <type_traits>

namespace Game {
    // Returned through a pointer in x8, as the game's functions return libc++ objects with destructors: a type over
    // 16 bytes (AAPCS64) or one that is non-trivial for the purposes of calls (Itanium C++ ABI: a non-trivial copy
    // constructor, move constructor or destructor). Any other comes back in x0 and x1.
    template<typename T>
    concept ReturnedThroughX8 = sizeof(T) > 16 || !std::is_trivially_destructible_v<T> ||
                                !std::is_trivially_copy_constructible_v<T> || !std::is_trivially_move_constructible_v<T>;
}
