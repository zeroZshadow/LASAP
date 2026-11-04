#include "config/seed_file.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "loggers.hpp"
#include <string>
#include <vector>

namespace {
    struct Section {
        std::string_view name;
        std::vector<std::string_view> lines;
    };
    std::string g_Text;  // the file, which the sections and the loaders view
    std::vector<Section> g_Sections;
}

namespace SeedFile {
    // "[<section>]" lines, each followed by its lines; every line end becomes a 0.
    void Load() {
        if (R_FAILED(FileHandler::ReadFile(AP::SEED_PATH, g_Text))) {
            Logging.Log("[AP] no seed file %s", AP::SEED_PATH);
            return;
        }
        size_t section = SIZE_MAX, lines = 0;
        for (size_t at = 0; at < g_Text.size();) {
            size_t end = std::min(g_Text.find('\n', at), g_Text.size());
            size_t stop = end > at && g_Text[end - 1] == '\r' ? end - 1 : end;
            g_Text[stop] = 0;  // at the string's end, its terminator
            std::string_view line(g_Text.data() + at, stop - at);
            if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
                section = g_Sections.size();
                g_Sections.push_back({line.substr(1, line.size() - 2), {}});
            } else if (section != SIZE_MAX && !line.empty()) {
                g_Sections[section].lines.push_back(line);
                lines++;
            }
            at = end + 1;
        }
        Logging.Log("[AP] seed file: %zu sections, %zu lines", g_Sections.size(), lines);
    }

    std::span<const std::string_view> Lines(std::string_view section) {
        for (const Section& s : g_Sections) {
            if (s.name == section) return s.lines;
        }
        return {};
    }
}
