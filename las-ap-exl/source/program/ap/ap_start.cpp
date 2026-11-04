// The start state (DESIGN.md, Start state): a new save's starting items and flags, from the seed file's start. The
// items come once Link stands in his first level; the flags the first time he stands in another level than Tarin's
// house, where the opening and Marin's talk still find them unset, as LASR's start event set them on his first exit.
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "config/seed_file.hpp"
#include "game/data/inventory.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/hooks/level.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace AP::Bridge {
namespace {
    constexpr const char* kStartLevel = "MarinTarinHouse";  // where a new game wakes Link up
    // Set as Marin's talk at the wake-up ends (PlayerStart.bfevfl, op), seconds after the items come; an older mod's
    // start event gave them in that talk.
    constexpr const char* kOpeningTalked = "OpTalked";
    constexpr int kRupeesMax = 9999;

    struct StartItem { std::string_view key; int dungeon; };
    std::vector<StartItem> g_StartItems;
    int g_StartRupees = 0;
    std::vector<std::string_view> g_StartFlags;

    void GiveStartItems() {
        for (const StartItem& item : g_StartItems) GrantItem(std::string(item.key), 1, item.dungeon);
        auto* items = Game::Data::Inventory::Items();
        items->Rupees = std::min(items->Rupees + g_StartRupees, kRupeesMax);
        EventFlags::SetFlag(AP::START_ITEMS_FLAG, true);
        Logging.Log("[AP] start: %zu items, %d rupees", g_StartItems.size(), g_StartRupees);
    }

    // In the file's order, START_MARKER last: a save holds all of them once it is set.
    void SetStartFlags() {
        for (std::string_view flag : g_StartFlags) EventFlags::SetFlag(std::string(flag), true);
        Logging.Log("[AP] start: %zu flags", g_StartFlags.size());
    }
}

    // The seed file's start: "I <item key> <dungeon index>" (-1 none) per starting item, "R <rupees>" and "F <flag>"
    // per flag (patcher las_ap_patch.start_lines)
    void LoadStart() {
        SeedFile::ForEach("start", [](Fields& f) {
            std::string_view kind = f.Str();
            if (kind == "I") {
                std::string_view key = f.Str();
                if (!key.empty()) g_StartItems.push_back({key, f.Num<int>().value_or(-1)});
            } else if (kind == "R") {
                g_StartRupees = f.Num<int>().value_or(0);
            } else if (kind == "F") {
                std::string_view flag = f.Str();
                if (!flag.empty()) g_StartFlags.push_back(flag);
            }
        });
        Logging.Log("[AP] start state: %zu items, %d rupees, %zu flags", g_StartItems.size(), g_StartRupees,
                    g_StartFlags.size());
    }

    // Nothing without the file, which always sets START_MARKER. START_MARKER set: the save has its start state, from
    // here or from an older mod's start event. That event gave the items in the wake-up talk and the flags on the
    // first exit, so a save of it past the talk but still in the house has its items.
    void TickStart() {
        if (g_StartFlags.empty() || !PlayerReady() || EventFlags::CheckFlag(AP::START_MARKER)) return;
        if (!EventFlags::CheckFlag(AP::START_ITEMS_FLAG)) {
            if (EventFlags::CheckFlag(kOpeningTalked)) EventFlags::SetFlag(AP::START_ITEMS_FLAG, true);
            else GiveStartItems();
        }
        if (LevelSystem::GetCurrentLevel() != kStartLevel) SetStartFlags();
    }
}
