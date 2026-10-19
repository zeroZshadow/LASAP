// Lists built from the game's layouts (ap_list.hpp): layouts of Game.arc built at runtime and hung under a pause menu
// page (TESTING.md, "Layouts at runtime", "Text box glyphs"; ap_nametag.cpp), the Memories list with its rows, cursor
// and scroll bar, the pad, and the button guide made from the overworld map's.
#include "AP/ap.hpp"
#include "AP/ap_list.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
    using namespace AP::ListUi;

    struct StrView { const char* data; s64 length; };

    // LayoutWrapper_Build and LayoutWrapper_AttachTo; UiManager_QueueGlyphs; the sound player (name, null, volume,
    // pitch).
    constexpr uintptr_t kOperatorNew = 0x1431170, kOperatorDelete = 0x1431110, kWrapperCtor = 0xe59100,
                        kWrapperAttach = 0xe59d40, kUiManager = 0x1c971c0, kQueueGlyphs = 0xeda8d0,
                        kPlaySound = 0xa57e0;
    constexpr size_t kWrapperSize = 0x78, kWrapperLayout = 0x18, kLayoutRoot = 0x18, kWrapperDelete = 0x18 / 8,
                     kFindPane = 0x60 / 8;
    constexpr size_t kUiGlyphQueues = 0xab40, kQueueStride = 0x18, kQueueSize = 0x10, kNodeCount = 0x18,
                     kNodeTextBox = 0x20;
    // The UI manager's UIs: a list at +0x950, the UI at node +0x10, its id from virtual +0x60.
    constexpr size_t kUiList = 0x950, kNodeUi = 0x10, kUiId = 0x60 / 8;
    constexpr int kPriority = 10;
    // Text box: +0x190 set once the font has drawn its glyphs; virtual +0x120 the text's extent (left, top, right,
    // bottom); AllocateStringBuffer (virtual +0xe8: box, gfx device, length; the device *(the graphics object at
    // 0x1cc8b30)[1]), the capacity at +0x110.
    constexpr size_t kTextGlyphsReady = 0x190, kTextRect = 0x120 / 8, kTextAllocate = 0xe8 / 8, kTextCapacity = 0x110;
    constexpr uintptr_t kGraphics = 0x1cc8b30;
    struct Rect {
        float left, top, right, bottom;
        Rect() {}
        ~Rect() {}  // not trivial: returned through x8, as the game's function does
    };

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

    // Navi, the pause menu header: page ids u32 +0x230; the guide at +0x318. Navi_SetGuide
    // (navi, index) clears the guide, then GuideSet(guide, slot, kind) per slot; slot objects at Navi +0x3a8 + slot
    // * 0x110 (virtual +0x48: an object whose +0x18 is the slot's pane), or +0xd38 for the slot +0x1224 while
    // +0x122a is set; +0x1228 set redraws. GuideSet loads a slot's caption only when its kind differs from the one
    // kept at guide +0xb40 + slot * 4. The overworld map's page id 0xf, its guide X, Y, R, A, B.
    constexpr uintptr_t kNaviSetGuide = 0xeed6f0, kGuideSet = 0xf571b0;
    constexpr size_t kNaviIds = 0x230, kNaviGuide = 0x318, kGuideKinds = 0xb40;
    constexpr int kGuideSlotCount = 5, kGuideKindSlots = 6;
    constexpr size_t kGuideSlots = 0x3a8, kGuideSlotSize = 0x110, kGuideSwapSlot = 0xd38, kGuideSwapIndex = 0x1224,
                     kGuideSwapping = 0x122a, kGuideRedraw = 0x1228, kGuideSlotObject = 0x48 / 8;
    constexpr u32 kOverworldMap = 0xf;
    constexpr u32 kMapGuide[kGuideSlotCount] = {kGuideX, kGuideY, kGuideR, kGuideA, kGuideB};

    u8* UiManager() { return reinterpret_cast<u8*>(g_BaseAddress + kUiManager); }

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

    // The next GuideSet loads every caption again: the sources' guides and the map's share kinds.
    void ForgetGuideKinds(u8* navi) {
        for (int slot = 0; slot < kGuideKindSlots; slot++) Field<u32>(navi + kNaviGuide, kGuideKinds + slot * 4) = ~0u;
    }

    void HideGuideSlot(u8* navi, int index) {
        u8* slot = navi + kGuideSlots + index * kGuideSlotSize;
        if (Field<u8>(navi, kGuideSwapping) != 0 && Field<u32>(navi, kGuideSwapIndex) == static_cast<u32>(index)) {
            slot = navi + kGuideSwapSlot;
        }
        u8* object = reinterpret_cast<u8* (*)(void*)>(VTable(slot)[kGuideSlotObject])(slot);
        SetVisible(object != nullptr ? Field<u8*>(object, 0x18) : nullptr, false);
    }
}

// A source's guide is the overworld map's with its slots set to the source's kinds and the rest hidden, its captions
// read as the source's during the call (ap_text.cpp).
HOOK_DEFINE_TRAMPOLINE(NaviSetGuide) {
    static void Callback(u8* navi, u32 index) {
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
        u32 mapId = Field<u32>(navi, kNaviIds);
        Field<u32>(navi, kNaviIds) = kOverworldMap;
        g_Guide = guide;
        Orig(navi, 0);
        auto set = GameFn<void (*)(void*, int, u32)>(kGuideSet);
        for (int slot = 0; slot < guide->count && slot < kGuideSlotCount; slot++) {
            if (guide->kinds[slot] != kMapGuide[slot]) set(navi + kNaviGuide, slot, guide->kinds[slot]);
        }
        g_Guide = nullptr;
        Field<u32>(navi, kNaviIds) = mapId;
        for (int slot = guide->count; slot < kGuideSlotCount; slot++) HideGuideSlot(navi, slot);
        Field<u8>(navi, kGuideRedraw) = 1;
    }
};

namespace AP::ListUi {
    u8* FindPane(u8* root, const char* name) {
        if (root == nullptr) return nullptr;
        return reinterpret_cast<u8* (*)(void*, const char*, bool)>(VTable(root)[kFindPane])(root, name, true);
    }

    u8* FindUi(int id) {
        u8* end = UiManager() + kUiList;
        for (u8* node = Field<u8*>(end, 8); node != end; node = Field<u8*>(node, 8)) {
            u8* ui = Field<u8*>(node, kNodeUi);
            if (ui != nullptr && reinterpret_cast<int (*)(void*)>(VTable(ui)[kUiId])(ui) == id) return ui;
        }
        return nullptr;
    }

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    void PlaySound(const char* name) {
        StrView view{name, static_cast<s64>(strlen(name))};
        GameFn<void (*)(const StrView*, void*, float, float)>(kPlaySound)(&view, nullptr, 1.0f, 1.0f);
    }

    void QueueGlyphs(u8* textBox) {
        struct { void* owner; void* count; void* textBox; } request{nullptr, nullptr, textBox};
        GameFn<bool (*)(void*, void*, int)>(kQueueGlyphs)(UiManager(), &request, 0);
    }

    // Takes a text box out of the UI manager's glyph queues (nodes without an owner: ours).
    void Unqueue(u8* textBox) {
        if (textBox == nullptr) return;
        for (int i = 0; i < 2; i++) {
            u8* end = UiManager() + kUiGlyphQueues + i * kQueueStride;
            for (u8* node = Field<u8*>(end, 8); node != end;) {
                u8* next = Field<u8*>(node, 8);
                if (Field<u8*>(node, kNodeTextBox) == textBox && Field<void*>(node, kNodeCount) == nullptr) {
                    u8* previous = Field<u8*>(node, 0);
                    Field<u8*>(previous, 8) = next;
                    Field<u8*>(next, 0) = previous;
                    Field<u64>(end, kQueueSize)--;
                    GameFn<void (*)(void*)>(kOperatorDelete)(node);
                }
                node = next;
            }
        }
    }

    void SetVisible(u8* pane, bool visible) {
        if (pane == nullptr) return;
        Field<u8>(pane, kPaneFlags) = (Field<u8>(pane, kPaneFlags) & ~kPaneVisible) | (visible ? kPaneVisible : 0);
    }

    void SetAlpha(u8* pane, int alpha) {
        if (pane == nullptr) return;
        u8 a = static_cast<u8>(std::clamp(alpha, 0, 255));
        Field<u8>(pane, kPaneAlpha) = a;
        Field<u8>(pane, kPaneGlobalAlpha) = a;
    }

    void MoveTo(u8* pane, float x, float y) {
        if (pane == nullptr) return;
        Field<float>(pane, kPaneX) = x;
        Field<float>(pane, kPaneY) = y;
        Field<u8>(pane, kPaneFlags) |= kPaneDirty;
    }

    void ScaleTo(u8* pane, float x, float y) {
        if (pane == nullptr) return;
        Field<float>(pane, kPaneScaleX) = x;
        Field<float>(pane, kPaneScaleY) = y;
        Field<u8>(pane, kPaneFlags) |= kPaneDirty;
    }

    void ScaleTo(u8* pane, float scale) { ScaleTo(pane, scale, scale); }

    void Bind(Text& text, u8* box) {
        text.box = box;
        Field<u8*>(text.holder, 0x10) = box;
    }

    void SetText(Text& text, const std::string& value) {
        if (text.box == nullptr || (value == text.value && !value.empty())) return;
        text.value = value;
        AP::SetPaneText(text.holder, value);
        QueueGlyphs(text.box);
    }

    bool Allocate(u8* box, u16 chars) {
        if (box == nullptr) return false;
        if (Field<u16>(box, kTextCapacity) > chars) return true;
        u8* graphics = *reinterpret_cast<u8**>(g_BaseAddress + kGraphics);
        void* device = nullptr;
        if (graphics != nullptr && Field<void**>(graphics, 8) != nullptr) device = *Field<void**>(graphics, 8);
        if (device == nullptr) return false;
        reinterpret_cast<void (*)(void*, void*, u16)>(VTable(box)[kTextAllocate])(box, device, chars);
        return Field<u16>(box, kTextCapacity) > chars;
    }

    bool Drawn(const Text& text) { return text.box != nullptr && Field<u8>(text.box, kTextGlyphsReady) != 0; }

    bool Span(const Text& text, float& left, float& right) {
        if (!Drawn(text) || text.value.empty()) return false;
        Rect extent = reinterpret_cast<Rect (*)(void*)>(VTable(text.box)[kTextRect])(text.box);
        if (!std::isfinite(extent.left + extent.right) || extent.right <= extent.left) return false;
        left = extent.left;
        right = extent.right;
        return true;
    }

    u8* PageRoot(u8* page) {
        u8* pageLayout = page != nullptr ? Field<u8*>(page, kWrapperLayout) : nullptr;
        return pageLayout != nullptr ? Field<u8*>(pageLayout, kLayoutRoot) : nullptr;
    }

    bool Build(Layout& layout, const char* file, u8* page, u8* pageRoot) {
        layout.wrapper = GameFn<u8* (*)(size_t)>(kOperatorNew)(kWrapperSize);
        StrView view{file, static_cast<s64>(strlen(file))};
        GameFn<void (*)(void*, StrView*, int, int, bool)>(kWrapperCtor)(layout.wrapper, &view, kPriority, 0, true);
        u8* lyt = Field<u8*>(layout.wrapper, kWrapperLayout);
        layout.root = lyt != nullptr ? Field<u8*>(lyt, kLayoutRoot) : nullptr;
        if (layout.root == nullptr) {
            Logging.Log(std::string("[AP] list: layout not built: ") + file);
            return false;
        }
        GameFn<void (*)(void*, void*, void*)>(kWrapperAttach)(layout.wrapper, page, pageRoot);
        Field<u8>(layout.root, kPaneFlags) |= kPaneInfluencedAlpha;
        SetAlpha(layout.root, 0);
        return true;
    }

    void Drop(Layout& layout) {
        Unqueue(layout.note.box);
        if (layout.wrapper != nullptr) {
            reinterpret_cast<void (*)(void*)>(VTable(layout.wrapper)[kWrapperDelete])(layout.wrapper);
        }
        layout = Layout{};
    }

    bool BuildList(List& list, u8* page, u8* pageRoot, float x, float y) {
        if (!Build(list.layout, kListLayout, page, pageRoot)) return false;
        u8* root = list.layout.root;
        MoveTo(root, x, y);
        SetVisible(FindPane(root, "N_Panel_00"), false);
        for (const char* name : kBlankRows) {
            u8* blank = FindPane(root, name);
            if (blank != nullptr) Field<u8>(blank, kPaneFlags) |= kPaneInfluencedAlpha;
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
            Bind(row.text, FindPane(row.part, "T_Item_00"));
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
        g_PadSeen = NowMs();
    }

    u64 PadHeld() { return NowMs() - g_PadSeen < kPadStaleMs ? g_PadHeld : 0; }

    void AddGuideSource(GuideSource source) { g_GuideSources.push_back(source); }

    void RefreshGuide(u32 index) {
        u8* navi = FindUi(kNaviUi);
        if (navi != nullptr) GameFn<void (*)(void*, u32)>(kNaviSetGuide)(navi, index);
    }

    std::string GuideCaption(int button) {
        if (g_Guide == nullptr || button < 0 || button > 2) return "";
        return g_Guide->captions[button];
    }

    void InstallHooks() {
        NaviSetGuide::InstallAtOffset(offset_manager.Offset(kNaviSetGuide));
    }
}
