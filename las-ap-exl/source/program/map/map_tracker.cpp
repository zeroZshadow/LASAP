// Map tracker (DESIGN.md, Map tracker): the pause map shows the slot's unchecked locations, and the whole overworld
// and every dungeon room are revealed.
//
// The patcher adds four UiFieldMapIcons rows per overworld spot holding checks (a group), one per look. The map shows
// a row's icon while the event flag named in the row is set; those flags are answered here. The flag is
// MAP_FLAG_PREFIX<group> for the in-logic look and MAP_FLAG_PREFIX<mark><group> for the others (MAP_LOOK_MARKS); the
// group's look is set and the others are not. In logic: every unchecked location of the group is in logic, or what is
// in logic is not known yet (the client sends it); partly in logic: some are; out of logic: none is, or the group's
// name is hidden (a shuffled dungeon not entered yet); collected: all are checked. Hovering an icon shows Place:<its flag>, whose text names the unchecked locations, or the collected
// ones (ap_text.cpp). The map's X button, which opens the memories, instead hides or shows the out-of-logic icons
// ("Toggle out of logic"): those icons are always made, and hidden after the map adds them while they are toggled
// off; the toggle shows or hides the page's markers in place. A on an icon of one unchecked location marks it skipped
// or not (saved per seed, ap_checks.cpp): a skipped location counts as checked on the map. A on an icon of several
// locations opens its check list: a panel in the middle of the map (ap_list.cpp's Gm_MemoryList) listing them, where
// A marks one skipped or not. An icon is made in every look skipping can give it, and those not shown are hidden the
// same way. The overworld's areas show once their UI_FieldMapTraverse_<area> flag is set (UI_FieldMapOpen_<area> once
// the reveal has played); both read as set.
//
// In dungeons the map draws the rooms Link visited and, with the compass, the room icons. Both come from the
// dungeon's DungeonItems entry: its visited-room mask is filled while Link is in the dungeon (the rooms then count as
// visited in the save), and an inline hook makes the compass and map owned for the map screen only, so the items keep
// their use. The dungeon page has no icons of its own for checks, so the module adds a dot per spot holding checks
// (ap_map.txt M lines): a map marker (Gm_MapMarker, the overworld's dots) hung beside the room icons, in the looks of
// the overworld's icons, shown on its room's floor only. Its hover, the cursor easing onto it and its name on the
// cursor follow the overworld page's icons; A on it does what A on an overworld icon does. The icon update hides the
// game's chest icons of the slot's chests.
//
// This file reads the patcher's map file and answers the game's flags and labels; the overworld icons
// (map_icons.cpp), the check list (map_checklist.cpp) and the dungeon dots (map_dots.cpp) have files of their own.
#include "map/map_tracker_impl.hpp"
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include <sstream>

namespace AP::MapTracker {
    bool g_Loaded = false;
    bool g_Enabled = false;
    bool g_IconsHidden = false;
    std::vector<std::vector<int>> g_Overworld;
    std::vector<Dot> g_Dots;
    std::vector<Chest> g_Chests;
    std::vector<std::string> g_GroupNames;
    std::map<int, std::string> g_CheckNames;
    std::map<size_t, std::pair<std::string, std::string>> g_HiddenNames;
    int g_FlagsCalls = 0, g_IconCalls = 0, g_ChestMatches = 0;

namespace {
    constexpr const char* kTraversePrefix = "UI_FieldMapTraverse_";
    constexpr const char* kOpenPrefix = "UI_FieldMapOpen_";
    // A logic or checks update arrived (MapDataChanged): the open map refreshes once a frame passes without one, as an
    // update comes in several lines.
    bool g_MapDataChanged = false, g_MapDataChangedLately = false;
}

    // The open map after a change of the logic, the checks or a skip mark: the overworld page's markers or the dungeon
    // page's dots take their looks, the open check list its rows, and the name on the cursor, while it shows one of
    // ours, its text.
    void RefreshOpenMap() {
        Map::MapCursor* cursor = FindCursor();
        bool named = cursor != nullptr && cursor->nameHidden == 0;
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

    void Load() {
        // Written by the patcher (ap_map.py): "O <group> <local id>...", "M <dot> <room> <x> <z> <local id>..." (a
        // dungeon dot), "C <level> <x> <z> <local id>" (a dungeon chest), "G <group> <name>" (an overworld group's
        // name), "MG <dot> <name>" (a dot's name), "H <group> <flag> <name>" (the group's name until the flag is set)
        // and "N <local id> <name>" (a location's name without its area, for the check list) lines.
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::MAP_PATH, text))) {
            Logging.Log("[AP] no map tracker file");
            return;
        }
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::stringstream ss(line);
            std::string kind;
            ss >> kind;
            if (kind == "G") {
                size_t group;
                if (!(ss >> group)) continue;
                std::string name;
                std::getline(ss, name);
                if (g_GroupNames.size() <= group) g_GroupNames.resize(group + 1);
                g_GroupNames[group] = name.empty() ? name : name.substr(1);
                continue;
            }
            if (kind == "N") {
                int id;
                std::string name;
                if (!(ss >> id)) continue;
                std::getline(ss, name);
                g_CheckNames[id] = name.empty() ? name : name.substr(1);
                continue;
            }
            if (kind == "MG") {
                size_t dot;
                std::string name;
                if (!(ss >> dot)) continue;
                std::getline(ss, name);
                if (g_Dots.size() <= dot) g_Dots.resize(dot + 1);
                g_Dots[dot].name = name.empty() ? name : name.substr(1);
                continue;
            }
            if (kind == "H") {
                size_t group;
                std::string flag, name;
                if (!(ss >> group >> flag)) continue;
                std::getline(ss, name);
                g_HiddenNames[group] = {flag, name.empty() ? name : name.substr(1)};
                continue;
            }
            if (kind == "C") {
                Chest chest{};
                if (ss >> chest.level >> chest.x >> chest.z >> chest.id) g_Chests.push_back(chest);
                continue;
            }
            size_t group;
            if (!(ss >> group)) continue;
            std::vector<int> ids;
            if (kind == "O") {
                for (int id; ss >> id;) ids.push_back(id);
                if (g_Overworld.size() <= group) g_Overworld.resize(group + 1);
                g_Overworld[group] = ids;
            } else if (kind == "M") {
                if (g_Dots.size() <= group) g_Dots.resize(group + 1);
                Dot& dot = g_Dots[group];
                ss >> dot.room >> dot.x >> dot.z;
                for (int id; ss >> id;) dot.ids.push_back(id);
            }
        }
        g_Loaded = g_Enabled = true;
        InstallIconHooks();
        InstallDotHooks();
        AP::ListUi::AddGuideSource(ListGuide);
        Logging.Log("[AP] map tracker: %zu overworld icons, %zu dungeon dots, %zu chests", g_Overworld.size(),
                    g_Dots.size(), g_Chests.size());
    }

    bool Flag(std::string_view name, bool& value) {
        if (!g_Enabled || name.size() < 6) return false;
        Look look;
        int group = ParseIcon(name, look);
        if (group >= 0) {
            value = !g_IconsHidden && static_cast<size_t>(group) < g_Overworld.size() &&
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
            const std::vector<int>* ids = HoveredDotIds();
            return ids != nullptr ? IconText(*ids, g_Dots[g_DotMarkers[g_DotHovered].dot].name) : "";
        }
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return "";
        if (NameHidden(group)) return g_HiddenNames[group].second;
        std::string name = static_cast<size_t>(group) < g_GroupNames.size() ? g_GroupNames[group] : "";
        return IconText(g_Overworld[group], name);
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
