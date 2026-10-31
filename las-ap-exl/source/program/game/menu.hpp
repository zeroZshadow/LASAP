#pragma once
// The pause menu's header (Navi), its guide and pages, the autosave popup, the message window and the title screen
// (scripts/re/UI.md).
#include "game/state_machine.hpp"
#include "game/stl.hpp"
#include "game/ui.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Ui {
    // A slot of the pause menu's guide (L_BtnNN, vtable 0x1c2e620; 0x110 bytes): a button and its caption.
    struct GuideSlot {
        Part part;
        Animation normal;          // 0x90
        Animation disable;         // 0xa8
        Animation attentionInOut;  // 0xc0: the attention slot's
        Animation attentionLoop;   // 0xd8: played once attentionInOut ends
        TextHolder caption;        // 0xf0: T_Operation_00
        u8 enabled;                // 0x108: Normal, else Disable
        u8 attentionIn;            // 0x109: attentionInOut played forward (GuideSlot_AttentionIn)
        u8 pad_10a[0x6];
    };
    static_assert(offsetof(GuideSlot, normal) == 0x90 && offsetof(GuideSlot, caption) == 0xf0);
    static_assert(offsetof(GuideSlot, enabled) == 0x108 && sizeof(GuideSlot) == 0x110);

    // A caption's text: UTF-16 and its length.
    struct GuideCaption {
        char16_t text[0x20];
        u64 length;                // 0x40
    };
    static_assert(sizeof(GuideCaption) == 0x48);

    // The guide at the bottom of the pause menu (Guide_Ctor, L_Operation_00, vtable 0x1c2e538; 0xf18 bytes; Navi
    // +0x318). Navi_SetGuide(navi, index) clears it, then Guide_Set(guide, slot, kind) per slot, which loads a slot's
    // caption only when its kind differs from the one kept in `kinds`. The update lays the shown slots out from the
    // right while `relayout` is set.
    struct NaviGuide {
        Part part;
        GuideSlot slots[9];        // 0x90: L_Btn00..08
        GuideSlot attentionSlot;   // 0xa20: L_BtnAttention00, shown in place of slot attentionIndex while attention
        float area[4];             // 0xb30: left, top, right, bottom
        u32 kinds[9];              // 0xb40
        u8 pad_b64[0x4];
        GuideCaption captions[9];  // 0xb68
        TagProcessor tags;         // 0xdf0: the slots' text boxes'
        float spacing;             // 0xf08: between slots
        u32 attentionIndex;        // 0xf0c
        u8 relayout;               // 0xf10
        u8 hasAttention;           // 0xf11: the layout has L_BtnAttention00
        u8 attention;              // 0xf12
        u8 attentionEnding;        // 0xf13: ends once both its animations stop
        u8 pad_f14[0x4];
    };
    static_assert(offsetof(NaviGuide, slots) == 0x90 && offsetof(NaviGuide, attentionSlot) == 0xa20);
    static_assert(offsetof(NaviGuide, area) == 0xb30 && offsetof(NaviGuide, kinds) == 0xb40);
    static_assert(offsetof(NaviGuide, captions) == 0xb68 && offsetof(NaviGuide, tags) == 0xdf0);
    static_assert(offsetof(NaviGuide, spacing) == 0xf08 && offsetof(NaviGuide, attentionIndex) == 0xf0c);
    static_assert(offsetof(NaviGuide, relayout) == 0xf10 && offsetof(NaviGuide, attention) == 0xf12);
    static_assert(sizeof(NaviGuide) == 0xf18);

    struct MenuPage;

    // The pause menu's header (UI kNaviUi, Gm_Navi.bflyt, Navi_Ctor, vtable 0x1c2aa18; 0x1350 bytes): the page titles,
    // the guide and the pages, by index (0 the map, 1 Items, 2 System).
    struct Navi {
        LayoutWrapper wrapper;
        u8 pad_78[0x8];
        StateMachine<6> machine;   // 0x80: Navi_ChangeState; kNaviOpen..kNaviClose
        u32 pageIds[3];            // 0x230: the overworld map's kOverworldMapPage (the dungeon map's 0xe), 6, 10
        u8 current;                // 0x23c: Navi_WaitKey sets it from target on its first frame on a page
        u8 target;                 // 0x23d
        u8 pad_23e[0x2];
        Animation in;              // 0x240
        Animation out;             // 0x258
        Animation nextOut;         // 0x270
        Animation nextIn;          // 0x288
        Animation backOut;         // 0x2a0
        Animation backIn;          // 0x2b8
        TextHolder title;          // 0x2d0: the current page's title
        TextHolder back;           // 0x2e8: the page under L
        TextHolder next;           // 0x300: the page under R
        NaviGuide guide;           // 0x318
        PaneHandle hudPane;        // 0x1230: N_HUD_00
        PaneHandle nextPicture;    // 0x1248
        PaneHandle backPicture;    // 0x1260
        PaneHandle nextIcon;       // 0x1278
        PaneHandle backIcon;       // 0x1290
        PaneHandle anchors[3];     // 0x12a8: the panes the pages hang under
        SharedPtr<MenuPage> pages[3];  // 0x12f0
        u8 overworldZoom;          // 0x1320: kept from the overworld map's zoom index
        u8 dungeonZoom;            // 0x1321
        u8 pad_1322[0x6];
        float mapCursor[2];        // 0x1328: the last map page's cursor
        u8 mapCursorSaved;         // 0x1330
        u8 pinType;                // 0x1331: the pin type selected in the map pages' pin bar
        u8 pad_1332[0x2];
        u32 itemCursor;            // 0x1334: the Items page's cursor
        u8 direction;              // 0x1338: 1 R
        s8 levelIndex;             // 0x1339: LevelIndex at the setup
        u8 memoryTab;              // 0x133a: the Memories UI's tab, kept while the menu is open
        u8 memoryListPos[3];       // 0x133b: its first tab's list position
        u8 panelListPos[3];        // 0x133e: its Chamber Dungeon panels list position
        u8 lr;                     // 0x1341: 1 L, 2 R; kept from a press while the next page opens until a page change
                                   // takes it
        u8 inputOn;                // 0x1342: L and R are read only while set
        u8 pad_1343[0xd];
    };
    static_assert(offsetof(Navi, machine) == 0x80 && offsetof(Navi, pageIds) == 0x230);
    static_assert(offsetof(Navi, current) == 0x23c && offsetof(Navi, target) == 0x23d);
    static_assert(offsetof(Navi, in) == 0x240 && offsetof(Navi, backIn) == 0x2b8);
    static_assert(offsetof(Navi, title) == 0x2d0 && offsetof(Navi, back) == 0x2e8 && offsetof(Navi, next) == 0x300);
    static_assert(offsetof(Navi, guide) == 0x318 && offsetof(Navi, hudPane) == 0x1230);
    static_assert(offsetof(Navi, anchors) == 0x12a8 && offsetof(Navi, pages) == 0x12f0);
    static_assert(offsetof(Navi, overworldZoom) == 0x1320 && offsetof(Navi, mapCursor) == 0x1328);
    static_assert(offsetof(Navi, pinType) == 0x1331 && offsetof(Navi, itemCursor) == 0x1334);
    static_assert(offsetof(Navi, direction) == 0x1338 && offsetof(Navi, memoryTab) == 0x133a);
    static_assert(offsetof(Navi, lr) == 0x1341 && offsetof(Navi, inputOn) == 0x1342 && sizeof(Navi) == 0x1350);
    constexpr u8 kNaviOpen = 1, kNaviWaitKey = 2, kNaviSelected = 3, kNaviCloseOldUi = 4, kNaviClose = 5;
    constexpr u32 kOverworldMapPage = 0xf;

    // A pause menu page (Page_vtable 0x1c2a950, Page_Dtor; 0xf0 bytes): the base of Gm_Item, Gm_System, the maps and
    // the Memories. The derived pages' state machines follow it.
    struct MenuPage {
        LayoutWrapper wrapper;
        Animation in;              // 0x78
        Animation out;             // 0x90
        u8 pad_a8[0x8];
        Function setGuide;         // 0xb0: (const u32& slot, const u32& kind): the Navi guide's slot, kind 0x1c hides
                                   // it (Navi_GuideCallback)
        u8 canClose;               // 0xe0: set, B closes the menu and L/R turn the page
        u8 pad_e1[0xf];
    };
    static_assert(offsetof(MenuPage, in) == 0x78 && offsetof(MenuPage, setGuide) == 0xb0);
    static_assert(offsetof(MenuPage, canClose) == 0xe0 && sizeof(MenuPage) == 0xf0);

    // A choice of the System page (L_SystemChoice_NN, SystemChoice_vtable 0x1c2c440; 0x168 bytes).
    struct SystemChoice {
        Part part;
        Animation on;              // 0x90: the hovered choice's
        Animation off;             // 0xa8
        Animation loop;            // 0xc0
        Animation decide;          // 0xd8
        Animation normal;          // 0xf0
        Animation disable;         // 0x108
        Animation set;             // 0x120: frame 1 on RubyOn
        Animation toggleOnOff;     // 0x138: RubyOn's
        TextHolder text;           // 0x150: T_List_00
    };
    static_assert(offsetof(SystemChoice, decide) == 0xd8 && offsetof(SystemChoice, text) == 0x150);
    static_assert(sizeof(SystemChoice) == 0x168);

    // A line of the System page's controls list (L_Op_NN, SystemOpEntry_vtable 0x1c2f9f8; 0xa8 bytes).
    struct SystemOpEntry {
        Part part;
        TextHolder text;           // 0x90: T_Operation_00, a UI:SystemOperating* label
    };
    static_assert(sizeof(SystemOpEntry) == 0xa8);

    // The System page's controls list (L_SystemOp_00, SystemOp_vtable 0x1c2f980; 0x678 bytes): Item, Map, Guard,
    // ItemX, ItemY, Dash, Action, Attack, Move.
    struct SystemOp {
        Part part;
        SystemOpEntry entries[9];  // 0x90
    };
    static_assert(offsetof(SystemOp, entries) == 0x90 && sizeof(SystemOp) == 0x678);

    // The System page (UI 10, Gm_System.bflyt, vtable 0x1c2fe50, GmSystem_Dtor; 0xd70 bytes): Save, Load and (in
    // Japanese) ruby text.
    struct GmSystem {
        MenuPage page;
        StateMachine<6> machine;   // 0xf0: None, Open, KeyWait, Decide, WaitUiClose, ChangeRuby
        u32 choiceUis[2];          // 0x2a0: the UI each choice opens: 0x1f save, 0x20 load (Cm_FileOperate)
        SystemChoice choices[3];   // 0x2a8: Save, Load, RubyOn
        u8 previousCursor;         // 0x6e0
        u8 cursor;                 // 0x6e1: the hovered choice
        u8 pad_6e2[0x6];
        u64 choiceCount;           // 0x6e8: 3 in Japanese, else 2
        u8 rubyOn;                 // 0x6f0
        u8 pad_6f1[0x7];
        SystemOp systemOp;         // 0x6f8
    };
    static_assert(offsetof(GmSystem, machine) == 0xf0 && offsetof(GmSystem, choiceUis) == 0x2a0);
    static_assert(offsetof(GmSystem, choices) == 0x2a8 && offsetof(GmSystem, cursor) == 0x6e1);
    static_assert(offsetof(GmSystem, choiceCount) == 0x6e8 && offsetof(GmSystem, systemOp) == 0x6f8);
    static_assert(sizeof(GmSystem) == 0xd70);

    // The autosave popup (UI 0x2a, Ld_ThrobberSave.bflyt, "Now Saving", ThrobberSave_Ctor; 0x2a0 bytes):
    // ThrobberSave_Show, ThrobberSave_ChangeState.
    struct ThrobberSave {
        LayoutWrapper wrapper;
        u8 pad_78[0x8];
        StateMachine<6> machine;   // 0x80: kThrobberNone..kThrobberLoopAuto
        Animation in;              // 0x230
        Animation loop;            // 0x248
        Animation out;             // 0x260
        TextHolder text;           // 0x278: T_Info_00, UI:SystemSaving
        float timer;               // 0x290: LoopAuto counts it down and slides out at zero
        u8 pad_294[0xc];
    };
    static_assert(offsetof(ThrobberSave, machine) == 0x80 && offsetof(ThrobberSave, in) == 0x230);
    static_assert(offsetof(ThrobberSave, text) == 0x278 && offsetof(ThrobberSave, timer) == 0x290);
    static_assert(sizeof(ThrobberSave) == 0x2a0);
    constexpr u8 kThrobberNone = 0, kThrobberIn = 1, kThrobberLoop = 2, kThrobberOut = 3, kThrobberInAuto = 4,
                 kThrobberLoopAuto = 5;

    // The message window's next-page icon (L_MesFeed_00, MessageFeed_vtable 0x1c2e058; 0xf8 bytes).
    struct MessageFeed {
        Part part;
        Animation in;              // 0x90: Loop plays once it ends
        Animation out;             // 0xa8
        Animation loop;            // 0xc0
        Animation decide;          // 0xd8
        u8 shown;                  // 0xf0: In played by KeyWaiting, cleared by Out
        u8 pad_f1[0x7];
    };
    static_assert(offsetof(MessageFeed, decide) == 0xd8 && offsetof(MessageFeed, shown) == 0xf0);
    static_assert(sizeof(MessageFeed) == 0xf8);

    // A part of the message window that shows text (MessageText_vtable; 0x298 bytes): talk, item get, system, telop
    // and modeless messages. MessageText_ShowLetters reveals the page's letters.
    struct MessageText {
        Part part;
        Animation in;              // 0x90: Opening waits for its end
        Animation out;             // 0xa8
        Animation letters;         // 0xc0: Typing; its frame is the letters shown
        TextHolder mainText;       // 0xd8: T_Mes_00
        TextHolder altText;        // 0xf0: T_Mes_01, for some window kinds (MessageText_SelectText)
        TextHolder stalfosText;    // 0x108: T_MesStalfos_00 (MessageText_SelectStalfosText)
        TextHolder text;           // 0x120: the one of the three in use
        PaneHandle mesPosPane;     // 0x138: N_MesPos_00, the pane the balloon's follower moves
        PaneHandle normalPane;     // 0x150: N_Normal_00
        Vector<Animation> kindAnims;  // 0x168: by window kind (ItemGet, KeyItemGet, ...)
        MessageFeed feed;          // 0x180: L_MesFeed_00
        u16 letterFrames;          // 0x278: the letters animation's frames per page
        u8 pad_27a[0x2];
        float framesPerLetter;     // 0x27c: Typing's frames / letterFrames
        float speed;               // 0x280: letterFrames per letter
        float revealRate;          // 0x284: 1, 2 once B hurries the page (MessageText_Hurry)
        s32 lettersShown;          // 0x288
        s32 lettersTarget;         // 0x28c
        s32 lettersFade;           // 0x290
        u8 hasFeed;                // 0x294: the layout has L_MesFeed_00
        u8 pad_295[0x3];
    };
    static_assert(offsetof(MessageText, letters) == 0xc0 && offsetof(MessageText, mainText) == 0xd8);
    static_assert(offsetof(MessageText, text) == 0x120 && offsetof(MessageText, normalPane) == 0x150);
    static_assert(offsetof(MessageText, feed) == 0x180 && offsetof(MessageText, revealRate) == 0x284);
    static_assert(offsetof(MessageText, kindAnims) == 0x168 && offsetof(MessageText, letterFrames) == 0x278);
    static_assert(offsetof(MessageText, lettersShown) == 0x288 && sizeof(MessageText) == 0x298);

    // An option of a choice (L_Choice_NN, MessageChoice_vtable; 0x110 bytes).
    struct MessageChoice {
        Part part;
        Animation on;              // 0x90
        Animation off;             // 0xa8
        Animation loop;            // 0xc0
        Animation decide;          // 0xd8
        TextHolder text;           // 0xf0: T_Choice_00
        u8 hovered;                // 0x108
        u8 decided;                // 0x109: Selecting waits for it and Decide's end
        u8 pad_10a[0x6];
    };
    static_assert(offsetof(MessageChoice, text) == 0xf0 && offsetof(MessageChoice, decided) == 0x109);
    static_assert(sizeof(MessageChoice) == 0x110);

    // A page's or an option's text: UTF-16 and its length.
    template<int N>
    struct MessageString {
        char16_t text[N];
        u64 length;
    };
    static_assert(sizeof(MessageString<0x180>) == 0x308 && sizeof(MessageString<0x40>) == 0x88);

    // A frame counter: count runs up to limit by step a frame.
    struct WaitCounter {
        float step;
        s16 count;                 // 0x4
        s16 limit;                 // 0x6
    };
    static_assert(offsetof(WaitCounter, count) == 0x4 && sizeof(WaitCounter) == 0x8);

    // The message window (UI 0x24, Cm_Message.bflyt, MessageWindow_Ctor, vtable 0x1c2abd0; 0x1d20 bytes) for talk,
    // item-get and telop text.
    struct MessageWindow {
        LayoutWrapper wrapper;
        Animation inOutChoice[3];  // 0x78: InOutChoice2..4; a choice of n options plays [n - 2]
        Animation position;        // 0xc0
        MessageText talk;          // 0xd8: L_Message_00
        PaneHandle talkName[2];    // 0x370: T_Name_00, W_Name_00
        MessageText getItem;       // 0x3a0: L_MessageGetItem_00
        MessageText system;        // 0x638: L_MessageSystem_00
        MessageText telop;         // 0x8d0: L_MessageTelop_00
        MessageText modeless;      // 0xb68: L_MessageModeless_00
        TextHolder modelessName;   // 0xe00: T_Name_00
        MessageText* text;         // 0xe18: the part in use
        MessageChoice choices[4];  // 0xe20
        StateMachine<15> machine;  // 0x1260: Closed, Opening, FadeIn, Reading, TextFadeOut, KeyWaiting, PreSelecting,
                                   // Selecting, Selected, TimerWaiting, EventWaiting, ButtonWaiting, DrawEnd, FadeOut,
                                   // Closing
        u8 pageEnd;                // 0x1600: 3 more pages follow
        u8 resumeState;            // 0x1601: after TimerWaiting or EventWaiting
        u8 pad_1602[0x6];
        SharedPtr<> msbt;          // 0x1608: the open message's file
        s32 label;                 // 0x1618: its label index
        u8 pad_161c[0x4];
        s32 page;                  // 0x1620
        u8 pad_1624[0x4];
        MessageString<0x180> pageText;  // 0x1628
        MessageString<0x40> choiceText[4];  // 0x1930
        u32 pageLineBreaks;        // 0x1b50: the current page's; not read
        u8 result;                 // 0x1b54: the chosen option + 1, 0xff none
        u8 pad_1b55[0x3];
        SharedPtr<> handle;        // 0x1b58: the opener's request handle
        u8 kind;                   // 0x1b68: MessageWindow_KindNames' index, 0x14 none (MessageWindow_SetKind)
        u8 pad_1b69;
        u16 pageLetters;           // 0x1b6a: letters in the current page
        u8 pad_1b6c[0x4];
        List pendingTags;          // 0x1b70: the page's control tags not yet reached
        float timer;               // 0x1b88: TimerWaiting
        u32 waitButtons[3];        // 0x1b8c: ButtonWaiting's
        TagProcessor tagProcessor; // 0x1b98
        u8 kindOfStyle[0x14];      // 0x1cb0: the window kind per message project style (MessageWindow_MapStyles)
        s32 choiceCount;           // 0x1cc4: 2 to 4
        s32 choiceCursor;          // 0x1cc8: 0 the first option, Down adds one
        s32 choiceDefault;         // 0x1ccc
        s32 choiceCancel;          // 0x1cd0: the option B picks, -1 none
        s32 fadeIn;                // 0x1cd4
        s32 fadeOut;               // 0x1cd8: set, the page ends in FadeOut
        u8 pad_1cdc[0x4];
        float balloonOffset[2];    // 0x1ce0
        WaitCounter followWait;    // 0x1ce8
        // After KeyWaiting or PreSelecting starts, keyWait.count runs up to its limit (6 for a page, 10 before a
        // choice); a button counts only once it is there.
        WaitCounter keyWait;       // 0x1cf0
        WaitCounter fadeWait;      // 0x1cf8: TextFadeOut
        float modelessPos[3];      // 0x1d00
        float modelessHalfSize[2]; // 0x1d0c
        s32 pageColor;             // 0x1d14: the colour the next page starts in, from a colour tag
        u8 decided;                // 0x1d18: A taken in Selecting
        u8 eventDone;              // 0x1d19
        u8 skipping;               // 0x1d1a: set by B: show pages whole, skip the text's pauses
        u8 follow;                 // 0x1d1b: the balloon follows a world point
        u8 closeAtEnd;             // 0x1d1c: the last page closes the window
        u8 pad_1d1d[0x3];
    };
    static_assert(offsetof(MessageWindow, inOutChoice) == 0x78 && offsetof(MessageWindow, talk) == 0xd8);
    static_assert(offsetof(MessageWindow, getItem) == 0x3a0 && offsetof(MessageWindow, modeless) == 0xb68);
    static_assert(offsetof(MessageWindow, text) == 0xe18 && offsetof(MessageWindow, choices) == 0xe20);
    static_assert(offsetof(MessageWindow, machine) == 0x1260 && offsetof(MessageWindow, msbt) == 0x1608);
    static_assert(offsetof(MessageWindow, pageText) == 0x1628 && offsetof(MessageWindow, choiceText) == 0x1930);
    static_assert(offsetof(MessageWindow, result) == 0x1b54 && offsetof(MessageWindow, pageLetters) == 0x1b6a);
    static_assert(offsetof(MessageWindow, pendingTags) == 0x1b70 && offsetof(MessageWindow, tagProcessor) == 0x1b98);
    static_assert(offsetof(MessageWindow, kindOfStyle) == 0x1cb0 && offsetof(MessageWindow, choiceCount) == 0x1cc4);
    static_assert(offsetof(MessageWindow, choiceCursor) == 0x1cc8 && offsetof(MessageWindow, pageColor) == 0x1d14);
    static_assert(offsetof(MessageWindow, keyWait) == 0x1cf0 && offsetof(MessageWindow, modelessPos) == 0x1d00);
    static_assert(offsetof(MessageWindow, skipping) == 0x1d1a && sizeof(MessageWindow) == 0x1d20);

    // The title screen (UI kTitleUi, Tl_Title.bflyt, Title_vtable): Title_Setup puts its machine in Deactive, where
    // it waits while the intro plays.
    struct Title {
        LayoutWrapper wrapper;
        u8 pad_78[0x8];
        StateMachine<6> machine;   // 0x80: Title_ChangeState; Deactive, LogoIn, BtnIn, WaitLR, GameStart, End
    };
    static_assert(offsetof(Title, machine) == 0x80);
    constexpr int kTitleUi = 0x1d;
    constexpr u8 kTitleDeactive = 0;
}
