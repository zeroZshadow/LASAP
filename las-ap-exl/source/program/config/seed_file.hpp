#pragma once
// The seed file (DESIGN.md, Seed file): the patcher's per-seed tables, one [section] of lines each (patcher
// seed_file.py). Read once at boot into a buffer the module keeps, so the loaders keep views of its names.
#include "config/fields.hpp"
#include <span>
#include <string_view>

namespace SeedFile {
    // Reads the file (AP::SEED_PATH); without it every section is empty.
    void Load();
    // The section's lines in the file's order, ended with a 0; none when the file has no such section.
    std::span<const std::string_view> Lines(std::string_view section);

    // f(Fields&) for each line of the section; each field read is ended with a 0, as the game reads some names to their
    // terminator.
    template<typename F>
    void ForEach(std::string_view section, F&& f) {
        for (std::string_view line : Lines(section)) {
            Fields fields(line, true);
            f(fields);
        }
    }
}
