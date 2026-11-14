// Level jumps the seed leads elsewhere (DESIGN.md, Shuffled dungeons; Goal): every level change goes through
// LevelManager_LevelJump (scripts/re/ACTORS.md, Level jump), whose hook (game/hooks/level.cpp) asks LeadJump. A door's
// loading zone, an event's RequestLevelJump and an instrument's warp all name where they lead; the patcher's table
// (the seed file's jumps) names, per level Link leaves, the targets that lead elsewhere, and for a led jump onto a door
// something covers, the flag that opens it (scripts/re/ENTRANCES.md, Blocked landings). The overworld map's dungeon markers
// follow the doors (the seed file's markers): once the game loaded the map's sheets, each door's row takes the label and show
// flag of the dungeon behind it. The jumps between the overworld and other levels go to the client as DOOR lines, from
// which it learns the shuffled doors the player has gone through (DESIGN.md, Entrances, Doors gone through). The shop's
// stolen items, which the shop door's arrival entry gives, come wherever its exit leads.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "config/seed_file.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/map.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace AP::Bridge {
namespace {
    struct Jump { std::string_view from, level, locator, toLevel, toLocator, opening; };  // in the seed file
    std::vector<Jump> g_Jumps;
    // a door's marker label, and the label and show flag of the dungeon behind it, which its row takes
    struct Marker { std::string_view door, label, flag; };
    std::vector<Marker> g_Markers;
    // The DOOR lines of the jumps between the overworld and other levels since the game started, each once
    std::vector<std::string> g_DoorJumps;
    constexpr std::string_view kOverworld = "Field";

    // Stealing (scripts/re/ENTRANCES.md, Doors, companions and arrival events): leaving the shop with an item sets
    // StealSuccess, and Field_10D's arrival entry ExitOutShop gives the item and clears it. A shop exit led elsewhere
    // lands on another door's entry, so the patcher's copy of that chain plays once Link stands free outside the
    // shop; with StealSuccess left set the shopkeeper punishes him on the way back in.
    constexpr const char* kShopLevel = "Shop";
    constexpr const char* kStealFlag = "StealSuccess";
    constexpr const char* kPlayerStartEvent = "event/PlayerStart.bfevfl";
    // The chain is requested and has not taken Link yet: no second request until it has, or until he is in the shop.
    bool g_StolenGoodsRequested = false;

    // In UiDatasheets_Load, once the maps' holder took its sheets (0xf9cfb0).
    constexpr uintptr_t kMapSheetsLoaded = 0x86835c;

    // Each door's row takes its dungeon's label and show flag, which g_Markers keeps. The doors' rows are all found
    // before any changes, as a swap renames a row another marker looks for.
    void LeadMarkers() {
        Game::Map::MapSheets* sheets = Game::MapSheets_ptr.Get();
        auto* sheet = sheets != nullptr ? sheets->fieldMapIcons.ptr : nullptr;
        if (sheet == nullptr || sheet->data == nullptr) return;
        auto* rows = const_cast<Game::Map::UiFieldMapIconsRow*>(sheet->data->rows);
        std::vector<std::pair<Game::Map::UiFieldMapIconsRow*, const Marker*>> doors;
        for (u32 i = 0; i < sheet->data->count; i++) {
            for (const Marker& marker : g_Markers) {
                if (rows[i].nameLabel.View() == marker.door) doors.push_back({&rows[i], &marker});
            }
        }
        for (auto [row, marker] : doors) {
            row->nameLabel = marker->label;
            row->showFlag = marker->flag;
        }
        Logging.Log("[AP] dungeon markers led: %zu", doors.size());
    }

    HOOK_DEFINE_INLINE(MapSheetsLoaded) {
        static void Callback(exl::hook::nx64::InlineCtx*) { LeadMarkers(); }
    };
}

    // The seed file's jumps: "<from level> <level> <locator> <new level> <new locator> [<flag>]" lines, a jump from a
    // room of the first level, or of any level for "*", to that level and locator goes to the new ones, setting the
    // flag (patcher las_ap_patch.jump_lines)
    void LoadJumps() {
        SeedFile::ForEach("jumps", [](Fields& f) {
            Jump jump{f.Str(), f.Str(), f.Str(), f.Str(), f.Str(), f.Str()};
            if (!jump.toLocator.empty()) g_Jumps.push_back(jump);
        });
        Logging.Log("[AP] level jumps led elsewhere: %zu", g_Jumps.size());
    }

    // The seed file's markers: "<door's marker label> <dungeon's marker label> <dungeon's show flag>" lines, the
    // dungeon behind the door's, which the door's UiFieldMapIcons row takes (patcher ap_entrances.markers)
    void LoadMarkers() {
        SeedFile::ForEach("markers", [](Fields& f) {
            Marker marker{f.Str(), f.Str(), f.Str()};
            if (!marker.flag.empty()) g_Markers.push_back(marker);
        });
        if (!g_Markers.empty()) MapSheetsLoaded::InstallAtPtr(Game::Addr(kMapSheetsLoaded));
        Logging.Log("[AP] dungeon markers to lead: %zu", g_Markers.size());
    }

    // Through Field_10D ExitOutShop clears the flag before Link is free, so this plays only after a led exit, or on a
    // save left with the flag set.
    void TickStolenGoods() {
        if (!PlayerFree() || g_LastLevel == kShopLevel) {
            g_StolenGoodsRequested = false;  // the chain took Link, or he is back in the shop
            return;
        }
        if (g_StolenGoodsRequested || !EventFlags::CheckFlag(kStealFlag)) return;
        if (PlayerCarrying() || LinkDead() || !ItemEventIdle()) return;
        if (RequestEvent(kPlayerStartEvent, AP::STOLEN_GOODS_ENTRY, {})) {
            g_StolenGoodsRequested = true;
            Logging.Log("[AP] stolen goods requested in %s", g_LastLevel.c_str());
        }
    }
}

namespace AP {
    using namespace Bridge;

    std::optional<std::pair<std::string_view, std::string_view>> LeadJump(std::string_view from, std::string_view level,
                                                                          std::string_view locator) {
        for (const Jump& jump : g_Jumps) {
            if ((jump.from != "*" && jump.from != from) || jump.level != level || jump.locator != locator) continue;
            Logging.Log("[AP] jump to %s %s leads to %s %s", jump.level.data(), jump.locator.data(), jump.toLevel.data(),
                        jump.toLocator.data());
            // set before the new level's actors are made, which read it
            if (!jump.opening.empty()) EventFlags::SetFlag(std::string(jump.opening), true);
            return std::pair<std::string_view, std::string_view>{jump.toLevel, jump.toLocator};
        }
        return std::nullopt;
    }

    void NoteJump(std::string_view from, std::string_view level, std::string_view locator, std::string_view to) {
        if ((from == kOverworld) == (level == kOverworld) || locator.empty() || to.empty()) return;
        std::string line = "DOOR " + std::string(locator) + " " + std::string(to);
        if (std::find(g_DoorJumps.begin(), g_DoorJumps.end(), line) != g_DoorJumps.end()) return;
        g_DoorJumps.push_back(line);
        Send(line);
    }

    void ReportDoorJumps() {
        for (const std::string& line : g_DoorJumps) Send(line);
    }
}
