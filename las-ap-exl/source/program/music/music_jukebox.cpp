// Jukebox page (DESIGN.md "Jukebox page"): the pause menu's fourth page, after System, as a mode of
// index 2, System's slot. The header (Navi) keeps its three pages; while g_Jukebox is set, the Gm_System page it
// builds for index 2 hides its content and shows the Jukebox instead, built from the game's own layouts:
// - the track list: Gm_MemoryList (the Memories list, ap_list.cpp), right of the middle;
// - "Now playing": Gm_MemoryCollect (the Memories count card) on the left, with a note (the font's U+266A) in its
//   paper window and notes floating up out of it while music plays;
// - the name and description of the track under the cursor: the Items page's info texts (Gm_Item N_Info_00);
// - the guide "X Shuffle, Y Repeat one, A Play, B Back" (A Stop on the chosen track: the Jukebox off, the game's own
//   music again): the map's guide with its captions swapped (ap_list.cpp, ap_text.cpp);
// - the playback state under the card: Shuffle and Repeat one, on or off, and the d-pad's use in the game.
// The module steers the moves that differ from the game's: R on System and L on the Jukebox change index 2 to
// index 2 (System is removed and built again), L on the map lands on the Jukebox. Not in the Chamber Dungeon,
// whose third page is the dungeon's own.
#include "music/music_jukebox.hpp"
#include "music/music.hpp"
#include "ap/ap.hpp"
#include "ap/ap_list.hpp"
#include "ap/ap_text.hpp"
#include "game/menu.hpp"
#include "game/sym.hpp"
#include "game/ui.hpp"
#include "ghost/ghost_net.hpp"
#include "loggers.hpp"
#include "music/music_tracks.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    using namespace AP::ListUi;
    using namespace Game::Ui;
    using AP::ListUi::Layout;

    // Navi, the pause menu header (Game::Ui::Navi): its L/R (lr) and pages by index.
    constexpr u8 kLeft = 1, kRight = 2;
    constexpr int kMap = 0, kSystem = 2;
    constexpr u8 kChamberDungeon = 8;  // the level index (Game::LevelIndex) of the Chamber Dungeon (third page id 0xb)

    // Gm_System (Game::Ui::GmSystem): setup (vtable +0x20), update (+0x28), the destructor its deleting destructor
    // calls (GmSystem_Dtor).
    constexpr const char* kSystemContent = "N_SystemChoice_00";

    // The track list (Gm_MemoryList), right of the middle.
    constexpr float kListX = 250, kListY = 40;
    constexpr u8 kHighlight = 96;  // W_Add alpha of the row animation On: the track that plays

    // Gm_MemoryCollect: T_Collect_00 (two lines, 33 characters) over the paper window W_window_00 (y -46), which holds
    // A_Collect_00 (x 10): the icon P_pict_00 (x -86, hidden here) and T_CollectNum_00 / T_CollectMaxNum_00. The text
    // moves up from y 54, leaving room for the notes that float up out of the window.
    constexpr const char* kCardLayout = "Gm_MemoryCollect.bflyt";
    constexpr float kCardX = -300, kCardY = 36, kCardTextY = 150;
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

    // The playback state under the card: a second Gm_MemoryCollect with only its T_Collect_00 (centred, two lines),
    // smaller: Shuffle and Repeat one, on or off, and the d-pad's use in the game. The text box gets room for
    // kStatusChars UTF-16 units.
    constexpr u16 kStatusChars = 80;
    constexpr float kStatusY = kCardY - 46 - 49 - 40, kStatusScale = 0.72f;

    // Buttons (nn::hid): the d-pad's left, up and right alone.
    constexpr u64 kDpadLeft = 0x1000, kDpadUp = 0x2000, kDpadRight = 0x4000;

    constexpr int kEntries = AP::Tracks::COUNT;

    bool InChamberDungeon() { return *Game::LevelIndex == kChamberDungeon; }

    // The header and the mode
    bool g_Jukebox = false;         // index 2 shows the Jukebox
    bool g_Pending = false;         // the value g_Jukebox takes once the old page is removed
    bool g_ChangeAhead = false;     // a page change runs: g_Pending applies when CloseOldUi ends
    bool g_GuideShown = false;      // the guide shows the Jukebox's captions
    bool g_GuideStop = false;       // its A caption reads Stop (the cursor was on the chosen track)
    bool g_Loud = false;            // a track was chosen on the page: it plays at full volume while the menu is open
    int g_Changes = 0;              // changes through index 2 the module steered (2 to 2)

    struct Page {
        GmSystem* page = nullptr;       // the Gm_System object that shows the Jukebox
        List list;
        Layout card, info, icon, mark, floating[kFloating], status;
        Text title, number, slash, total, name, about;
    };
    Page g_Ui;
    int g_Built = 0, g_Dropped = 0; // pages built and dropped
    std::string g_Shown;            // the chosen track the page shows
    u32 g_Frame = 0;
    int g_Fade = 0;                 // 0..255, the alpha the page's layouts draw with
    u64 g_GamePad = 0;              // the buttons at the last tick, for the d-pad in the game

    Navi* FindNavi() { return reinterpret_cast<Navi*>(FindUi(kNaviUi)); }

    void SetLabel(TextHolder* text, const char* label) {
        Game::StrView view{label};
        Game::MessageText_SetLabel(text, &view);
    }

    void SetHeaderText(TextHolder* holder, const std::string& text) {
        AP::SetPaneText(holder, text);
        QueueGlyphs(holder->box);
    }

    // 0 at 0, 1 at half, 0 at 1, repeating
    float Triangle(float x) {
        float f = x - static_cast<float>(static_cast<int>(x));
        return 1.0f - (f < 0.5f ? 1.0f - 2.0f * f : 2.0f * f - 1.0f);
    }

    const AP::Tracks::Track& Entry(int entry) { return AP::Tracks::TRACKS[entry]; }

    // The entry of a track id, -1 for "" or a track not in the list.
    int EntryOf(const std::string& id) {
        for (int i = 0; i < AP::Tracks::COUNT; i++) {
            if (id == AP::Tracks::TRACKS[i].id) return i;
        }
        return -1;
    }

    // The cursor is on the chosen track: A stops the Jukebox.
    bool OnChosen() { return g_Ui.page != nullptr && AP::Music::IsChosen(Entry(g_Ui.list.at).id); }

    void DropPage() {
        if (g_Ui.page == nullptr) return;
        for (Text* text : {&g_Ui.title, &g_Ui.number, &g_Ui.slash, &g_Ui.total, &g_Ui.name, &g_Ui.about}) {
            Unqueue(text->box);
        }
        for (auto& note : g_Ui.floating) Drop(note);
        Drop(g_Ui.status);
        Drop(g_Ui.mark);
        Drop(g_Ui.icon);
        Drop(g_Ui.info);
        Drop(g_Ui.card);
        DropList(g_Ui.list);
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
        List& list = g_Ui.list;
        for (int r = 0; r < kRows; r++) {
            Row& row = list.rows[r];
            int entry = list.top + r;
            if (entry >= kEntries) continue;
            SetText(row.text, Entry(entry).name);
            SetAlpha(row.add0, entry == chosenEntry ? kHighlight : 0);
            SetAlpha(row.add1, entry == chosenEntry ? kHighlight : 0);
        }
        Place(list);
        bool marked = chosenEntry >= 0 && chosenEntry >= list.top && chosenEntry < list.top + kRows;
        SetVisible(g_Ui.mark.root, marked);
        if (marked) MoveTo(g_Ui.mark.root, kMarkX, kListY + kRowTop - kRowStep * (chosenEntry - list.top));

        bool silent = chosen.empty() && AP::Music::ZoneTrack().empty();
        const char* playingName = playing >= 0 ? Entry(playing).name : silent ? "Silence" : "The game's music";
        SetText(g_Ui.title, std::string(chosen.empty() ? "Jukebox off\n" : "Now playing\n") + playingName);
        SetText(g_Ui.number, playing >= 0 ? std::to_string(playing + 1) : "-");
        SetText(g_Ui.total, std::to_string(AP::Tracks::COUNT));
        SetText(g_Ui.name, Entry(list.at).name);
        SetText(g_Ui.about, Entry(list.at).info);

        SetText(g_Ui.status.note, std::string(AP::Music::Shuffle() ? "Shuffle: On" : "Shuffle: Off") +
                                      (AP::Music::RepeatOne() ? "    Repeat one: On" : "    Repeat one: Off") +
                                      "\nIn game: \xe2\x86\x90 \xe2\x86\x92 change track");
    }

    // Once a frame: the page fades in, and out while the header leaves it; the note bobs; notes float up while
    // music plays.
    void Animate() {
        g_Frame++;
        Navi* header = FindNavi();
        u8 navi = header != nullptr ? header->machine.state : 0;
        bool leaving = navi == kNaviCloseOldUi || navi == kNaviClose;
        g_Fade = std::clamp(g_Fade + (leaving ? -kFadeStep : kFadeStep), 0, 255);
        for (Layout* layout : {&g_Ui.list.layout, &g_Ui.card, &g_Ui.info, &g_Ui.icon, &g_Ui.mark, &g_Ui.status}) {
            SetAlpha(layout->root, g_Fade);
        }

        MoveTo(g_Ui.icon.root, kIconX, kIconY + 6.0f * Triangle(static_cast<float>(g_Frame) / kBobFrames));
        // a two-line description lower, every frame: the layout's first update lays its alignment pane out again
        float lower = strchr(Entry(g_Ui.list.at).info, '\n') != nullptr ? kInfoLower : 0;
        if (g_Ui.about.box != nullptr) MoveTo(&g_Ui.about.box->pane, 0, kInfoY - lower);
        bool music = AP::Music::HasMusic();
        for (int i = 0; i < kFloating; i++) {
            Pane* note = g_Ui.floating[i].root;
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
    void BuildPage(GmSystem* page) {
        LayoutWrapper* wrapper = &page->page.wrapper;
        Pane* pageRoot = RootOf(wrapper);
        if (pageRoot == nullptr) return;
        SetVisible(FindPane(pageRoot, kSystemContent), false);
        g_Ui.page = page;
        g_Built++;
        bool ok = Build(g_Ui.info, kInfoLayout, wrapper, pageRoot) && Build(g_Ui.card, kCardLayout, wrapper, pageRoot) &&
                  BuildList(g_Ui.list, wrapper, pageRoot, kListX, kListY) &&
                  Build(g_Ui.icon, kNoteLayout, wrapper, pageRoot) && Build(g_Ui.mark, kNoteLayout, wrapper, pageRoot);
        for (auto& note : g_Ui.floating) ok = ok && Build(note, kNoteLayout, wrapper, pageRoot);
        ok = ok && Build(g_Ui.status, kCardLayout, wrapper, pageRoot);
        if (!ok) {
            // the page stays System, whole: its choices are not left working unseen
            DropPage();
            SetVisible(FindPane(pageRoot, kSystemContent), true);
            g_Jukebox = false;
            return;
        }

        // the card, its icon replaced by the note
        Pane* card = g_Ui.card.root;
        MoveTo(FindPane(card, "N_Collect_00"), kCardX, kCardY);
        MoveTo(FindPane(card, "T_Collect_00"), 0, kCardTextY);
        SetVisible(FindPane(card, "P_pict_00"), false);
        SetAlpha(FindPane(card, "W_window_00"), kCardPaper);
        Bind(g_Ui.title, FindText(card, "T_Collect_00"));
        Bind(g_Ui.number, FindText(card, "T_CollectNum_00"));
        Bind(g_Ui.slash, FindText(card, "T_CollectSlash_00"));
        Bind(g_Ui.total, FindText(card, "T_CollectMaxNum_00"));
        SetText(g_Ui.slash, "/");

        // the notes
        for (Layout* note : {&g_Ui.icon, &g_Ui.mark, &g_Ui.floating[0], &g_Ui.floating[1], &g_Ui.floating[2]}) {
            Bind(note->note, FindText(note->root, "T_Operation_00"));
            SetText(note->note, kNote);
        }
        ScaleTo(g_Ui.icon.root, kIconScale);
        ScaleTo(g_Ui.mark.root, kMarkScale);

        // the playback state, with room for its text (left empty if there is none)
        Pane* status = g_Ui.status.root;
        SetVisible(FindPane(status, "W_window_00"), false);
        SetVisible(FindPane(status, "A_Collect_00"), false);
        MoveTo(FindPane(status, "N_Collect_00"), kCardX, kStatusY);
        TextBox* statusText = FindText(status, "T_Collect_00");
        if (statusText != nullptr) {
            MoveTo(&statusText->pane, 0, 0);
            ScaleTo(&statusText->pane, kStatusScale);
        }
        if (Allocate(statusText, kStatusChars)) Bind(g_Ui.status.note, statusText);

        // the info texts
        Pane* info = g_Ui.info.root;
        for (const char* hidden : kInfoHidden) SetVisible(FindPane(info, hidden), false);
        SetAlpha(FindPane(info, "N_Item_00"), 255);
        Bind(g_Ui.name, FindText(info, "T_Name_00"));
        Bind(g_Ui.about, FindText(info, "T_Info_00"));

        // the cursor on the chosen track (else the zone's own), a few rows from the top
        int at = EntryOf(AP::Music::Track());
        if (at < 0) at = std::max(EntryOf(AP::Music::ZoneTrack()), 0);
        SetEntries(g_Ui.list, kEntries, at);
        g_Fade = 0;
        Fill();
        Animate();
        Logging.Log("[AP] jukebox: page built");
    }

    // The page's buttons, once a frame while it takes them: Up/Down (repeating while held), A, X, Y.
    void Input() {
        bool moved;
        u64 pressed = AP::ListUi::Input(g_Ui.list, moved);
        if (moved) Fill();
        if (pressed & kButtonA) {
            AP::Music::Choose(OnChosen() ? "" : Entry(g_Ui.list.at).id);  // A on the chosen track: the Jukebox off
            g_Loud = true;
            PlaySound("SE_SY_DECIDE");
            Fill();
        }
        if (pressed & (kButtonX | kButtonY)) {
            if (pressed & kButtonX) AP::Music::SetShuffle(!AP::Music::Shuffle());
            if (pressed & kButtonY) AP::Music::SetRepeatOne(!AP::Music::RepeatOne());
            PlaySound("SE_SY_DECIDE");
            Fill();
        }
    }

    // What Navi_WaitKey asks before it turns the page: no dialog open (UIs 0x1f, 0x20, 0x26) and the page's +0xe0 set.
    bool MayTurn(Navi* navi) {
        auto* page = static_cast<MenuPage*>(navi->pages[navi->target].ptr);
        if (page == nullptr || page->canClose == 0) return false;
        return FindUi(0x20) == nullptr && FindUi(0x1f) == nullptr && FindUi(0x26) == nullptr;
    }

    // A page change the header would not make: index 2 to index 2.
    void ChangeInPlace(Navi* navi, bool jukebox, u8 direction) {
        int index = navi->target;
        auto* old = static_cast<MenuPage*>(navi->pages[index].ptr);
        if (old != nullptr) old->wrapper.active = 0;
        navi->lr = 0;
        navi->direction = direction == kRight;
        navi->current = index;  // as Navi_WaitKey would have, on the page's first frame
        g_Pending = jukebox;
        g_ChangeAhead = true;
        g_Changes++;
        Game::Navi_ChangeState(&navi->machine, kNaviCloseOldUi);
        PlaySound("SE_SY_MENU_LR_MOVE");
    }

    // The Jukebox's guide is the overworld map's (X, Y, R, A, B) with A and B moved up over R and the last slot
    // hidden, its captions read as Shuffle, Repeat one and Play (or Stop).
    const Guide* JukeboxGuide(u32 index) {
        static Guide guide;
        g_GuideShown = index == kSystem && g_Jukebox && !InChamberDungeon();
        if (!g_GuideShown) return nullptr;
        g_GuideStop = OnChosen();
        guide = Guide{4, {kGuideX, kGuideY, kGuideA, kGuideB}, {"Shuffle", "Repeat one", g_GuideStop ? "Stop" : "Play"}};
        return &guide;
    }
}

HOOK_DEFINE_TRAMPOLINE(NaviWaitKey) {
    static void Callback(Navi* navi) {
        if (InChamberDungeon()) return Orig(navi);
        // the page shown: the target, which the current index still trails on the page's first frame (an L or R
        // pressed while the page opened turns it there)
        int index = navi->target;
        u8 lr = navi->lr;
        bool inPlace = index == kSystem && ((lr == kRight && !g_Jukebox) || (lr == kLeft && g_Jukebox));
        if (inPlace && MayTurn(navi)) return ChangeInPlace(navi, lr == kRight, lr);
        Orig(navi);  // refuses a turn in place that MayTurn refused
        if (navi->machine.state == kNaviCloseOldUi && !g_ChangeAhead) {
            // the game's own change: the Jukebox comes after the map going left, and is left any other way
            g_Pending = index == kMap && lr == kLeft && navi->target == kSystem;
            g_ChangeAhead = true;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(NaviCloseOldUiUpdate) {
    static void Callback(Navi* navi) {
        Orig(navi);
        if (g_ChangeAhead && navi->machine.state == kNaviSelected) {
            g_Jukebox = g_Pending;
            g_ChangeAhead = false;
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(NaviCloseEnter) {
    static void Callback(Navi* navi) {
        Orig(navi);
        // the header's names queued without an owner go before the header does
        for (TextHolder* text : {&navi->title, &navi->back, &navi->next}) Unqueue(text->box);
        g_Jukebox = false;
        g_Pending = false;
        g_ChangeAhead = false;
        g_Loud = false;
    }
};

HOOK_DEFINE_TRAMPOLINE(NaviSetTitles) {
    static void Callback(Navi* navi, u32 index) {
        Orig(navi, index);
        if (InChamberDungeon()) return;
        if (index == kSystem && g_Jukebox) {
            SetHeaderText(&navi->title, "Jukebox");
            SetLabel(&navi->back, "UI:SubScreenTitleSystem");
            SetLabel(&navi->next, "UI:SubScreenTitleMap");
        } else if (index == kSystem) {
            SetHeaderText(&navi->next, "Jukebox");
        } else if (index == kMap) {
            SetHeaderText(&navi->back, "Jukebox");
        }
    }
};

HOOK_DEFINE_TRAMPOLINE(SystemSetup) {
    static void Callback(GmSystem* page) {
        Orig(page);
        if (!g_Jukebox) return;
        DropPage();
        BuildPage(page);
    }
};

HOOK_DEFINE_TRAMPOLINE(SystemUpdate) {
    static void Callback(GmSystem* page) {
        if (page != g_Ui.page) return Orig(page);
        Animate();
        // AP::Tick does not run while the menu is open: a choice applies here, and the track chosen on the page plays
        // at full volume
        AP::Music::Tick();
        if (g_Loud) AP::Music::Loud();
        if (!AP::Music::IsChosen(g_Shown.c_str())) Fill();  // the choice changed (a test command)
        if (g_GuideShown && OnChosen() != g_GuideStop) RefreshGuide(kSystem);  // A reads Play or Stop
        // Gm_System's own update runs while it opens (its In animation), never once it takes buttons
        if (page->machine.state == 0 || page->machine.state == 0xff) return Orig(page);
        page->page.canClose = 1;
        Input();
    }
};

HOOK_DEFINE_TRAMPOLINE(PageDtor) {
    static void Callback(GmSystem* page) {
        if (page == g_Ui.page) DropPage();
        Orig(page);
    }
};

namespace AP::JukeboxPane {
    void InstallHooks() {
        NaviWaitKey::InstallAtFuncPtr(Game::Navi_WaitKey.Ptr());
        NaviCloseOldUiUpdate::InstallAtFuncPtr(Game::Navi_CloseOldUiUpdate.Ptr());
        NaviCloseEnter::InstallAtFuncPtr(Game::Navi_CloseEnter.Ptr());
        NaviSetTitles::InstallAtFuncPtr(Game::Navi_SetTitles.Ptr());
        SystemSetup::InstallAtFuncPtr(Game::GmSystem_Setup.Ptr());
        SystemUpdate::InstallAtFuncPtr(Game::GmSystem_Update.Ptr());
        PageDtor::InstallAtFuncPtr(Game::GmSystem_Dtor.Ptr());
        AddGuideSource(JukeboxGuide);
    }

    void Tick(bool free) {
        u64 pad = PadHeld(), pressed = pad & ~g_GamePad;
        g_GamePad = pad;
        if (!free) return;
        if (pressed & kDpadRight) {
            AP::Music::Skip(1);
        } else if (pressed & kDpadLeft) {
            AP::Music::Skip(-1);
        } else if (pressed & kDpadUp) {
            AP::GhostNet::ToggleNames();
        }
    }

#ifdef LASAP_TEST
    // STATE's " pane=<0-3, - with no header> navi=<header state> jbox=<cursor>/<top>", while the game is paused.
    std::string StateField(bool paused) {
        std::string s = " pane=";
        Navi* navi = paused ? FindNavi() : nullptr;
        if (navi == nullptr) return s + "- navi=- jbox=-";
        int index = navi->current;
        s += std::to_string(index == kSystem && g_Jukebox ? 3 : index);
        s += " navi=" + std::to_string(navi->machine.state);
        s += " jbox=";
        s += g_Ui.page != nullptr ? std::to_string(g_Ui.list.at) + "/" + std::to_string(g_Ui.list.top) : std::string("-");
        return s;
    }

    std::string NaviState() {
        Navi* navi = FindNavi();
        return navi != nullptr ? std::to_string(navi->machine.state) : "-";
    }

    // DBG jukebox
    std::string Info() {
        std::string s = "JUKEBOX mode=" + std::to_string(g_Jukebox) + " pending=" + std::to_string(g_Pending) +
                        " ahead=" + std::to_string(g_ChangeAhead) + " changes=" + std::to_string(g_Changes) +
                        " built=" + std::to_string(g_Built) + " dropped=" + std::to_string(g_Dropped);
        char buf[64];
        snprintf(buf, sizeof(buf), " page=%lx list=%lx", reinterpret_cast<uintptr_t>(g_Ui.page),
                 reinterpret_cast<uintptr_t>(g_Ui.list.layout.wrapper));
        s += buf;
        if (g_Ui.page != nullptr) s += " sysstate=" + std::to_string(g_Ui.page->machine.state);
        int texts = 0, glyphs = 0;
        auto count = [&](const Text& text) {
            if (text.box == nullptr) return;
            texts++;
            glyphs += Drawn(text);
        };
        for (auto& row : g_Ui.list.rows) count(row.text);
        s += " rows=" + std::to_string(texts) + " drawn=" + std::to_string(glyphs);
        texts = glyphs = 0;
        for (const Text* text : {&g_Ui.title, &g_Ui.number, &g_Ui.slash, &g_Ui.total, &g_Ui.name, &g_Ui.about}) {
            count(*text);
        }
        s += " texts=" + std::to_string(texts) + " textsdrawn=" + std::to_string(glyphs);
        s += " fade=" + std::to_string(g_Fade);
        s += " shuffle=" + std::to_string(AP::Music::Shuffle()) + " repeat=" + std::to_string(AP::Music::RepeatOne());
        s += std::string(" guide=") + (g_GuideStop ? "stop" : "play");
        if (g_Ui.page != nullptr) s += " name=" + std::string(Entry(g_Ui.list.at).name);
        for (char& c : s) c = c == '\n' ? '|' : c;
        return s;
    }
#endif
}
