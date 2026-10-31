#pragma once
// A location by its id in this game: the world's location id less the game's base. The patcher's files, the client's
// lines and the placeholder item's index give it as a number.
#include <istream>
#include <string>
#include <utility>

namespace AP {
    enum class LocationId : int {};

    inline std::string ToString(LocationId id) { return std::to_string(std::to_underlying(id)); }

    // Reads one as its number
    inline std::istream& operator>>(std::istream& in, LocationId& id) {
        int n;
        if (in >> n) id = LocationId{n};
        return in;
    }
}
