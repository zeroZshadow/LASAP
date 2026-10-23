// DBG map, maplist and mapdot (test build): the map tracker's state, its check list's and its dungeon dots'.
#ifdef LASAP_TEST
#include "map/map_tracker_impl.hpp"
#include "test/test_impl.hpp"
#include "AP/ap.hpp"
#include "Game/time.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace AP::MapTracker {
    int g_HoverGroup = -1;
    int g_HoverLook = -1;
    std::string g_CursorText;

namespace {
    int g_ListAsked = -1;           // DBG maplist open <group>: the list the map's next frame opens
    int g_DotListAsked = -1;        // DBG mapdot press <dot>: the dot the dungeon map's next frame takes A on
    // The list's cost on the game thread, in system ticks (19.2 MHz): its last opening, and each frame's tick since
    // (sum, most, frames).
    u64 g_OpenTicks = 0, g_TickSum = 0, g_TickMost = 0, g_TickFrames = 0;

    std::string SkipState() {
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
    }
}

    bool TestFieldPin(Map::OverworldMap* page) {
        Look look;
        g_HoverGroup = HoveredIcon(page, look);
        g_HoverLook = g_HoverGroup >= 0 ? look : -1;
        if (!Listable(g_ListAsked)) return false;
        OpenList(&page->wrapper, g_Overworld[g_ListAsked], g_GroupNames[g_ListAsked], g_ListAsked);
        g_ListAsked = -1;
        return true;
    }

    bool TestDotPressed() {
        bool pressed = g_DotListAsked >= 0 && static_cast<size_t>(g_DotListAsked) < g_DotMarkers.size();
        if (pressed) g_DotHovered = g_DotListAsked;
        g_DotListAsked = -1;
        return pressed;
    }

    void TestListOpened(u64 ticks) {
        g_OpenTicks = ticks;
        g_TickSum = g_TickMost = g_TickFrames = 0;
    }

    void TestListTicked(u64 ticks) {
        g_TickSum += ticks;
        g_TickMost = std::max(g_TickMost, ticks);
        g_TickFrames++;
    }

    std::string State() {
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

    int HoverGroup() {
        return g_HoverGroup;
    }

    std::string ListField() {
        if (g_Panel.page == nullptr) return " mlist=-";
        return " mlist=" + std::to_string(g_Panel.group) + "/" + std::to_string(g_Panel.list.at) + "/" +
               std::to_string(g_Panel.list.top) + (g_Panel.closing ? "/closing" : "");
    }

    std::string DotInfo(const std::vector<std::string>& a) {
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
        return s + " | text=" + LabelText(kDotGroup);
    }

    std::string ListInfo(const std::vector<std::string>& a) {
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
        const auto& ids = g_Panel.ids;
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
}
#endif
