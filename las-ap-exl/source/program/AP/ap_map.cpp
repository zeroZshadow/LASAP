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
// or not (saved per seed, ap_game.cpp): a skipped location counts as checked on the map. A on an icon of several
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
#include "AP/ap.hpp"
#include "AP/ap_list.hpp"
#include "Game/map.hpp"
#include "Game/mem.hpp"
#include "Game/sym.hpp"
#include "Game/time.hpp"
#include "Game/ui.hpp"
#include "loggers.hpp"
#include "Config/file_handler.hpp"
#include "Hooks/Common/level.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
    namespace Map = Game::Map;
    namespace Ui = Game::Ui;
    using Game::Ui::PlaySound;

    // Dungeon map screen (game 1.0.0)
    constexpr uintptr_t kDungeonMapFlags = 0xe76204;    // w8 = DungeonItems flags byte (bit0 compass, bit1 map)
    // The dungeon map page (Game::Map::DungeonMap, scripts/re/UI.md "Dungeon map"): its setup and destructor;
    // DungeonMap_ShowCellFloor shows a room on the shown floor (its UiDungeonArea row's floor == the page's
    // shownFloor); KeyWait runs MoveCursor, PinButton () -> A pressed, then UpdateHover, the pins' hover.
    // DungeonMap_PositionIcon: an icon's position under N_MapPin_00. A dot within kHoverRadius of the cursor's size
    // is hovered; the cursor eases onto it while the stick is idle (the page's cursor speed kIdleSpeed).
    constexpr float kIdleSpeed = 4.25f, kEase = 0.3f, kHoverRadius = 0.2f;

    // Field map (game 1.0.0)
    // The X button, which opens the memories: in the overworld map's KeyWait state w21 = X pressed (x19 page), and
    // the dungeon map's check bool(page*).
    constexpr uintptr_t kFieldMemoryButton = 0xe95278;
    constexpr uintptr_t kMemoryButton = 0xe87840;
    // In the overworld map's KeyWait state, after it reads A (w0, page x19), which opens the pin menu. The page's
    // hovered marker is set later in the same update, so it is the last frame's.
    constexpr uintptr_t kFieldPinButton = 0xe950f4;
    // In the map's icon setup, after a shown icon's marker joined the page's markers: x19 page, x26 marker.
    constexpr uintptr_t kMarkerAdded = 0xe917c8;

    constexpr const char* kTraversePrefix = "UI_FieldMapTraverse_";
    constexpr const char* kOpenPrefix = "UI_FieldMapOpen_";

    // A dungeon dot (M line): its room ("Lv01TailCave_05F"), its world position and checks, and its name (MG line).
    struct Dot {
        std::string room;
        float x, z;
        std::vector<int> ids;
        std::string name;
    };
    // A dot on the open dungeon page: its marker (a shared pointer's object and control block) and position under
    // N_MapPosition_00, and whether its room is on the shown floor.
    struct DotMarker {
        size_t dot;
        Map::MapMarker* marker = nullptr;
        void* control = nullptr;
        float x = 0, y = 0;
        bool onFloor = false;
    };
    struct Chest {
        std::string level;
        float x, z;
        int id;
    };

    bool g_Loaded = false;
    bool g_Enabled = false;  // loaded and not switched off for a test (DBG map 0)
    bool g_IconsHidden = false;  // revealed maps without overworld icons, for screenshots (DBG map 2)
    bool g_OutHidden = false;    // out-of-logic icons toggled off with the X button
#ifdef LASAP_TEST
    int g_HoverGroup = -1;       // the hovered icon's group and look, the last frame the overworld map took buttons
    int g_HoverLook = -1;
#endif
    Map::OverworldMap* g_MarkerPage = nullptr;  // the map page whose markers hold ours (the icon setup ran for it)
    std::vector<std::vector<int>> g_Overworld;  // group -> local ids
    std::vector<Dot> g_Dots;
    std::vector<Chest> g_Chests;
    // The open dungeon page, its rooms by name as its setup showed them (room, on the shown floor), its dots and the
    // hovered one (index into g_DotMarkers, -1 none).
    Map::DungeonMap* g_DungeonPage = nullptr;
    std::map<std::string, std::pair<Map::Area*, bool>, std::less<>> g_Rooms;
    std::vector<DotMarker> g_DotMarkers;
    int g_DotHovered = -1;
    int g_DotsMade = 0;  // for DBG map
    void ApplyDots();
    void RefreshOpenMap();
    // A logic or checks update arrived (MapDataChanged): the open map refreshes once a frame passes without one, as an
    // update comes in several lines.
    bool g_MapDataChanged = false, g_MapDataChangedLately = false;
#ifdef LASAP_TEST
    std::string g_CursorText;  // the last name text handed to the map cursor (NoteMapText)
#endif
    std::vector<std::string> g_GroupNames;      // group -> its name
    std::map<int, std::string> g_CheckNames;    // local id -> the location's name without its area
    // group -> the flag until which it has another name (a shuffled dungeon's: which dungeon is behind its door)
    std::map<size_t, std::pair<std::string, std::string>> g_HiddenNames;
    int g_FlagsCalls = 0, g_IconCalls = 0, g_ChestMatches = 0;  // for DBG map

    bool StartsWith(std::string_view s, const char* prefix) {
        return s.substr(0, std::strlen(prefix)) == prefix;
    }

    // InLogic, then the looks of MAP_LOOK_MARKS in their order.
    enum Look { InLogic, Partial, OutOfLogic, Collected, kLooks };

    bool Reachable(int id) {
        return !AP::HasLogic() || AP::IsInLogic(id);
    }

    // Checked, or marked skipped by the player: the map counts it as done.
    bool Done(int id) {
        return AP::IsLocationChecked(id) || AP::IsLocationSkipped(id);
    }

    // A group whose other name shows: a shuffled dungeon's until its flag is set.
    bool NameHidden(size_t group) {
        auto hidden = g_HiddenNames.find(group);
        return hidden != g_HiddenNames.end() && !EventFlags::CheckFlag(hidden->second.first);
    }

    // The look of an icon of these locations; none counts as in logic while its name is hidden, as its look would tell
    // which dungeon its door leads into.
    Look IdsLook(const std::vector<int>& ids, bool nameHidden) {
        size_t left = 0, reachable = 0;
        for (int id : ids) {
            if (Done(id)) continue;
            left++;
            if (!nameHidden && Reachable(id)) reachable++;
        }
        if (left == 0) return Collected;
        return reachable == left ? InLogic : reachable > 0 ? Partial : OutOfLogic;
    }

    Look GroupLook(size_t group) {
        return IdsLook(g_Overworld[group], NameHidden(group));
    }

    // A group A marks skipped or not: one location, not checked.
    bool Skippable(const std::vector<int>& ids) {
        return ids.size() == 1 && !AP::IsLocationChecked(ids[0]);
    }

    // Whether a group's icon in this look is made: the look shown, and every look skipping or a logic update can give
    // it while the map is open (collected and out of logic with an unchecked location, in logic too unless its name is
    // hidden, partly with two), so the open map swaps them.
    bool LookMade(size_t group, Look look) {
        if (GroupLook(group) == look) return true;
        size_t unchecked = 0;
        for (int id : g_Overworld[group]) unchecked += AP::IsLocationChecked(id) ? 0 : 1;
        bool hidden = NameHidden(group);
        switch (look) {
            case Collected:
            case OutOfLogic: return unchecked > 0;
            case InLogic: return unchecked > 0 && !hidden;
            case Partial: return unchecked > 1 && !hidden;
            default: return false;
        }
    }

    // Whether a made icon shows: its look is the group's, and it is not an out-of-logic icon toggled off.
    bool LookShown(int group, Look look) {
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return false;
        return GroupLook(group) == look && !(look == OutOfLogic && g_OutHidden);
    }

    // The group and look a flag or label names (MAP_FLAG_PREFIX[mark]<group>), or -1.
    int ParseIcon(std::string_view name, Look& look) {
        if (!StartsWith(name, AP::MAP_FLAG_PREFIX)) return -1;
        name.remove_prefix(std::strlen(AP::MAP_FLAG_PREFIX));
        look = InLogic;
        const char* mark = name.empty() ? nullptr : std::strchr(AP::MAP_LOOK_MARKS, name[0]);
        if (mark != nullptr && *mark != '\0') {
            look = static_cast<Look>(InLogic + 1 + (mark - AP::MAP_LOOK_MARKS));
            name.remove_prefix(1);
        }
        if (name.empty() || name[0] < '0' || name[0] > '9') return -1;
        return std::atoi(std::string(name).c_str());
    }

    Map::MapMarker* MarkerOf(const Game::SharedPtr& marker) { return static_cast<Map::MapMarker*>(marker.ptr); }

    // The group and look of a map marker's row, or -1.
    int MarkerIcon(const Map::MapMarker* marker, Look& look) {
        const Map::UiFieldMapIconsRow* row = marker->row;
        if (row == nullptr || row->showFlag.data == nullptr) return -1;
        return ParseIcon(row->showFlag.View(), look);
    }

    Ui::Pane* MarkerPane(Map::MapMarker* marker) {
        return Game::CallVirtual<Ui::Layout*()>(marker, Ui::LayoutWrapperVirtual::kLayout)->root;
    }

    void SetMarkerVisible(Map::MapMarker* marker, bool visible) { Ui::SetVisible(MarkerPane(marker), visible); }

    // Shows the page's markers of ours whose look shows (LookShown) and hides the others.
    void ApplyLooks(Map::OverworldMap* page) {
        for (const Game::SharedPtr& sp : page->markers) {
            Map::MapMarker* marker = MarkerOf(sp);
            Look look;
            int group = marker != nullptr ? MarkerIcon(marker, look) : -1;
            if (group >= 0) SetMarkerVisible(marker, LookShown(group, look));
        }
    }

    // The page's hovered marker of ours, or null.
    Map::MapMarker* HoveredMarker(const Map::OverworldMap* page) {
        s32 index = page->hoveredMarker;
        if (index < 0 || static_cast<size_t>(index) >= page->markers.size()) return nullptr;
        return MarkerOf(page->markers[index]);
    }

    // The group and look of the page's hovered marker, or -1.
    int HoveredIcon(const Map::OverworldMap* page, Look& look) {
        Map::MapMarker* marker = HoveredMarker(page);
        return marker != nullptr ? MarkerIcon(marker, look) : -1;
    }

    // X on a map page (overworld or dungeon).
    void ToggleOutOfLogic(const void* page) {
        g_OutHidden = !g_OutHidden;
        if (page == g_MarkerPage) ApplyLooks(g_MarkerPage);
        if (page == g_DungeonPage) ApplyDots();
        PlaySound(g_OutHidden ? "SE_SY_MAP_PIN_OFF" : "SE_SY_MAP_PIN_ON");
    }

    // A on the overworld map: marks the hovered icon's location skipped, or not. False (the game takes the press)
    // unless the icon is one of a single unchecked location.
    bool ToggleSkipped(Map::OverworldMap* page) {
        Look look;
        int group = HoveredIcon(page, look);
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return false;
        const auto& ids = g_Overworld[group];
        if (!Skippable(ids)) return false;
        AP::SetLocationSkipped(ids[0], !AP::IsLocationSkipped(ids[0]));
        if (page == g_MarkerPage) ApplyLooks(page);
        PlaySound("SE_SY_DECIDE");
        return true;
    }

    // An icon's count, in its hover text and its check list's header: "<to do>/<unchecked>", its locations in logic
    // neither checked nor skipped over those not checked (skipping one lowers the first, not the second). Empty when
    // every location is checked.
    std::string CountText(const std::vector<int>& ids) {
        int unchecked = 0, todo = 0;
        for (int id : ids) {
            if (AP::IsLocationChecked(id)) continue;
            unchecked++;
            if (!Done(id) && Reachable(id)) todo++;
        }
        return unchecked == 0 ? "" : std::to_string(todo) + "/" + std::to_string(unchecked);
    }

    // An icon's hover text: its name after its count when it has several locations and some are unchecked, or after
    // "Skipped: " when its one location is skipped.
    std::string IconText(const std::vector<int>& ids, const std::string& name) {
        if (Skippable(ids) && AP::IsLocationSkipped(ids[0])) return "Skipped: " + name;
        std::string count = CountText(ids);
        if (ids.size() < 2 || count.empty()) return name;
        return count + " " + name;
    }

    // The value MapLabelGroup gives the dungeon dots' label (MAP_DOT_LABEL).
    constexpr int kDotGroup = 1 << 20;

}

// The check list of an icon of several locations (DESIGN.md, Map tracker, Check list): a panel in the middle of the
// overworld map, hung under the map page while it is open, the map dimmed behind it:
// - the dim: Gm_MapMarkList (the map's pin bar) with only its black P_pict_00, stretched over the map between the
//   page's rules (the black of Cm_Dlg, the game's dialogs, would bring its masks, which capture the screen into a
//   texture the pause menu's backdrop shares);
// - the back: Gm_MemoryCollect (the Memories' count) with only its paper window W_window_00, as on the Jukebox
//   page, sized to the header and the rows;
// - the list: Gm_MemoryList (ap_list.cpp), one row per location of the icon, its name without the area; the header
//   above the rows names the icon (as its hover text) with its count (CountText) on the right, in two
//   Gm_SystemOpUnit text boxes;
// - per row the icon's dot of the location's look: a Gm_MapMarker at the frame of its SetMark animation the map's
//   rows use (ap_map_icons.py), and for a skipped location "skipped" after its name (Gm_SystemOpUnit, italic).
// While it is open the map and the header see no buttons and no sticks (MapListTakesPad); the list reads the pad
// itself. B closes it; A marks the location under the cursor skipped or not, as A on an icon of one location does.
namespace {
    namespace L = AP::ListUi;

    // Gm_MapMarker's SetMark animation picks the marker's texture and size by frame: the game's markers, then the map
    // tracker's looks at a group's size (ap_map_icons.frame).
    constexpr int kGameMarkFrames = 7;  // ap_map_icons.GAME_FRAMES

    // Gm_MapMarkList: in N_MarkPosition_00 the bar's black P_pict_00, its windows, pins, count and cursor.
    constexpr const char* kDimLayout = "Gm_MapMarkList.bflyt";
    constexpr const char* kDimHidden[] = {"W_window_01", "W_window_02", "P_pict_01", "A_PinNum_00", "A_alignment_00"};
    constexpr float kDimY = -16, kDimWidth = 1280, kDimHeight = 525;  // the map's part of the page
    // Gm_MemoryCollect: in N_Collect_00 the text T_Collect_00, the paper window W_window_00 (rounded corners) and
    // the count A_Collect_00, hidden here but the window.
    constexpr const char* kBackLayout = "Gm_MemoryCollect.bflyt";
    constexpr const char* kBackHidden[] = {"T_Collect_00", "A_Collect_00"};
    // The list's rows span x -197..195 from its middle; its scroll bar (x 214, 30 wide, its track 390 high at y -20)
    // is fitted to the rows, and the panel widens to hold it while the locations do not fit.
    constexpr float kRowLeft = -197, kRowRight = 195, kScrollY = L::kRowTop - L::kRowStep * 3, kScrollHeight = 390;
    constexpr float kScrollRoom = 28;
    constexpr const char* kDotLayout = "Gm_MapMarker.bflyt";
    constexpr const char* kTextLayout = "Gm_SystemOpUnit.bflyt";
    constexpr int kFadeStep = 48;
    constexpr u16 kNameChars = 48;      // room in a row's text box: the longest location name and more
    constexpr float kNameWidth = 1000;  // a row's text box's width (338 in the file), so a long name does not wrap
    constexpr float kTagGap = 10, kTagLower = -3;  // "skipped" after a name: the space between, how much lower
    constexpr float kNameSqueeze = 0.6f;  // a long name is narrowed to fit its row, down to this
    // A colour tag in the text does not reach a text box of a layout: its colours are set instead.

    // The panel (layout units): its middle, its margins beside the rows and above and below them, the header's
    // height; a row's dot and the right end of its name and "skipped", from the list's middle.
    constexpr float kPanelX = 0, kPanelY = -15, kPanelSide = 25, kPanelMargin = 18, kHeaderHeight = 52;
    constexpr float kHeaderScale = 1.1f, kDotX = -168, kDotScale = 1.4f, kNameRight = 180;
    constexpr float kTagScale = 0.72f, kTagSlant = 0.25f;
    constexpr int kPanelAlpha = 245, kDimAlpha = 150;
    constexpr int kDoneAlpha = 170;  // a checked or skipped location's name

    constexpr u8 kInk[] = {0x0f, 0x07, 0x02, 0xff};   // the header's: dark brown on the paper (shows as 46 30 1e)
    constexpr u8 kGray[] = {0xb4, 0xb4, 0xb4, 0xff};  // "skipped": light grey (shows as d9)

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
    Panel g_Panel;
    int g_ListsBuilt = 0, g_ListToggles = 0;
#ifdef LASAP_TEST
    int g_ListAsked = -1;           // DBG maplist open <group>: the list the map's next frame opens
    int g_DotListAsked = -1;        // DBG mapdot press <dot>: the dot the dungeon map's next frame takes A on
    // The list's cost on the game thread, in system ticks (19.2 MHz): its last opening, and each frame's tick since
    // (sum, most, frames).
    u64 g_OpenTicks = 0, g_TickSum = 0, g_TickMost = 0, g_TickFrames = 0;
    using Game::Ticks;
#endif
    u64 g_PadHidden = 0;            // buttons held when the list closed: hidden from the game until released

    bool ListOpen() { return g_Panel.page != nullptr && !g_Panel.closing; }

    Map::MapCursor* FindCursor() { return reinterpret_cast<Map::MapCursor*>(Ui::FindUi(Map::kMapCursorUi)); }

    // The look a location's row shows: checked or skipped collected, else in or out of logic.
    Look RowLook(int id) { return Done(id) ? Collected : Reachable(id) ? InLogic : OutOfLogic; }

    std::string CheckName(int id) {
        auto it = g_CheckNames.find(id);
        return it != g_CheckNames.end() ? it->second : "Location " + std::to_string(id);
    }

    const std::vector<int>& ListIds() { return g_Panel.ids; }

    // The map's cursor and its icon's name, hidden behind the list.
    void ShowMapCursor(bool shown) {
        Ui::SetVisible(Ui::RootOf(Ui::FindUi(Map::kMapCursorUi)), shown);
    }

    void DropList() {
        if (g_Panel.page == nullptr) return;
        ShowMapCursor(true);
        for (auto& mark : g_Panel.marks) Game::Release(mark.owner);
        for (auto& tag : g_Panel.tags) L::Drop(tag);
        for (auto& dot : g_Panel.dots) L::Drop(dot);
        L::Drop(g_Panel.count);
        L::Drop(g_Panel.title);
        L::DropList(g_Panel.list);
        L::Drop(g_Panel.back);
        L::Drop(g_Panel.dim);
        g_Panel = Panel{};
    }

    // The header's count, the rows' names, dots and tags.
    void FillList() {
        const auto& ids = ListIds();
        L::SetText(g_Panel.title.note, g_Panel.heading);
        L::SetText(g_Panel.count.note, CountText(ids));
        L::List& list = g_Panel.list;
        for (int r = 0; r < L::kRows; r++) {
            int entry = list.top + r;
            bool shown = entry < list.count;
            Ui::SetVisible(g_Panel.dots[r].root, shown);
            Ui::SetVisible(g_Panel.tags[r].root, shown && AP::IsLocationSkipped(ids[entry]));
            if (!shown) continue;
            int id = ids[entry];
            L::Row& row = list.rows[r];
            L::SetText(row.text, CheckName(id));
            Ui::SetAlpha(Ui::FindPane(row.part, "A_Item_00"), Done(id) ? kDoneAlpha : 255);
            Ui::Animator* animator = g_Panel.marks[r].animator;
            float frame = static_cast<float>(kGameMarkFrames + RowLook(id));
            if (animator != nullptr && frame < Game::Animator_FrameCount(animator)) animator->frame = frame;
        }
        L::Place(list);
    }

    // Once a frame: the fade, the header's texts at its edges, each row's text fitted to the row with its tag after
    // it (the text boxes' extents are known once the font has drawn their glyphs).
    void LayOut() {
        g_Panel.fade = std::clamp(g_Panel.fade + (g_Panel.closing ? -kFadeStep : kFadeStep), 0, 255);
        for (L::Layout* layout : {&g_Panel.dim, &g_Panel.back, &g_Panel.list.layout, &g_Panel.title, &g_Panel.count}) {
            Ui::SetAlpha(layout->root, g_Panel.fade);
        }
        ShowMapCursor(g_Panel.closing);
        // the panel: the header, then the rows
        L::List& list = g_Panel.list;
        int rows = std::min(list.count, L::kRows);
        float left = kRowLeft - kPanelSide, right = kRowRight + kPanelSide + (list.count > L::kRows ? kScrollRoom : 0);
        float width = right - left, height = 2 * kPanelMargin + kHeaderHeight + L::kRowStep * rows;
        float top = kPanelY + height / 2, x = kPanelX - (left + right) / 2;  // the list's middle
        float y = top - kPanelMargin - kHeaderHeight - L::kRowStep / 2 - L::kRowTop;
        Ui::MoveTo(list.layout.root, x, y);
        Ui::MoveTo(g_Panel.window, kPanelX, kPanelY);
        g_Panel.window->width = width;
        g_Panel.window->height = height;
        for (L::Layout* text : {&g_Panel.title, &g_Panel.count}) {
            float textLeft, textRight;
            if (!L::Span(text->note, textLeft, textRight)) continue;
            // the title's left edge over the rows' left end, the count's right edge over their right end
            float at = text == &g_Panel.title ? x + kRowLeft - textLeft * kHeaderScale
                                              : x + kRowRight - textRight * kHeaderScale;
            Ui::MoveTo(text->root, at, top - kPanelMargin - kHeaderHeight / 2);
        }
        for (int r = 0; r < L::kRows; r++) {
            float rowY = y + L::kRowTop - L::kRowStep * r;
            Ui::SetAlpha(g_Panel.dots[r].root, g_Panel.fade);
            Ui::MoveTo(g_Panel.dots[r].root, x + kDotX, rowY);
            L::Row& row = list.rows[r];
            Ui::Pane* align = Ui::FindPane(row.part, "A_Item_00");
            L::Layout& tag = g_Panel.tags[r];
            bool tagged = list.top + r < list.count && AP::IsLocationSkipped(ListIds()[list.top + r]);
            float nameLeft, nameRight, tagLeft = 0, tagRight = 0;
            if (tagged && !L::Span(tag.note, tagLeft, tagRight)) tagged = false;
            Ui::SetAlpha(tag.root, tagged ? g_Panel.fade : 0);
            if (align == nullptr || !L::Span(row.text, nameLeft, nameRight)) continue;
            // the name on the page: the list, the alignment pane, the text box in it (scaled about its origin)
            float origin = x + align->x + row.text.box->pane.x;
            float tagWidth = tagged ? (tagRight - tagLeft) * kTagScale + kTagGap : 0;
            float room = x + kNameRight - tagWidth - (origin + nameLeft);
            float scale = std::clamp(room / (nameRight - nameLeft), kNameSqueeze, 1.0f);
            Ui::ScaleTo(&row.text.box->pane, scale, 1.0f);
            Ui::MoveTo(tag.root, origin + nameRight * scale + kTagGap - tagLeft * kTagScale, rowY + kTagLower);
        }
    }

    void SetTextColor(Ui::TextBox* box, const u8 (&color)[4]) {
        if (box == nullptr) return;
        std::memcpy(box->colorTop, color, 4);
        std::memcpy(box->colorBottom, color, 4);
    }

    bool BuildText(L::Layout& layout, Ui::LayoutWrapper* page, Ui::Pane* root, u16 chars) {
        if (!L::Build(layout, kTextLayout, page, root)) return false;
        Ui::TextBox* box = Ui::FindText(layout.root, "T_Operation_00");
        if (box != nullptr && Ui::Allocate(box, chars)) L::Bind(layout.note, box);
        return true;
    }

    // A on an icon of several locations: its check list over the map.
    void OpenList(Ui::LayoutWrapper* page, const std::vector<int>& ids, const std::string& heading, int group) {
        DropList();
        Ui::Pane* root = Ui::RootOf(page);
        if (root == nullptr) return;
        g_Panel.page = page;
        g_Panel.group = group;
        g_Panel.ids = ids;
        g_Panel.heading = heading;
        bool ok = L::Build(g_Panel.dim, kDimLayout, page, root) && L::Build(g_Panel.back, kBackLayout, page, root) &&
                  L::BuildList(g_Panel.list, page, root, kPanelX, kPanelY) &&
                  BuildText(g_Panel.title, page, root, 40) && BuildText(g_Panel.count, page, root, 12);
        for (int r = 0; ok && r < L::kRows; r++) {
            ok = L::Build(g_Panel.dots[r], kDotLayout, page, root) &&
                 BuildText(g_Panel.tags[r], page, root, 16);
        }
        if (!ok) {
            DropList();
            return;
        }
        for (const char* name : kDimHidden) Ui::SetVisible(Ui::FindPane(g_Panel.dim.root, name), false);
        g_Panel.shade = Ui::FindPane(g_Panel.dim.root, "P_pict_00");
        Ui::MoveTo(g_Panel.shade, 0, kDimY);
        Ui::SetAlpha(g_Panel.shade, kDimAlpha);
        if (g_Panel.shade != nullptr) {
            g_Panel.shade->width = kDimWidth;
            g_Panel.shade->height = kDimHeight;
        }
        for (const char* name : kBackHidden) Ui::SetVisible(Ui::FindPane(g_Panel.back.root, name), false);
        g_Panel.window = Ui::FindPane(g_Panel.back.root, "W_window_00");
        Ui::SetAlpha(g_Panel.window, kPanelAlpha);
        for (L::Layout* text : {&g_Panel.title, &g_Panel.count}) {
            Ui::ScaleTo(text->root, kHeaderScale);
            SetTextColor(text->note.box, kInk);
        }
        Ui::MoveTo(g_Panel.list.scroll, g_Panel.list.scroll->x, kScrollY);
        Ui::ScaleTo(g_Panel.list.scroll, 1, L::kRowStep * L::kRows / kScrollHeight);
        for (int r = 0; r < L::kRows; r++) {
            L::Row& row = g_Panel.list.rows[r];
            // one line however long the name: it is shrunk to fit the row instead
            Ui::Allocate(row.text.box, kNameChars);
            if (row.text.box != nullptr) row.text.box->pane.width = kNameWidth;
            Ui::ScaleTo(g_Panel.dots[r].root, kDotScale);
            g_Panel.marks[r] = Ui::GetAnimation(g_Panel.dots[r].wrapper, "SetMark");
            L::Layout& tag = g_Panel.tags[r];
            Ui::ScaleTo(tag.root, kTagScale);
            if (tag.note.box != nullptr) tag.note.box->italicRatio = kTagSlant;
            SetTextColor(tag.note.box, kGray);
            L::SetText(tag.note, "skipped");
        }
        L::SetEntries(g_Panel.list, static_cast<int>(ids.size()), 0);
        FillList();
        LayOut();
        L::RefreshGuide(0);
        PlaySound("SE_SY_MAP_OMOIDE_ON");
        g_ListsBuilt++;
        Logging.Log("[AP] map: check list of " + heading);
    }

    void CloseList() {
        g_Panel.closing = true;
        g_PadHidden = L::PadHeld();
        L::RefreshGuide(0);
        PlaySound("SE_SY_MAP_OMOIDE_OFF");
    }

    // A on a row: the location skipped or not, as on an icon of one location; a location checked in the game or on
    // the server is not.
    void ToggleRow() {
        int id = ListIds()[g_Panel.list.at];
        if (AP::IsLocationChecked(id)) {
            PlaySound("SE_SY_BEEP");
            return;
        }
        AP::SetLocationSkipped(id, !AP::IsLocationSkipped(id));
        g_ListToggles++;
        RefreshOpenMap();
        PlaySound("SE_SY_DECIDE");
    }

    // Once a frame of the map's KeyWait while the list is there.
    void TickList(Ui::LayoutWrapper* page) {
        if (page != g_Panel.page) return;
        LayOut();
        if (g_Panel.closing) {
            if (g_Panel.fade == 0) DropList();
            return;
        }
        bool moved;
        u64 pressed = L::Input(g_Panel.list, moved);
        if (moved) FillList();
        if (pressed & L::kButtonB) {
            CloseList();
        } else if (pressed & L::kButtonA) {
            ToggleRow();
        }
    }

    // The map's guide while the list is open: A Skip / unskip, B Back.
    const L::Guide* ListGuide(u32 index) {
        static const L::Guide guide{2, {L::kGuideA, L::kGuideB}, {"", "", "Skip / unskip"}};
        return index == 0 && ListOpen() ? &guide : nullptr;
    }

    // Whether A opens the group's check list: it has several locations and its name is not hidden.
    bool Listable(int group) {
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size() || g_Overworld[group].size() < 2) {
            return false;
        }
        return !NameHidden(group);
    }
}

// The dungeon page's dots (DESIGN.md, Map tracker, Dungeon dots). The page's setup shows each room on its floor
// (ShowCellFloor), which also gives the rooms by name; after it, each dot of the dungeon gets a map marker placed as
// the page places its room icons and hung after them under N_MapPin_00, so the pins draw over the dots. The floor
// buttons show the rooms again, and the dots of each room follow it.
namespace {
    const std::pair<Map::Area*, bool>* FindRoom(const std::string& room) {
        auto it = g_Rooms.find(room);
        return it != g_Rooms.end() ? &it->second : nullptr;
    }

    // A room's name as the dots have it: its file's ("level/Lv01TailCave/Lv01TailCave_03G.leb") without the folder
    // and the extension.
    std::string RoomName(const Map::Area* room) {
        std::string_view path(room->path, std::min<u64>(room->pathLength, sizeof(room->path)));
        size_t slash = path.rfind('/');
        if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
        if (path.size() > 4 && path.substr(path.size() - 4) == ".leb") path.remove_suffix(4);
        return std::string(path);
    }

    // A map marker's or room icon's root pane.
    Ui::Pane* MarkerRoot(Ui::LayoutWrapper* wrapper) {
        return Game::CallVirtual<Ui::Pane*()>(wrapper, Ui::LayoutWrapperVirtual::kRootPane);
    }

    // A dot shows while its room is on the shown floor, unless its look is out of logic and those are toggled off.
    bool DotShown(const DotMarker& dm) {
        return dm.onFloor && !(g_OutHidden && IdsLook(g_Dots[dm.dot].ids, false) == OutOfLogic);
    }

    // Each dot's look (its SetMark frame, smaller for one check) and whether it shows.
    void ApplyDots() {
        for (auto& dm : g_DotMarkers) {
            const Dot& dot = g_Dots[dm.dot];
            Ui::Animator* animator = dm.marker->setMark;
            int look = IdsLook(dot.ids, false);
            float frame = static_cast<float>(kGameMarkFrames + (dot.ids.size() == 1 ? kLooks : 0) + look);
            if (animator != nullptr && frame < Game::Animator_FrameCount(animator)) animator->frame = frame;
            Ui::SetVisible(MarkerRoot(&dm.marker->wrapper), DotShown(dm));
        }
    }

    void SetMarkerState(Map::MapMarker* marker, u8 state) {
        Game::MapMarker_ChangeState(&marker->machine, state);
    }


    void DropDots() {
        for (auto& dm : g_DotMarkers) Game::ReleaseOwner(dm.control);
        g_DotMarkers.clear();
        g_DotHovered = -1;
        g_DungeonPage = nullptr;
    }

    // After the dungeon page's setup: the dots of the current dungeon whose room the page has.
    void MakeDots(Map::DungeonMap* page) {
        DropDots();
        g_DungeonPage = page;
        std::string level = LevelSystem::GetCurrentLevel() + "_";
        Ui::Pane* pinRoot = page->mapPin;
        Ui::Pane* pinOffset = page->pinOffset;
        if (pinRoot == nullptr || pinOffset == nullptr) return;
        size_t missing = 0;
        for (size_t d = 0; d < g_Dots.size(); d++) {
            const Dot& dot = g_Dots[d];
            if (dot.room.compare(0, level.size(), level) != 0) continue;
            const auto* room = FindRoom(dot.room);
            if (room == nullptr) {
                missing++;
                continue;
            }
            Game::SharedPtr made{}, roomPtr{room->first, nullptr};
            Game::MapMarker_Make(&made);
            Map::MapMarker* marker = MarkerOf(made);
            if (marker == nullptr) continue;
            Game::MapMarker_SetupPart(marker);
            Game::LayoutWrapper_AttachBefore(&marker->wrapper, &page->wrapper, pinRoot, pinOffset);
            float at[2] = {};
            Game::DungeonMap_PositionIcon(dot.x, 0, dot.z, page, at, &roomPtr);
            Ui::MoveTo(MarkerRoot(&marker->wrapper), at[0], at[1]);
            DotMarker dm{d, marker, made.ctrl};
            // under N_MapPosition_00, which moves after the setup (DotAt)
            dm.x = at[0] + pinRoot->x;
            dm.y = at[1] + pinRoot->y;
            dm.onFloor = room->second;
            g_DotMarkers.push_back(dm);
        }
        g_DotsMade += static_cast<int>(g_DotMarkers.size());
        ApplyDots();
        Logging.Log("[AP] map: " + std::to_string(g_DotMarkers.size()) + " dungeon dots, " + std::to_string(missing) +
                    " without their room");
    }

    // A room shown or hidden for the shown floor: its dots follow it (a hidden dot loses the hover on the next frame
    // of KeyWait).
    void ShowRoomDots(const std::string& name, bool onFloor) {
        for (auto& dm : g_DotMarkers) {
            if (g_Dots[dm.dot].room == name) dm.onFloor = onFloor;
        }
        ApplyDots();
    }

    // A dot's position in the cursor's units (N_MapOrigin_00's): N_MapPosition_00 moves after the page's setup (y 48
    // in Tail Cave), as the pins' hover reads it each frame.
    void DotAt(const Map::DungeonMap* page, const DotMarker& dm, float& x, float& y) {
        const Ui::Pane* position = page->mapPosition;
        x = dm.x + (position != nullptr ? position->x : 0);
        y = dm.y + (position != nullptr ? position->y : 0);
    }

    const std::vector<int>* HoveredDotIds() {
        return g_DotHovered >= 0 ? &g_Dots[g_DotMarkers[g_DotHovered].dot].ids : nullptr;
    }

    // A name on the map cursor: Place:<label>, whose text MapLabelText gives.
    void ShowCursorName(std::string_view placeLabel) {
        Map::MapCursor* cursor = FindCursor();
        if (cursor == nullptr) return;
        std::string label = "Place:" + std::string(placeLabel);
        Game::StrView view{label};
        Game::MessageRef ref{};
        u8* manager = *Game::MessageManager_ptr;
        if (manager == nullptr) return;
        Game::MessageManager_Find(&ref, manager, &view);
        if (ref.index >= 0) Game::MapCursor_ShowName(cursor, &ref);
        Game::ReleaseOwner(ref.ctrl);
    }

    // The hovered dot's name on the map cursor.
    void ShowDotName() {
        ShowCursorName(AP::MAP_DOT_LABEL);
    }

    void HideCursorName() {
        Map::MapCursor* cursor = FindCursor();
        if (cursor != nullptr) cursor->nameHidden = 0x101;
    }

    // Once a frame of the dungeon page's KeyWait, after its pins' hover: the nearest shown dot within the pins'
    // radius, unless a pin is hovered or Y shows the dungeon's info.
    void UpdateDotHover(Map::DungeonMap* page) {
        for (auto& dm : g_DotMarkers) {
            Game::CallVirtual<void()>(dm.marker, Map::MapMarkerVirtual::kUpdate);
        }
        int hovered = -1;
        Map::MapCursor* cursor = FindCursor();
        bool free = page->hoveredPin == nullptr && page->info == 0 && !ListOpen() && cursor != nullptr;
        if (free) {
            float radius = cursor->size * kHoverRadius * page->cursorScale;
            float cx = page->cursorX, cy = page->cursorY;
            float best = radius * radius;
            for (size_t i = 0; i < g_DotMarkers.size(); i++) {
                const DotMarker& dm = g_DotMarkers[i];
                if (!DotShown(dm)) continue;
                float x, y;
                DotAt(page, dm, x, y);
                float dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
                if (d2 <= best) {
                    best = d2;
                    hovered = static_cast<int>(i);
                }
            }
        }
        if (hovered == g_DotHovered) return;
        if (g_DotHovered >= 0) SetMarkerState(g_DotMarkers[g_DotHovered].marker, Map::kMarkerOff);
        bool had = g_DotHovered >= 0;
        g_DotHovered = hovered;
        if (hovered >= 0) {
            SetMarkerState(g_DotMarkers[hovered].marker, Map::kMarkerOn);
            ShowDotName();
        } else if (had && page->hoveredPin == nullptr) {
            HideCursorName();
        }
    }

    // After the dungeon page moves its cursor: with the stick idle the cursor eases onto the hovered dot, as onto a
    // hovered pin.
    void EaseOntoDot(Map::DungeonMap* page) {
        if (g_DotHovered < 0 || page->hoveredPin != nullptr || page->info != 0 || page->cursorSpeed != kIdleSpeed) {
            return;
        }
        float x, y;
        DotAt(page, g_DotMarkers[g_DotHovered], x, y);
        page->cursorX += (x - page->cursorX) * kEase;
        page->cursorY += (y - page->cursorY) * kEase;
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
}

HOOK_DEFINE_INLINE(DungeonMapFlags) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        g_FlagsCalls++;
        if (g_Enabled) ctx->W[8] |= 3;  // compass and map
    }
};

HOOK_DEFINE_INLINE(MarkerAdded) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        g_MarkerPage = reinterpret_cast<Map::OverworldMap*>(ctx->X[19]);
        auto* marker = reinterpret_cast<Map::MapMarker*>(ctx->X[26]);
        Look look;
        int group = MarkerIcon(marker, look);
        if (group >= 0 && !LookShown(group, look)) SetMarkerVisible(marker, false);
    }
};

HOOK_DEFINE_INLINE(FieldPinButton) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        auto* page = reinterpret_cast<Map::OverworldMap*>(ctx->X[19]);
        if (g_Panel.page != nullptr) {
#ifdef LASAP_TEST
            u64 start = Ticks();
            TickList(&page->wrapper);
            u64 spent = Ticks() - start;
            g_TickSum += spent;
            g_TickMost = std::max(g_TickMost, spent);
            g_TickFrames++;
#else
            TickList(&page->wrapper);
#endif
            ctx->W[0] = 0;
            return;
        }
        if (!g_Enabled || g_IconsHidden) return;
#ifdef LASAP_TEST
        Look look;
        g_HoverGroup = HoveredIcon(page, look);
        g_HoverLook = g_HoverGroup >= 0 ? look : -1;
        if (Listable(g_ListAsked)) {
            u64 start = Ticks();
            OpenList(&page->wrapper, g_Overworld[g_ListAsked], g_GroupNames[g_ListAsked], g_ListAsked);
            g_OpenTicks = Ticks() - start;
            g_TickSum = g_TickMost = g_TickFrames = 0;
            g_ListAsked = -1;
            ctx->W[0] = 0;
            return;
        }
#endif
        if ((ctx->W[0] & 1) == 0) return;
        Look hovered;
        int group = HoveredIcon(page, hovered);
        if (Listable(group)) {
#ifdef LASAP_TEST
            u64 start = Ticks();
            OpenList(&page->wrapper, g_Overworld[group], g_GroupNames[group], group);
            g_OpenTicks = Ticks() - start;
            g_TickSum = g_TickMost = g_TickFrames = 0;
#else
            OpenList(&page->wrapper, g_Overworld[group], g_GroupNames[group], group);
#endif
            ctx->W[0] = 0;  // no pin menu
        } else if (ToggleSkipped(page)) {
            ctx->W[0] = 0;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(MapPageDtor) {
    static void Callback(Map::OverworldMap* page) {
        if (&page->wrapper == g_Panel.page) DropList();
        if (page == g_MarkerPage) g_MarkerPage = nullptr;
        Orig(page);
    }
};

HOOK_DEFINE_INLINE(FieldMemoryButton) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        if (ctx->W[21] == 0 || !g_Enabled) return;
        ToggleOutOfLogic(reinterpret_cast<const void*>(ctx->X[19]));
        ctx->W[21] = 0;  // no memories
    }
};

HOOK_DEFINE_TRAMPOLINE(MemoryButton) {
    static bool Callback(Map::DungeonMap* page) {
        bool pressed = Orig(page);
        if (!pressed || !g_Enabled) return pressed;
        ToggleOutOfLogic(page);
        return false;  // no memories
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonIconUpdate) {
    static void Callback(Map::DungeonMapIcon* icon, const Map::MapActorDesc* actor) {
        Orig(icon, actor);
        g_IconCalls++;
        if (!g_Enabled || g_Chests.empty()) return;
        // A chest of the slot's: its dot stands for it, so its icon goes (shown flag clear, which the floor's
        // set-shown keeps).
        std::string level = LevelSystem::GetCurrentLevel();
        for (const auto& chest : g_Chests) {
            if (chest.level == level && std::fabs(chest.x - actor->x) < 0.01f && std::fabs(chest.z - actor->z) < 0.01f) {
                g_ChestMatches++;
                icon->shown = 0;
                Ui::SetVisible(MarkerRoot(&icon->wrapper), false);
                return;
            }
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonPageSetup) {
    static void Callback(Map::DungeonMap* page) {
        g_Rooms.clear();
        Orig(page);
        if (g_Enabled) MakeDots(page);
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonPageDtor) {
    static void Callback(Map::DungeonMap* page) {
        if (&page->wrapper == g_Panel.page) DropList();
        if (page == g_DungeonPage) DropDots();
        Orig(page);
    }
};

HOOK_DEFINE_TRAMPOLINE(ShowCellFloor) {
    static void Callback(Map::DungeonMap* page, u8 cell, Game::SharedPtr* room, const Map::UiDungeonAreaRow* area) {
        Orig(page, cell, room, area);
        if (!g_Enabled || room == nullptr || room->ptr == nullptr || area == nullptr) return;
        auto* roomArea = static_cast<Map::Area*>(room->ptr);
        std::string name = RoomName(roomArea);
        bool onFloor = area->floor == page->shownFloor;
        g_Rooms[name] = {roomArea, onFloor};
        if (page == g_DungeonPage) ShowRoomDots(name, onFloor);
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonMoveCursor) {
    static void Callback(Map::DungeonMap* page, bool move) {
        Orig(page, move);
        if (page == g_DungeonPage) EaseOntoDot(page);
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonUpdateHover) {
    static void Callback(Map::DungeonMap* page) {
        Orig(page);
        if (page == g_DungeonPage) UpdateDotHover(page);
    }
};

// A in the dungeon page's KeyWait. The page is not passed: it is the one with dots.
HOOK_DEFINE_TRAMPOLINE(DungeonPinButton) {
    static bool Callback() {
        bool pressed = Orig();
        Map::DungeonMap* page = g_DungeonPage;
        if (page == nullptr || page->machine.state != Map::kKeyWait) return pressed;
        if (g_Panel.page == &page->wrapper) {
            TickList(&page->wrapper);
            return false;
        }
#ifdef LASAP_TEST
        if (g_DotListAsked >= 0 && static_cast<size_t>(g_DotListAsked) < g_DotMarkers.size()) {
            g_DotHovered = g_DotListAsked;
            pressed = true;
        }
        g_DotListAsked = -1;
#endif
        const std::vector<int>* ids = HoveredDotIds();
        if (!pressed || ids == nullptr || page->hoveredPin != nullptr) return pressed;
        if (ids->size() > 1) {
            OpenList(&page->wrapper, *ids, g_Dots[g_DotMarkers[g_DotHovered].dot].name, -1);
        } else if (Skippable(*ids)) {
            AP::SetLocationSkipped((*ids)[0], !AP::IsLocationSkipped((*ids)[0]));
            ApplyDots();
            ShowDotName();
            PlaySound("SE_SY_DECIDE");
        } else {
            PlaySound("SE_SY_BEEP");
        }
        return false;  // no pin
    }
};

namespace AP {
    void LoadMap() {
        // Written by the patcher (ap_map.py): "O <group> <local id>...", "M <dot> <room> <x> <z> <local id>..." (a
        // dungeon dot), "C <level> <x> <z> <local id>" (a dungeon chest), "G <group> <name>" (an overworld group's
        // name), "MG <dot> <name>" (a dot's name), "H <group> <flag> <name>" (the group's name until the flag is set)
        // and "N <local id> <name>" (a location's name without its area, for the check list) lines.
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(MAP_PATH, text))) {
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
        DungeonMapFlags::InstallAtPtr(Game::Addr(kDungeonMapFlags));
        DungeonIconUpdate::InstallAtFuncPtr(Game::DungeonMapIcon_Update.Ptr());
        MarkerAdded::InstallAtPtr(Game::Addr(kMarkerAdded));
        MemoryButton::InstallAtFuncPtr(Game::DungeonMap_MemoriesButton.Ptr());
        FieldMemoryButton::InstallAtPtr(Game::Addr(kFieldMemoryButton));
        FieldPinButton::InstallAtPtr(Game::Addr(kFieldPinButton));
        MapPageDtor::InstallAtFuncPtr(Game::OverworldMap_Dtor.Ptr());
        DungeonPageSetup::InstallAtFuncPtr(Game::DungeonMap_Setup.Ptr());
        DungeonPageDtor::InstallAtFuncPtr(Game::DungeonMap_Dtor.Ptr());
        ShowCellFloor::InstallAtFuncPtr(Game::DungeonMap_ShowCellFloor.Ptr());
        DungeonMoveCursor::InstallAtFuncPtr(Game::DungeonMap_MoveCursor.Ptr());
        DungeonUpdateHover::InstallAtFuncPtr(Game::DungeonMap_UpdateHover.Ptr());
        DungeonPinButton::InstallAtFuncPtr(Game::DungeonMap_PinButton.Ptr());
        AP::ListUi::AddGuideSource(ListGuide);
        Logging.Log("[AP] map tracker: " + std::to_string(g_Overworld.size()) + " overworld icons, " +
                    std::to_string(g_Dots.size()) + " dungeon dots, " + std::to_string(g_Chests.size()) + " chests");
    }

    bool MapFlag(std::string_view name, bool& value) {
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

    int MapLabelGroup(std::string_view label) {
        if (!g_Enabled) return -1;
        if (label == MAP_DOT_LABEL) return kDotGroup;
        Look look;
        return ParseIcon(label, look);
    }

    std::string MapLabelText(int group) {
        // The group's name, after its count (CountText) when it has several locations and some are unchecked, or
        // after "Skipped: " when its one location is skipped; or its other name
        // alone until its flag is set (the count would tell a dungeon by its number of locations). The dungeon dots'
        // label names the hovered dot the same way.
        if (group == kDotGroup) {
            const std::vector<int>* ids = HoveredDotIds();
            return ids != nullptr ? IconText(*ids, g_Dots[g_DotMarkers[g_DotHovered].dot].name) : "";
        }
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return "";
        if (NameHidden(group)) return g_HiddenNames[group].second;
        std::string name = static_cast<size_t>(group) < g_GroupNames.size() ? g_GroupNames[group] : "";
        return IconText(g_Overworld[group], name);
    }

    void MapDataChanged() {
        g_MapDataChanged = g_MapDataChangedLately = true;
    }

    void TickMap() {
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

    std::string MapCaptionText() {
        // Offline, the looks are those of the last logic the client sent, or all in logic before it sent any.
        if (!g_Enabled) return "";
        if (AP::IsServerConnected()) return "Toggle out of logic";
        return AP::HasLogic() ? "Offline: toggle out of logic" : "Offline: logic unknown";
    }

    bool MapListTakesPad(nn::hid::NpadBaseState* state) {
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
    void NoteMapText(const std::string& text) {
        g_CursorText = text;
    }
#endif

    void SetMapMode(int mode) {
        g_Enabled = g_Loaded && mode != 0;
        g_IconsHidden = mode == 2;
    }

#ifdef LASAP_TEST
    int MapHoverGroup() {
        return g_HoverGroup;
    }

    std::string MapListField() {
        if (g_Panel.page == nullptr) return " mlist=-";
        return " mlist=" + std::to_string(g_Panel.group) + "/" + std::to_string(g_Panel.list.at) + "/" +
               std::to_string(g_Panel.list.top) + (g_Panel.closing ? "/closing" : "");
    }

    std::string MapDotInfo(const std::vector<std::string>& a) {
        // DBG mapdot [cursor <n> | near <n> | press <n>]: the open dungeon page's dots; cursor puts the page's cursor
        // on dot n (near: 8 units right of it, inside the hover's radius) and runs the dots' hover, press takes A on
        // dot n the next frame, as if hovered.
        if (a.size() >= 4 && g_DungeonPage != nullptr) {
            size_t n = static_cast<size_t>(std::atoi(a[3].c_str()));
            if ((a[2] == "cursor" || a[2] == "near") && n < g_DotMarkers.size()) {
                DotAt(g_DungeonPage, g_DotMarkers[n], g_DungeonPage->cursorX, g_DungeonPage->cursorY);
                if (a[2] == "near") g_DungeonPage->cursorX += 8;
                UpdateDotHover(g_DungeonPage);
            } else if (a[2] == "press") {
                g_DotListAsked = static_cast<int>(n);
            }
        }
        // Each dot's look as drawn: its SetMark frame, and its root pane's visibility.
        static const char kLookLetters[] = "ipoc";
        std::string looks;
        size_t shown = 0;
        for (const auto& dm : g_DotMarkers) {
            const Ui::Animator* animator = dm.marker->setMark;
            int look = animator == nullptr ? -1 :
                static_cast<int>(animator->frame) - kGameMarkFrames - (g_Dots[dm.dot].ids.size() == 1 ? kLooks : 0);
            bool on = (MarkerRoot(&dm.marker->wrapper)->flags & Ui::kPaneVisible) != 0;
            shown += on;
            char letter = look >= 0 && look < kLooks ? kLookLetters[look] : '?';
            looks += on ? letter : static_cast<char>(std::toupper(letter));
        }
        std::string s = "MAPDOT made=" + std::to_string(g_DotsMade) + " page=" + (g_DungeonPage ? "1" : "0") +
                        " dots=" + std::to_string(g_DotMarkers.size()) + " shown=" + std::to_string(shown) +
                        " rooms=" + std::to_string(g_Rooms.size()) + " chests=" + std::to_string(g_ChestMatches);
        if (g_DungeonPage != nullptr) {
            s += " floor=" + std::to_string(g_DungeonPage->shownFloor) + " cursor=" +
                 std::to_string(std::lround(g_DungeonPage->cursorX)) + "," +
                 std::to_string(std::lround(g_DungeonPage->cursorY));
        }
        s += " hover=" + std::to_string(g_DotHovered);
        if (g_DotHovered >= 0 && g_DungeonPage != nullptr) {
            const DotMarker& dm = g_DotMarkers[g_DotHovered];
            float x, y;
            DotAt(g_DungeonPage, dm, x, y);
            s += "/" + std::to_string(dm.dot) + "/" + std::to_string(g_Dots[dm.dot].ids.size()) + " at=" +
                 std::to_string(std::lround(x)) + "," + std::to_string(std::lround(y));
        }
        s += " looks=" + (looks.empty() ? std::string("-") : looks);
        return s + " | text=" + MapLabelText(kDotGroup);
    }

    std::string MapListInfo(const std::vector<std::string>& a) {
        // DBG maplist [open <group>]: the check list's state; open: the overworld map's next frame opens that
        // group's list.
        if (a.size() >= 4 && a[2] == "open") g_ListAsked = std::atoi(a[3].c_str());
        std::string s = "MAPLIST built=" + std::to_string(g_ListsBuilt) + " toggles=" + std::to_string(g_ListToggles);
        s += " hidden=" + std::to_string(g_PadHidden);
        // the last opening's time and each frame's since (mean/most), in microseconds
        s += " open_us=" + std::to_string(g_OpenTicks * 10 / 192) + " tick_us=" +
             std::to_string(g_TickSum * 10 / 192 / std::max<u64>(g_TickFrames, 1)) + "/" +
             std::to_string(g_TickMost * 10 / 192) + " ticks=" + std::to_string(g_TickFrames);
        if (g_Panel.page == nullptr) return s + " open=0";
        const auto& ids = ListIds();
        static const char kRowLooks[] = "ipoc";
        std::string looks;
        for (int id : ids) looks += IsLocationSkipped(id) ? 's' : kRowLooks[RowLook(id)];
        int texts = 0, drawn = 0;
        for (auto& row : g_Panel.list.rows) {
            if (row.text.box == nullptr) continue;
            texts++;
            drawn += AP::ListUi::Drawn(row.text);
        }
        s += " open=" + std::string(g_Panel.closing ? "closing" : "1") + " group=" + std::to_string(g_Panel.group) +
             " at=" + std::to_string(g_Panel.list.at) + " top=" + std::to_string(g_Panel.list.top) +
             " looks=" + looks + " fade=" + std::to_string(g_Panel.fade) + " rows=" + std::to_string(texts) +
             " drawn=" + std::to_string(drawn) + " count=" + g_Panel.count.note.value +
             " name=" + CheckName(ids[g_Panel.list.at]) + " | title=" + g_Panel.title.note.value;
        return s;
    }
#endif

    std::string SkipState() {
#ifdef LASAP_TEST
        // The skipped locations of overworld icons, and the hovered icon: its group, locations and look, or -1. On
        // the open overworld page: its markers of ours, those shown by look, and the last name the cursor took.
        static const char* const kLookNames[kLooks] = {"in", "partly", "out", "collected"};
        size_t skipped = 0;
        for (const auto& ids : g_Overworld) {
            for (int id : ids) skipped += IsLocationSkipped(id) ? 1 : 0;
        }
        std::string drawn;
        if (g_MarkerPage != nullptr) {
            size_t made = 0, shown[kLooks] = {};
            for (const Game::SharedPtr& sp : g_MarkerPage->markers) {
                Map::MapMarker* marker = MarkerOf(sp);
                Look look;
                if (marker == nullptr || MarkerIcon(marker, look) < 0) continue;
                made++;
                if (MarkerPane(marker)->flags & Ui::kPaneVisible) shown[look]++;
            }
            drawn = " made=" + std::to_string(made) + " drawn=" + std::to_string(shown[InLogic]) + "," +
                    std::to_string(shown[Partial]) + "," + std::to_string(shown[OutOfLogic]) + "," +
                    std::to_string(shown[Collected]);
        }
        std::string hover = "-1";
        if (g_HoverGroup >= 0 && static_cast<size_t>(g_HoverGroup) < g_Overworld.size()) {
            hover = std::to_string(g_HoverGroup) + "/" + std::to_string(g_Overworld[g_HoverGroup].size()) + "/" +
                    kLookNames[g_HoverLook];
        }
        return " skipped=" + std::to_string(skipped) + " hover=" + hover + drawn + " | name=" + g_CursorText;
#else
        return "";
#endif
    }

    std::string MapState() {
        size_t looks[kLooks] = {};
        for (size_t group = 0; group < g_Overworld.size(); group++) looks[GroupLook(group)]++;
        size_t open = g_Overworld.size() - looks[Collected];
        return "map=" + std::string(g_Enabled ? "1" : "0") + " icons=" + std::to_string(open) + "/" +
               std::to_string(g_Overworld.size()) + " looks=" + std::to_string(looks[InLogic]) + "," +
               std::to_string(looks[Partial]) + "," + std::to_string(looks[OutOfLogic]) + "," +
               std::to_string(looks[Collected]) + " logic=" + (HasLogic() ? "1" : "0") +
               " server=" + (IsServerConnected() ? "1" : "0") +
               " hooks=" + std::to_string(g_FlagsCalls) + "," + std::to_string(g_IconCalls) + "," +
               std::to_string(g_DotsMade) + "," + std::to_string(g_ChestMatches) + " out=" +
               (g_OutHidden ? "hidden" : "shown") + SkipState();
    }
}
