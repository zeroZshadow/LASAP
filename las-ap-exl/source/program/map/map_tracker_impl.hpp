#pragma once
// The map tracker's parts, shared by the map_*.cpp files and by the test build's DBG map, maplist and mapdot
// (test_map.cpp). See map_tracker.cpp for the whole.
#include "map/map_tracker.hpp"
#include "AP/ap_list.hpp"
#include "Game/map.hpp"
#include "Game/stl.hpp"
#include "Game/ui.hpp"
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AP::MapTracker {
    namespace Map = Game::Map;
    namespace Ui = Game::Ui;
    namespace L = AP::ListUi;

    // ---- What the patcher's map file gives (map_tracker.cpp) ----

    // A dungeon dot (M line): its room ("Lv01TailCave_05F"), its world position and checks, and its name (MG line).
    struct Dot {
        std::string room;
        float x, z;
        std::vector<int> ids;
        std::string name;
    };
    struct Chest {
        std::string level;
        float x, z;
        int id;
    };
    extern bool g_Loaded;
    extern bool g_Enabled;      // loaded and not switched off for a test (DBG map 0)
    extern bool g_IconsHidden;  // revealed maps without overworld icons, for screenshots (DBG map 2)
    extern std::vector<std::vector<int>> g_Overworld;  // group -> local ids
    extern std::vector<Dot> g_Dots;
    extern std::vector<Chest> g_Chests;
    extern std::vector<std::string> g_GroupNames;      // group -> its name
    extern std::map<int, std::string> g_CheckNames;    // local id -> the location's name without its area
    // group -> the flag until which it has another name (a shuffled dungeon's: which dungeon is behind its door)
    extern std::map<size_t, std::pair<std::string, std::string>> g_HiddenNames;
    // How often the hooks ran (DBG map): the dungeon map's flags, the dungeon icons' update, chests matched
    extern int g_FlagsCalls, g_IconCalls, g_ChestMatches;

    // ---- Overworld icons (map_icons.cpp) ----

    // InLogic, then the looks of MAP_LOOK_MARKS in their order.
    enum Look { InLogic, Partial, OutOfLogic, Collected, kLooks };
    // The value LabelGroup gives the dungeon dots' label (MAP_DOT_LABEL).
    constexpr int kDotGroup = 1 << 20;
    extern bool g_OutHidden;                    // out-of-logic icons toggled off with the X button
    extern Map::OverworldMap* g_MarkerPage;     // the map page whose markers hold ours (the icon setup ran for it)

    bool StartsWith(std::string_view s, const char* prefix);
    bool Reachable(int id);
    // Checked, or marked skipped by the player: the map counts it as done.
    bool Done(int id);
    // A group whose other name shows: a shuffled dungeon's until its flag is set.
    bool NameHidden(size_t group);
    // The look of an icon of these locations.
    Look IdsLook(const std::vector<int>& ids, bool nameHidden);
    Look GroupLook(size_t group);
    // A group A marks skipped or not: one location, not checked.
    bool Skippable(const std::vector<int>& ids);
    // Whether a group's icon in this look is made.
    bool LookMade(size_t group, Look look);
    // The group and look a flag or label names (MAP_FLAG_PREFIX[mark]<group>), or -1.
    int ParseIcon(std::string_view name, Look& look);
    Map::MapMarker* MarkerOf(const Game::SharedPtr& marker);
    // The group and look of a map marker's row, or -1.
    int MarkerIcon(const Map::MapMarker* marker, Look& look);
    Ui::Pane* MarkerPane(Map::MapMarker* marker);
    // The page's hovered marker of ours, or null; its group and look, or -1.
    Map::MapMarker* HoveredMarker(const Map::OverworldMap* page);
    int HoveredIcon(const Map::OverworldMap* page, Look& look);
    // Shows the page's markers of ours whose look shows and hides the others.
    void ApplyLooks(Map::OverworldMap* page);
    // X on a map page (overworld or dungeon).
    void ToggleOutOfLogic(const void* page);
    // An icon's count, "<to do>/<unchecked>", and its hover text.
    std::string CountText(const std::vector<int>& ids);
    std::string IconText(const std::vector<int>& ids, const std::string& name);
    void InstallIconHooks();

    // ---- The check list (map_checklist.cpp) ----

    // Gm_MapMarker's SetMark animation picks the marker's texture and size by frame: the game's markers, then the map
    // tracker's looks at a group's size (ap_map_icons.frame).
    constexpr int kGameMarkFrames = 7;  // ap_map_icons.GAME_FRAMES
    struct Panel {
        Ui::LayoutWrapper* page = nullptr;  // the map page it hangs under
        int group = -1;             // the overworld icon's group, -1 for a dungeon dot
        std::vector<int> ids;       // its locations
        std::string heading;        // the icon's name
        L::List list;
        L::Layout dim, back, title, count, dots[L::kRows], tags[L::kRows];
        Ui::Pane* shade = nullptr;  // the dim's black pane
        Ui::Pane* window = nullptr; // the back's paper window behind the header and the list
        Ui::Animation marks[L::kRows];  // each dot's SetMark
        int fade = 0;               // 0..255, the alpha its layouts draw with
        bool closing = false;       // B closed it: it fades out, then goes
    };
    extern Panel g_Panel;
    extern int g_ListsBuilt, g_ListToggles;
    extern u64 g_PadHidden;  // buttons held when the list closed: hidden from the game until released

    bool ListOpen();
    Map::MapCursor* FindCursor();
    // The look a location's row shows.
    Look RowLook(int id);
    std::string CheckName(int id);
    void DropList();
    // The header's count, the rows' names, dots and tags.
    void FillList();
    // A on an icon of several locations: its check list over the map.
    void OpenList(Ui::LayoutWrapper* page, const std::vector<int>& ids, const std::string& heading, int group);
    // Once a frame of the map's KeyWait while the list is there.
    void TickList(Ui::LayoutWrapper* page);
    // The map's guide while the list is open.
    const L::Guide* ListGuide(u32 index);
    // Whether A opens the group's check list.
    bool Listable(int group);

    // ---- Dungeon dots (map_dots.cpp) ----

    // A dot on the open dungeon page: its marker (a shared pointer's object and control block) and position under
    // N_MapPosition_00, and whether its room is on the shown floor.
    struct DotMarker {
        size_t dot;
        Map::MapMarker* marker = nullptr;
        void* control = nullptr;
        float x = 0, y = 0;
        bool onFloor = false;
    };
    // The open dungeon page, its rooms by name as its setup showed them (room, on the shown floor), its dots and the
    // hovered one (index into g_DotMarkers, -1 none).
    extern Map::DungeonMap* g_DungeonPage;
    extern std::map<std::string, std::pair<Map::Area*, bool>, std::less<>> g_Rooms;
    extern std::vector<DotMarker> g_DotMarkers;
    extern int g_DotHovered;
    extern int g_DotsMade;  // for DBG map

    // A map marker's or room icon's root pane.
    Ui::Pane* MarkerRoot(Ui::LayoutWrapper* wrapper);
    // Each dot's look and whether it shows.
    void ApplyDots();
    // A dot's position in the cursor's units.
    void DotAt(const Map::DungeonMap* page, const DotMarker& dm, float& x, float& y);
    const std::vector<int>* HoveredDotIds();
    // A name on the map cursor: Place:<label>, whose text LabelText gives.
    void ShowCursorName(std::string_view placeLabel);
    // The hovered dot's name on the map cursor.
    void ShowDotName();
    // Once a frame of the dungeon page's KeyWait, after its pins' hover: the hovered dot.
    void UpdateDotHover(Map::DungeonMap* page);
    void InstallDotHooks();

    // ---- The open map (map_tracker.cpp) ----

    // The open map after a change of the logic, the checks or a skip mark.
    void RefreshOpenMap();

#ifdef LASAP_TEST
    // ---- DBG map, maplist, mapdot (test_map.cpp) ----

    // The hovered icon's group and look, the last frame the overworld map took buttons.
    extern int g_HoverGroup, g_HoverLook;
    extern std::string g_CursorText;  // the last name text handed to the map cursor (NoteText)
    // The overworld map's frame took buttons: notes the hover; true when it opened the list DBG maplist open asked for.
    bool TestFieldPin(Map::OverworldMap* page);
    // The dungeon map's A: the dot DBG mapdot press asked for, as if hovered and pressed.
    bool TestDotPressed();
    // The list's cost on the game thread (system ticks): its opening, a frame of it.
    void TestListOpened(u64 ticks);
    void TestListTicked(u64 ticks);
#endif
}
