#pragma once
// Lists built from the game's layouts (ap_list.cpp): the Memories list (Gm_MemoryList) with its rows, cursor and
// scroll bar, hung under a pause menu page, and the button guide made from the overworld map's. The Jukebox page
// (ap_jukebox.cpp) and the map's check list (ap_map.cpp) use them.
#include "lib.hpp"
#include "offsets.hpp"
#include <string>

namespace AP::ListUi {
    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    template<typename T>
    T& Field(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<u8*>(base) + offset); }

    inline uintptr_t* VTable(const void* object) { return *reinterpret_cast<uintptr_t* const*>(object); }

    // A pane of a layout by name, or null (also for a null root).
    u8* FindPane(u8* root, const char* name);
    // The UI of that id in the UI manager's list (Navi 5, overworld map 0xf), or null.
    u8* FindUi(int id);
    constexpr int kNaviUi = 5;
    u64 NowMs();
    void PlaySound(const char* name);
    // Queues a text box with the UI manager so the font draws its glyphs; Unqueue takes it out again.
    void QueueGlyphs(u8* textBox);
    void Unqueue(u8* textBox);

    // nn::ui2d pane fields
    constexpr size_t kPaneX = 0x30, kPaneY = 0x34, kPaneScaleX = 0x48, kPaneScaleY = 0x4c, kPaneWidth = 0x50,
                     kPaneHeight = 0x54, kPaneFlags = 0x58, kPaneAlpha = 0x59, kPaneGlobalAlpha = 0x5a;
    constexpr u8 kPaneVisible = 0x01, kPaneInfluencedAlpha = 0x02, kPaneDirty = 0x10;
    void SetVisible(u8* pane, bool visible);
    void SetAlpha(u8* pane, int alpha);
    void MoveTo(u8* pane, float x, float y);
    void ScaleTo(u8* pane, float scale);
    void ScaleTo(u8* pane, float x, float y);

    // A text box and the text object AP::SetPaneText fills it through (+0x10 the text pane).
    struct Text {
        u8* box = nullptr;
        alignas(8) u8 holder[0x18] = {};
        std::string value;          // the text it shows: set again only when it changes
    };
    void Bind(Text& text, u8* box);
    void SetText(Text& text, const std::string& value);
    // Room for that many UTF-16 units in a text box (ui2d's AllocateStringBuffer); false if it has less.
    bool Allocate(u8* box, u16 chars);
    // Whether the font has drawn the text's glyphs; the text's left and right edge as laid out (in the text box,
    // unscaled), false until then.
    bool Drawn(const Text& text);
    bool Span(const Text& text, float& left, float& right);

    // A layout of Game.arc built at runtime and hung under a page's root pane, so it draws with the page.
    struct Layout {
        u8* wrapper = nullptr;
        u8* root = nullptr;
        Text note;                  // the text of a one-text layout (Gm_SystemOpUnit)
    };
    // The page's root pane (its layout's), or null.
    u8* PageRoot(u8* page);
    // Builds the layout transparent (SetAlpha on its root fades it in).
    bool Build(Layout& layout, const char* file, u8* page, u8* pageRoot);
    void Drop(Layout& layout);

    // Gm_MemoryList: 7 rows, y 119 down by 48; the list's own panel P_pict_00 (420 by 470) behind them.
    constexpr int kRows = 7;
    constexpr float kRowTop = 119, kRowStep = 48;
    struct Row {
        u8* part = nullptr;         // the row's Gm_MemoryListItem
        u8* add0 = nullptr;         // its highlight windows W_Add_00/01
        u8* add1 = nullptr;
        Text text;
    };
    struct List {
        Layout layout;
        Row rows[kRows];
        u8* cursor = nullptr;
        u8* scroll = nullptr;       // the scroll bar
        u8* handle = nullptr;       // its handle
        int count = 0;              // entries
        int at = 0;                 // the entry under the cursor
        int top = 0;                // the entry in the first row
        u64 prevPad = 0;            // the buttons last frame
        int heldFrames = 0;         // frames Up or Down has been held
    };
    // Builds the list at (x, y) on the page, without the Memories' marks and count, its rows empty.
    bool BuildList(List& list, u8* page, u8* pageRoot, float x, float y);
    void DropList(List& list);
    // The entries: the cursor on `at`, a few rows from the top when they scroll.
    void SetEntries(List& list, int count, int at);
    // Shows the rows that hold an entry, the cursor on its row, the scroll bar while the entries do not fit.
    void Place(List& list);
    // Once a frame: Up/Down move the cursor (held: again after 20 frames, then every 5) with a sound; returns the
    // buttons pressed since last frame. It reads the buttons of the polls since its last call. `moved` is set when the cursor moved (the rows then show other entries).
    u64 Input(List& list, bool& moved);

    // The pad: each input poll's buttons (nn::hid), and those of the last poll that saw any, while recent (50 ms).
    void OnPad(u64 buttons);
    u64 PadHeld();
    // Buttons (nn::hid): A, B, X, Y; Up and Down on the d-pad or the left stick.
    constexpr u64 kButtonA = 0x1, kButtonB = 0x2, kButtonX = 0x4, kButtonY = 0x8, kButtonUp = 0x2000 | (1ull << 17),
                  kButtonDown = 0x8000 | (1ull << 19);

    // A guide made from the overworld map's (X 0x13, Y 0x14, R 0x15, A 0x12, B 0x11; Navi_SetGuide with index 0 and
    // the map's id): its slots' kinds in order, the other slots hidden, and the words after the button of the map's
    // X (Memories), Y (Hide markers) and A (Pin) captions ("" the game's).
    constexpr u32 kGuideX = 0x13, kGuideY = 0x14, kGuideR = 0x15, kGuideA = 0x12, kGuideB = 0x11;
    struct Guide {
        int count = 0;
        u32 kinds[5] = {};
        const char* captions[3] = {"", "", ""};
    };
    // A page that shows its own guide: its guide for the header's page index, or null.
    using GuideSource = const Guide* (*)(u32 index);
    void AddGuideSource(GuideSource source);
    // Sets the guide of the header's page at that index again (Navi_SetGuide), as the page changes its own.
    void RefreshGuide(u32 index);
    // While a guide of a source is set: the words for the map's caption of button 0 X, 1 Y or 2 A; else "".
    std::string GuideCaption(int button);

    void InstallHooks();
}
