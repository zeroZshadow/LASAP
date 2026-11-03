// Map tracker (DESIGN.md, Map tracker): reads the patcher's map file into static storage and answers the game's flags
// and labels; the overworld icons (map_icons.cpp), the check list (map_checklist.cpp) and the dungeon dots
// (map_dots.cpp) have files of their own.
#include "map/map_tracker_impl.hpp"
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace AP::MapTracker {
    bool g_Loaded = false;
    bool g_Enabled = false;
    bool g_IconsHidden = false;
    int g_FlagsCalls = 0, g_IconCalls = 0, g_ChestMatches = 0;

namespace {
    // The map file's storage, sized with room to spare for the largest file the patcher writes (every location of the
    // game in the slot). A line that does not fit is left out, and Load logs how many were.
    constexpr size_t kMaxGroups = 160, kMaxDots = 192, kMaxChests = 192, kMaxChecks = 448;
    constexpr size_t kMaxIds = 768, kMaxText = 20 * 1024;
    // A location's name (N line); kept in local id order.
    struct LocationName {
        LocationId id{};
        Ref name;
    };
    Group g_Groups[kMaxGroups];
    Dot g_Dots[kMaxDots];
    Chest g_Chests[kMaxChests];
    LocationName g_LocationNames[kMaxChecks];
    LocationId g_Ids[kMaxIds];
    char g_Text[kMaxText];
    size_t g_GroupCount = 0, g_DotCount = 0, g_ChestCount = 0, g_CheckCount = 0, g_IdCount = 0, g_TextSize = 0;
    size_t g_LeftOut = 0;  // lines that did not fit
    Ref g_LastText;        // the text added last, which the same text again shares (a dungeon's chests, a room's dots)

    constexpr const char* kTraversePrefix = "UI_FieldMapTraverse_";
    constexpr const char* kOpenPrefix = "UI_FieldMapOpen_";
    // A logic, checks or hints update arrived (DataChanged): the open map refreshes once a frame passes without one,
    // as an update comes in several lines.
    bool g_MapDataChanged = false, g_MapDataChangedLately = false;
}

    void RefreshOpenMap() {
        Map::MapCursor* cursor = FindCursor();
        bool named = cursor != nullptr && cursor->nameHidden == 0 && cursor->nameHideRequest == 0;
        if (g_MarkerPage != nullptr) {
            ApplyLooks(g_MarkerPage);
            Look look;
            if (named && HoveredIcon(g_MarkerPage, look) >= 0) {
                ShowCursorName(HoveredMarker(g_MarkerPage)->row->showFlag.View());
            }
        }
        if (g_DungeonPage != nullptr) {
            ApplyDots();
            if (named && g_DotHovered >= 0) ShowDotName();
        }
        if (ListOpen()) FillList();
    }

namespace {
    // Adds a text to the texts; false when it does not fit.
    bool AddText(std::string_view text, Ref& ref) {
        if (g_TextSize > 0 && TextOf(g_LastText) == text) {
            ref = g_LastText;
            return true;
        }
        if (g_TextSize + text.size() + 1 > kMaxText) return false;
        std::memcpy(g_Text + g_TextSize, text.data(), text.size());
        g_Text[g_TextSize + text.size()] = '\0';
        ref = g_LastText = {static_cast<u16>(g_TextSize), static_cast<u16>(text.size())};
        g_TextSize += text.size() + 1;
        return true;
    }

    // The rest of a line after the space that ends its last field: a name.
    std::string RestOf(std::stringstream& ss) {
        std::string rest;
        std::getline(ss, rest);
        return rest.empty() ? rest : rest.substr(1);
    }

    // Adds the line's remaining ids to the ids; false (none added) when they do not fit.
    bool AddIds(std::stringstream& ss, Ref& ref) {
        size_t start = g_IdCount;
        for (LocationId id{}; ss >> id;) {
            if (g_IdCount == kMaxIds) {
                g_IdCount = start;
                return false;
            }
            g_Ids[g_IdCount++] = id;
        }
        ref = {static_cast<u16>(start), static_cast<u16>(g_IdCount - start)};
        return true;
    }

    // The first location name at or after the id.
    LocationName* FindCheck(LocationId id) {
        return std::lower_bound(g_LocationNames, g_LocationNames + g_CheckCount, id,
                                [](const LocationName& check, LocationId i) { return check.id < i; });
    }

    // Adds a location's name in local id order. A location in two icons (its dungeon's at the entrance and its dot)
    // keeps the first.
    bool AddCheckName(LocationId id, std::string_view name) {
        LocationName* end = g_LocationNames + g_CheckCount;
        LocationName* at = FindCheck(id);
        if (at != end && at->id == id) return true;
        Ref ref;
        if (g_CheckCount == kMaxChecks || !AddText(name, ref)) return false;
        std::move_backward(at, end, end + 1);
        *at = {id, ref};
        g_CheckCount++;
        return true;
    }

    // One line of the map file; false when it does not fit.
    bool ReadLine(const std::string& line) {
        std::stringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "C") {
            std::string level;
            Chest chest;
            if (!(ss >> level >> chest.x >> chest.z >> chest.id)) return true;
            if (g_ChestCount == kMaxChests || !AddText(level, chest.level)) return false;
            g_Chests[g_ChestCount++] = chest;
            return true;
        }
        if (kind == "N") {
            LocationId id{};
            return !(ss >> id) || AddCheckName(id, RestOf(ss));
        }
        size_t index;
        if (!(ss >> index)) return true;
        if (kind == "O" || kind == "G" || kind == "H") {
            if (index >= kMaxGroups) return false;
            Group& group = g_Groups[index];
            if (kind == "O") {
                if (!AddIds(ss, group.ids)) return false;
                g_GroupCount = std::max(g_GroupCount, index + 1);
                return true;
            }
            if (kind == "G") return AddText(RestOf(ss), group.name);
            std::string flag;
            if (!(ss >> flag)) return true;
            return AddText(flag, group.hiddenFlag) && AddText(RestOf(ss), group.hiddenName);
        }
        if (kind == "M" || kind == "MG") {
            if (index >= kMaxDots) return false;
            Dot& dot = g_Dots[index];
            g_DotCount = std::max(g_DotCount, index + 1);
            if (kind == "MG") return AddText(RestOf(ss), dot.name);
            std::string room;
            ss >> room >> dot.x >> dot.z;
            return AddText(room, dot.room) && AddIds(ss, dot.ids);
        }
        return true;
    }
}

    Ids IdsOf(Ref ref) { return {g_Ids + ref.at, ref.size}; }
    std::string_view TextOf(Ref ref) { return {g_Text + ref.at, ref.size}; }
    std::span<const Group> Groups() { return {g_Groups, g_GroupCount}; }
    std::span<const Dot> Dots() { return {g_Dots, g_DotCount}; }
    std::span<const Chest> Chests() { return {g_Chests, g_ChestCount}; }
    Ids GroupIds(size_t group) { return IdsOf(g_Groups[group].ids); }
    std::string_view GroupName(size_t group) { return TextOf(g_Groups[group].name); }
    Ids DotIds(size_t dot) { return IdsOf(g_Dots[dot].ids); }

    bool FindCheckName(LocationId id, std::string_view& name) {
        const LocationName* at = FindCheck(id);
        if (at == g_LocationNames + g_CheckCount || at->id != id) return false;
        name = TextOf(at->name);
        return true;
    }

    void Load() {
        // Written by the patcher (ap_map.py); its lines: DESIGN.md, Map tracker, ap_map.txt.
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::MAP_PATH, text))) {
            Logging.Log("[AP] no map tracker file");
            return;
        }
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!ReadLine(line)) g_LeftOut++;
        }
        g_Loaded = g_Enabled = true;
        InstallIconHooks();
        InstallDotHooks();
        AP::ListUi::AddGuideSource(ListGuide);
        Logging.Log("[AP] map tracker: %zu overworld icons, %zu dungeon dots, %zu chests, %zu location names; ids "
                    "%zu/%zu, text %zu/%zu bytes", g_GroupCount, g_DotCount, g_ChestCount, g_CheckCount, g_IdCount,
                    kMaxIds, g_TextSize, kMaxText);
        if (g_LeftOut > 0) {
            Logging.Log("[AP] map tracker: %s is bigger than the module's storage for it, %zu lines left out",
                        AP::MAP_PATH, g_LeftOut);
        }
    }

    bool Flag(std::string_view name, bool& value) {
        if (!g_Enabled || name.size() < 6) return false;
        Look look;
        int group = ParseIcon(name, look);
        if (group >= 0) {
            value = !g_IconsHidden && static_cast<size_t>(group) < g_GroupCount &&
                    LookMade(group, look);
            return true;
        }
        if (StartsWith(name, kTraversePrefix) || StartsWith(name, kOpenPrefix)) {
            value = true;
            return true;
        }
        return false;
    }

    int LabelGroup(std::string_view label) {
        if (!g_Enabled) return -1;
        if (label == AP::MAP_DOT_LABEL) return kDotGroup;
        Look look;
        return ParseIcon(label, look);
    }

    std::string LabelText(int group) {
        // The group's name, after its count (CountText) when it has several locations and some are unchecked, or
        // after "Skipped: " when its one location is skipped; or its other name alone until its flag is set (the count
        // would tell a dungeon by its number of locations). The dungeon dots' label names the hovered dot the same way.
        if (group == kDotGroup) {
            const Dot* dot = HoveredDot();
            return dot != nullptr ? IconText(IdsOf(dot->ids), TextOf(dot->name)) : "";
        }
        if (group < 0 || static_cast<size_t>(group) >= g_GroupCount) return "";
        if (NameHidden(group)) return std::string(TextOf(g_Groups[group].hiddenName));
        return IconText(GroupIds(group), GroupName(group));
    }

    void DataChanged() {
        g_MapDataChanged = g_MapDataChangedLately = true;
    }

    void Tick() {
        if (!g_Enabled) return;
        if (g_MapDataChanged && !g_MapDataChangedLately) {
            g_MapDataChanged = false;
            RefreshOpenMap();
        }
        g_MapDataChangedLately = false;
        UpdateHintStrip();
        u8 dungeon = *Game::LevelIndex;
        Map::DungeonItemsEntry** entries = *Game::DungeonItems_ptr;
        if (dungeon == 0xff || entries == nullptr || entries[dungeon] == nullptr) return;
        entries[dungeon]->visited = ~0ULL;
    }

    std::string CaptionText() {
        // Offline, the looks are those of the last logic the client sent, or all in logic before it sent any.
        if (!g_Enabled) return "";
        if (AP::IsServerConnected()) return "Toggle out of logic";
        return AP::HasLogic() ? "Offline: toggle out of logic" : "Offline: logic unknown";
    }

    bool ListTakesPad(nn::hid::NpadBaseState* state) {
        u64& buttons = state->mButtons._storage[0];
        g_PadHidden &= AP::ListUi::PadHeld();
        buttons &= ~g_PadHidden;
        if (!ListOpen()) return false;
        buttons = 0;
        state->mAnalogStickL = {};
        state->mAnalogStickR = {};
        return true;
    }

#ifdef LASAP_TEST
    void NoteText(const std::string& text) {
        g_CursorText = text;
    }
#endif

    void SetMode(int mode) {
        g_Enabled = g_Loaded && mode != 0;
        g_IconsHidden = mode == 2;
    }
}
