#pragma once
// The player's shuffles (DESIGN.md, Options): music, lighting and dialogue, from the seed file's shuffles.
#include "game/sym.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace AP::Shuffles {
    // Reads the shuffles and hooks the environment and message lookups when it shuffles them.
    void Install();
    // The track the game's request for `track` plays: the shuffled one, "" for none; empty when not shuffled.
    std::optional<std::string_view> Track(std::string_view track);
    // A message the game reads (Msbt_GetText, ap_text.cpp) shown in another's place gets that one's input wait at
    // its end.
    void FixText(Game::MsbtText* out, void* msbt, s32 index);
#ifdef LASAP_TEST
    // The shuffles read and the last environment swapped, for DBG shuffles
    std::string State();
#endif
}
