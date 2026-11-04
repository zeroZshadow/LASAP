#pragma once
// The map tracker's parts (DESIGN.md, Map tracker), shared by the map_*.cpp files and by the test build's DBG map,
// maplist and mapdot (test_map.cpp).
#include "map/map_tracker.hpp"
#include "ap/ap_list.hpp"
#include "ap/location_id.hpp"
#include "game/map.hpp"
#include "game/stl.hpp"
#include "game/ui.hpp"
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AP::MapTracker {
    namespace Map = Game::Map;
    namespace Ui = Game::Ui;
    namespace L = AP::ListUi;

    // ---- What the patcher's map file gives (map_tracker.cpp) ----

    // Read once at boot and never changed: the texts are views into the seed file (each terminated), the local ids
    // views into one flat array.
    using AP::LocationId;
    using Ids = std::span<const LocationId>;
    // An overworld group (O line): its locations, its name (G line), and the flag until which it has another name (H
    // line; a shuffled dungeon's: which dungeon is behind its door), none when the flag is empty.
    struct Group {
        Ids ids;
        std::string_view name, hiddenFlag, hiddenName;
    };
    // A dungeon dot (M line): its room ("Lv01TailCave_05F"), its world position and checks, and its name (MG line).
    struct Dot {
        std::string_view room;
        float x = 0, z = 0;
        Ids ids;
        std::string_view name;
    };
    // A dungeon chest of the slot's (C line): its level ("Lv01TailCave"), its world position and its location.
    struct Chest {
        std::string_view level;
        float x = 0, z = 0;
        LocationId id{};
    };
    extern bool g_Loaded;
    extern bool g_Enabled;      // loaded and not switched off for a test (DBG map 0)
    extern bool g_IconsHidden;  // revealed maps without overworld icons, for screenshots (DBG map 2)
    std::span<const Group> Groups();
    std::span<const Dot> Dots();
    std::span<const Chest> Chests();
    Ids GroupIds(size_t group);
    std::string_view GroupName(size_t group);
    Ids DotIds(size_t dot);
    // A location's name without its area (N line); false when the file has none.
    bool FindCheckName(LocationId id, std::string_view& name);
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
    bool Reachable(LocationId id);
    // Checked, or marked skipped by the player: the map counts it as done.
    bool Done(LocationId id);
    // A group whose other name shows: a shuffled dungeon's until its flag is set.
    bool NameHidden(size_t group);
    // The look of an icon of these locations.
    Look IdsLook(Ids ids, bool nameHidden);
    Look GroupLook(size_t group);
    // A group A marks skipped or not: one location, not checked.
    bool Skippable(Ids ids);
    // Whether a group's icon in this look is made.
    bool LookMade(size_t group, Look look);
    // The group and look a flag or label names (MAP_FLAG_PREFIX[mark]<group>), or -1.
    int ParseIcon(std::string_view name, Look& look);
    Map::MapMarker* MarkerOf(const Game::SharedPtr<Map::MapMarker>& marker);
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
    std::string CountText(Ids ids);
    std::string IconText(Ids ids, std::string_view name);
    // A location holding a server hint not yet found, neither checked nor skipped, whose item has a name.
    bool HintShown(LocationId id);
    bool AnyHintShown(Ids ids);
    // A group whose icon gets a ring and the hint strip: a hint shows there, and its name is not hidden (a shuffled
    // dungeon's door would tell which dungeon holds the hinted location).
    bool Ringed(size_t group);
    // The items hinted at these locations, each as HintedItemText gives it.
    std::vector<std::string> HintItems(Ids ids);
    // A gold ring around an icon or dot with a hint shown: a map marker at the ring's SetMark frame, attached under
    // parent before the icon's root pane (so it draws under the icon) at the icon's place.
    Game::Shared<Map::MapMarker> MakeRing(Ui::LayoutWrapper* page, Ui::Pane* parent, Ui::Pane* icon);
    // The ring's size (a single check's or a group's) and whether it shows; never shown when the assets lack its
    // frame.
    void ShowRing(Map::MapMarker* ring, bool single, bool shown);
    // The open overworld page's rings, made after its setup for the groups with a hint shown then.
    struct Ring {
        size_t group;
        Game::Shared<Map::MapMarker> marker;
    };
    extern std::vector<Ring> g_Rings;
    void InstallIconHooks();

    // ---- The check list (map_checklist.cpp) ----

    // Gm_MapMarker's SetMark animation picks the marker's texture and size by frame: the game's markers, then the map
    // tracker's looks at a group's size (ap_map_icons.frame).
    constexpr int kGameMarkFrames = 7;  // ap_map_icons.GAME_FRAMES
    // A colour tag in the text does not reach a text box of a layout: its colours are set instead.
    constexpr u8 kInk[] = {0x0f, 0x07, 0x02, 0xff};      // dark brown on the paper (shows as 46 30 1e)
    void SetTextColor(Ui::TextBox* box, const u8 (&color)[4]);
    // A Gm_SystemOpUnit text of room for that many characters, hung under the page's root pane.
    bool BuildText(L::Layout& layout, Ui::LayoutWrapper* page, Ui::Pane* root, u16 chars);
    // The ring around a group's dot, then a single check's (ap_map_icons.ring_frame).
    constexpr int kRingFrame = kGameMarkFrames + 2 * kLooks;
    struct Panel {
        Ui::LayoutWrapper* page = nullptr;  // the map page it hangs under
        int group = -1;             // the overworld icon's group, -1 for a dungeon dot
        Ids ids;                    // its locations
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
    Look RowLook(LocationId id);
    std::string CheckName(LocationId id);
    void DropList();
    // The header's count, the rows' names, dots and tags.
    void FillList();
    // A on an icon of several locations: its check list over the map.
    void OpenList(Ui::LayoutWrapper* page, Ids ids, std::string_view heading, int group);
    // Once a frame of the map's KeyWait while the list is there.
    void TickList(Ui::LayoutWrapper* page);
    // The map's guide while the list is open.
    const L::Guide* ListGuide(u32 index);
    // Whether A opens the group's check list.
    bool Listable(int group);

    // ---- The hint strip (map_hintstrip.cpp) ----

    // A paper strip along the map's bottom edge naming the items hinted at the hovered icon or dot.
    struct HintStrip {
        Ui::LayoutWrapper* page = nullptr;  // the map page it hangs under
        L::Layout back, label, items;
        Ui::Pane* window = nullptr;         // the back's paper window
        std::vector<std::string> hinted;    // the items hinted at the hovered icon or dot
        size_t named = 0;                   // how many of them the text names, the others counted ("+3 more")
        int settle = 0;                     // frames until the text, set anew, is measured
        bool shown = false;
    };
    extern HintStrip g_HintStrip;
    // Built with a page that has rings, dropped with it (only that page's: any page may be passed).
    void BuildHintStrip(Ui::LayoutWrapper* page);
    void DropHintStrip(Ui::LayoutWrapper* page);
    // Once a tick: the strip shows while the page waits for buttons with a ringed icon or dot hovered.
    void UpdateHintStrip();

    // ---- Dungeon dots (map_dots.cpp) ----

    // A dot on the open dungeon page: its marker and position under N_MapPosition_00, and whether its room is on the
    // shown floor.
    struct DotMarker {
        size_t dot;
        Game::Shared<Map::MapMarker> marker;
        Game::Shared<Map::MapMarker> ring;  // made when a hint showed at the page's setup (MakeRing)
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
    // The hovered dot, or null.
    const Dot* HoveredDot();
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
