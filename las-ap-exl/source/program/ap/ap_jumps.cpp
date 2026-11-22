// Level jumps the seed leads elsewhere (DESIGN.md, Shuffled dungeons; Goal): every level change goes through
// LevelManager_LevelJump (scripts/re/ACTORS.md, Level jump), whose hook (game/hooks/level.cpp) asks LeadJump. A door's
// loading zone, an event's RequestLevelJump and an instrument's warp all name where they lead; the patcher's table
// (the seed file's jumps) names, per level Link leaves, the targets that lead elsewhere, and the flags a led jump sets:
// for a door something covers, the one that opens it (scripts/re/ENTRANCES.md, Blocked landings), and those a trigger box
// on the vanilla door sets (Door trigger boxes). The overworld map's dungeon markers
// follow the doors (the seed file's markers): once the game loaded the map's sheets, each door's row takes the label and show
// flag of the dungeon behind it. The jumps between the overworld and other levels go to the client as DOOR lines, from
// which it learns the shuffled doors the player has gone through (DESIGN.md, Entrances, Doors gone through). The shop's
// stolen items, which the shop door's arrival entry gives, come wherever its exit leads. A Link who lands in water he
// cannot swim in goes back the way he came (DESIGN.md, Entrances, Water landings).
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "config/seed_file.hpp"
#include "game/actors/player.hpp"
#include "game/framework.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/hooks/level.hpp"
#include "game/map.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace AP::Bridge {
namespace {
    struct Jump {  // in the seed file
        std::string_view from, level, locator, toLevel, toLocator;
        std::vector<std::string_view> flags;
    };
    std::vector<Jump> g_Jumps;
    // a door's marker label, and the label and show flag of the dungeon behind it, which its row takes
    struct Marker { std::string_view door, label, flag; };
    std::vector<Marker> g_Markers;
    // The DOOR lines of the jumps between the overworld and other levels since the game started, each once, the
    // latest jump's last: the client takes the last line as the door Link came through last
    std::vector<std::string> g_DoorJumps;
    constexpr std::string_view kOverworld = "Field";
    // a warp point's spot in Field (WarpPoint.gsheet mLocator; scripts/re/UI.md, Warp map)
    constexpr std::string_view kWarpLocator = "Warp_";

    // Stealing (scripts/re/ENTRANCES.md, Doors, companions and arrival events): leaving the shop with an item sets
    // StealSuccess, and Field_10D's arrival entry ExitOutShop gives the item and clears it. A shop exit led elsewhere
    // lands on another door's entry, so the patcher's copy of that chain plays once Link stands or swims free outside
    // the shop. Back in the shop before that, its arrival entry EntranceIn_Tool plays the chain.
    constexpr const char* kShopLevel = "Shop";
    constexpr const char* kStealFlag = "StealSuccess";
    constexpr const char* kPlayerStartEvent = "event/PlayerStart.bfevfl";
    // The chain is requested and has not taken Link yet: no second request until it has, or until he is in the shop.
    bool g_StolenGoodsRequested = false;

    // Water landings (the seed file's returns): a led jump's landing and the jump of the door at it, as the game names
    // it before the seed's jumps lead it back to where Link came from.
    struct Return { std::string_view level, locator, backLevel, backLocator; };
    std::vector<Return> g_Returns;
    // Link's way since a level jump. Arrived: Link's respawn still goes to his last safe spot in the level he left, as
    // no safe ground here has taken its place; falling into water then would put him there, out of this level's
    // ground, again and again. Fell: he falls into water, and the respawn puts him back on its surface. Back: the
    // APReturn event is requested, whose jump goes back the way he came: through the door at a led landing of
    // g_Returns, else (a hole, a door the seed leaves as it is) to that safe spot in the level he left.
    enum class Landing { None, Arrived, Fell, Back };
    Landing g_Landing = Landing::None;
    const Return* g_LandingReturn = nullptr;
    Game::Vec3 g_LeftSpot{};  // Link's respawn spot as the jump started
    std::string g_LeftLevel;  // the level the jump left
    // The level jump's locator for a world position: its offset, a Vec4 whose x and z an event's jump fills
    // (scripts/re/ENTRANCES.md, Arrival)
    constexpr std::string_view kSpotLocator = "$NONE";
    Game::Vec4 g_LeftOffset{};

    bool SameSpot(const Game::Vec3& a, const Game::Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

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

    // The seed file's jumps: "<from level> <level> <locator> <new level> <new locator> [<flag>...]" lines, a jump from
    // a room of the first level, or of any level for "*", to that level and locator goes to the new ones, setting the
    // flags (patcher las_ap_patch.jump_lines)
    void LoadJumps() {
        SeedFile::ForEach("jumps", [](Fields& f) {
            Jump jump{f.Str(), f.Str(), f.Str(), f.Str(), f.Str(), {}};
            for (std::string_view flag = f.Str(); !flag.empty(); flag = f.Str()) jump.flags.push_back(flag);
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

    // The seed file's returns: "<level> <locator> <door's level> <door's locator>" lines, per landing of a led jump the
    // jump of the door at it (patcher ap_entrances.returns)
    void LoadReturns() {
        SeedFile::ForEach("returns", [](Fields& f) {
            Return back{f.Str(), f.Str(), f.Str(), f.Str()};
            if (!back.backLocator.empty()) g_Returns.push_back(back);
        });
        Logging.Log("[AP] landings with a way back: %zu", g_Returns.size());
    }

    // Link moves freely: standing, or swimming where a led shop exit put him in water
    bool StandsOrSwims() {
        if (PlayerFree()) return true;
        Game::Framework* fmwk = Game::GetFramework();
        return PlayerReady() && fmwk->paused == 0 && fmwk->player->State() == Game::Actors::PlayerState::Swim;
    }

    // Through Field_10D ExitOutShop clears the flag before Link is free, so this plays only after a led exit, or on a
    // save left with the flag set.
    void TickStolenGoods() {
        if (!StandsOrSwims() || g_LastLevel == kShopLevel) {
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

    // FallAbyss takes Link out of deep water he cannot swim in (scripts/re/PLAYER.md, Respawn): its end puts him at
    // respawnCoords, which only safe ground he stands on updates. Right after a led landing that is still the spot he
    // left in the other level. The fall's spot takes its place, and once he is back on the surface the APReturn event
    // holds him and jumps back through the door; a stolen item comes first, so the shop lets him back in.
    void TickLanding() {
        if (g_Landing == Landing::None || g_Landing == Landing::Back || LevelSystem::Jumping()) return;
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->player == nullptr || fmwk->player->playerCollision == nullptr) return;
        Game::Actors::Player* player = fmwk->player;
        bool falling = player->State() == Game::Actors::PlayerState::FallAbyss;
        if (g_Landing == Landing::Arrived) {
            if (!SameSpot(player->respawnCoords.pos, g_LeftSpot)) {
                g_Landing = Landing::None;  // he stood on safe ground here
                return;
            }
            if (!falling) return;
            player->respawnCoords.pos = player->playerCollision->base.node.coordsNew.pos;
            g_Landing = Landing::Fell;
            Logging.Log("[AP] fell in at %s", g_LastLevel.c_str());
            return;
        }
        if (falling) return;
        if (!g_StolenGoodsRequested && EventFlags::CheckFlag(kStealFlag) &&
            RequestEvent(kPlayerStartEvent, AP::STOLEN_GOODS_ENTRY, {})) {
            g_StolenGoodsRequested = true;
            Logging.Log("[AP] stolen goods requested before the way back");
        }
        if (RequestCommonEvent(AP::RETURN_ENTRY)) {
            g_Landing = Landing::Back;
            AP::ReturnTarget back = AP::ReturnJump();
            Logging.Log("[AP] way back to %.*s %.*s", static_cast<int>(back.level.size()), back.level.data(),
                        static_cast<int>(back.locator.size()), back.locator.data());
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
            for (std::string_view flag : jump.flags) EventFlags::SetFlag(std::string(flag), true);
            return std::pair<std::string_view, std::string_view>{jump.toLevel, jump.toLocator};
        }
        return std::nullopt;
    }

    void NoteJump(std::string_view from, std::string_view level, std::string_view locator, std::string_view to) {
        if (locator.empty() || to.empty()) return;
        // the warp home, from the overworld too, comes out of Tarin's door's pair; a warp point's warp lands on its spot
        bool warp = level == kOverworld && (locator == AP::HOME_LOCATOR || locator.starts_with(kWarpLocator));
        if ((from == kOverworld) == (level == kOverworld) && !warp) return;
        std::string line = "DOOR " + std::string(locator) + " " + std::string(to);
        auto known = std::find(g_DoorJumps.begin(), g_DoorJumps.end(), line);
        if (known != g_DoorJumps.end()) g_DoorJumps.erase(known);
        g_DoorJumps.push_back(line);
        Send(line);
    }

    void ReportDoorJumps() {
        for (const std::string& line : g_DoorJumps) Send(line);
    }

    ReturnTarget ReturnJump() {
        if (g_LandingReturn != nullptr) return {g_LandingReturn->backLevel, g_LandingReturn->backLocator, nullptr};
        if (!g_LeftLevel.empty()) return {g_LeftLevel, kSpotLocator, &g_LeftOffset};
        return {kOverworld, AP::HOME_LOCATOR, nullptr};
    }

    void JumpStarted(std::string_view from, std::string_view level, std::string_view locator, bool led) {
        StartJump(from, level);
        g_Landing = Landing::None;
        g_LandingReturn = nullptr;
        g_LeftLevel.clear();
        Game::Framework* fmwk = Game::GetFramework();
        if (from.empty() || level.empty() || fmwk == nullptr || fmwk->player == nullptr) return;
        g_LeftSpot = fmwk->player->respawnCoords.pos;
        g_LeftLevel = from;
        g_LeftOffset = {g_LeftSpot.x, g_LeftSpot.y, g_LeftSpot.z, 0.0f};
        g_Landing = Landing::Arrived;
        if (!led) return;
        for (const Return& back : g_Returns) {
            if (back.level == level && back.locator == locator) g_LandingReturn = &back;
        }
    }
}
