#pragma once
// The pause menu's header (Navi) and pages, the autosave popup and the message window: the fields the module reads
// (scripts/re/UI.md).
#include "game/stl.hpp"
#include "game/ui.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Ui {
    // A slot of the pause menu's guide: its virtual kObject gives an object whose +0x18 is the slot's pane.
    struct GuideSlot {
        const uintptr_t* vtable;
        u8 pad_8[0x108];
    };
    static_assert(sizeof(GuideSlot) == 0x110);
    namespace GuideSlotVirtual {
        constexpr size_t kObject = 0x48;
    }

    // The guide at the bottom of the pause menu (Navi +0x318). Navi_SetGuide(navi, index) clears it, then
    // Guide_Set(guide, slot, kind) per slot, which loads a slot's caption only when its kind differs from the one kept
    // in `kinds`.
    struct NaviGuide {
        u8 pad_0[0x90];
        GuideSlot slots[5];        // 0x90
        u8 pad_5e0[0xa20 - 0x5e0];
        GuideSlot swapSlot;        // 0xa20: stands for slot Navi::guideSwapIndex while Navi::guideSwapping
        u8 pad_b30[0xb40 - 0xb30];
        u32 kinds[6];              // 0xb40
    };
    static_assert(offsetof(NaviGuide, slots) == 0x90 && offsetof(NaviGuide, swapSlot) == 0xa20);
    static_assert(offsetof(NaviGuide, kinds) == 0xb40);

    // The pause menu's header (UI kNaviUi, vtable 0x1c2aa18): the page titles, the guide and the pages, by index (0
    // the map, 1 Items, 2 System).
    struct Navi {
        LayoutWrapper wrapper;
        u8 pad_78[0x8];
        StateMachine machine;      // 0x80: Navi_ChangeState; kNaviOpen..kNaviClose
        u8 pad_8e[0x230 - 0x80 - sizeof(StateMachine)];
        u32 pageIds[3];            // 0x230: the overworld map's kOverworldMapPage
        u8 current;                // 0x23c: Navi_WaitKey sets it from target on its first frame on a page
        u8 target;                 // 0x23d
        u8 pad_23e[0x2d0 - 0x23e];
        TextHolder title;          // 0x2d0: the current page's title
        TextHolder back;           // 0x2e8: the page under L
        TextHolder next;           // 0x300: the page under R
        NaviGuide guide;           // 0x318
        u8 pad_e70[0x1224 - 0xe70];
        u32 guideSwapIndex;        // 0x1224
        u8 guideRedraw;            // 0x1228: set, the guide is drawn again
        u8 pad_1229;
        u8 guideSwapping;          // 0x122a
        u8 pad_122b[0x12f0 - 0x122b];
        SharedPtr pages[3];        // 0x12f0: MenuPage
        u8 pad_1320[0x1338 - 0x1320];
        u8 direction;              // 0x1338: 1 R
        u8 pad_1339[0x1341 - 0x1339];
        u8 lr;                     // 0x1341: 1 L, 2 R; kept from a press while the next page opens until a page change
                                   // takes it
    };
    static_assert(offsetof(Navi, machine) == 0x80 && offsetof(Navi, pageIds) == 0x230);
    static_assert(offsetof(Navi, current) == 0x23c && offsetof(Navi, target) == 0x23d);
    static_assert(offsetof(Navi, title) == 0x2d0 && offsetof(Navi, back) == 0x2e8 && offsetof(Navi, next) == 0x300);
    static_assert(offsetof(Navi, guide) == 0x318 && offsetof(Navi, guideSwapIndex) == 0x1224);
    static_assert(offsetof(Navi, guideRedraw) == 0x1228 && offsetof(Navi, guideSwapping) == 0x122a);
    static_assert(offsetof(Navi, pages) == 0x12f0 && offsetof(Navi, direction) == 0x1338);
    static_assert(offsetof(Navi, lr) == 0x1341);
    constexpr u8 kNaviOpen = 1, kNaviWaitKey = 2, kNaviSelected = 3, kNaviCloseOldUi = 4, kNaviClose = 5;
    constexpr u32 kOverworldMapPage = 0xf;

    // A pause menu page (Page_vtable 0x1c2a950: Gm_Item, Gm_System, the maps); LayoutWrapper::pageActive is its +0x70.
    struct MenuPage {
        LayoutWrapper wrapper;
        u8 pad_78[0xe0 - 0x78];
        u8 canClose;               // 0xe0: set, B closes the menu and L/R turn the page
        u8 pad_e1[0xf];
        StateMachine machine;      // 0xf0 (Gm_System, Gm_Item, the maps)
    };
    static_assert(offsetof(MenuPage, canClose) == 0xe0 && offsetof(MenuPage, machine) == 0xf0);

    // The autosave popup (Ld_ThrobberSave, "Now Saving"): Throbber_Show, Throbber_ChangeState.
    struct Throbber {
        u8 pad_0[0x80];
        StateMachine machine;      // 0x80: kThrobberNone..kThrobberLoopAuto
        u8 pad_8e[0x278 - 0x80 - sizeof(StateMachine)];
        TextHolder text;           // 0x278: UI:SystemSaving
        float timer;               // 0x290: LoopAuto counts it down and slides out at zero
    };
    static_assert(offsetof(Throbber, machine) == 0x80 && offsetof(Throbber, text) == 0x278);
    static_assert(offsetof(Throbber, timer) == 0x290);
    constexpr u8 kThrobberNone = 0, kThrobberIn = 1, kThrobberLoop = 2, kThrobberOut = 3, kThrobberInAuto = 4,
                 kThrobberLoopAuto = 5;

    // The message window's text (MessageText_ShowLetters).
    struct MessageText {
        u8 pad_0[0x288];
        s32 lettersShown;          // 0x288
    };
    static_assert(offsetof(MessageText, lettersShown) == 0x288);

    // The message window (Cm_Message's owner, UI 0x24, vtable 0x1c2abd0) for talk, item-get and telop text.
    struct MessageWindow {
        u8 pad_0[0x58];
        u8 choiceLayouts[16][0x18];  // 0x58: object first (+0x3c f32: 0 once the choice takes buttons)
        u8 pad_1d8[0xe18 - 0x1d8];
        MessageText* text;         // 0xe18
        u8 pad_e20[0x1260 - 0xe20];
        StateMachine machine;      // 0x1260: Closed, Opening, FadeIn, Reading, TextFadeOut, KeyWaiting, ...
        u8 pad_126e[0x1b6a - 0x1260 - sizeof(StateMachine)];
        u16 pageLetters;           // 0x1b6a: letters in the current page
        u8 pad_1b6c[0x1cc4 - 0x1b6c];
        s32 choiceLayout;          // 0x1cc4
        u8 choiceCursor;           // 0x1cc8: 0 the first option, Down adds one
        u8 pad_1cc9[0x1cf4 - 0x1cc9];
        // After KeyWaiting or PreSelecting starts, waitCount runs up to waitLimit (6 for a page, 10 before a choice);
        // a button counts only once it is there.
        s16 waitCount;             // 0x1cf4
        s16 waitLimit;             // 0x1cf6
        u8 pad_1cf8[0x1d1a - 0x1cf8];
        u8 skipping;               // 0x1d1a: set by B: show pages whole, skip the text's pauses
    };
    static_assert(offsetof(MessageWindow, choiceLayouts) == 0x58 && offsetof(MessageWindow, text) == 0xe18);
    static_assert(offsetof(MessageWindow, machine) == 0x1260 && offsetof(MessageWindow, pageLetters) == 0x1b6a);
    static_assert(offsetof(MessageWindow, choiceLayout) == 0x1cc4 && offsetof(MessageWindow, choiceCursor) == 0x1cc8);
    static_assert(offsetof(MessageWindow, waitCount) == 0x1cf4 && offsetof(MessageWindow, waitLimit) == 0x1cf6);
    static_assert(offsetof(MessageWindow, skipping) == 0x1d1a);
    constexpr size_t kChoiceLayoutAnim = 0x3c;
}
