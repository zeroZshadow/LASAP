// Archipelago bridge, the game side: Init, and Tick, which runs the bridge's parts once per input poll on the game
// thread; the player's state, the connection notices and DeathLink. Received items (ap_items.cpp), item-gets
// (ap_itemget.cpp, ap_held_model.cpp), checks and hints (ap_checks.cpp), the client's lines (ap_protocol.cpp) and the
// warp menu (ap_warp_menu.cpp) have files of their own.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_list.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_popup.hpp"
#include "ap/ap_text.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/hooks/level.hpp"
#include "game/menu.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "ghost/ghost_net.hpp"
#include "ghost/ghost_puppets.hpp"
#include "loggers.hpp"
#include "map/map_tracker.hpp"
#include "music/music.hpp"
#include "music/music_jukebox.hpp"
#ifdef LASAP_TEST
#include "test/test.hpp"
#endif

namespace AP::Bridge {
    std::string g_SeedHash = "00000000";
    std::string g_LastLevel;
    u64 g_LevelSince = 0;

namespace {
    using Game::NowMs;
    using Game::SharedPtr;
    using Game::StrView;

    // Time to wait after a level change before granting items.
    constexpr u64 kSettleMs = 1500;

    // A save is loaded and its received count reported (AP::Tick): the client sends ITEM only after a RECV, so a save
    // loaded from the title, an older one or another file, gets the items it lacks.
    bool g_SaveRecvReported = false;
    // DeathLink (DESIGN.md, DeathLink): when the last received death was applied, 0 when none is pending
    u64 g_DeathLinkAt = 0;
    u64 g_DiedPolled = 0;
    constexpr u64 kDiedPollMs = 100;
    constexpr u64 kDeathLinkClearMs = 10000;     // a received death that reached no game over by then is forgotten
    // The game over menu showed: Retry stands Link up with full health for about a second, then reloads the level
    // (a new player) under the same name. Nothing that needs the player starts until the reload.
    bool g_GameOver = false;

    // Connection notices: in a level this long without the client or the server, say so, then again this often while
    // it lasts.
    constexpr u64 kOfflineNoticeMs = 20000;
    constexpr u64 kOfflineRepeatMs = 300000;
    enum class Link { Unknown, Online, NoClient, NoServer };
    Link g_Link = Link::Unknown;
    u64 g_NextOfflineNotice = 0;
    bool g_WasConnected = false;
    bool g_ServerConnected = false;  // the client is logged into the slot (SERVER lines)

    // Tells the player when the client goes away, and while the client or the server is not connected.
    void TickConnection() {
        bool client = AP::IsClientConnected();
        if (g_WasConnected && !client) QueueNotice("Archipelago client|disconnected");
        g_WasConnected = client;
        if (!client) g_ServerConnected = false;
        Link link = !client ? Link::NoClient : g_ServerConnected ? Link::Online : Link::NoServer;
        u64 now = NowMs();
        if (link != g_Link) {
            // A dropped connection was just announced (here, or by the client's NOTICE): remind later.
            g_NextOfflineNotice = now + (g_Link == Link::Online ? kOfflineRepeatMs : kOfflineNoticeMs);
            g_Link = link;
        }
        if (link == Link::Online || now < g_NextOfflineNotice || !PlayerFree() || g_LevelSince == 0 ||
            now - g_LevelSince < kOfflineNoticeMs) {
            return;
        }
        g_NextOfflineNotice = now + kOfflineRepeatMs;
        QueueNotice(link == Link::NoClient ? "Archipelago client|not connected" : "Archipelago server|not connected");
    }
}

    // The intro, the title or the file menus show: no save is loaded.
    bool NoSaveLoaded(Game::Framework* fmwk) {
        Game::Scene scene = Game::SceneOf(fmwk);
        return fmwk == nullptr || scene == Game::Scene::Title || scene == Game::Scene::FileMenus;
    }

    bool PlayerReady() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr && fmwk->player != nullptr && !g_GameOver && NowMs() - g_LevelSince >= kSettleMs;
    }

    // True when the player can move: no talk, event, cutscene or pause menu.
    bool PlayerFree() {
        Game::Framework* fmwk = Game::GetFramework();
        if (!PlayerReady()) return false;
        return fmwk->paused == 0 && fmwk->player->machine.state == 0;
    }

    // An item-get takes what Link holds over his head out of the game, so item-gets wait until he puts it down.
    bool PlayerCarrying() {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->player == nullptr) return false;
        return fmwk->player->carried != nullptr;
    }

    // Health is zero: Link is dying (until the Secret Medicine or a game over's Retry refills it).
    bool LinkDead() {
        auto* items = Game::Data::Inventory::Items();
        return items != nullptr && items->Health <= 0;
    }

    // Starting an event of Common.bfevfl, as the game does for its game over:
    // load the flowchart resource, make an event for the entry, prepare it and hand it to the event manager.
    bool RequestCommonEvent(const char* entry) {
        void* resources = *Game::ResourceManager_ptr;
        void* events = *Game::EventManager_ptr;
        if (resources == nullptr || events == nullptr) return false;
        static const char kPath[] = "event/Common.bfevfl";
        StrView path{kPath, sizeof(kPath) - 1};
        StrView name{entry};
        SharedPtr handle{}, flowchart{}, event{}, request{};
        Game::Resource_Load(&handle, resources, &path);
        if (handle.ptr != nullptr) Game::Resource_Wait(handle.ptr);
        Game::Resource_TakeFlowchart(&flowchart, &handle);
        bool ok = false;
        if (flowchart.ptr != nullptr) {
            Game::Event_Make(&event, &flowchart, &name);
            if (event.ptr != nullptr) {
                Game::Event_Prepare(event.ptr);
                Game::EventManager_Request(&request, events, &event);
                ok = true;
            }
        }
        Game::Release(request);
        Game::Release(event);
        Game::Release(flowchart);
        Game::Release(handle);
        return ok;
    }

    // A toast in the autosave popup; '|' starts a new line.
    void QueueNotice(std::string text) {
        for (auto& c : text) if (c == '|') c = '\n';
        AP::QueueToast(text);
    }

    void SetServerConnected(bool connected) { g_ServerConnected = connected; }

    // A death from another player. Without a save (intro, title, file menus) or with Link dead already it is dropped;
    // otherwise it waits until Link is free (no event, message or pause menu) and no item display is starting or
    // showing.
    bool ReceiveDeath(const std::string& source) {
        Game::Framework* fmwk = Game::GetFramework();
        auto* items = Game::Data::Inventory::Items();
        if (NoSaveLoaded(fmwk)) {
            Logging.Log("[AP] DeathLink dropped: no save loaded");
            AP::Send("DEATHDROPPED");
            return true;
        }
        if (fmwk->player == nullptr || items == nullptr) return false;
        if (items->Health <= 0) return true;
        if (!PlayerFree() || !ItemEventIdle()) return false;
        // The game over flow runs as for any death (the Secret Medicine saves Link) and reports none (patcher).
        EventFlags::SetFlag(AP::DEATHLINK_FLAG, true);
        g_DeathLinkAt = NowMs();
        items->Health = 0;
        QueueNotice(source.empty() ? "DeathLink" : "DeathLink|from " + source);
        Logging.Log("[AP] DeathLink applied");
        return true;
    }

#ifdef LASAP_TEST
    std::string ScreenName(Game::Framework* fmwk) {
        if (fmwk == nullptr) return "none";
        // The intro plays while the title screen's UI is not there yet or waits in Deactive.
        auto* title = reinterpret_cast<Game::Ui::Title*>(Game::Ui::FindUi(Game::Ui::kTitleUi));
        switch (Game::SceneOf(fmwk)) {
            case Game::Scene::Title:
                return title == nullptr || title->machine.state == Game::Ui::kTitleDeactive ||
                               title->machine.state == 0xff
                           ? "intro"
                           : "title";
            case Game::Scene::FileMenus: return "menu";
            case Game::Scene::Level: return fmwk->player != nullptr ? "level" : "loading";
            case Game::Scene::Other: return "other";
            default: return "none";
        }
    }
#endif
}

namespace AP {
    using namespace Bridge;

    void Init(const std::string& seed_hash, int port) {
        if (!seed_hash.empty()) g_SeedHash = seed_hash;
        InstallTextHooks();
        Puppets::InstallHooks();
        Music::InstallHooks();
        InstallItemGetHooks();
        InstallPickupEventHooks();
        InstallItemPoseHooks();
        InstallHeldModelHooks();
        Popup::InstallHooks();
        LoadLocationNames();
        MapTracker::Load();
        LoadIndices();
        LoadKept();
        LoadHints();
        ListUi::InstallHooks();
        JukeboxPane::InstallHooks();
#ifdef LASAP_TEST
        Test::InstallHooks();
#endif
        StartNetwork(port > 0 ? port : PORT);
    }

    std::string HelloLine() {
        return "HELLO " + std::to_string(PROTOCOL_VERSION) + " " + g_SeedHash;
    }

    void Log(const std::string& text) {
        Logging.Log("[AP] %s", text.c_str());
        Send("LOG " + text);
    }

    void OnPlaceholderCollected(int local_id) {
        g_FoundSinceLoad.insert(local_id);
        SetFoundLocation(local_id);
        NoteChestFind(local_id);
        RecordCheck(local_id);
    }

    void OnGoal() {
        Logging.Log("[AP] goal reached");
        Send("GOAL");
    }

    void OnSignRead() {
        if (EventFlags::CheckFlag(AP::SIGN_FLAG)) return;
        EventFlags::SetFlag(AP::SIGN_FLAG, true);
        Logging.Log("[AP] sign read");
        Send("SIGN");
    }

    bool IsServerConnected() {
        return g_ServerConnected;
    }

    void Tick() {
        std::string level = LevelSystem::GetCurrentLevel();
        if (level != g_LastLevel) {
            g_LastLevel = level;
            g_LevelSince = NowMs();
            Send("LOG level " + level);  // test scripts wait on these
            if (level == kGoalLevel) OnGoal();
        }

        // A level (re)loads while there is no player: the settle time starts once he stands in it. The game over
        // menu holds everything back until its Retry has reloaded the level.
        Game::Framework* fmwk = Game::GetFramework();
        if (NoSaveLoaded(fmwk)) {
            g_SaveRecvReported = false;
            g_FoundSinceLoad.clear();
        } else if (!g_SaveRecvReported && PlayerReady()) {
            g_SaveRecvReported = true;
            Send("RECV " + std::to_string(GetRecvCount()));
        }
        if (fmwk == nullptr || fmwk->player == nullptr) {
            g_LevelSince = NowMs();
            g_GameOver = false;
        } else if (LinkDead() && fmwk->paused != 0) {
            g_GameOver = true;
        }

        // DeathLink: the game over flow sets APDied for a death that no Secret Medicine saved and no received death
        // caused (patcher).
        if (fmwk != nullptr && fmwk->player != nullptr && NowMs() - g_DiedPolled >= kDiedPollMs) {
            g_DiedPolled = NowMs();
            if (EventFlags::CheckFlag(AP::DIED_FLAG)) {
                EventFlags::SetFlag(AP::DIED_FLAG, false);
                Send("DIED");
            }
            auto* items = Game::Data::Inventory::Items();
            if (g_DeathLinkAt != 0 && NowMs() - g_DeathLinkAt > kDeathLinkClearMs && items != nullptr &&
                items->Health > 0) {
                g_DeathLinkAt = 0;
                EventFlags::SetFlag(AP::DEATHLINK_FLAG, false);
            }
        }


        if (TakeNewConnection()) {
            SendAllChecks();
            Send("LOG level " + g_LastLevel);  // the client passes it on to trackers
            ResetHints();  // sent again once the save is loaded
            GhostNet::Reset();
        }

        TickConnection();
        GhostNet::Tick();
        Puppets::Tick();
        TickKeptFlags();
        TickHintFlags();
        AP::TickToasts(PlayerReady());
        TickItemGet();
        TickHeldModel();
        Popup::Tick();
        TickWarpMenu();
        TickMedicine();
        // the d-pad changes the music in free play, not where a minigame may steer with it
        JukeboxPane::Tick(PlayerFree() && g_LastLevel != kCraneGameLevel && g_LastLevel != "FishingPond");
        MapTracker::Tick();
        Music::Tick(PlayerFree());
#ifdef LASAP_TEST
        Test::Tick();
#endif
        PumpLines();
    }

    void ApplyInjectedInput(nn::hid::NpadBaseState* state) {
#ifdef LASAP_TEST
        Test::ApplyInput(state);
#endif
        ListUi::OnPad(state->mButtons._storage[0]);
        if (MapTracker::ListTakesPad(state)) {
            // A Minus held as the list opened is let go: not a tap once released, nor the warp menu.
            WarpMenuDropMinus();
            return;
        }
        WarpMenuPad(state->mButtons._storage[0]);
    }
}
