// Jukebox pane, a spike (JUKEBOX_PLAN.md): the pause menu's fourth pane as a mode of index 2, System's slot. The
// header (Navi) keeps its three pages; while g_Jukebox is set, the Gm_System page it builds for index 2 hides its
// content and shows a track list instead. The module steers the moves that differ from the game's: R on System and
// L on the Jukebox change index 2 to index 2 (System is removed and built again), L on the map lands on the Jukebox.
// Test build only for now.
#ifdef LASAP_TEST
#include "AP/ap.hpp"
#include "offsets.hpp"
#include "loggers.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace {
    struct StrView { const char* data; s64 length; };

    // Navi, the pause menu header (vtable 0x1c2aa18): machine at +0x80, state byte +0x8d (Open 1, WaitKey 2,
    // Selected 3, CloseOldUi 4, Close 5); page ids u32 +0x230; current index u8 +0x23c, target +0x23d; L/R pressed
    // +0x1341 (1 L, 2 R); direction +0x1338 (1 R); page slots (shared pointers) +0x12f0 + index * 0x10; message texts
    // +0x2d0 the current page's title, +0x2e8 the page under L, +0x300 the page under R.
    constexpr uintptr_t kNaviWaitKey = 0xee8ca0, kNaviCloseOldUiUpdate = 0xeea4f0, kNaviCloseEnter = 0xeeac10,
                        kNaviSetTitles = 0xeedec0, kNaviChangeState = 0xee7dc0;
    constexpr size_t kNaviMachine = 0x80, kNaviState = 0x8d, kNaviIds = 0x230, kNaviCurrent = 0x23c,
                     kNaviTarget = 0x23d, kNaviLR = 0x1341, kNaviDirection = 0x1338, kNaviSlots = 0x12f0,
                     kNaviTitle = 0x2d0, kNaviBack = 0x2e8, kNaviNext = 0x300;
    constexpr u8 kStateSelected = 3, kStateCloseOldUi = 4;
    constexpr u8 kLeft = 1, kRight = 2;
    constexpr int kMap = 0, kSystem = 2;
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

    // Layouts at runtime (TESTING.md, "Layouts at runtime", "Text box glyphs"; ap_nametag.cpp)
    constexpr uintptr_t kOperatorNew = 0x1431170, kOperatorDelete = 0x1431110, kWrapperCtor = 0xe59100,
                        kWrapperAttach = 0xe59d40, kUiManager = 0x1c971c0, kQueueGlyphs = 0xeda8d0;
    constexpr size_t kWrapperSize = 0x78, kWrapperLayout = 0x18, kLayoutRoot = 0x18, kWrapperDelete = 0x18 / 8,
                     kFindPane = 0x60 / 8, kHolderPane = 0x10, kPaneFlags = 0x58, kTextGlyphsReady = 0x190;
    constexpr size_t kUiGlyphQueues = 0xab40, kQueueStride = 0x18, kQueueSize = 0x10, kNodeCount = 0x18,
                     kNodeTextBox = 0x20;
    constexpr u8 kPaneVisible = 0x01;
    constexpr const char* kListLayout = "Gm_MemoryList.bflyt";
    constexpr int kRows = 7;

    // Buttons (nn::hid): A, Up/Down on the d-pad and the left stick.
    constexpr u64 kButtonA = 0x1, kButtonUp = 0x2000 | (1ull << 17), kButtonDown = 0x8000 | (1ull << 19);
    constexpr u64 kPadStaleMs = 50;

    const char* const kTracks[] = {
        "BGM_DUNGEON_LV6_FACE", "BGM_DUNGEON_LV4_ANGLER", "BGM_CHICKEN_HURT", "BGM_GHOST_HOUSE", "BGM_ANIMAL_VILLAGE",
        "BGM_DUNGEON_LV1_TAIL", "BGM_DUNGEON_2D_SIDEVIEW", "BGM_DUGEON_CASTLE", "BGM_DUNGEON_LV3_KEY", "BGM_CAVE",
        "BGM_TARUTARU", "BGM_HOUSE", "BGM_MEVE", "BGM_FISHINGMAN", "BGM_DUNGEON_LV2_POT", "BGM_STRANGE_FOREST",
        "BGM_DUNGEON_HOLY_EGG", "BGM_SEASHELL_HOUSE", "BGM_TELEPHONE", "BGM_DANPEI", "BGM_SHOP", "BGM_GAME_SHOP",
        "BGM_GOAT_HOUSE", "BGM_DREAMSHRINE", "BGM_WRIGHT", "BGM_GAME_SHOP_FOR_POND", "BGM_RICHARD",
        "BGM_EVENT_RESCUE_BOWBOW", "BGM_DUNGEON_LV10_CLOTH", "BGM_PLACE_OF_FACE_KEY", "BGM_DUNGEON_LV5_CATFISH",
        "BGM_FAIRY", "BGM_DUNGEON_LV7_TOWER", "BGM_DUNGEON_LV8_TURTLE", "BGM_FIELD_NORMAL", "BGM_FIELD_MARINE_NORMAL",
        "BGM_LASTBOSS_APPEAR", "BGM_LASTBOSS_BATTLE", "BGM_MARINE_NAME", "BGM_MINIGAME_FISHING",
        "BGM_PANEL_DUNG_BEGINNER", "BGM_PANEL_DUNG_DIFFICULT", "BGM_PANEL_DUNG_MEDIUM", "BGM_PANEL_EDIT_MODE",
        "BGM_NAME_INPUT", "BGM_DUNGEON_BOSS", "BGM_DUNGEON_BOSS_MIDDLE", "BGM_DUNGEON_LV8_ENT_BATTLE",
        "BGM_EVENT_MARINE_IN_BEACH", "BGM_EVENT_RESCUE_BOWBOW_INTRO", "BGM_FIELD_FIRST", "BGM_FIELD_NORMAL_INTRO",
        "BGM_GAME_OF_RAFT", "BGM_PANEL_SHADOW_LINK", "BGM_RAFTING_TIMEATTACK", "BGM_RICHARD_230",
        "BGM_STRANGE_FOREST_MARINE", "BGM_TARUTARU2_AFTER_THE_RESCUE", "BGM_TARUTARU_MARINE", "BGM_TOTAKEKE_SONG",
        "BGM_ZELDA_NAME", "BGM_PANEL_RESULT", "BGM_DUNGEON_LV7_BOSS", "BGM_HOUSE_FIRST", "BGM_MADBATTER",
        "BGM_EVENT_DATE", "BGM_LASTBOSS_WIN", "BGM_OWL", "BGM_OWL_LAST", "BGM_DEMO_AFTER_LASTBOSS",
    };
    constexpr int kTrackCount = sizeof(kTracks) / sizeof(kTracks[0]);
    constexpr int kEntries = kTrackCount + 1;  // entry 0: the game's own music

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    template<typename T>
    T& Field(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<u8*>(base) + offset); }

    uintptr_t* VTable(const void* object) { return *reinterpret_cast<uintptr_t* const*>(object); }

    u8* FindPane(u8* root, const char* name) {
        return reinterpret_cast<u8* (*)(void*, const char*, bool)>(VTable(root)[kFindPane])(root, name, true);
    }

    u8* UiManager() { return reinterpret_cast<u8*>(g_BaseAddress + kUiManager); }

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    // The header and the mode
    u8* g_Navi = nullptr;
    bool g_Jukebox = false;         // index 2 shows the Jukebox
    bool g_Pending = false;         // the value g_Jukebox takes once the old page is removed
    bool g_ChangeAhead = false;     // a page change runs: g_Pending applies when CloseOldUi ends
    int g_Changes = 0;              // changes through index 2 the module steered (2 to 2)

    // The page and its list
    struct Row {
        u8* text = nullptr;
        alignas(8) u8 holder[0x18] = {};  // a text object for AP::SetPaneText: +0x10 the text pane
    };
    u8* g_Page = nullptr;           // the Gm_System object that shows the Jukebox
    u8* g_List = nullptr;           // the list layout's wrapper
    Row g_Rows[kRows];
    int g_Cursor = 0, g_Top = 0;
    int g_Built = 0, g_Dropped = 0; // list layouts built and dropped
    u64 g_PrevPad = 0;

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

    void SetText(u8* holder, u8* textBox, const std::string& text) {
        AP::SetPaneText(holder, text);
        QueueGlyphs(textBox);
    }

    std::string EntryName(int entry) {
        if (entry == 0) return "Game music";
        const char* name = kTracks[entry - 1];
        return strncmp(name, "BGM_", 4) == 0 ? name + 4 : name;
    }

    void FillRows() {
        for (int r = 0; r < kRows; r++) {
            if (g_Rows[r].text == nullptr) continue;
            int entry = g_Top + r;
            std::string text = entry < kEntries ? (entry == g_Cursor ? "> " : "  ") + EntryName(entry) : "";
            SetText(g_Rows[r].holder, g_Rows[r].text, text);
        }
    }

    void DropList() {
        for (auto& row : g_Rows) {
            if (row.text != nullptr) Unqueue(row.text);
            row = Row{};
        }
        if (g_List != nullptr) {
            reinterpret_cast<void (*)(void*)>(VTable(g_List)[kWrapperDelete])(g_List);
            g_Dropped++;
        }
        g_List = nullptr;
        g_Page = nullptr;
    }

    // The list layout hung under the page's root pane.
    void BuildList(u8* page) {
        u8* pageLayout = Field<u8*>(page, kWrapperLayout);
        u8* pageRoot = pageLayout != nullptr ? Field<u8*>(pageLayout, kLayoutRoot) : nullptr;
        if (pageRoot == nullptr) return;
        u8* content = FindPane(pageRoot, kSystemContent);
        if (content != nullptr) Field<u8>(content, kPaneFlags) &= ~kPaneVisible;
        g_List = GameFn<u8* (*)(size_t)>(kOperatorNew)(kWrapperSize);
        StrView file{kListLayout, static_cast<s64>(strlen(kListLayout))};
        GameFn<void (*)(void*, StrView*, int, int, bool)>(kWrapperCtor)(g_List, &file, 10, 0, true);
        u8* layout = Field<u8*>(g_List, kWrapperLayout);
        u8* root = layout != nullptr ? Field<u8*>(layout, kLayoutRoot) : nullptr;
        if (root == nullptr) {
            Logging.Log("[AP] jukebox: list layout not built");
            DropList();
            return;
        }
        g_Built++;
        g_Page = page;
        GameFn<void (*)(void*, void*, void*)>(kWrapperAttach)(g_List, page, pageRoot);
        char name[16];
        for (int r = 0; r < kRows; r++) {
            snprintf(name, sizeof(name), "L_Item_%02d", r);
            u8* item = FindPane(root, name);
            u8* text = item != nullptr ? FindPane(item, "T_Item_00") : nullptr;
            g_Rows[r].text = text;
            Field<u8*>(g_Rows[r].holder, kHolderPane) = text;
        }
        FillRows();
        Logging.Log("[AP] jukebox: list built under the page");
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
        if (*reinterpret_cast<u8*>(g_BaseAddress + kLevelIndex) == kChamberDungeon) return Orig(navi);
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
            Logging.Log(std::string("[AP] jukebox: index 2 shows ") + (g_Jukebox ? "the Jukebox" : "System"));
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
        if (*reinterpret_cast<u8*>(g_BaseAddress + kLevelIndex) == kChamberDungeon) return;
        // the title texts' text panes, for the glyph queue
        u8* title = Field<u8*>(navi, kNaviTitle + kHolderPane);
        u8* back = Field<u8*>(navi, kNaviBack + kHolderPane);
        u8* next = Field<u8*>(navi, kNaviNext + kHolderPane);
        if (index == kSystem && g_Jukebox) {
            SetText(navi + kNaviTitle, title, "Jukebox");
            SetLabel(navi + kNaviBack, "UI:SubScreenTitleSystem");
            SetLabel(navi + kNaviNext, "UI:SubScreenTitleMap");
        } else if (index == kSystem) {
            SetText(navi + kNaviNext, next, "Jukebox");
        } else if (index == kMap) {
            SetText(navi + kNaviBack, back, "Jukebox");
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(SystemSetup) {
    static void Callback(u8* page) {
        Orig(page);
        if (!g_Jukebox) return;
        if (g_Page != nullptr) DropList();
        g_Cursor = 0;
        g_Top = 0;
        g_PrevPad = PadHeld();
        BuildList(page);
    }
};

HOOK_DEFINE_TRAMPOLINE(SystemUpdate) {
    static void Callback(u8* page) {
        if (page != g_Page) return Orig(page);
        // Gm_System's own update runs while it opens (its In animation), never once it takes buttons
        if (Field<u8>(page, kSystemState) == 0 || Field<u8>(page, kSystemState) == 0xff) return Orig(page);
        Field<u8>(page, kPageCanClose) = 1;
        u64 pad = PadHeld(), pressed = pad & ~g_PrevPad;
        g_PrevPad = pad;
        int cursor = g_Cursor;
        if (pressed & kButtonDown) cursor = cursor + 1 < kEntries ? cursor + 1 : cursor;
        if (pressed & kButtonUp) cursor = cursor > 0 ? cursor - 1 : 0;
        if (cursor != g_Cursor) {
            g_Cursor = cursor;
            if (g_Cursor < g_Top) g_Top = g_Cursor;
            if (g_Cursor >= g_Top + kRows) g_Top = g_Cursor - kRows + 1;
            FillRows();
            PlaySound("SE_SY_CURSOL_MOVE");
        }
        if (pressed & kButtonA) {
            std::vector<std::string> line{"MUSIC", g_Cursor == 0 ? "off" : kTracks[g_Cursor - 1]};
            AP::Music::HandleLine(line);
            AP::Music::Tick();  // Tick does not run while the menu is open: apply now, as a preview
            PlaySound("SE_SY_DECIDE");
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(PageDtor) {
    static void Callback(u8* page) {
        if (page == g_Page) DropList();
        Orig(page);
    }
};

namespace AP::JukeboxPane {
    void InstallHooks() {
        NaviWaitKey::InstallAtOffset(offset_manager.Offset(kNaviWaitKey));
        NaviCloseOldUiUpdate::InstallAtOffset(offset_manager.Offset(kNaviCloseOldUiUpdate));
        NaviCloseEnter::InstallAtOffset(offset_manager.Offset(kNaviCloseEnter));
        NaviSetTitles::InstallAtOffset(offset_manager.Offset(kNaviSetTitles));
        SystemSetup::InstallAtOffset(offset_manager.Offset(kSystemSetup));
        SystemUpdate::InstallAtOffset(offset_manager.Offset(kSystemUpdate));
        PageDtor::InstallAtOffset(offset_manager.Offset(kPageDtor));
    }

    void OnPad(u64 buttons) {
        if (buttons == 0) return;
        g_PadHeld = buttons;
        g_PadSeen = NowMs();
    }

    // STATE's " pane=<0-3, - with no header> navi=<header state> jbox=<cursor>/<top>"; the header pointer is kept
    // after the menu closes, so pane= counts only while the game is paused.
    std::string StateField(bool paused) {
        std::string s = " pane=";
        if (g_Navi == nullptr || !paused) return s + "- navi=- jbox=-";
        int index = Field<u8>(g_Navi, kNaviCurrent);
        s += std::to_string(index == kSystem && g_Jukebox ? 3 : index);
        s += " navi=" + std::to_string(Field<u8>(g_Navi, kNaviState));
        s += " jbox=" + (g_Page != nullptr ? std::to_string(g_Cursor) + "/" + std::to_string(g_Top) : std::string("-"));
        return s;
    }

    // DBG jukebox
    std::string Info() {
        std::string s = "JUKEBOX mode=" + std::to_string(g_Jukebox) + " pending=" + std::to_string(g_Pending) +
                        " ahead=" + std::to_string(g_ChangeAhead) + " changes=" + std::to_string(g_Changes) +
                        " built=" + std::to_string(g_Built) + " dropped=" + std::to_string(g_Dropped);
        char buf[64];
        snprintf(buf, sizeof(buf), " page=%lx list=%lx", reinterpret_cast<uintptr_t>(g_Page),
                 reinterpret_cast<uintptr_t>(g_List));
        s += buf;
        if (g_Page != nullptr) s += " sysstate=" + std::to_string(Field<u8>(g_Page, kSystemState));
        int glyphs = 0, rows = 0;
        for (auto& row : g_Rows) {
            if (row.text == nullptr) continue;
            rows++;
            glyphs += Field<u8>(row.text, kTextGlyphsReady) != 0;
        }
        s += " rows=" + std::to_string(rows) + " drawn=" + std::to_string(glyphs);
        return s;
    }
}
#endif
