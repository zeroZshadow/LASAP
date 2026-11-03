// The hint strip (DESIGN.md, Map tracker, Hints): while the map waits for buttons with the cursor on an icon or dot
// that has a ring, a paper strip under the map names the items hinted there, as many as fit and the others counted
// ("+3 more"). Built from the check list's layouts and hung under the map page: Gm_MemoryCollect's paper window
// W_window_00, and two Gm_SystemOpUnit texts, "Hints:" and the items.
#include "map/map_tracker_impl.hpp"
#include <algorithm>
#include <cmath>

namespace AP::MapTracker {
    HintStrip g_HintStrip;

namespace {
    constexpr const char* kBackLayout = "Gm_MemoryCollect.bflyt";
    constexpr const char* kBackHidden[] = {"T_Collect_00", "A_Collect_00"};
    constexpr const char* kLabel = "Hints:";
    constexpr u16 kLabelChars = 16, kItemsChars = 400;  // room for every location of an icon hinted
    // The strip (layout units): its middle's height, on the rule between the map and the button guide, where neither
    // page draws (a dungeon's rooms reach the map's bottom edge); its height, the margin beside the texts and the gap
    // between them; at most this wide.
    constexpr float kStripY = -286, kStripHeight = 40, kStripSide = 22, kStripGap = 12, kStripMaxWidth = 1000;
    constexpr int kStripAlpha = 245;
    // The paper window is sized this much larger and scaled down: smaller, its corner pieces overlap.
    constexpr float kWindowScale = 0.6f;
    // The texts' boxes' width (338 in the file), so every item hinted fits on one line to be measured
    constexpr float kTextWidth = 8000;

    // The first n items, the others counted.
    std::string Named(const std::vector<std::string>& items, size_t n) {
        std::string text;
        for (size_t i = 0; i < n && i < items.size(); i++) text += (i == 0 ? "" : ", ") + items[i];
        if (n < items.size()) text += " +" + std::to_string(items.size() - n) + " more";
        return text;
    }

    void SetItems(size_t named) {
        HintStrip& strip = g_HintStrip;
        strip.named = named;
        L::SetText(strip.items.note, Named(strip.hinted, named));
        strip.settle = 1;
    }

    // The hovered icon's or dot's locations while the strip's page waits for buttons and a ring is there; none else.
    Ids HoveredHinted() {
        HintStrip& strip = g_HintStrip;
        if (ListOpen()) return {};
        if (g_MarkerPage != nullptr && strip.page == &g_MarkerPage->page.wrapper) {
            Look look;
            int group = g_MarkerPage->machine.state == Map::kKeyWait ? HoveredIcon(g_MarkerPage, look) : -1;
            return group >= 0 && static_cast<size_t>(group) < Groups().size() && Ringed(group) ? GroupIds(group)
                                                                                               : Ids{};
        }
        if (g_DungeonPage != nullptr && strip.page == &g_DungeonPage->page.wrapper &&
            g_DungeonPage->machine.state == Map::kKeyWait) {
            const Dot* dot = HoveredDot();
            if (dot != nullptr && AnyHintShown(IdsOf(dot->ids))) return IdsOf(dot->ids);
        }
        return {};
    }

    // The texts side by side in the middle of the window, which fits them; false until the font has laid them out and
    // they fit. Items that do not fit are counted instead: the text's width over its length estimates how many fit,
    // and the next frames measure again. One item alone too long is narrowed.
    bool LayOut() {
        HintStrip& strip = g_HintStrip;
        float labelLeft, labelRight, itemsLeft, itemsRight;
        if (strip.settle > 0) {
            strip.settle--;
            return false;
        }
        if (!L::Span(strip.label.note, labelLeft, labelRight) || !L::Span(strip.items.note, itemsLeft, itemsRight)) {
            return false;
        }
        float labelWidth = labelRight - labelLeft;
        float room = kStripMaxWidth - 2 * kStripSide - kStripGap - labelWidth;
        if (itemsRight - itemsLeft > room && strip.named > 1) {
            size_t fit = static_cast<size_t>(std::floor(room / (itemsRight - itemsLeft) *
                                                        strip.items.note.value.size()));
            size_t named = strip.named - 1;
            while (named > 1 && Named(strip.hinted, named).size() > fit) named--;
            SetItems(named);
            return false;
        }
        float scale = std::min(1.0f, room / (itemsRight - itemsLeft));
        float itemsWidth = (itemsRight - itemsLeft) * scale;
        float width = 2 * kStripSide + labelWidth + kStripGap + itemsWidth;
        float left = -width / 2 + kStripSide;
        Ui::MoveTo(strip.window, 0, kStripY);
        Ui::ScaleTo(strip.window, kWindowScale);
        strip.window->width = width / kWindowScale;
        strip.window->height = kStripHeight / kWindowScale;
        Ui::MoveTo(strip.label.root, left - labelLeft, kStripY);
        Ui::ScaleTo(strip.items.root, scale);
        Ui::MoveTo(strip.items.root, left + labelWidth + kStripGap - itemsLeft * scale, kStripY);
        return true;
    }

    void SetShown(bool shown) {
        HintStrip& strip = g_HintStrip;
        strip.shown = shown;
        Ui::SetAlpha(strip.window, shown ? kStripAlpha : 0);
        for (L::Layout* text : {&strip.label, &strip.items}) Ui::SetAlpha(text->root, shown ? 255 : 0);
    }
}

    void BuildHintStrip(Ui::LayoutWrapper* page) {
        DropHintStrip(g_HintStrip.page);
        Ui::Pane* root = Ui::RootOf(page);
        if (root == nullptr) return;
        HintStrip& strip = g_HintStrip;
        strip.page = page;
        if (!L::Build(strip.back, kBackLayout, page, root) || !BuildText(strip.label, page, root, kLabelChars) ||
            !BuildText(strip.items, page, root, kItemsChars)) {
            DropHintStrip(page);
            return;
        }
        for (const char* name : kBackHidden) Ui::SetVisible(Ui::FindPane(strip.back.root, name), false);
        strip.window = Ui::FindPane(strip.back.root, "W_window_00");
        Ui::SetAlpha(strip.back.root, 255);
        for (L::Layout* text : {&strip.label, &strip.items}) {
            if (text->note.box != nullptr) text->note.box->pane.width = kTextWidth;
        }
        SetTextColor(strip.label.note.box, kInk);
        SetTextColor(strip.items.note.box, kInk);
        L::SetText(strip.label.note, kLabel);
        SetShown(false);
    }

    void DropHintStrip(Ui::LayoutWrapper* page) {
        HintStrip& strip = g_HintStrip;
        if (page == nullptr || strip.page != page) return;
        L::Drop(strip.items);
        L::Drop(strip.label);
        L::Drop(strip.back);
        strip = HintStrip{};
    }

    void UpdateHintStrip() {
        HintStrip& strip = g_HintStrip;
        if (strip.page == nullptr || strip.window == nullptr) return;
        std::vector<std::string> hinted = HintItems(HoveredHinted());
        if (hinted != strip.hinted) {
            strip.hinted = std::move(hinted);
            if (!strip.hinted.empty()) SetItems(strip.hinted.size());
        }
        bool shown = !strip.hinted.empty() && LayOut();
        if (shown != strip.shown) SetShown(shown);
    }
}
