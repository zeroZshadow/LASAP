// Lists built from the game's layouts (ap_list.hpp): layouts of Game.arc built at runtime and hung under a pause menu
// page (TESTING.md, "Layouts at runtime", "Text box glyphs"; ghost_nametag.cpp), the Memories list with its rows, cursor
// and scroll bar, the pad, and the button guide made from the overworld map's.
#include "ap/ap.hpp"
#include "ap/ap_list.hpp"
#include "ap/ap_text.hpp"
#include "game/mem.hpp"
#include "game/menu.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {
    using namespace AP::ListUi;
    using namespace Game::Ui;
    using AP::ListUi::Layout;
    using Game::Field;

    constexpr int kPriority = 10;

    // Gm_MemoryList: rows L_Item_00..06 (Gm_MemoryListItem: T_Item_00 in the alignment pane A_Item_00, alpha 0 in
    // the file, beside the Memories' marks L_Way_00, B_bounding_01 and L_MemoryMark_00, hidden here; the highlight
    // windows W_Add_00/01); the blank rows L_Blank_00..03 above and below; the cursor L_ListCursor_00 (Cm_ListCursor)
    // at the root; the scroll bar L_ScrollBar_00 with its handle W_ScrollHandle_00 (y 189 at the top, 94 high in a
    // 390 track); the Memories' panel count N_Panel_00, hidden. A_List_00 is an alignment pane: a hidden row gives
    // its place to the rows under it, so the blank rows stay and are made transparent instead.
    constexpr const char* kListLayout = "Gm_MemoryList.bflyt";
    constexpr const char* kRowHidden[] = {"L_Way_00", "B_bounding_01", "L_MemoryMark_00"};
    constexpr const char* kBlankRows[] = {"L_Blank_00", "L_Blank_01", "L_Blank_02", "L_Blank_03"};
    constexpr float kHandleTop = 189, kHandleTravel = 390 - 94;
    constexpr u64 kPadStaleMs = 50;  // the last poll's buttons count as held this long
    constexpr int kRepeatDelay = 20, kRepeatEvery = 5;  // frames before Up/Down repeat, and between repeats

    // The pause menu header's guide (Game::Ui::NaviGuide), and the overworld map's: X, Y, R, A, B.
    constexpr int kGuideSlotCount = std::extent_v<decltype(NaviGuide::slots)>;
    constexpr u32 kMapGuide[kGuideSlotCount] = {kGuideX, kGuideY, kGuideR, kGuideA, kGuideB};

    u64 g_PadHeld = 0, g_PadSeen = 0;
    // The polls since a list last read the pad (once a frame): their buttons together, and how many (each controller
    // style is polled, those not in use with no buttons); the buttons read last, kept while no poll came since. A
    // button let go is gone the next frame, so a held Up or Down stops repeating as it is released.
    u64 g_PadPolled = 0, g_PadPolls = 0, g_PadFrame = 0;
    u64 TakePad() {
        if (g_PadPolls != 0) g_PadFrame = g_PadPolled;
        g_PadPolled = g_PadPolls = 0;
        return g_PadFrame;
    }
    std::vector<GuideSource> g_GuideSources;
    const Guide* g_Guide = nullptr;  // the guide Navi_SetGuide sets now
    bool g_GuideShown = false;       // the guide shows a source's guide

    Navi* FindNavi() { return reinterpret_cast<Navi*>(FindUi(kNaviUi)); }

    // The next GuideSet loads every caption again: the sources' guides and the map's share kinds.
    void ForgetGuideKinds(Navi* navi) {
        for (u32& kind : navi->guide.kinds) kind = ~0u;
    }

    void HideGuideSlot(Navi* navi, int index) {
        GuideSlot* slot = &navi->guide.slots[index];
        if (navi->guideSwapping != 0 && navi->guideSwapIndex == static_cast<u32>(index)) slot = &navi->guide.swapSlot;
        u8* object = Game::CallVirtual<u8*()>(slot, GuideSlotVirtual::kObject);
        SetVisible(object != nullptr ? Field<Pane*>(object, 0x18) : nullptr, false);
    }
}

// A source's guide is the overworld map's with its slots set to the source's kinds and the rest hidden, its captions
// read as the source's during the call (ap_text.cpp).
HOOK_DEFINE_TRAMPOLINE(NaviSetGuide) {
    static void Callback(Navi* navi, u32 index) {
        const Guide* guide = nullptr;
        for (GuideSource source : g_GuideSources) {
            if ((guide = source(index)) != nullptr) break;
        }
        if (guide == nullptr) {
            if (g_GuideShown) ForgetGuideKinds(navi);
            g_GuideShown = false;
            return Orig(navi, index);
        }
        ForgetGuideKinds(navi);
        g_GuideShown = true;
        u32 mapId = navi->pageIds[0];
        navi->pageIds[0] = kOverworldMapPage;
        g_Guide = guide;
        Orig(navi, 0);
        for (int slot = 0; slot < guide->count && slot < kGuideSlotCount; slot++) {
            if (guide->kinds[slot] != kMapGuide[slot]) Game::Guide_Set(&navi->guide, slot, guide->kinds[slot]);
        }
        g_Guide = nullptr;
        navi->pageIds[0] = mapId;
        for (int slot = guide->count; slot < kGuideSlotCount; slot++) HideGuideSlot(navi, slot);
        navi->guideRedraw = 1;
    }
};

namespace AP::ListUi {
    void Bind(Text& text, TextBox* box) {
        text.box = box;
        text.holder.box = box;
    }

    void SetText(Text& text, const std::string& value) {
        if (text.box == nullptr || (value == text.value && !value.empty())) return;
        text.value = value;
        AP::SetPaneText(&text.holder, value);
        QueueGlyphs(text.box);
    }

    bool Drawn(const Text& text) { return GlyphsDrawn(text.box); }

    bool Span(const Text& text, float& left, float& right) {
        if (!Drawn(text) || text.value.empty()) return false;
        Rect extent = TextExtent(text.box);
        if (!std::isfinite(extent.left + extent.right) || extent.right <= extent.left) return false;
        left = extent.left;
        right = extent.right;
        return true;
    }

    bool Build(Layout& layout, const char* file, LayoutWrapper* page, Pane* pageRoot) {
        layout.wrapper = BuildLayout(file, kPriority);
        layout.root = RootOf(layout.wrapper);
        if (layout.root == nullptr) {
            Logging.Log("[AP] list: layout not built: %s", file);
            return false;
        }
        Attach(layout.wrapper, page, pageRoot);
        layout.root->flags |= kPaneInfluencedAlpha;
        SetAlpha(layout.root, 0);
        return true;
    }

    void Drop(Layout& layout) {
        Unqueue(layout.note.box);
        Delete(layout.wrapper);
        layout = Layout{};
    }

    bool BuildList(List& list, LayoutWrapper* page, Pane* pageRoot, float x, float y) {
        if (!Build(list.layout, kListLayout, page, pageRoot)) return false;
        Pane* root = list.layout.root;
        MoveTo(root, x, y);
        SetVisible(FindPane(root, "N_Panel_00"), false);
        for (const char* name : kBlankRows) {
            Pane* blank = FindPane(root, name);
            if (blank != nullptr) blank->flags |= kPaneInfluencedAlpha;
            SetAlpha(blank, 0);
        }
        list.cursor = FindPane(root, "L_ListCursor_00");
        list.scroll = FindPane(root, "L_ScrollBar_00");
        list.handle = FindPane(list.scroll, "W_ScrollHandle_00");
        char name[16];
        for (int r = 0; r < kRows; r++) {
            Row& row = list.rows[r];
            snprintf(name, sizeof(name), "L_Item_%02d", r);
            row.part = FindPane(root, name);
            row.add0 = FindPane(row.part, "W_Add_00");
            row.add1 = FindPane(row.part, "W_Add_01");
            Bind(row.text, FindText(row.part, "T_Item_00"));
            SetAlpha(FindPane(row.part, "A_Item_00"), 255);
            SetAlpha(row.add0, 0);
            SetAlpha(row.add1, 0);
            for (const char* hidden : kRowHidden) SetVisible(FindPane(row.part, hidden), false);
        }
        list.prevPad = PadHeld();
        TakePad();
        list.heldFrames = 0;
        return true;
    }

    void DropList(List& list) {
        for (auto& row : list.rows) Unqueue(row.text.box);
        Drop(list.layout);
        list = List{};
    }

    void SetEntries(List& list, int count, int at) {
        list.count = std::max(count, 0);
        list.at = std::clamp(at, 0, std::max(list.count - 1, 0));
        list.top = std::clamp(list.at - kRows / 2, 0, std::max(list.count - kRows, 0));
    }

    void Place(List& list) {
        for (int r = 0; r < kRows; r++) SetVisible(list.rows[r].part, list.top + r < list.count);
        SetVisible(list.cursor, list.count > 0);
        MoveTo(list.cursor, 0, kRowTop - kRowStep * (list.at - list.top));
        SetVisible(list.scroll, list.count > kRows);
        float scrolled = list.count > kRows ? static_cast<float>(list.top) / (list.count - kRows) : 0;
        MoveTo(list.handle, 0, kHandleTop - kHandleTravel * scrolled);
    }

    u64 Input(List& list, bool& moved) {
        moved = false;
        u64 pad = TakePad(), pressed = pad & ~list.prevPad;
        list.prevPad = pad;
        int step = (pad & kButtonDown) ? 1 : (pad & kButtonUp) ? -1 : 0;
        list.heldFrames = step != 0 ? list.heldFrames + 1 : 0;
        bool repeat = list.heldFrames > kRepeatDelay && (list.heldFrames - kRepeatDelay) % kRepeatEvery == 0;
        if ((pressed & (kButtonUp | kButtonDown)) || repeat) {
            int next = std::clamp(list.at + step, 0, std::max(list.count - 1, 0));
            if (next != list.at) {
                list.at = next;
                if (list.at < list.top) list.top = list.at;
                if (list.at >= list.top + kRows) list.top = list.at - kRows + 1;
                Place(list);
                PlaySound("SE_SY_CURSOL_MOVE");
                moved = true;
            }
        }
        return pressed;
    }

    void OnPad(u64 buttons) {
        g_PadPolled |= buttons;
        g_PadPolls++;
        if (buttons == 0) return;
        g_PadHeld = buttons;
        g_PadSeen = Game::NowMs();
    }

    u64 PadHeld() { return Game::NowMs() - g_PadSeen < kPadStaleMs ? g_PadHeld : 0; }

    void AddGuideSource(GuideSource source) { g_GuideSources.push_back(source); }

    void RefreshGuide(u32 index) {
        Navi* navi = FindNavi();
        if (navi != nullptr) Game::Navi_SetGuide(navi, index);
    }

    std::string GuideCaption(int button) {
        if (g_Guide == nullptr || button < 0 || button > 2) return "";
        return g_Guide->captions[button];
    }

    void InstallHooks() {
        NaviSetGuide::InstallAtFuncPtr(Game::Navi_SetGuide.Ptr());
    }
}
