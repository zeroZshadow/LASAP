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
// In dungeons the map draws the rooms Link visited and, with the compass, the chests (open or closed). Both come
// from the dungeon's DungeonItems entry: its visited-room mask is filled while Link is in the dungeon (the rooms then
// count as visited in the save), and an inline hook makes the compass and map owned for the map screen only, so the
// items keep their use. The icon update shows a chest open once its location is checked, also on the server. Dungeon
// checks that are not chests get UiDungeonIcon rows (chest icons) from the patcher; the icon update shows them open
// once all of the room's checks are done.
#include "AP/ap.hpp"
#include "AP/ap_list.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include "loggers.hpp"
#include "Config/file_handler.hpp"
#include "Hooks/Common/level.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // Dungeon map screen (game 1.0.0)
    constexpr uintptr_t kDungeonMapFlags = 0xe76204;    // w8 = DungeonItems flags byte (bit0 compass, bit1 map)
    // DungeonItems: per dungeon a pointer to its entry (flags byte at +1, visited-room mask u64 at +8, bit col + 8·row)
    constexpr uintptr_t kDungeonItems = 0x1cc11b0;
    constexpr uintptr_t kDungeonIndex = 0x1cc13c8;     // u8: the current dungeon, 0xff outside dungeons
    constexpr size_t kDungeonVisited = 8;
    constexpr uintptr_t kDungeonIconUpdate = 0xf38040;  // void(icon*, const actor*): visibility and open state
    // The actor an icon stands for: position, and switches (kind u8, index u16) for "shown" and "open". Icons from
    // UiDungeonIcon rows have the "shown" switch of kind 4 (always set).
    constexpr size_t kActorPosX = 0x4, kActorPosZ = 0xc, kActorShownKind = 0x1e;
    constexpr u8 kSwitchAlways = 4;
    // The icon's two state panes (open, closed); their virtuals set visibility and play the state.
    constexpr size_t kIconOpenPane = 0x14 * 8, kIconClosedPane = 0x17 * 8;
    constexpr size_t kPaneSetVisible = 0x20, kPanePlay = 0xc0;
    using PaneSetVisibleFn = void (*)(void*, bool);
    using PanePlayFn = void (*)(void*, float);

    // Field map (game 1.0.0)
    // The X button, which opens the memories: in the overworld map's KeyWait state w21 = X pressed (x19 page), and
    // the dungeon map's check bool(page*).
    constexpr uintptr_t kFieldMemoryButton = 0xe95278;
    constexpr uintptr_t kMemoryButton = 0xe87840;
    // In the overworld map's KeyWait state, after it reads A (w0, page x19), which opens the pin menu. The page's
    // hovered marker: s32 index into its markers, -1 none (set later in the same update, so the last frame's).
    constexpr uintptr_t kFieldPinButton = 0xe950f4;
    constexpr size_t kPageHovered = 0x8150;
    // In the map's icon setup, after a shown icon's marker joined the page's markers: x19 page, x26 marker.
    constexpr uintptr_t kMarkerAdded = 0xe917c8;
    constexpr size_t kPageMarkers = 0x710;          // std::vector<std::shared_ptr<marker>>: begin, end
    constexpr size_t kMarkerRow = 0x240;            // UiFieldMapIcons row; its show flag name view at +0x20
    constexpr size_t kRowFlagName = 0x20;
    constexpr size_t kMarkerLayout = 0x48;          // virtual: the marker's layout, root pane at +0x18
    constexpr size_t kLayoutPane = 0x18;
    constexpr size_t kPaneFlags = 0x58;             // bit 0 visible
    constexpr uintptr_t kPlaySound = 0xa57e0;       // void(const StrView* name, void*, float volume, float pitch)
    struct StrView { const char* data; s64 length; };
    using LayoutFn = u8* (*)(u8*);
    using PlaySoundFn = void (*)(const StrView*, void*, float, float);

    constexpr const char* kTraversePrefix = "UI_FieldMapTraverse_";
    constexpr const char* kOpenPrefix = "UI_FieldMapOpen_";

    struct DungeonGroup {
        float x, z;
        std::vector<int> ids;
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
    u8* g_MarkerPage = nullptr;  // the map page whose markers hold ours (the icon setup ran for it)
    std::vector<std::vector<int>> g_Overworld;  // group -> local ids
    std::vector<DungeonGroup> g_Dungeon;
    std::vector<Chest> g_Chests;
    std::vector<std::string> g_GroupNames;      // group -> its name
    std::map<int, std::string> g_CheckNames;    // local id -> the location's name without its area
    // group -> the flag until which it has another name (a shuffled dungeon's: which dungeon is behind its door)
    std::map<size_t, std::pair<std::string, std::string>> g_HiddenNames;
    int g_FlagsCalls = 0, g_IconCalls = 0, g_IconMatches = 0, g_ChestMatches = 0;  // for DBG map

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

    // A location of the group in logic. None is while the group's name is hidden: its look would tell which dungeon
    // its door leads into.
    bool Reachable(size_t group, int id) {
        return !NameHidden(group) && Reachable(id);
    }

    Look GroupLook(size_t group) {
        size_t left = 0, reachable = 0;
        for (int id : g_Overworld[group]) {
            if (Done(id)) continue;
            left++;
            if (Reachable(group, id)) reachable++;
        }
        if (left == 0) return Collected;
        return reachable == left ? InLogic : reachable > 0 ? Partial : OutOfLogic;
    }

    // A group A marks skipped or not: one location, not checked.
    bool Skippable(const std::vector<int>& ids) {
        return ids.size() == 1 && !AP::IsLocationChecked(ids[0]);
    }

    // Whether a group's icon in this look is made: the look shown, and every look skipping some of its unchecked
    // locations gives it (collected, in logic with one in logic, out of logic with one out of logic, partly with
    // both), so A can swap them on the open map.
    bool LookMade(size_t group, Look look) {
        if (GroupLook(group) == look) return true;
        bool in = false, out = false;
        for (int id : g_Overworld[group]) {
            if (AP::IsLocationChecked(id)) continue;
            (Reachable(group, id) ? in : out) = true;
        }
        switch (look) {
            case Collected: return in || out;
            case InLogic: return in;
            case OutOfLogic: return out;
            case Partial: return in && out;
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

    // The group and look of a map marker's row, or -1.
    int MarkerIcon(u8* marker, Look& look) {
        const u8* row = *reinterpret_cast<u8**>(marker + kMarkerRow);
        if (row == nullptr) return -1;
        auto* name = reinterpret_cast<const std::pair<const char*, size_t>*>(row + kRowFlagName);
        if (name->first == nullptr) return -1;
        return ParseIcon(std::string_view(name->first, name->second), look);
    }

    void SetMarkerVisible(u8* marker, bool visible) {
        void** vtable = *reinterpret_cast<void***>(marker);
        u8* layout = reinterpret_cast<LayoutFn>(vtable[kMarkerLayout / 8])(marker);
        u8* pane = *reinterpret_cast<u8**>(layout + kLayoutPane);
        pane[kPaneFlags] = (pane[kPaneFlags] & ~1) | (visible ? 1 : 0);
    }

    // Shows the page's markers of ours whose look shows (LookShown) and hides the others.
    void ApplyLooks(u8* page) {
        auto** marker = *reinterpret_cast<u8***>(page + kPageMarkers);
        auto** end = *reinterpret_cast<u8***>(page + kPageMarkers + 8);
        for (; marker < end; marker += 2) {  // shared_ptr: object, control block
            Look look;
            int group = *marker != nullptr ? MarkerIcon(*marker, look) : -1;
            if (group >= 0) SetMarkerVisible(*marker, LookShown(group, look));
        }
    }

    // The group and look of the page's hovered marker, or -1.
    int HoveredIcon(u8* page, Look& look) {
        s32 index = *reinterpret_cast<s32*>(page + kPageHovered);
        auto** begin = *reinterpret_cast<u8***>(page + kPageMarkers);
        auto** end = *reinterpret_cast<u8***>(page + kPageMarkers + 8);
        if (index < 0 || index >= (end - begin) / 2 || begin[index * 2] == nullptr) return -1;
        return MarkerIcon(begin[index * 2], look);
    }

    void PlaySound(const char* name) {
        StrView view{name, static_cast<s64>(std::strlen(name))};
        reinterpret_cast<PlaySoundFn>(g_BaseAddress + offset_manager.Offset(kPlaySound))(&view, nullptr, 1.0f, 1.0f);
    }

    void ToggleOutOfLogic(u8* page) {
        g_OutHidden = !g_OutHidden;
        if (page == g_MarkerPage) ApplyLooks(page);
        PlaySound(g_OutHidden ? "SE_SY_MAP_PIN_OFF" : "SE_SY_MAP_PIN_ON");
    }

    // A on the overworld map: marks the hovered icon's location skipped, or not. False (the game takes the press)
    // unless the icon is one of a single unchecked location.
    bool ToggleSkipped(u8* page) {
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

    bool AnyUnchecked(const std::vector<int>& ids) {
        for (int id : ids) {
            if (!AP::IsLocationChecked(id)) return true;
        }
        return false;
    }

    void SetPane(u8* icon, size_t offset, bool visible) {
        void* pane = *reinterpret_cast<void**>(icon + offset);
        void** vtable = *reinterpret_cast<void***>(pane);
        reinterpret_cast<PaneSetVisibleFn>(vtable[kPaneSetVisible / 8])(pane, visible);
        if (visible) reinterpret_cast<PanePlayFn>(vtable[kPanePlay / 8])(pane, 1.0f);
    }
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

    // The overworld map page: its destructor (vtable 0x1c281b0 +0x10).
    constexpr uintptr_t kMapPageDtor = 0xe8e680;
    // A layout's animation by name: LayoutWrapper virtual +0x58 (wrapper, StringView* name), the result through x8: a
    // shared pointer and the animator, whose frame is the float at +0x20 (the layout's update applies it);
    // 0x114c840 (animator) its frame count. Gm_MapMarker's SetMark picks the marker's texture and size by frame: the
    // game's markers, then the map tracker's looks at a group's size (ap_map_icons.frame).
    constexpr size_t kGetAnimation = 0x58 / 8, kAnimFrame = 0x20;
    constexpr uintptr_t kAnimFrameCount = 0x114c840, kReleaseWeak = 0x14311f0;
    constexpr int kGameMarkFrames = 7;  // ap_map_icons.GAME_FRAMES
    struct Anim {
        void* object = nullptr;
        u8* control = nullptr;
        u8* animator = nullptr;
    };

    // Gm_MapMarkList: in N_MarkPosition_00 the bar's black P_pict_00, its windows, pins, count and cursor.
    constexpr const char* kDimLayout = "Gm_MapMarkList.bflyt";
    constexpr const char* kDimHidden[] = {"W_window_01", "W_window_02", "P_pict_01", "A_PinNum_00", "A_alignment_00"};
    constexpr float kDimY = -16, kDimWidth = 1280, kDimHeight = 525;  // the map's part of the page
    // The map's cursor and the name over the icon it is on (Gm_MapCursor), a UI of its own drawn over the pages.
    constexpr int kMapCursorUi = 0x1c;
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
    // ui2d TextBox: the italic slant of its glyphs (a float; 0 upright)
    constexpr size_t kTextItalic = 0x118;
    // ui2d TextBox: its text's colours, top and bottom (RGBA bytes, linear: drawn as sRGB, so 0x5c shows as 0xa2); a
    // colour tag in the text does not reach a text box of a layout.
    constexpr size_t kTextColors = 0xe8;

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
        u8* page = nullptr;         // the overworld map page it hangs under
        int group = -1;
        L::List list;
        L::Layout dim, back, title, count, dots[L::kRows], tags[L::kRows];
        u8* shade = nullptr;        // the dim's black pane
        u8* window = nullptr;       // the back's paper window behind the header and the list
        Anim marks[L::kRows];       // each dot's SetMark
        int fade = 0;               // 0..255, the alpha its layouts draw with
        bool closing = false;       // B closed it: it fades out, then goes
    };
    Panel g_Panel;
    int g_ListsBuilt = 0, g_ListToggles = 0;
#ifdef LASAP_TEST
    int g_ListAsked = -1;           // DBG maplist open <group>: the list the map's next frame opens
    // The list's cost on the game thread, in system ticks (19.2 MHz): its last opening, and each frame's tick since
    // (sum, most, frames).
    u64 g_OpenTicks = 0, g_TickSum = 0, g_TickMost = 0, g_TickFrames = 0;
    u64 Ticks() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks;
    }
#endif
    u64 g_PadHidden = 0;            // buttons held when the list closed: hidden from the game until released

    bool ListOpen() { return g_Panel.page != nullptr && !g_Panel.closing; }

    void Release(Anim& anim) {
        if (anim.control != nullptr) {
            s64 owners = __atomic_fetch_sub(reinterpret_cast<s64*>(anim.control + 8), 1, __ATOMIC_ACQ_REL);
            if (owners == 0) {
                reinterpret_cast<void (*)(void*)>(L::VTable(anim.control)[0x10 / 8])(anim.control);
                L::GameFn<void (*)(void*)>(kReleaseWeak)(anim.control);
            }
        }
        anim = Anim{};
    }

    Anim GetAnimation(u8* wrapper, const char* name) {
        StrView view{name, static_cast<s64>(std::strlen(name))};
        return reinterpret_cast<Anim (*)(u8*, const StrView*)>(L::VTable(wrapper)[kGetAnimation])(wrapper, &view);
    }

    // The look a location's row shows: checked or skipped collected, else in or out of logic.
    Look RowLook(int id) { return Done(id) ? Collected : Reachable(id) ? InLogic : OutOfLogic; }

    std::string CheckName(int id) {
        auto it = g_CheckNames.find(id);
        return it != g_CheckNames.end() ? it->second : "Location " + std::to_string(id);
    }

    const std::vector<int>& ListIds() { return g_Overworld[g_Panel.group]; }

    // The map's cursor and its icon's name, hidden behind the list.
    void ShowMapCursor(bool shown) {
        u8* cursor = L::FindUi(kMapCursorUi);
        L::SetVisible(L::PageRoot(cursor), shown);
    }

    void DropList() {
        if (g_Panel.page == nullptr) return;
        ShowMapCursor(true);
        for (auto& mark : g_Panel.marks) Release(mark);
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
        L::SetText(g_Panel.title.note, g_GroupNames[g_Panel.group]);
        L::SetText(g_Panel.count.note, CountText(ids));
        L::List& list = g_Panel.list;
        for (int r = 0; r < L::kRows; r++) {
            int entry = list.top + r;
            bool shown = entry < list.count;
            L::SetVisible(g_Panel.dots[r].root, shown);
            L::SetVisible(g_Panel.tags[r].root, shown && AP::IsLocationSkipped(ids[entry]));
            if (!shown) continue;
            int id = ids[entry];
            L::Row& row = list.rows[r];
            L::SetText(row.text, CheckName(id));
            L::SetAlpha(L::FindPane(row.part, "A_Item_00"), Done(id) ? kDoneAlpha : 255);
            u8* animator = g_Panel.marks[r].animator;
            float frame = static_cast<float>(kGameMarkFrames + RowLook(id));
            if (animator != nullptr && frame < L::GameFn<u16 (*)(void*)>(kAnimFrameCount)(animator)) {
                L::Field<float>(animator, kAnimFrame) = frame;
            }
        }
        L::Place(list);
    }

    // Once a frame: the fade, the header's texts at its edges, each row's text fitted to the row with its tag after
    // it (the text boxes' extents are known once the font has drawn their glyphs).
    void LayOut() {
        g_Panel.fade = std::clamp(g_Panel.fade + (g_Panel.closing ? -kFadeStep : kFadeStep), 0, 255);
        for (L::Layout* layout : {&g_Panel.dim, &g_Panel.back, &g_Panel.list.layout, &g_Panel.title, &g_Panel.count}) {
            L::SetAlpha(layout->root, g_Panel.fade);
        }
        ShowMapCursor(g_Panel.closing);
        // the panel: the header, then the rows
        L::List& list = g_Panel.list;
        int rows = std::min(list.count, L::kRows);
        float left = kRowLeft - kPanelSide, right = kRowRight + kPanelSide + (list.count > L::kRows ? kScrollRoom : 0);
        float width = right - left, height = 2 * kPanelMargin + kHeaderHeight + L::kRowStep * rows;
        float top = kPanelY + height / 2, x = kPanelX - (left + right) / 2;  // the list's middle
        float y = top - kPanelMargin - kHeaderHeight - L::kRowStep / 2 - L::kRowTop;
        L::MoveTo(list.layout.root, x, y);
        L::MoveTo(g_Panel.window, kPanelX, kPanelY);
        L::Field<float>(g_Panel.window, L::kPaneWidth) = width;
        L::Field<float>(g_Panel.window, L::kPaneHeight) = height;
        for (L::Layout* text : {&g_Panel.title, &g_Panel.count}) {
            float textLeft, textRight;
            if (!L::Span(text->note, textLeft, textRight)) continue;
            // the title's left edge over the rows' left end, the count's right edge over their right end
            float at = text == &g_Panel.title ? x + kRowLeft - textLeft * kHeaderScale
                                              : x + kRowRight - textRight * kHeaderScale;
            L::MoveTo(text->root, at, top - kPanelMargin - kHeaderHeight / 2);
        }
        for (int r = 0; r < L::kRows; r++) {
            float rowY = y + L::kRowTop - L::kRowStep * r;
            L::SetAlpha(g_Panel.dots[r].root, g_Panel.fade);
            L::MoveTo(g_Panel.dots[r].root, x + kDotX, rowY);
            L::Row& row = list.rows[r];
            u8* align = L::FindPane(row.part, "A_Item_00");
            L::Layout& tag = g_Panel.tags[r];
            bool tagged = list.top + r < list.count && AP::IsLocationSkipped(ListIds()[list.top + r]);
            float nameLeft, nameRight, tagLeft = 0, tagRight = 0;
            if (tagged && !L::Span(tag.note, tagLeft, tagRight)) tagged = false;
            L::SetAlpha(tag.root, tagged ? g_Panel.fade : 0);
            if (align == nullptr || !L::Span(row.text, nameLeft, nameRight)) continue;
            // the name on the page: the list, the alignment pane, the text box in it (scaled about its origin)
            float origin = x + L::Field<float>(align, L::kPaneX) + L::Field<float>(row.text.box, L::kPaneX);
            float tagWidth = tagged ? (tagRight - tagLeft) * kTagScale + kTagGap : 0;
            float room = x + kNameRight - tagWidth - (origin + nameLeft);
            float scale = std::clamp(room / (nameRight - nameLeft), kNameSqueeze, 1.0f);
            L::ScaleTo(row.text.box, scale, 1.0f);
            L::MoveTo(tag.root, origin + nameRight * scale + kTagGap - tagLeft * kTagScale, rowY + kTagLower);
        }
    }

    void SetTextColor(u8* box, const u8 (&color)[4]) {
        if (box == nullptr) return;
        for (size_t at : {kTextColors, kTextColors + 4}) std::memcpy(box + at, color, 4);
    }

    bool BuildText(L::Layout& layout, u8* page, u8* root, u16 chars) {
        if (!L::Build(layout, kTextLayout, page, root)) return false;
        u8* box = L::FindPane(layout.root, "T_Operation_00");
        if (box != nullptr && L::Allocate(box, chars)) L::Bind(layout.note, box);
        return true;
    }

    // A on an icon of several locations: its check list over the map.
    void OpenList(u8* page, int group) {
        DropList();
        u8* root = L::PageRoot(page);
        if (root == nullptr) return;
        g_Panel.page = page;
        g_Panel.group = group;
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
        for (const char* name : kDimHidden) L::SetVisible(L::FindPane(g_Panel.dim.root, name), false);
        g_Panel.shade = L::FindPane(g_Panel.dim.root, "P_pict_00");
        L::MoveTo(g_Panel.shade, 0, kDimY);
        L::SetAlpha(g_Panel.shade, kDimAlpha);
        if (g_Panel.shade != nullptr) {
            L::Field<float>(g_Panel.shade, L::kPaneWidth) = kDimWidth;
            L::Field<float>(g_Panel.shade, L::kPaneHeight) = kDimHeight;
        }
        for (const char* name : kBackHidden) L::SetVisible(L::FindPane(g_Panel.back.root, name), false);
        g_Panel.window = L::FindPane(g_Panel.back.root, "W_window_00");
        L::SetAlpha(g_Panel.window, kPanelAlpha);
        for (L::Layout* text : {&g_Panel.title, &g_Panel.count}) {
            L::ScaleTo(text->root, kHeaderScale);
            SetTextColor(text->note.box, kInk);
        }
        L::MoveTo(g_Panel.list.scroll, L::Field<float>(g_Panel.list.scroll, L::kPaneX), kScrollY);
        L::ScaleTo(g_Panel.list.scroll, 1, L::kRowStep * L::kRows / kScrollHeight);
        for (int r = 0; r < L::kRows; r++) {
            L::Row& row = g_Panel.list.rows[r];
            // one line however long the name: it is shrunk to fit the row instead
            L::Allocate(row.text.box, kNameChars);
            if (row.text.box != nullptr) L::Field<float>(row.text.box, L::kPaneWidth) = kNameWidth;
            L::ScaleTo(g_Panel.dots[r].root, kDotScale);
            g_Panel.marks[r] = GetAnimation(g_Panel.dots[r].wrapper, "SetMark");
            L::Layout& tag = g_Panel.tags[r];
            L::ScaleTo(tag.root, kTagScale);
            if (tag.note.box != nullptr) L::Field<float>(tag.note.box, kTextItalic) = kTagSlant;
            SetTextColor(tag.note.box, kGray);
            L::SetText(tag.note, "skipped");
        }
        L::SetEntries(g_Panel.list, static_cast<int>(g_Overworld[group].size()), 0);
        FillList();
        LayOut();
        L::RefreshGuide(0);
        PlaySound("SE_SY_MAP_OMOIDE_ON");
        g_ListsBuilt++;
        Logging.Log("[AP] map: check list of group " + std::to_string(group));
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
        if (g_Panel.page == g_MarkerPage) ApplyLooks(g_Panel.page);
        FillList();
        PlaySound("SE_SY_DECIDE");
    }

    // Once a frame of the map's KeyWait while the list is there.
    void TickList(u8* page) {
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

HOOK_DEFINE_INLINE(DungeonMapFlags) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        g_FlagsCalls++;
        if (g_Enabled) ctx->W[8] |= 3;  // compass and map
    }
};

HOOK_DEFINE_INLINE(MarkerAdded) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        g_MarkerPage = reinterpret_cast<u8*>(ctx->X[19]);
        u8* marker = reinterpret_cast<u8*>(ctx->X[26]);
        Look look;
        int group = MarkerIcon(marker, look);
        if (group >= 0 && !LookShown(group, look)) SetMarkerVisible(marker, false);
    }
};

HOOK_DEFINE_INLINE(FieldPinButton) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        u8* page = reinterpret_cast<u8*>(ctx->X[19]);
        if (g_Panel.page != nullptr) {
#ifdef LASAP_TEST
            u64 start = Ticks();
            TickList(page);
            u64 spent = Ticks() - start;
            g_TickSum += spent;
            g_TickMost = std::max(g_TickMost, spent);
            g_TickFrames++;
#else
            TickList(page);
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
            OpenList(page, g_ListAsked);
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
            OpenList(page, group);
            g_OpenTicks = Ticks() - start;
            g_TickSum = g_TickMost = g_TickFrames = 0;
#else
            OpenList(page, group);
#endif
            ctx->W[0] = 0;  // no pin menu
        } else if (ToggleSkipped(page)) {
            ctx->W[0] = 0;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(MapPageDtor) {
    static void Callback(u8* page) {
        if (page == g_Panel.page) DropList();
        if (page == g_MarkerPage) g_MarkerPage = nullptr;
        Orig(page);
    }
};

HOOK_DEFINE_INLINE(FieldMemoryButton) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        if (ctx->W[21] == 0 || !g_Enabled) return;
        ToggleOutOfLogic(reinterpret_cast<u8*>(ctx->X[19]));
        ctx->W[21] = 0;  // no memories
    }
};

HOOK_DEFINE_TRAMPOLINE(MemoryButton) {
    static bool Callback(u8* page) {
        bool pressed = Orig(page);
        if (!pressed || !g_Enabled) return pressed;
        ToggleOutOfLogic(page);
        return false;  // no memories
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonIconUpdate) {
    static void Callback(u8* icon, const u8* actor) {
        Orig(icon, actor);
        g_IconCalls++;
        if (!g_Enabled) return;
        float x = *reinterpret_cast<const float*>(actor + kActorPosX);
        float z = *reinterpret_cast<const float*>(actor + kActorPosZ);
        if (actor[kActorShownKind] != kSwitchAlways) {
            // A room actor's icon: a chest whose location is checked shows open, as when it is opened in the game.
            if (g_Chests.empty()) return;
            std::string level = LevelSystem::GetCurrentLevel();
            for (const auto& chest : g_Chests) {
                if (chest.level == level && std::fabs(chest.x - x) < 0.01f && std::fabs(chest.z - z) < 0.01f &&
                    AP::IsLocationChecked(chest.id)) {
                    g_ChestMatches++;
                    SetPane(icon, kIconClosedPane, false);
                    SetPane(icon, kIconOpenPane, true);
                    return;
                }
            }
            return;
        }
        for (const auto& group : g_Dungeon) {
            if (std::fabs(group.x - x) < 0.01f && std::fabs(group.z - z) < 0.01f) {
                g_IconMatches++;
                bool open = !AnyUnchecked(group.ids);
                SetPane(icon, open ? kIconClosedPane : kIconOpenPane, false);
                SetPane(icon, open ? kIconOpenPane : kIconClosedPane, true);
                return;
            }
        }
    }
};

namespace AP {
    void LoadMap() {
        // Written by the patcher (ap_map.py): "O <group> <local id>...", "D <group> <x> <z> <local id>...",
        // "C <level> <x> <z> <local id>" (a dungeon chest), "G <group> <name>" (an overworld group's name),
        // "H <group> <flag> <name>" (the group's name until the flag is set) and "N <local id> <name>" (an overworld
        // group's location's name without its area) lines.
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
            } else if (kind == "D") {
                DungeonGroup dg{};
                ss >> dg.x >> dg.z;
                for (int id; ss >> id;) dg.ids.push_back(id);
                g_Dungeon.push_back(dg);
            }
        }
        g_Loaded = g_Enabled = true;
        DungeonMapFlags::InstallAtOffset(offset_manager.Offset(kDungeonMapFlags));
        DungeonIconUpdate::InstallAtOffset(offset_manager.Offset(kDungeonIconUpdate));
        MarkerAdded::InstallAtOffset(offset_manager.Offset(kMarkerAdded));
        MemoryButton::InstallAtOffset(offset_manager.Offset(kMemoryButton));
        FieldMemoryButton::InstallAtOffset(offset_manager.Offset(kFieldMemoryButton));
        FieldPinButton::InstallAtOffset(offset_manager.Offset(kFieldPinButton));
        MapPageDtor::InstallAtOffset(offset_manager.Offset(kMapPageDtor));
        AP::ListUi::AddGuideSource(ListGuide);
        Logging.Log("[AP] map tracker: " + std::to_string(g_Overworld.size()) + " overworld and " +
                    std::to_string(g_Dungeon.size()) + " dungeon icons, " + std::to_string(g_Chests.size()) +
                    " chests");
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
        Look look;
        return g_Enabled ? ParseIcon(label, look) : -1;
    }

    std::string MapLabelText(int group) {
        // The group's name, after its count (CountText) when it has several locations and some are unchecked, or
        // after "Skipped: " when its one location is skipped; or its other name
        // alone until its flag is set (the count would tell a dungeon by its number of locations).
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return "";
        if (NameHidden(group)) return g_HiddenNames[group].second;
        std::string name = static_cast<size_t>(group) < g_GroupNames.size() ? g_GroupNames[group] : "";
        const auto& ids = g_Overworld[group];
        if (Skippable(ids) && IsLocationSkipped(ids[0])) return "Skipped: " + name;
        std::string count = CountText(ids);
        if (ids.size() < 2 || count.empty()) return name;
        return count + " " + name;
    }

    void TickMap() {
        if (!g_Enabled) return;
        u8 dungeon = *reinterpret_cast<u8*>(g_BaseAddress + kDungeonIndex);
        u8** entries = *reinterpret_cast<u8***>(g_BaseAddress + kDungeonItems);
        if (dungeon == 0xff || entries == nullptr || entries[dungeon] == nullptr) return;
        *reinterpret_cast<u64*>(entries[dungeon] + kDungeonVisited) = ~0ULL;
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
        // The skipped locations of overworld icons, and the hovered icon: its group, locations and look, or -1.
        static const char* const kLookNames[kLooks] = {"in", "partly", "out", "collected"};
        size_t skipped = 0;
        for (const auto& ids : g_Overworld) {
            for (int id : ids) skipped += IsLocationSkipped(id) ? 1 : 0;
        }
        std::string hover = "-1";
        if (g_HoverGroup >= 0 && static_cast<size_t>(g_HoverGroup) < g_Overworld.size()) {
            hover = std::to_string(g_HoverGroup) + "/" + std::to_string(g_Overworld[g_HoverGroup].size()) + "/" +
                    kLookNames[g_HoverLook];
        }
        return " skipped=" + std::to_string(skipped) + " hover=" + hover;
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
               std::to_string(g_IconMatches) + "," + std::to_string(g_ChestMatches) + " out=" +
               (g_OutHidden ? "hidden" : "shown") + SkipState();
    }
}
