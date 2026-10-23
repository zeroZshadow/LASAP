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
#include "map/map_tracker_impl.hpp"
#include "AP/ap.hpp"
#include "Game/sym.hpp"
#include "Game/time.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <cstring>

namespace AP::MapTracker {
    Panel g_Panel;
    int g_ListsBuilt = 0, g_ListToggles = 0;
    u64 g_PadHidden = 0;

namespace {
    using Game::Ui::PlaySound;


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

    const std::vector<int>& ListIds() { return g_Panel.ids; }

    // The map's cursor and its icon's name, hidden behind the list.
    void ShowMapCursor(bool shown) {
        Ui::SetVisible(Ui::RootOf(Ui::FindUi(Map::kMapCursorUi)), shown);
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

    void BuildList(Ui::LayoutWrapper* page, const std::vector<int>& ids, const std::string& heading, int group) {
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
        Logging.Log("[AP] map: check list of %s", heading.c_str());
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

    void StepList() {
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
}

    bool ListOpen() { return g_Panel.page != nullptr && !g_Panel.closing; }

    Map::MapCursor* FindCursor() { return reinterpret_cast<Map::MapCursor*>(Ui::FindUi(Map::kMapCursorUi)); }

    // The look a location's row shows: checked or skipped collected, else in or out of logic.
    Look RowLook(int id) { return Done(id) ? Collected : Reachable(id) ? InLogic : OutOfLogic; }

    std::string CheckName(int id) {
        auto it = g_CheckNames.find(id);
        return it != g_CheckNames.end() ? it->second : "Location " + std::to_string(id);
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

    void OpenList(Ui::LayoutWrapper* page, const std::vector<int>& ids, const std::string& heading, int group) {
#ifdef LASAP_TEST
        u64 start = Game::Ticks();
        BuildList(page, ids, heading, group);
        TestListOpened(Game::Ticks() - start);
#else
        BuildList(page, ids, heading, group);
#endif
    }

    void TickList(Ui::LayoutWrapper* page) {
        if (page != g_Panel.page) return;
#ifdef LASAP_TEST
        u64 start = Game::Ticks();
        StepList();
        TestListTicked(Game::Ticks() - start);
#else
        StepList();
#endif
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
