// Jukebox page (DESIGN.md "Jukebox page"): the pause menu's fourth page, after System, as a mode of
// index 2, System's slot. The header (Navi) keeps its three pages; while g_Jukebox is set, the Gm_System page it
// builds for index 2 hides its content and shows the Jukebox instead, built from the game's own layouts:
// - the track list: Gm_MemoryList (the Memories list), right of the middle;
// - "Now playing": Gm_MemoryCollect (the Memories count card) on the left, with a note (the font's U+266A) in its
//   paper window and notes floating up out of it while music plays;
// - the name and description of the track under the cursor: the Items page's info texts (Gm_Item N_Info_00);
// - the guide "A Play / B Back": the Items page's guide with its X/Y caption swapped (ap_text.cpp).
// The module steers the moves that differ from the game's: R on System and L on the Jukebox change index 2 to
// index 2 (System is removed and built again), L on the map lands on the Jukebox. Not in the Chamber Dungeon,
// whose third page is the dungeon's own.
#include "AP/ap.hpp"
#include "AP/ap_tracks.hpp"
#include "offsets.hpp"
#include "loggers.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    struct StrView { const char* data; s64 length; };

    // Navi, the pause menu header (vtable 0x1c2aa18): machine at +0x80, state byte +0x8d (Open 1, WaitKey 2,
    // Selected 3, CloseOldUi 4, Close 5); current index u8 +0x23c, target +0x23d; L/R pressed +0x1341 (1 L, 2 R);
    // direction +0x1338 (1 R); page slots (shared pointers) +0x12f0 + index * 0x10; message texts +0x2d0 the current
    // page's title, +0x2e8 the page under L, +0x300 the page under R. Navi_SetGuide (navi, index) fills the button
    // guide for a page.
    constexpr uintptr_t kNaviWaitKey = 0xee8ca0, kNaviCloseOldUiUpdate = 0xeea4f0, kNaviCloseEnter = 0xeeac10,
                        kNaviSetTitles = 0xeedec0, kNaviSetGuide = 0xeed6f0, kNaviChangeState = 0xee7dc0;
    constexpr size_t kNaviMachine = 0x80, kNaviState = 0x8d, kNaviCurrent = 0x23c, kNaviTarget = 0x23d,
                     kNaviLR = 0x1341, kNaviDirection = 0x1338, kNaviSlots = 0x12f0, kNaviTitle = 0x2d0,
                     kNaviBack = 0x2e8, kNaviNext = 0x300;
    constexpr u8 kStateSelected = 3, kStateCloseOldUi = 4, kStateClose = 5;
    constexpr u8 kLeft = 1, kRight = 2;
    constexpr int kMap = 0, kItems = 1, kSystem = 2;
    constexpr uintptr_t kLevelIndex = 0x1cc13c8;  // the dungeon index; 8 the Chamber Dungeon (third page id 0xb)
    constexpr u8 kChamberDungeon = 8;

    // Gm_System (vtable 0x1c2fe50): setup (vtable +0x20), update (+0x28; its machine at +0xf0, state byte +0xfd),
    // the destructor its deleting destructor calls (shared with other pages). A page's +0xe0 set: B closes the menu;
    // +0x70 cleared by the header when it leaves the page.
    constexpr uintptr_t kSystemSetup = 0xf82f40, kSystemUpdate = 0xf83650, kPageDtor = 0xec4330;
    constexpr size_t kSystemState = 0xfd, kPageCanClose = 0xe0, kPageActive = 0x70;
    constexpr const char* kSystemContent = "N_SystemChoice_00";

    // Sound (name, null, volume, pitch); MessageText_SetLabel (text, "file:label")
    constexpr uintptr_t kPlaySound = 0xa57e0, kSetLabel = 0xf8c8b0;

    // Layouts at runtime (TESTING.md, "Layouts at runtime", "Text box glyphs"; ap_nametag.cpp): built with
    // LayoutWrapper_Build and hung under the page's root pane with LayoutWrapper_AttachTo, so they draw with the page.
    constexpr uintptr_t kOperatorNew = 0x1431170, kOperatorDelete = 0x1431110, kWrapperCtor = 0xe59100,
                        kWrapperAttach = 0xe59d40, kUiManager = 0x1c971c0, kQueueGlyphs = 0xeda8d0;
    constexpr size_t kWrapperSize = 0x78, kWrapperLayout = 0x18, kLayoutRoot = 0x18, kWrapperDelete = 0x18 / 8,
                     kFindPane = 0x60 / 8, kHolderPane = 0x10, kTextGlyphsReady = 0x190;
    constexpr size_t kUiGlyphQueues = 0xab40, kQueueStride = 0x18, kQueueSize = 0x10, kNodeCount = 0x18,
                     kNodeTextBox = 0x20;
    constexpr int kPriority = 10;
    // Pane (nn::ui2d): translation +0x30, scale +0x48, flags +0x58 (visible 0x01, alpha passed to the children 0x02,
    // global matrix dirty 0x10), alpha +0x59, the alpha it draws with +0x5a.
    constexpr size_t kPaneX = 0x30, kPaneY = 0x34, kPaneScaleX = 0x48, kPaneScaleY = 0x4c, kPaneFlags = 0x58,
                     kPaneAlpha = 0x59, kPaneGlobalAlpha = 0x5a;
    constexpr u8 kPaneVisible = 0x01, kPaneInfluencedAlpha = 0x02, kPaneDirty = 0x10;

    // Gm_MemoryList: 7 rows L_Item_00..06 (Gm_MemoryListItem: T_Item_00 in the alignment pane A_Item_00, alpha 0 in
    // the file, beside the Memories' marks L_Way_00, B_bounding_01 and L_MemoryMark_00, hidden here; the highlight
    // windows W_Add_00/01), y 119 down by 48; the blank rows L_Blank_00..03 above and below; the cursor
    // L_ListCursor_00 (Cm_ListCursor) at the root; the scroll bar's handle W_ScrollHandle_00 (y 189 at the top, 94
    // high in a 390 track); the Memories' panel count N_Panel_00, hidden. A_List_00 is an alignment pane: a hidden
    // row gives its place to the rows under it, so the blank rows stay and are made transparent instead.
    constexpr const char* kListLayout = "Gm_MemoryList.bflyt";
    constexpr int kRows = 7;
    constexpr const char* kRowHidden[] = {"L_Way_00", "B_bounding_01", "L_MemoryMark_00"};
    constexpr const char* kBlankRows[] = {"L_Blank_00", "L_Blank_01", "L_Blank_02", "L_Blank_03"};
    constexpr float kRowTop = 119, kRowStep = 48, kHandleTop = 189, kHandleTravel = 390 - 94;
    constexpr float kListX = 250, kListY = 40;
    constexpr u8 kHighlight = 96;  // W_Add alpha of the row animation On: the track that plays

    // Gm_MemoryCollect: T_Collect_00 (two lines, 33 characters) over the paper window W_window_00 (y -46), which holds
    // A_Collect_00 (x 10): the icon P_pict_00 (x -86, hidden here) and T_CollectNum_00 / T_CollectMaxNum_00. The text
    // moves up from y 54, leaving room for the notes that float up out of the window.
    constexpr const char* kCardLayout = "Gm_MemoryCollect.bflyt";
    constexpr float kCardX = -300, kCardY = -14, kCardTextY = 167;
    constexpr float kIconX = kCardX - 105, kIconY = kCardY - 46;  // left of the count
    constexpr u8 kCardPaper = 200;  // W_window_00's alpha (48 in the file)

    // Gm_Item: N_Item_00 holds the item grid, the instruments, the cursor, the pop-up list and the rupees, hidden here,
    // and N_Info_00 with T_Name_00 (41 characters, y 36.46) and T_Info_00 (83 characters, two lines, centred on its
    // box), where the Items page names the item under its cursor. A description of two lines moves down.
    constexpr const char* kInfoLayout = "Gm_Item.bflyt";
    constexpr const char* kInfoHidden[] = {"N_SetItem_00", "N_Collect_00", "L_Cursor_00", "L_PopList_00",
                                           "L_ItemRupee_00"};
    constexpr float kInfoY = -20.48f, kInfoLower = 14;

    // A note: Gm_SystemOpUnit (a label of the System page's controller picture), one text box T_Operation_00 (20
    // characters, 32 high), holding the font's eighth note.
    constexpr const char* kNoteLayout = "Gm_SystemOpUnit.bflyt";
    constexpr const char* kNote = "♪";
    constexpr int kFloating = 3;
    constexpr float kIconScale = 2.6f, kMarkScale = 1.2f, kMarkX = kListX - 172;
    constexpr int kFloatFrames = 96, kBobFrames = 50, kFadeStep = 32;

    // The guide's caption for A: the button tag (0x0E, group 1, type 1, one parameter byte: 0x15, A) and the word.
    const char kPlayCaption[] = "\x0e\x01\x00\x01\x00\x01\x00\x15Play";

    // Buttons (nn::hid): A, Up/Down on the d-pad and the left stick.
    constexpr u64 kButtonA = 0x1, kButtonUp = 0x2000 | (1ull << 17), kButtonDown = 0x8000 | (1ull << 19);
    constexpr u64 kPadStaleMs = 50;  // the last poll's buttons count as held this long
    constexpr int kRepeatDelay = 20, kRepeatEvery = 5;  // frames before Up/Down repeat, and between repeats

    constexpr int kEntries = AP::Tracks::COUNT + 1;  // entry 0: the game's own music

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    template<typename T>
    T& Field(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<u8*>(base) + offset); }

    uintptr_t* VTable(const void* object) { return *reinterpret_cast<uintptr_t* const*>(object); }

    u8* FindPane(u8* root, const char* name) {
        if (root == nullptr) return nullptr;
        return reinterpret_cast<u8* (*)(void*, const char*, bool)>(VTable(root)[kFindPane])(root, name, true);
    }

    u8* UiManager() { return reinterpret_cast<u8*>(g_BaseAddress + kUiManager); }

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    bool InChamberDungeon() { return *reinterpret_cast<u8*>(g_BaseAddress + kLevelIndex) == kChamberDungeon; }

    // The header and the mode
    u8* g_Navi = nullptr;
    bool g_Jukebox = false;         // index 2 shows the Jukebox
    bool g_Pending = false;         // the value g_Jukebox takes once the old page is removed
    bool g_ChangeAhead = false;     // a page change runs: g_Pending applies when CloseOldUi ends
    bool g_Guide = false;           // Navi_SetGuide runs for the Jukebox: the X/Y caption reads "A Play"
    int g_Changes = 0;              // changes through index 2 the module steered (2 to 2)

    // A text box and the text object AP::SetPaneText fills it through (+0x10 the text pane).
    struct Text {
        u8* box = nullptr;
        alignas(8) u8 holder[0x18] = {};
    };
    struct Row {
        u8* part = nullptr;
        u8* add0 = nullptr;
        u8* add1 = nullptr;
        Text text;
    };
    struct Layout {
        u8* wrapper = nullptr;
        u8* root = nullptr;
        Text note;                  // a note layout's text
    };
    struct Page {
        u8* page = nullptr;         // the Gm_System object that shows the Jukebox
        Layout list, card, info, icon, mark, floating[kFloating];
        Row rows[kRows];
        u8* cursor = nullptr;
        u8* handle = nullptr;
        Text title, number, slash, total, name, about;
    };
    Page g_Ui;
    int g_Cursor = 0, g_Top = 0;
    int g_Built = 0, g_Dropped = 0; // pages built and dropped
    std::string g_Shown;            // the chosen track the page shows
    u32 g_Frame = 0;
    int g_Fade = 0;                 // 0..255, the alpha the page's layouts draw with
    u64 g_PrevPad = 0;
    int g_HeldFrames = 0;

    // The buttons of the last input poll that saw any.
    u64 g_PadHeld = 0, g_PadSeen = 0;
    u64 PadHeld() { return NowMs() - g_PadSeen < kPadStaleMs ? g_PadHeld : 0; }

    void PlaySound(const char* name) {
        StrView view{name, static_cast<s64>(strlen(name))};
        GameFn<void (*)(const StrView*, void*, float, float)>(kPlaySound)(&view, nullptr, 1.0f, 1.0f);
    }

    void SetLabel(u8* text, const char* label) {
        StrView view{label, static_cast<s64>(strlen(label))};
        GameFn<void (*)(void*, const StrView*)>(kSetLabel)(text, &view);
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

    // The header's texts: the game's text objects, their text pane at +0x10.
    void SetHeaderText(u8* holder, const std::string& text) {
        AP::SetPaneText(holder, text);
        QueueGlyphs(Field<u8*>(holder, kHolderPane));
    }

    void Bind(Text& text, u8* box) {
        text.box = box;
        Field<u8*>(text.holder, kHolderPane) = box;
    }

    void SetText(Text& text, const std::string& value) {
        if (text.box == nullptr) return;
        AP::SetPaneText(text.holder, value);
        QueueGlyphs(text.box);
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

    void ScaleTo(u8* pane, float scale) {
        if (pane == nullptr) return;
        Field<float>(pane, kPaneScaleX) = scale;
        Field<float>(pane, kPaneScaleY) = scale;
        Field<u8>(pane, kPaneFlags) |= kPaneDirty;
    }

    // 0 at 0, 1 at half, 0 at 1, repeating
    float Triangle(float x) {
        float f = x - static_cast<float>(static_cast<int>(x));
        return 1.0f - (f < 0.5f ? 1.0f - 2.0f * f : 2.0f * f - 1.0f);
    }

    const AP::Tracks::Track& Entry(int entry) {
        return entry == 0 ? AP::Tracks::GAME_MUSIC : AP::Tracks::TRACKS[entry - 1];
    }

    // The entry of a track id, 0 for "" or a track not in the list.
    int EntryOf(const std::string& id) {
        for (int i = 0; i < AP::Tracks::COUNT; i++) {
            if (id == AP::Tracks::TRACKS[i].id) return i + 1;
        }
        return 0;
    }

    // Builds a layout and hangs it under the page's root pane.
    bool Build(Layout& layout, const char* file, u8* page, u8* pageRoot) {
        layout.wrapper = GameFn<u8* (*)(size_t)>(kOperatorNew)(kWrapperSize);
        StrView view{file, static_cast<s64>(strlen(file))};
        GameFn<void (*)(void*, StrView*, int, int, bool)>(kWrapperCtor)(layout.wrapper, &view, kPriority, 0, true);
        u8* lyt = Field<u8*>(layout.wrapper, kWrapperLayout);
        layout.root = lyt != nullptr ? Field<u8*>(lyt, kLayoutRoot) : nullptr;
        if (layout.root == nullptr) {
            Logging.Log(std::string("[AP] jukebox: layout not built: ") + file);
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

    void DropPage() {
        if (g_Ui.page == nullptr) return;
        for (auto& row : g_Ui.rows) Unqueue(row.text.box);
        for (Text* text : {&g_Ui.title, &g_Ui.number, &g_Ui.slash, &g_Ui.total, &g_Ui.name, &g_Ui.about}) {
            Unqueue(text->box);
        }
        for (auto& note : g_Ui.floating) Drop(note);
        Drop(g_Ui.mark);
        Drop(g_Ui.icon);
        Drop(g_Ui.info);
        Drop(g_Ui.card);
        Drop(g_Ui.list);
        g_Ui = Page{};
        g_Dropped++;
    }

    // The list's rows, the cursor and the scroll handle, the playing mark; the card; the info under the list.
    void Fill() {
        std::string chosen = AP::Music::Track();
        g_Shown = chosen;
        int chosenEntry = EntryOf(chosen);
        // what plays: the chosen track, else the zone's own when it is in the list
        int playing = chosen.empty() ? EntryOf(AP::Music::ZoneTrack()) : chosenEntry;
        for (int r = 0; r < kRows; r++) {
            Row& row = g_Ui.rows[r];
            int entry = g_Top + r;
            SetVisible(row.part, entry < kEntries);
            if (entry >= kEntries) continue;
            SetText(row.text, Entry(entry).name);
            SetAlpha(row.add0, entry == chosenEntry ? kHighlight : 0);
            SetAlpha(row.add1, entry == chosenEntry ? kHighlight : 0);
        }
        MoveTo(g_Ui.cursor, 0, kRowTop - kRowStep * (g_Cursor - g_Top));
        float scrolled = static_cast<float>(g_Top) / (kEntries - kRows);
        MoveTo(g_Ui.handle, 0, kHandleTop - kHandleTravel * scrolled);
        bool marked = chosenEntry >= g_Top && chosenEntry < g_Top + kRows;
        SetVisible(g_Ui.mark.root, marked);
        if (marked) MoveTo(g_Ui.mark.root, kMarkX, kListY + kRowTop - kRowStep * (chosenEntry - g_Top));

        bool silent = chosen.empty() && AP::Music::ZoneTrack().empty();
        const char* playingName = playing > 0 ? Entry(playing).name : silent ? "Silence" : "Game music";
        SetText(g_Ui.title, std::string("Now playing\n") + playingName);
        SetText(g_Ui.number, playing > 0 ? std::to_string(playing) : "-");
        SetText(g_Ui.total, std::to_string(AP::Tracks::COUNT));
        SetText(g_Ui.name, Entry(g_Cursor).name);
        SetText(g_Ui.about, Entry(g_Cursor).info);
        MoveTo(g_Ui.about.box, 0, kInfoY - (strchr(Entry(g_Cursor).info, '\n') != nullptr ? kInfoLower : 0));
    }

    // Once a frame: the page fades in, and out while the header leaves it; the note bobs; notes float up while
    // music plays.
    void Animate() {
        g_Frame++;
        u8 navi = g_Navi != nullptr ? Field<u8>(g_Navi, kNaviState) : 0;
        bool leaving = navi == kStateCloseOldUi || navi == kStateClose;
        g_Fade = std::clamp(g_Fade + (leaving ? -kFadeStep : kFadeStep), 0, 255);
        for (Layout* layout : {&g_Ui.list, &g_Ui.card, &g_Ui.info, &g_Ui.icon, &g_Ui.mark}) {
            SetAlpha(layout->root, g_Fade);
        }

        MoveTo(g_Ui.icon.root, kIconX, kIconY + 6.0f * Triangle(static_cast<float>(g_Frame) / kBobFrames));
        bool music = !AP::Music::Track().empty() || !AP::Music::ZoneTrack().empty();
        for (int i = 0; i < kFloating; i++) {
            u8* note = g_Ui.floating[i].root;
            if (note == nullptr) continue;
            SetVisible(note, music);
            float t = static_cast<float>((g_Frame + i * kFloatFrames / kFloating) % kFloatFrames) / kFloatFrames;
            float side = i == 1 ? -1.0f : 1.0f;
            float x = kIconX + side * (10.0f + 24.0f * t) + 5.0f * (Triangle(t * 2) - 0.5f);
            float y = kIconY + 30.0f + 80.0f * t;
            MoveTo(note, x, y);
            ScaleTo(note, 1.1f + 0.5f * t);
            float a = t < 0.25f ? t / 0.25f : (1.0f - t) / 0.75f;
            SetAlpha(note, static_cast<int>(a * g_Fade));
        }
    }

    // The Jukebox in the Gm_System page: System's choices hidden, the layouts hung under the page.
    void BuildPage(u8* page) {
        u8* pageLayout = Field<u8*>(page, kWrapperLayout);
        u8* pageRoot = pageLayout != nullptr ? Field<u8*>(pageLayout, kLayoutRoot) : nullptr;
        if (pageRoot == nullptr) return;
        SetVisible(FindPane(pageRoot, kSystemContent), false);
        g_Ui.page = page;
        g_Built++;
        bool ok = Build(g_Ui.info, kInfoLayout, page, pageRoot) && Build(g_Ui.card, kCardLayout, page, pageRoot) &&
                  Build(g_Ui.list, kListLayout, page, pageRoot) && Build(g_Ui.icon, kNoteLayout, page, pageRoot) &&
                  Build(g_Ui.mark, kNoteLayout, page, pageRoot);
        for (auto& note : g_Ui.floating) ok = ok && Build(note, kNoteLayout, page, pageRoot);
        if (!ok) {
            DropPage();
            return;
        }

        // the list
        u8* list = g_Ui.list.root;
        MoveTo(list, kListX, kListY);
        SetVisible(FindPane(list, "N_Panel_00"), false);
        for (const char* name : kBlankRows) {
            u8* blank = FindPane(list, name);
            if (blank != nullptr) Field<u8>(blank, kPaneFlags) |= kPaneInfluencedAlpha;
            SetAlpha(blank, 0);
        }
        g_Ui.cursor = FindPane(list, "L_ListCursor_00");
        g_Ui.handle = FindPane(FindPane(list, "L_ScrollBar_00"), "W_ScrollHandle_00");
        char name[16];
        for (int r = 0; r < kRows; r++) {
            Row& row = g_Ui.rows[r];
            snprintf(name, sizeof(name), "L_Item_%02d", r);
            row.part = FindPane(list, name);
            row.add0 = FindPane(row.part, "W_Add_00");
            row.add1 = FindPane(row.part, "W_Add_01");
            Bind(row.text, FindPane(row.part, "T_Item_00"));
            SetAlpha(FindPane(row.part, "A_Item_00"), 255);
            for (const char* hidden : kRowHidden) SetVisible(FindPane(row.part, hidden), false);
        }

        // the card, its icon replaced by the note
        u8* card = g_Ui.card.root;
        MoveTo(FindPane(card, "N_Collect_00"), kCardX, kCardY);
        MoveTo(FindPane(card, "T_Collect_00"), 0, kCardTextY);
        SetVisible(FindPane(card, "P_pict_00"), false);
        SetAlpha(FindPane(card, "W_window_00"), kCardPaper);
        Bind(g_Ui.title, FindPane(card, "T_Collect_00"));
        Bind(g_Ui.number, FindPane(card, "T_CollectNum_00"));
        Bind(g_Ui.slash, FindPane(card, "T_CollectSlash_00"));
        Bind(g_Ui.total, FindPane(card, "T_CollectMaxNum_00"));
        SetText(g_Ui.slash, "/");

        // the notes
        for (Layout* note : {&g_Ui.icon, &g_Ui.mark, &g_Ui.floating[0], &g_Ui.floating[1], &g_Ui.floating[2]}) {
            Bind(note->note, FindPane(note->root, "T_Operation_00"));
            SetText(note->note, kNote);
        }
        ScaleTo(g_Ui.icon.root, kIconScale);
        ScaleTo(g_Ui.mark.root, kMarkScale);

        // the info texts
        u8* info = g_Ui.info.root;
        for (const char* hidden : kInfoHidden) SetVisible(FindPane(info, hidden), false);
        SetAlpha(FindPane(info, "N_Item_00"), 255);
        Bind(g_Ui.name, FindPane(info, "T_Name_00"));
        Bind(g_Ui.about, FindPane(info, "T_Info_00"));

        // the cursor on the chosen track, a few rows from the top
        g_Cursor = EntryOf(AP::Music::Track());
        g_Top = std::clamp(g_Cursor - kRows / 2, 0, kEntries - kRows);
        g_Fade = 0;
        g_PrevPad = PadHeld();
        g_HeldFrames = 0;
        Fill();
        Animate();
        Logging.Log("[AP] jukebox: page built");
    }

    void MoveCursor(int step) {
        int next = std::clamp(g_Cursor + step, 0, kEntries - 1);
        if (next == g_Cursor) return;
        g_Cursor = next;
        if (g_Cursor < g_Top) g_Top = g_Cursor;
        if (g_Cursor >= g_Top + kRows) g_Top = g_Cursor - kRows + 1;
        Fill();
        PlaySound("SE_SY_CURSOL_MOVE");
    }

    // The page's buttons, once a frame while it takes them: Up/Down (repeating while held), A.
    void Input() {
        u64 pad = PadHeld(), pressed = pad & ~g_PrevPad;
        g_PrevPad = pad;
        int step = (pad & kButtonDown) ? 1 : (pad & kButtonUp) ? -1 : 0;
        g_HeldFrames = step != 0 ? g_HeldFrames + 1 : 0;
        if (pressed & (kButtonUp | kButtonDown)) {
            MoveCursor(step);
        } else if (g_HeldFrames > kRepeatDelay && (g_HeldFrames - kRepeatDelay) % kRepeatEvery == 0) {
            MoveCursor(step);
        }
        if (pressed & kButtonA) {
            AP::Music::Choose(g_Cursor == 0 ? "" : Entry(g_Cursor).id);
            AP::Music::Tick();  // Tick does not run while the menu is open: apply now
            PlaySound("SE_SY_DECIDE");
            Fill();
        }
    }

    // A page change the header would not make: index 2 to index 2.
    void ChangeInPlace(u8* navi, bool jukebox, u8 direction) {
        int index = Field<u8>(navi, kNaviCurrent);
        u8* old = Field<u8*>(navi, kNaviSlots + index * 0x10);
        if (old != nullptr) Field<u8>(old, kPageActive) = 0;
        Field<u8>(navi, kNaviLR) = 0;
        Field<u8>(navi, kNaviDirection) = direction == kRight;
        Field<u8>(navi, kNaviTarget) = index;
        g_Pending = jukebox;
        g_ChangeAhead = true;
        g_Changes++;
        GameFn<void (*)(void*, int)>(kNaviChangeState)(navi + kNaviMachine, kStateCloseOldUi);
        PlaySound("SE_SY_MENU_LR_MOVE");
    }
}

HOOK_DEFINE_TRAMPOLINE(NaviWaitKey) {
    static void Callback(u8* navi) {
        g_Navi = navi;
        if (InChamberDungeon()) return Orig(navi);
        int index = Field<u8>(navi, kNaviCurrent);
        u8 lr = Field<u8>(navi, kNaviLR);
        if (index == kSystem && lr == kRight && !g_Jukebox) return ChangeInPlace(navi, true, kRight);
        if (index == kSystem && lr == kLeft && g_Jukebox) return ChangeInPlace(navi, false, kLeft);
        Orig(navi);
        if (Field<u8>(navi, kNaviState) == kStateCloseOldUi && !g_ChangeAhead) {
            // the game's own change: the Jukebox comes after the map going left, and is left any other way
            g_Pending = index == kMap && lr == kLeft && Field<u8>(navi, kNaviTarget) == kSystem;
            g_ChangeAhead = true;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(NaviCloseOldUiUpdate) {
    static void Callback(u8* navi) {
        Orig(navi);
        if (g_ChangeAhead && Field<u8>(navi, kNaviState) == kStateSelected) {
            g_Jukebox = g_Pending;
            g_ChangeAhead = false;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(NaviCloseEnter) {
    static void Callback(u8* navi) {
        Orig(navi);
        g_Jukebox = false;
        g_Pending = false;
        g_ChangeAhead = false;
    }
};

HOOK_DEFINE_TRAMPOLINE(NaviSetTitles) {
    static void Callback(u8* navi, u32 index) {
        Orig(navi, index);
        if (InChamberDungeon()) return;
        if (index == kSystem && g_Jukebox) {
            SetHeaderText(navi + kNaviTitle, "Jukebox");
            SetLabel(navi + kNaviBack, "UI:SubScreenTitleSystem");
            SetLabel(navi + kNaviNext, "UI:SubScreenTitleMap");
        } else if (index == kSystem) {
            SetHeaderText(navi + kNaviNext, "Jukebox");
        } else if (index == kMap) {
            SetHeaderText(navi + kNaviBack, "Jukebox");
        }
    }
};

// The Jukebox's guide is the Items page's (X/Y Set item, B Back) with the X/Y caption read as "A Play".
HOOK_DEFINE_TRAMPOLINE(NaviSetGuide) {
    static void Callback(u8* navi, u32 index) {
        if (index != kSystem || !g_Jukebox || InChamberDungeon()) return Orig(navi, index);
        g_Guide = true;
        Orig(navi, kItems);
        g_Guide = false;
    }
};

HOOK_DEFINE_TRAMPOLINE(SystemSetup) {
    static void Callback(u8* page) {
        Orig(page);
        if (!g_Jukebox) return;
        DropPage();
        BuildPage(page);
    }
};

HOOK_DEFINE_TRAMPOLINE(SystemUpdate) {
    static void Callback(u8* page) {
        if (page != g_Ui.page) return Orig(page);
        Animate();
        if (AP::Music::Track() != g_Shown) Fill();  // the client chose a track
        // Gm_System's own update runs while it opens (its In animation), never once it takes buttons
        if (Field<u8>(page, kSystemState) == 0 || Field<u8>(page, kSystemState) == 0xff) return Orig(page);
        Field<u8>(page, kPageCanClose) = 1;
        Input();
    }
};

HOOK_DEFINE_TRAMPOLINE(PageDtor) {
    static void Callback(u8* page) {
        if (page == g_Ui.page) DropPage();
        Orig(page);
    }
};

namespace AP::JukeboxPane {
    void InstallHooks() {
        NaviWaitKey::InstallAtOffset(offset_manager.Offset(kNaviWaitKey));
        NaviCloseOldUiUpdate::InstallAtOffset(offset_manager.Offset(kNaviCloseOldUiUpdate));
        NaviCloseEnter::InstallAtOffset(offset_manager.Offset(kNaviCloseEnter));
        NaviSetTitles::InstallAtOffset(offset_manager.Offset(kNaviSetTitles));
        NaviSetGuide::InstallAtOffset(offset_manager.Offset(kNaviSetGuide));
        SystemSetup::InstallAtOffset(offset_manager.Offset(kSystemSetup));
        SystemUpdate::InstallAtOffset(offset_manager.Offset(kSystemUpdate));
        PageDtor::InstallAtOffset(offset_manager.Offset(kPageDtor));
    }

    void OnPad(u64 buttons) {
        if (buttons == 0) return;
        g_PadHeld = buttons;
        g_PadSeen = NowMs();
    }

    std::string GuideCaption() { return g_Guide ? std::string(kPlayCaption, sizeof(kPlayCaption) - 1) : std::string(); }

#ifdef LASAP_TEST
    // STATE's " pane=<0-3, - with no header> navi=<header state> jbox=<cursor>/<top>"; the header pointer is kept
    // after the menu closes, so pane= counts only while the game is paused.
    std::string StateField(bool paused) {
        std::string s = " pane=";
        if (g_Navi == nullptr || !paused) return s + "- navi=- jbox=-";
        int index = Field<u8>(g_Navi, kNaviCurrent);
        s += std::to_string(index == kSystem && g_Jukebox ? 3 : index);
        s += " navi=" + std::to_string(Field<u8>(g_Navi, kNaviState));
        s += " jbox=";
        s += g_Ui.page != nullptr ? std::to_string(g_Cursor) + "/" + std::to_string(g_Top) : std::string("-");
        return s;
    }

    // DBG jukebox
    std::string Info() {
        std::string s = "JUKEBOX mode=" + std::to_string(g_Jukebox) + " pending=" + std::to_string(g_Pending) +
                        " ahead=" + std::to_string(g_ChangeAhead) + " changes=" + std::to_string(g_Changes) +
                        " built=" + std::to_string(g_Built) + " dropped=" + std::to_string(g_Dropped);
        char buf[64];
        snprintf(buf, sizeof(buf), " page=%lx list=%lx", reinterpret_cast<uintptr_t>(g_Ui.page),
                 reinterpret_cast<uintptr_t>(g_Ui.list.wrapper));
        s += buf;
        if (g_Ui.page != nullptr) s += " sysstate=" + std::to_string(Field<u8>(g_Ui.page, kSystemState));
        int texts = 0, glyphs = 0;
        auto count = [&](const Text& text) {
            if (text.box == nullptr) return;
            texts++;
            glyphs += Field<u8>(text.box, kTextGlyphsReady) != 0;
        };
        for (auto& row : g_Ui.rows) count(row.text);
        s += " rows=" + std::to_string(texts) + " drawn=" + std::to_string(glyphs);
        texts = glyphs = 0;
        for (const Text* text : {&g_Ui.title, &g_Ui.number, &g_Ui.slash, &g_Ui.total, &g_Ui.name, &g_Ui.about}) {
            count(*text);
        }
        s += " texts=" + std::to_string(texts) + " textsdrawn=" + std::to_string(glyphs);
        s += " fade=" + std::to_string(g_Fade);
        if (g_Ui.page != nullptr) s += " name=" + std::string(Entry(g_Cursor).name);
        for (char& c : s) c = c == '\n' ? '|' : c;
        return s;
    }
#endif
}
