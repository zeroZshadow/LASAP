// Game-thread side of the AP bridge: applies received items, reports collected locations,
// and runs test-only debug commands. Tick() runs once per input poll on the game thread.
#include "AP/ap.hpp"
#include "AP/ap_debug.hpp"
#include "AP/ap_list.hpp"
#include "Config/config.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Hooks/Common/inventory.hpp"
#include "Hooks/Common/level.hpp"
#include "Game/framework.hpp"
#include "Game/math.hpp"
#include "Game/model.hpp"
#include "Game/sym.hpp"
#include "Game/time.hpp"
#include "Game/Data/inventory.hpp"
#include "nn/fs.hpp"
#include "Config/file_handler.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <deque>
#include <malloc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <vector>

namespace {
    constexpr int kRecvBits = 16;
    constexpr size_t kChecksBytes = 65536 / 8;
    // Bits for received heart pieces and seashells (the game has 32 and 50), less those of the seed's vanilla sources
    // (AP::VanillaBits). Item actors ignore the inventory (randoFixes in patches.hpp), so receiving never hides one; the
    // game counts over all 64 bits.
    constexpr int kHeartPieceBits = 32;
    constexpr int kSeashellBits = 50;
    // Heart containers: Inventory_AddItemID takes indices 0-8 (scripts/re/ITEMS.md).
    constexpr int kHeartContainerBits = 9;
    // Chamber stones: received ones take the stones the Items page counts (PanelStone's mItemMenuCount: 3, 4, 8,
    // 10-13, 20-26), each a chamber panel Dampé makes at his next talk (scripts/re/ITEMS.md, Chamber stones).
    constexpr u64 kChamberStoneBits = 0x7f03d18;
    // Time to wait after a level change before granting items.
    constexpr u64 kSettleMs = 1500;

    using Game::NowMs;
    using Game::SharedPtr;
    using Game::StrView;

    std::string g_SeedHash = "00000000";
    std::string g_ChecksPath;
    std::vector<u8> g_Checks(kChecksBytes, 0);
    bool g_ChecksLoaded = false;
    // Locations the player marked as skipped on the map (map tracker), kept per seed next to the checks.
    std::string g_SkippedPath;
    std::vector<u8> g_Skipped(kChecksBytes, 0);
    // Locations the server has as checked (CHECKED lines), for the map tracker.
    std::vector<u8> g_ServerChecks(kChecksBytes, 0);
    // Locations in logic for the items received (INLOGIC lines), for the map tracker.
    std::vector<u8> g_InLogic(kChecksBytes, 0);
    bool g_HasLogic = false;

    std::deque<std::string> g_Deferred;

    void Defer(const std::string& line) {
        // The client resends SYNC and ITEM lines, so keep one copy of each.
        if (std::find(g_Deferred.begin(), g_Deferred.end(), line) != g_Deferred.end()) return;
        if (g_Deferred.size() >= 64) g_Deferred.pop_front();
        g_Deferred.push_back(line);
    }

    std::string g_LastLevel;
    // A save is loaded and its received count reported (AP::Tick): the client sends ITEM only after a RECV, so a save
    // loaded from the title, an older one or another file, gets the items it lacks.
    bool g_SaveRecvReported = false;
    // The placeholders collected since the save loaded: their own items were named by the found text.
    std::set<int> g_FoundSinceLoad;
    // DeathLink (DESIGN.md, DeathLink): when the last received death was applied, 0 when none is pending
    u64 g_DeathLinkAt = 0;
    u64 g_DiedPolled = 0;
    constexpr u64 kDiedPollMs = 100;
    constexpr u64 kDeathLinkClearMs = 10000;     // a received death that reached no game over by then is forgotten
    u64 g_LevelSince = 0;
    // The game over menu showed: Retry stands Link up with full health for about a second, then reloads the level
    // (a new player) under the same name. Nothing that needs the player starts until the reload.
    bool g_GameOver = false;

    // Holding Minus opens the warp menu; a short press still reaches the game (the map), sent when Minus is released.
    constexpr u64 kButtonMinus = 0x800;
    constexpr u64 kWarpMenuHoldMs = 1000;
    constexpr u64 kMinusReleasedMs = 60;   // no poll has seen Minus this long: it was released
    constexpr u64 kMinusTapMs = 120;       // how long the short press is passed on
    bool g_MinusHeld = false;
    bool g_MinusLong = false;              // held long enough for the menu
    bool g_WarpMenuWanted = false;         // the game thread opens the menu if it can
    bool g_WarpMenuShown = false;          // it did: releasing Minus sends nothing
    u64 g_MinusDownSince = 0;
    u64 g_MinusLastSeen = 0;
    u64 g_MinusTapUntil = 0;

    std::vector<std::string> Split(const std::string& line) {
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string p;
        while (ss >> p) parts.push_back(p);
        return parts;
    }

    int ItemIdFromKey(const std::string& key) {
        for (const auto& [id, name] : Inventory::kItemNames) {
            if (name == key) return id;
        }
        return -1;
    }

    bool PlayerReady() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr && fmwk->player != nullptr && !g_GameOver && NowMs() - g_LevelSince >= kSettleMs;
    }

    int GetRecvCount() {
        int n = 0;
        for (int i = 0; i < kRecvBits; i++) {
            if (EventFlags::CheckFlag("APRecv" + std::to_string(i))) n |= (1 << i);
        }
        return n;
    }

    void SetRecvCount(int n) {
        for (int i = 0; i < kRecvBits; i++) {
            EventFlags::SetFlag("APRecv" + std::to_string(i), (n >> i) & 1);
        }
    }

    template<typename T>
    int LowestClearBit(T bits, int limit) {
        for (int i = 0; i < limit; i++) {
            if (!((bits >> i) & 1)) return i;
        }
        return limit - 1;
    }

    // The lowest bit of allowed that is clear in bits, -1 for none.
    int LowestClearBitOf(u64 bits, u64 allowed) {
        u64 free = allowed & ~bits;
        return free == 0 ? -1 : __builtin_ctzll(free);
    }

    // The chamber stones owned (PanelPieceBits), bit i for stone i.
    u64 ChamberStoneBits() {
        const Game::BitTable* table = Game::PanelPieceBits.Ptr();
        u64 owned = 0;
        for (int i = 0; table->bits != nullptr && i < table->bytes && i < 8; i++) owned |= u64(table->bits[i]) << (8 * i);
        return owned;
    }

    // The bottle slots Link has (Bottle0..2) as bits 0..2: an added bottle fills the slot of its index.
    u64 BottleBits() {
        const auto& main = Game::Data::Inventory::Items()->MainItems;
        return main.Bottle0 | main.Bottle1 << 1 | main.Bottle2 << 2;
    }

    void Damage(int quarterHearts) {
        auto* items = Game::Data::Inventory::Items();
        items->Health = items->Health > quarterHearts ? items->Health - quarterHearts : 1;
    }

    // Native item-get (Link holds the item up and the item text shows). This is the body of the event action
    // Link.GenericItemGetSequenceByKey; it only displays, the inventory is changed separately.
    // It returns the running action as a libc++ shared_ptr (Player_ItemGetSequence).
    // The intro, the title or the file menus show: no save is loaded.
    bool NoSaveLoaded(Game::Framework* fmwk) {
        return fmwk == nullptr || fmwk->screen == Game::kScreenTitle || fmwk->screen == Game::kScreenFileMenus;
    }
#ifdef LASAP_TEST
    // Which screen the game shows, for test scripts.
    std::string ScreenName(Game::Framework* fmwk) {
        if (fmwk == nullptr) return "none";
        switch (fmwk->screen) {
            case Game::kScreenTitle: return fmwk->intro != nullptr ? "intro" : "title";
            case Game::kScreenFileMenus: return "menu";
            case Game::kScreenLoading: return "loading";
            case Game::kScreenLevel: return "level";
            default: return std::to_string(fmwk->screen);
        }
    }
#endif
    constexpr u64 kItemGetTimeoutMs = 120000;
    constexpr u64 kItemEventStartMs = 3000;      // a requested item event that has not started by then is retried
    constexpr size_t kShowQueueMax = 16;
    constexpr size_t kShowFillerMax = 3;          // with this many waiting, filler is no longer shown
    constexpr u64 kFreeSettleMs = 600;            // the player must be free this long before an item-get starts
    constexpr u64 kNextItemGetMs = 200;           // or this long when the last item event just ended
    constexpr u64 kPopupSettleMs = 200;           // and before a popup starts
    // A find waits this long before it shows: the event that gave it adds the item after the item-get (LASR's order,
    // the game's own), and a location that keeps its vanilla item is named once the item is added.
    constexpr u64 kFoundSettleMs = 50;
    // The trendy game: Link counts as free while the player steers the crane, which an item-get would cut off, so
    // displays wait until he leaves the shop.
    constexpr const char* kCraneGameLevel = "CraneGame";
    // The goal: Link reaches the Wind Fish and the owl after the Nightmare (room Ending_12O). The Wind Fish's event
    // sets ShadowClear only at its end, when the credits start.
    constexpr const char* kGoalLevel = "Ending";
    constexpr const char* kGoalFlag = "ShadowClear";
    constexpr int kItemEventTries = 3;

    int g_ItemEventTry = 0;
    u64 g_FreeSince = 0;
    u64 g_ItemEventEnded = 0;   // when the last item event ended, so a queued item-get follows it sooner
    bool g_WasFree = false;
    // A received item waiting to be shown; with a sender, it shows "You got <item> from <sender>!".
    // A trap (trap = its key) plays its effect instead; id is then -1.
    // With Instant Pickups, an item Link found (found): it shows as a popup with that toast ("" to name it when it
    // shows), queued at that time.
    struct Show {
        int id; std::string item; std::string sender; std::string trap;
        bool found = false; std::string toast; u64 at = 0;
    };
    std::deque<Show> g_ShowQueue;
    Show g_Showing{};  // what the current event shows; DBG itemget sets id -1 and no trap, which is not retried
    // A received item plays as the Common.bfevfl event AP::ITEM_EVENT_ENTRY, a trap as its own event: requested,
    // then running until the player is free again.
    enum class ItemEvent { None, Requested, Running };
    ItemEvent g_ItemEvent = ItemEvent::None;
    u64 g_ItemEventSince = 0;
    int g_ItemEventId = -1;            // what the event's item-get shows instead of the placeholder
    // The item event's item-get is still to be swapped (an item, not a trap). Kept apart from the event state, which
    // turns Running as soon as the event takes the player, before its item-get starts.
    bool g_ItemEventSwap = false;
    int g_ItemEventShown = -1;         // the item the last item event's item-get showed; -1 until the swap (STATE)
    std::string g_ItemEventMessage;    // and its message entry

    // AP locations that keep their vanilla item (VANILLA_PATH): the game adds the item with this id and index (-1:
    // any) when the location is collected, or, for a companion, sets this flag (item -1).
    struct KeptLocation { int item; int index; int local_id; std::string flag; };
    std::vector<KeptLocation> g_Kept;
    // The last such location collected by adding its item, and when: names a find of that item (Instant Pickups).
    int g_KeptAddedItem = -1, g_KeptAddedLocation = -1;
    u64 g_KeptAddedAt = 0;
    constexpr u64 kKeptFlagPollMs = 500;
    u64 g_KeptFlagsPolled = 0;
    bool g_Granting = false;  // GrantItem adds a received item

    // In-game hints the client makes server hints of (HINTS_PATH): the flag reading the hint sets, and the hinted
    // location as "<slot> <location id>". Reported once per connection, and all again on SYNC.
    struct HintFlag { std::string flag; std::string target; bool reported; };
    std::vector<HintFlag> g_Hints;
    u64 g_HintFlagsPolled = 0;

    // Connection notices: in a level this long without the client or the server, say so, then again this often while
    // it lasts.
    constexpr u64 kOfflineNoticeMs = 20000;
    constexpr u64 kOfflineRepeatMs = 300000;
    enum class Link { Unknown, Online, NoClient, NoServer };
    Link g_Link = Link::Unknown;
    u64 g_NextOfflineNotice = 0;
    bool g_WasConnected = false;
    bool g_ServerConnected = false;  // the client is logged into the slot (SERVER lines)

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

    // Plays an item-get of itemId with that message entry ("" for the item's own text) as the item event.
    bool StartItemEvent(int itemId, const std::string& messageEntry) {
        g_ItemEventId = itemId;
        g_ItemEventMessage = messageEntry;
        g_ItemEventShown = -1;
        Logging.Log("[AP] item event %d request", itemId);
        if (!RequestCommonEvent(AP::ITEM_EVENT_ENTRY)) return false;
        g_ItemEventSwap = true;
        g_ItemEvent = ItemEvent::Requested;
        g_ItemEventSince = NowMs();
        return true;
    }

    // Quarter hearts each trap event takes in the normal damage mode (LASR's trap flows).
    int TrapEventDamage(const std::string& key) {
        if (key == "ZapTrap") return 6;
        if (key == "SquishTrap") return 4;
        if (key == "DrownTrap" || key == "DeathballTrap" || key == "QuakeTrap") return 2;
        return -1;  // no event: the Zol Trap, which spawns enemies from a chest
    }

    // Plays a trap's effect as its event. In the normal damage mode a trap leaves at least a quarter heart.
    bool StartTrapEvent(const std::string& key) {
        int damage = TrapEventDamage(key);
        auto* items = Game::Data::Inventory::Items();
        if (damage < 0) {
            Damage(4);
            return true;
        }
        if (global_config.damage.mode == DamageMode::Normal && items->Health <= damage) items->Health = damage + 1;
        std::string entry = std::string(AP::TRAP_EVENT_PREFIX) + key;
        g_ItemEventId = -1;
        g_ItemEventSwap = false;
        Logging.Log("[AP] trap event %s request", key.c_str());
        if (!RequestCommonEvent(entry.c_str())) return false;
        g_ItemEvent = ItemEvent::Requested;
        g_ItemEventSince = NowMs();
        return true;
    }

    // True when the player can move: no talk, event, cutscene or pause menu.
    bool PlayerFree() {
        Game::Framework* fmwk = Game::GetFramework();
        if (!PlayerReady()) return false;
        return fmwk->paused == 0 && fmwk->player->state == 0;
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

    // Items whose own text tells where they came from in the vanilla game (the Boomerang's trade, the Mad Batter's
    // capacity upgrades); they show the AP text instead.
    bool HasSourceText(int itemId) {
        return itemId == 7 || itemId == 123 || itemId == 124 || itemId == 125;
    }

    bool IsFiller(int itemId) {
        // Rupees, ammo, hearts and other drops.
        return itemId == 4 || itemId == 11 || itemId == 60 || itemId >= 100;
    }

    // Names of the game's items that Link can find outside AP locations (Instant Pickups' toast), as the world names
    // them; any other shows its Items.gsheet key.
    struct ItemName { int id; const char* name; };
    constexpr ItemName kItemNames[] = {
        {4, "Bombs"}, {11, "Magic Powder"}, {17, "Green Tunic"}, {18, "Red Tunic"}, {19, "Blue Tunic"},
        {21, "Secret Medicine"}, {22, "Secret Seashell"}, {58, "Piece of Heart"}, {59, "Heart Container"},
        {60, "Arrows"}, {61, "Apple"}, {62, "Green Apple"}, {100, "Heart"}, {101, "1 Rupee"}, {102, "5 Rupees"},
        {104, "20 Rupees"}, {105, "50 Rupees"}, {106, "100 Rupees"}, {108, "300 Rupees"}, {110, "Fairy"},
        {113, "Bottle"}, {114, "Chamber Stone"}, {120, "Ballad of the Wind Fish"}, {121, "Manbo's Mambo"},
        {122, "Frog's Song of Soul"}, {126, "Fairy"},
    };

    std::string ItemDisplayName(int itemId) {
        for (const auto& n : kItemNames) {
            if (n.id == itemId) return n.name;
        }
        auto it = Inventory::kItemNames.find(itemId);
        return it != Inventory::kItemNames.end() ? it->second : std::to_string(itemId);
    }

    // A toast in the autosave popup; '|' starts a new line.
    void QueueNotice(std::string text) {
        for (auto& c : text) if (c == '|') c = '\n';
        AP::QueueToast(text);
    }

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

    void QueueShow(const Show& show) {
        if (g_ShowQueue.size() >= kShowQueueMax) return;
        if (g_ShowQueue.size() >= kShowFillerMax && IsFiller(show.id)) return;
        g_ShowQueue.push_back(show);
    }

    // Instant Pickups: a find shows before the received items that wait, after the finds before it.
    void QueueFound(const Show& show) {
        auto it = std::find_if(g_ShowQueue.begin(), g_ShowQueue.end(), [](const Show& s) { return !s.found; });
        g_ShowQueue.insert(it, show);
    }

    // True while a received item's event is requested or running.
    bool ItemEventActive(bool free) {
        u64 since = NowMs() - g_ItemEventSince;
        switch (g_ItemEvent) {
            case ItemEvent::None:
                return false;
            case ItemEvent::Requested:
                // It runs once it takes the player (a trap) or its item-get starts (the hook).
                if (!free) {
                    g_ItemEvent = ItemEvent::Running;
                    return true;
                }
                if (since < kItemEventStartMs) return true;
                Logging.Log("[AP] item event %d did not start", g_ItemEventId);
                if ((g_Showing.id >= 0 || !g_Showing.trap.empty()) && ++g_ItemEventTry < kItemEventTries) {
                    g_ShowQueue.push_front(g_Showing);
                }
                else g_ItemEventTry = 0;
                break;
            case ItemEvent::Running:
                if (!free && since < kItemGetTimeoutMs) return true;
                Logging.Log("[AP] item event %d ended after %lu ms", g_ItemEventId, since);
                g_ItemEventTry = 0;
                g_ItemEventEnded = NowMs();
                break;
        }
        g_ItemEvent = ItemEvent::None;
        g_ItemEventSwap = false;
        return false;
    }

    // A find's toast: as queued, else the location that keeps this vanilla item if the find collected it, else the
    // item's name.
    std::string FoundToast(const Show& show) {
        if (!show.toast.empty()) return show.toast;
        if (g_KeptAddedItem == show.id && g_KeptAddedAt >= show.at) {
            std::string toast = AP::FoundToast(g_KeptAddedLocation);
            if (!toast.empty()) return toast;
        }
        return "You found " + ItemDisplayName(show.id);
    }

    // Instant Pickups: the item's model above Link's head and the toast, while play goes on. The next one waits
    // until both are done. Traps still play their effect as an event. A find also plays its fanfare.
    void ShowPopup(const Show& show) {
        if (show.found) {
            AP::QueueToast(FoundToast(show));
            AP::Music::Fanfare(show.id);
        } else {
            std::string name = show.item;
            if (name.empty()) {
                auto it = Inventory::kItemNames.find(show.id);
                name = it != Inventory::kItemNames.end() ? it->second : std::to_string(show.id);
            }
            AP::QueueToast(AP::ReceivedToast(name, show.sender));
        }
        if (!AP::Popup::Show(show.id)) AP::Log("popup " + std::to_string(show.id) + " could not be made");
        g_ItemEventShown = show.id;
    }

    // Shows queued items one at a time, each as an event, once the player is free and holds nothing; with Instant
    // Pickups, as popups. Finds come first and show as soon as the popup is free, also during the event that gave
    // them.
    void TickItemGet() {
        bool free = PlayerFree();
        if (free && !g_WasFree) g_FreeSince = NowMs();
        if (free) AP::ExpireFoundLocation();
        g_WasFree = free;
        if (ItemEventActive(free)) {
            g_WasFree = false;  // the settle time starts again after each item-get
            return;
        }
        if (!g_ShowQueue.empty() && g_ShowQueue.front().found) {
            if (AP::Popup::Busy() || !AP::ToastsIdle() || !PlayerReady() ||
                NowMs() - g_ShowQueue.front().at < kFoundSettleMs) {
                return;
            }
            g_Showing = g_ShowQueue.front();
            g_ShowQueue.pop_front();
            ShowPopup(g_Showing);
            return;
        }
        bool popup = global_config.tweaks.instant_pickups && !g_ShowQueue.empty() && g_ShowQueue.front().trap.empty();
        if (popup && (AP::Popup::Busy() || !AP::ToastsIdle())) return;
        // An item-get right after the last item event's end follows it sooner than after other events
        u64 settle = popup ? kPopupSettleMs : g_FreeSince - g_ItemEventEnded < 100 ? kNextItemGetMs : kFreeSettleMs;
        if (g_ShowQueue.empty() || !free || LinkDead() || NowMs() - g_FreeSince < settle ||
            (!popup && (PlayerCarrying() || g_LastLevel == kCraneGameLevel))) {
            return;
        }
        g_Showing = g_ShowQueue.front();
        g_ShowQueue.pop_front();
        if (popup) {
            ShowPopup(g_Showing);
            return;
        }
        if (!g_Showing.trap.empty()) {
            // The notice names the trap and who sent it; a retry does not repeat it.
            if (g_ItemEventTry == 0) {
                AP::QueueToast(g_Showing.item + (g_Showing.sender.empty() ? "" : "\nfrom " + g_Showing.sender));
            }
            if (!StartTrapEvent(g_Showing.trap)) AP::Log("trap event " + g_Showing.trap + " could not be requested");
            return;
        }
        int id = g_Showing.id;
        std::string message;
        if (!g_Showing.sender.empty() || HasSourceText(id)) {
            AP::SetReceivedText(g_Showing.item, g_Showing.sender);
            message = AP::RECEIVED_LABEL;
        }
        if (!StartItemEvent(id, message)) AP::Log("item event " + std::to_string(id) + " could not be requested");
    }

    // NPC models as items (LASR's songs and tunics: Marin, Manbo, Mamu and the Color Dungeon's guards). The item-get
    // makes its model in a task (kItemModelTask, component "ItemModel"), a node of its own under the item's node (2
    // units above Link, tilted back 65 degrees towards the camera, its scale popping from 0 to 1, the shine). The
    // game gives the component a pose per kind of item: the ocarina and the songs kItemModelPos, rolled by about 21
    // degrees, scale 1.17; the tunics 0.15 higher, tilted forward 70 degrees, scale 0.8. Item models are small and
    // centred on their origin; an NPC model stands on its feet there. For these models the component gets the
    // ocarina's place, the fix's angle (from upright in the item's frame, facing the camera) and the scale that
    // gives the model the ocarina's size, and the model's root bone moves by -center, putting its middle on the
    // origin.
    // `radius` and `center` (model units) are the model's bind-pose bounds (scripts/model_bounds.py): its box centre
    // and the radius around it, all vertices in model space. `angle` turns the model about its centre (degrees in
    // the item's frame: x pitch, y yaw, z roll, applied roll first).
    struct HeldModelFix { const char* model; float radius; Game::Vec3 center; Game::Vec3 angle; };
    HeldModelFix g_HeldModelFixes[] = {
        {"Marin", 0.793f, {0.013f, 0.712f, -0.093f}, {20.0f, 0.0f, -20.0f}},
        {"ManboTamegoro", 1.966f, {0.0f, 0.209f, 0.080f}, {20.0f, 70.0f, -20.0f}},
        {"Mamu", 1.930f, {0.0f, 1.401f, -0.446f}, {20.0f, 0.0f, -20.0f}},
        {"SkeletalGuardRed", 0.673f, {0.0f, 0.523f, 0.035f}, {20.0f, 0.0f, -20.0f}},
        {"SkeletalGuardBlue", 0.673f, {0.0f, 0.523f, 0.035f}, {20.0f, 0.0f, -20.0f}},
    };
    // The ocarina's size in the item-get: its radius around its root (0.459) times the scale the game gives it
    constexpr float kItemModelRadius = 0.459f * 1.17f;
    constexpr Game::Vec3 kItemModelPos = {0.0f, 0.55f, 1.0f};  // the ocarina's place under the item's node
    // The item-get's model task (ItemGet_ModelTask): +0x8 the item-get's holder, +0x18 the component as a shared_ptr.
    // Node_MarkDirty does nothing while the node is marked (needsCoordUpdate), so setting that flag alone leaves the
    // model where it was.
    constexpr size_t kTaskHolder = 0x8, kHolderModel = 0x18;
    constexpr u64 kItemModelWindowMs = 3000;  // an item model task this soon after an item-get starts is its model

    u64 g_ItemGetAt = 0;
    bool g_InItemModelTask = false;
    const HeldModelFix* g_ItemModelFix = nullptr;  // the fix for the model the running task gets
    // The component being fixed, held weakly through its control block (libc++: +0x8 owners - 1, +0x10 weak owners
    // - 1); whether its root bone moved.
    u8* g_HeldModel = nullptr;
    u8* g_HeldCtrl = nullptr;
    const HeldModelFix* g_HeldFix = nullptr;
    bool g_HeldRootMoved = false;

    void DropHeldModel() {
        Game::ReleaseWeakOwner(g_HeldCtrl);
        g_HeldCtrl = nullptr;
        g_HeldModel = nullptr;
        g_HeldFix = nullptr;
    }

    bool HeldModelAlive() { return Game::Alive(g_HeldCtrl); }

    // The held NPC model's component node in the ocarina's place, turned by the fix's angle, at the ocarina's size;
    // the game writes its pose once the item pops up, and that write is replaced here.
    void PlaceHeldModel() {
        Game::Coords& c = reinterpret_cast<Game::RootComp*>(g_HeldModel)->coordsNew;
        const HeldModelFix& fix = *g_HeldFix;
        float scale = kItemModelRadius / fix.radius;
        Game::Quat q = Game::EulerQuat(fix.angle);
        c.pos = kItemModelPos;
        c.rotate = {q.x, q.y, q.z, q.w};
        c.scale = {scale, scale, scale};
    }

    HOOK_DEFINE_TRAMPOLINE(ModelResourceGet) {
        static void Callback(void* out, void* holder, const char* name) {
            if (g_InItemModelTask && name != nullptr) {
                g_ItemModelFix = nullptr;
                for (const auto& fix : g_HeldModelFixes) {
                    if (strcmp(name, fix.model) == 0) g_ItemModelFix = &fix;
                }
            }
            Orig(out, holder, name);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ItemModelTask) {
        static void Callback(u8* task) {
            g_InItemModelTask = true;
            g_ItemModelFix = nullptr;
            Orig(task);
            g_InItemModelTask = false;
            u8* holder = *reinterpret_cast<u8**>(task + kTaskHolder);
            if (AP::Popup::IsHolder(holder)) {
                // The popup's model: the same size, centre and angle
                const HeldModelFix* fix = g_ItemModelFix;
                if (fix != nullptr) AP::Popup::Fit(kItemModelRadius / fix->radius, fix->center, fix->angle);
                return;
            }
            if (g_ItemModelFix == nullptr || NowMs() - g_ItemGetAt > kItemModelWindowMs || holder == nullptr) return;
            u8* model = *reinterpret_cast<u8**>(holder + kHolderModel);
            u8* ctrl = *reinterpret_cast<u8**>(holder + kHolderModel + 8);
            if (model == nullptr || ctrl == nullptr) return;
            DropHeldModel();
            Game::AddWeakOwner(ctrl);
            g_HeldModel = model;
            g_HeldCtrl = ctrl;
            g_HeldFix = g_ItemModelFix;
            g_HeldRootMoved = false;
            PlaceHeldModel();
        }
    };

    HOOK_DEFINE_TRAMPOLINE(NodeMarkDirty) {
        static void Callback(Game::RootComp* node) {
            if (node != nullptr && node == reinterpret_cast<Game::RootComp*>(g_HeldModel)) {
                if (HeldModelAlive()) PlaceHeldModel();
                else DropHeldModel();
            }
            Orig(node);
        }
    };

    // Each frame: the held model's root bone once it can move, and its weak reference goes once the model is gone.
    void TickHeldModel() {
        if (g_HeldModel == nullptr) return;
        if (!HeldModelAlive()) DropHeldModel();
        else if (!g_HeldRootMoved) g_HeldRootMoved = Game::MoveRootBone(g_HeldModel, g_HeldFix->center);
    }

    // Instant Pickups (DESIGN.md, Instant pickups). The item-get is the body of the event actions
    // Link.GenericItemGetSequence(ByKey) (and of the Chamber Dungeon's panels): it returns a future the event waits
    // for. Delay_Make(seconds, out) makes one; for 0 seconds it is the game's ready future, which Link's own actions
    // return when they are done at once, so the event goes on in its next update. The item-get's other work is for
    // its display (Link's animation and held model, the text, the zone's music faded out for the fanfare), apart from
    // what the module does in its place: the fanfare, the buffs' music and the autosave. It also takes what Link
    // holds over his head out of the game; without it he keeps holding it (the shop's flows take their goods from him
    // before their item-get, RemoveGoods). The inventory and the item's getting flag are the event's AddItemByKey's.
    constexpr int kGuardianAcorn = 111, kPieceOfPower = 112;
    // The autosave: when the Items.gsheet row's autoSave holds (enabled, its condition true now: ItemGet_RowAutoSave),
    // the item-get asks the scene's save manager (scene +0x90) for one (SaveManager_RequestAutoSave(manager, kind 3,
    // false)), which saves once no event runs. SceneHolder_Lock(out, the framework's game state) gives the scene.
    constexpr size_t kSceneSaveManager = 0x90;
    constexpr u8 kAutoSaveKind = 3;

    void RequestItemAutoSave(int itemId) {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || !Game::ItemGet_RowAutoSave(static_cast<u32>(itemId))) return;
        void* holder = fmwk->gameState;
        if (holder == nullptr) return;
        SharedPtr scene{};
        Game::SceneHolder_Lock(&scene, holder);
        if (scene.ptr != nullptr) {
            Game::SaveManager_RequestAutoSave(static_cast<u8*>(scene.ptr) + kSceneSaveManager, kAutoSaveKind, false);
        }
        Game::Release(scene);
    }

    // An item Link found shows as a popup instead of an item-get; the buffs only start their music.
    void InstantPickup(int itemId) {
        Logging.Log("[AP] instant pickup %d", itemId);
        RequestItemAutoSave(itemId);
        if (itemId == kGuardianAcorn || itemId == kPieceOfPower) {
            AP::Music::BuffMusic();
            return;
        }
        Show show{itemId, "", "", ""};
        show.found = true;
        // the placeholder's location is known now (its flows add it first); a vanilla item is added after
        if (AP::IsPlaceholderItem(itemId)) {
            show.toast = AP::FoundToast();
            if (show.toast.empty()) show.toast = "You found an\nArchipelago item";
        }
        show.at = NowMs();
        QueueFound(show);
    }

    // The item event's item-get shows the placeholder with RECEIVED_LABEL as message entry; the requested item
    // and its message take their place. With Instant Pickups any other item-get is a find, unless its entry has the
    // native mark.
    HOOK_DEFINE_TRAMPOLINE(ItemGetSequence) {
        static void Callback(SharedPtr* out, void* player, int itemId, bool keepCarry, StrView* messageEntry) {
            g_ItemGetAt = NowMs();
            size_t len = strlen(AP::RECEIVED_LABEL);
            bool received = messageEntry != nullptr && messageEntry->size == len &&
                            memcmp(messageEntry->data, AP::RECEIVED_LABEL, len) == 0;
            StrView entry = messageEntry != nullptr ? *messageEntry : StrView{"", 0};
            bool native = entry.size > 0 && entry.data[0] == AP::NATIVE_ITEM_GET_MARK;
            if (native) {
                entry.data++;
                entry.size--;
            }
            if (global_config.tweaks.instant_pickups && !received && !native) {
                InstantPickup(itemId);
                Game::Delay_Make(0.0f, out);
                return;
            }
            if (native) messageEntry = &entry;
            if (g_ItemEventSwap && g_ItemEvent != ItemEvent::None && itemId == AP::PLACEHOLDER_ITEM_ID && received) {
                StrView message{g_ItemEventMessage.c_str(), g_ItemEventMessage.size()};
                g_ItemEventSwap = false;
                g_ItemEventShown = g_ItemEventId;
                if (g_ItemEvent == ItemEvent::Requested) g_ItemEventSince = NowMs();
                g_ItemEvent = ItemEvent::Running;
                Logging.Log("[AP] item event %d start", g_ItemEventId);
                Orig(out, player, g_ItemEventId, keepCarry, &message);
                return;
            }
            Orig(out, player, itemId, keepCarry, messageEntry);
        }
    };

    // A shorter item-get, for every item-get (TESTING.md, Item-get timing). The sequence (the item-get) runs as
    // steps: Link raises the item (animation item_get_st), a delay of 1 s, the item's text; then a short delay and
    // Link lowers the item (item_get_ed). Alongside the text runs the item's fanfare (its Items.gsheet sound, a BGM
    // or an SE), and the text's step ends only once the text has closed and the fanfare has stopped, which held Link
    // up to 2.5 s after a text closed with B. The fanfare still plays on; the item-get no longer waits for it, and
    // the text opens 0.25 s after the raise.
    constexpr uintptr_t kItemGetTextDelay = 0xd741f8;   // in the text's step: ldr s0, [x20, #0xa0], the delay
    constexpr u32 kTextDelayQuarter = 0x1e2a1000;       // fmov s0, #0.25

    HOOK_DEFINE_REPLACE(ItemGetFanfareDone) {
        static bool Callback(void*) { return true; }
    };

    bool IsTrapKey(const std::string& key) { return key.ends_with("Trap") || key == "$ENEMY"; }

    // A received trap waits like an item display, then plays its effect (StartTrapEvent).
    void QueueTrap(const std::string& key, const std::string& item, const std::string& sender) {
        QueueShow(Show{-1, item.empty() ? key : item, sender, key});
    }

    // Secret Medicines waiting (AP::MEDICINE_FLAG): the game holds one at a time (scripts/re/ITEMS.md).
    constexpr int kSecretMedicine = 21;
    constexpr u64 kMedicinePollMs = 500;
    u64 g_MedicinePolled = 0;

    int GetMedicineWaiting() {
        int n = 0;
        for (int i = 0; i < AP::MEDICINE_FLAG_BITS; i++) {
            if (EventFlags::CheckFlag(AP::MEDICINE_FLAG + std::to_string(i))) n |= 1 << i;
        }
        return n;
    }

    void SetMedicineWaiting(int n) {
        for (int i = 0; i < AP::MEDICINE_FLAG_BITS; i++) {
            EventFlags::SetFlag(AP::MEDICINE_FLAG + std::to_string(i), (n >> i) & 1);
        }
    }

    // Grants one item the way an in-game pickup would, including LASR's progressive items.
    // Returns the item id granted, or -1 for traps (QueueTrap) and unknown keys.
    int GrantItem(const std::string& key, int count, int dungeon) {
        auto* items = Game::Data::Inventory::Items();
        std::string k = key;
        if (IsTrapKey(k)) return -1;
        // LASR's "$EXT:" keys only name a chest text (Master Stalfos' Note); "None" is an item the client has no key for.
        if (k.starts_with("$EXT:") || k == "None") return -1;
        if (k == "SwordLv1" && EventFlags::CheckFlag("SwordFoundFlag")) k = "SwordLv2";
        else if (k == "Shield" && EventFlags::CheckFlag("ShieldFoundFlag")) k = "MirrorShield";
        else if (k == "PowerBraceletLv1" && EventFlags::CheckFlag("BraceletFoundFlag")) k = "PowerBraceletLv2";
        else if (k == "ClothesRed") EventFlags::SetFlag("RedTunicFoundFlag", true);
        else if (k == "ClothesBlue") EventFlags::SetFlag("BlueTunicFoundFlag", true);

        int id = ItemIdFromKey(k);
        if (id < 0) {
            AP::Log("unknown item key " + key);
            return -1;
        }

        int index = -1;
        switch (id) {
            case 22: index = LowestClearBit(items->Seashells | AP::VanillaBits('S'), kSeashellBits); break;
            case 23: index = LowestClearBit(items->GoldenLeaves | AP::VanillaBits('L'), 5); break;
            case 58: index = LowestClearBit(items->HeartPieces | AP::VanillaBits('H'), kHeartPieceBits); break;
            case 59: index = LowestClearBit(items->HeartContainers | AP::VanillaBits('C'), kHeartContainerBits); break;
            case 113: index = LowestClearBit(BottleBits() | AP::VanillaBits('B'), 3); break; // the bottle slot
            case 114: index = LowestClearBitOf(ChamberStoneBits() | AP::VanillaBits('P'), kChamberStoneBits); break;
            case 44: case 45: case 47: case 48: case 49: index = dungeon; break; // dungeon items
            default: break;
        }
        // A Secret Medicine while Link holds one waits (TickMedicine); its item-get shows now.
        if (id == kSecretMedicine && items->ExtraItems.Medicine) {
            int waiting = GetMedicineWaiting();
            if (waiting < (1 << AP::MEDICINE_FLAG_BITS) - 1) SetMedicineWaiting(waiting + 1);
            AP::Log("Secret Medicine waits (" + std::to_string(GetMedicineWaiting()) + ")");
            return id;
        }
        g_Granting = true;
        Inventory::AddItemID(id, count, index);
        g_Granting = false;
        return id;
    }

    // The text after the first n space-separated fields of a line.
    std::string RestAfter(const std::string& line, int n) {
        size_t pos = 0;
        for (int i = 0; i < n && pos != std::string::npos; i++) {
            pos = line.find_first_not_of(' ', pos);
            pos = pos == std::string::npos ? pos : line.find(' ', pos);
        }
        return pos == std::string::npos ? "" : line.substr(pos + 1);
    }

    void ReadBits(const std::string& path, std::vector<u8>& bits) {
        nn::fs::FileHandle h{};
        if (R_SUCCEEDED(nn::fs::OpenFile(&h, path.c_str(), nn::fs::OpenMode_Read))) {
            nn::fs::ReadFile(h, 0, bits.data(), kChecksBytes);
            nn::fs::CloseFile(h);
        }
    }

    void WriteBits(const std::string& path, const std::vector<u8>& bits) {
        nn::fs::FileHandle h{};
        if (R_FAILED(nn::fs::OpenFile(&h, path.c_str(), nn::fs::OpenMode_Write))) {
            nn::fs::CreateFile(path.c_str(), kChecksBytes);
            if (R_FAILED(nn::fs::OpenFile(&h, path.c_str(), nn::fs::OpenMode_Write))) {
                AP::Log("cannot write " + path);
                return;
            }
        }
        nn::fs::WriteFile(h, 0, bits.data(), kChecksBytes, nn::fs::WriteOption{nn::fs::WriteOptionFlag_Flush});
        nn::fs::CloseFile(h);
    }

    void LoadChecks() {
        g_ChecksPath = "sd:/lasap/" + g_SeedHash + ".chk";
        g_SkippedPath = "sd:/lasap/" + g_SeedHash + ".skp";
        nn::fs::CreateDirectory("sd:/lasap");
        ReadBits(g_ChecksPath, g_Checks);
        ReadBits(g_SkippedPath, g_Skipped);
        g_ChecksLoaded = true;
    }

    void SaveChecks() {
        WriteBits(g_ChecksPath, g_Checks);
    }

    // Saves a location collected here and reports it.
    void RecordCheck(int local_id) {
        if (!g_ChecksLoaded) LoadChecks();
        if (local_id >= 0 && local_id < (int)(kChecksBytes * 8)) {
            g_Checks[local_id / 8] |= (1 << (local_id % 8));
            SaveChecks();
        }
        Logging.Log("[AP] collected location %d", local_id);
        AP::Send("CHECK " + std::to_string(local_id));
    }

    bool CollectedHere(int local_id) {
        if (local_id < 0 || local_id >= (int)(kChecksBytes * 8)) return false;
        if (!g_ChecksLoaded) LoadChecks();
        return (g_Checks[local_id / 8] >> (local_id % 8)) & 1;
    }

    const KeptLocation* FindKept(int local_id) {
        if (local_id < 0) return nullptr;
        for (const auto& kept : g_Kept) {
            if (kept.local_id == local_id) return &kept;
        }
        return nullptr;
    }

    // Whether the save holds a kept location's item, as the Trendy Game decides whether to offer a prize: the bit of
    // its index, or for the Yoshi Doll (no index) its getting flag, TradeYoshiDollGet.
    bool SaveHasKept(const KeptLocation& kept) {
        auto* items = Game::Data::Inventory::Items();
        u64 bit = kept.index >= 0 && kept.index < 64 ? u64(1) << kept.index : 0;
        switch (kept.item) {
            case 22: return items->Seashells & bit;
            case 58: return items->HeartPieces & bit;
            case 114: return ChamberStoneBits() & bit;
            default: return EventFlags::CheckGettingFlag(kept.item);
        }
    }

    // The server's item for a location that keeps its vanilla item. The game gave it when the location was collected
    // in this save; a save without it (an older one, another file, a !collect) gets it now with the location's own
    // index, which also takes the prize out of the Trendy Game. A companion joins only in the game. Returns the item
    // to show, -1 for none.
    int GrantKept(const KeptLocation& kept) {
        if (!kept.flag.empty() || SaveHasKept(kept)) return -1;
        g_Granting = true;
        Inventory::AddItemID(kept.item, 1, kept.index);
        g_Granting = false;
        return kept.item;
    }

    // "<item key> <index> <local id>" and "F <flag> <local id>" lines (patcher las_ap_patch.write_vanilla)
    void LoadKept() {
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::VANILLA_PATH, text))) return;
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::stringstream ss(line);
            std::string key;
            if (line.starts_with("F ")) {
                std::string flag;
                int local_id;
                if (ss >> key >> flag >> local_id) g_Kept.push_back({-1, -1, local_id, flag});
                continue;
            }
            int index, local_id;
            if (!(ss >> key >> index >> local_id)) continue;
            int item = ItemIdFromKey(key);
            if (item < 0) {
                AP::Log("unknown kept item " + key);
                continue;
            }
            g_Kept.push_back({item, index, local_id, ""});
        }
        Logging.Log("[AP] kept locations: %zu", g_Kept.size());
    }

    // Reports a companion once the flag its joining sets is set.
    void TickKeptFlags() {
        if (!PlayerReady() || NowMs() - g_KeptFlagsPolled < kKeptFlagPollMs) return;
        g_KeptFlagsPolled = NowMs();
        for (const auto& kept : g_Kept) {
            if (!kept.flag.empty() && !CollectedHere(kept.local_id) && EventFlags::CheckFlag(kept.flag.c_str())) {
                RecordCheck(kept.local_id);
            }
        }
    }

    // "<n> <flag> <slot> <location id>" lines (patcher ap_hints.py)
    void LoadHints() {
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::HINTS_PATH, text))) return;
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::stringstream ss(line);
            std::string n, flag, slot, location;
            if (ss >> n >> flag >> slot >> location) g_Hints.push_back({flag, slot + " " + location, false});
        }
        Logging.Log("[AP] hints: %zu", g_Hints.size());
    }

    // Sends HINT for the hints read (their flag set) and not yet reported on this connection, or for all read.
    void ReportHints(bool all) {
        if (!AP::IsClientConnected()) return;
        for (auto& hint : g_Hints) {
            if ((all || !hint.reported) && EventFlags::CheckFlag(hint.flag.c_str())) {
                AP::Send("HINT " + hint.target);
                hint.reported = true;
            }
        }
    }

    void TickHintFlags() {
        if (g_Hints.empty() || !PlayerReady() || NowMs() - g_HintFlagsPolled < kKeptFlagPollMs) return;
        g_HintFlagsPolled = NowMs();
        ReportHints(false);
    }

    void SendAllChecks() {
        std::string line = "CHECKS";
        int onLine = 0;
        for (size_t i = 0; i < kChecksBytes * 8; i++) {
            if (g_Checks[i / 8] & (1 << (i % 8))) {
                line += " " + std::to_string(i);
                if (++onLine == 64) {
                    AP::Send(line);
                    line = "CHECKS";
                    onLine = 0;
                }
            }
        }
        if (onLine > 0) AP::Send(line);
    }

#ifdef LASAP_TEST
    // Test-only commands (DBG) and the STATE line: only in the test build (scripts/build_exl.sh).
    std::string HexU64(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

    std::string StateLine() {
        Game::Framework* fmwk = Game::GetFramework();
        auto* items = Game::Data::Inventory::Items();
        std::string s = "STATE level=" + LevelSystem::GetCurrentLevel();
        s += " screen=" + ScreenName(fmwk);
        s += " ms_in_level=" + std::to_string(NowMs() - g_LevelSince);
        s += " player=" + std::string(fmwk && fmwk->player ? "1" : "0");
        if (items != nullptr) {
            s += " health=" + std::to_string(items->Health);
            s += " rupees=" + std::to_string(items->Rupees);
            s += " shells=" + HexU64(items->Seashells);
            s += " hp=" + HexU64(items->HeartPieces);
            s += " hc=" + std::to_string(items->HeartContainers);
            s += " leaves=" + std::to_string(items->GoldenLeaves);
            s += " stones=" + HexU64(ChamberStoneBits());
            s += " medicine=" + std::to_string(items->ExtraItems.Medicine) + "/" + std::to_string(GetMedicineWaiting());
            s += " sword=" + std::to_string(items->MainItems.SwordLv1) + "/" + std::to_string(items->UpgradeItems.SwordLv2);
            s += " shield=" + std::to_string(items->MainItems.Shield) + "/" + std::to_string(items->UpgradeItems.MirrorShield);
            s += " bracelet=" + std::to_string(items->MainItems.PowerBraceletLv1) + "/" + std::to_string(items->UpgradeItems.PowerBraceletLv2);
            s += " feather=" + std::to_string(items->MainItems.RocsFeather);
            s += " boots=" + std::to_string(items->MainItems.PegasusBoots);
            s += " flippers=" + std::to_string(items->ExtraItems.Flippers);
            s += " bombs=" + std::to_string(items->BombCount);
            s += " arrows=" + std::to_string(items->ArrowCount);
            s += " bombbag=" + std::to_string(items->MainItems.Bomb);
            s += " bow=" + std::to_string(items->MainItems.Bow);
            s += " cello=" + std::to_string(items->ExtraItems.FullMoonCello);
        }
        if (fmwk && fmwk->player && fmwk->player->playerCollision) {
            auto& p = fmwk->player->playerCollision->coordsNew.pos;
            char buf[96];
            snprintf(buf, sizeof(buf), " pos=%.2f,%.2f,%.2f", p.x, p.y, p.z);
            s += buf;
        }
        s += " recv=" + std::to_string(GetRecvCount());
        if (fmwk && fmwk->player) {
            s += " event=" + std::to_string(fmwk->player->state);
            s += " paused=" + std::to_string(fmwk->paused);
            s += std::string(" carry=") + (PlayerCarrying() ? "1" : "0");
        }
        s += " showq=" + std::to_string(g_ShowQueue.size()) + (g_ItemEvent != ItemEvent::None ? "+1" : "");
        s += " shown=" + std::to_string(g_ItemEventShown);
        s += std::string(" popup=") + (AP::Popup::Busy() ? "1" : "0");
        s += std::string(" toast=") + (AP::ToastShowing() ? "1" : "0");
        int ghosts = 0;
        for (int n = 0; n < AP::GhostNet::SLOTS; n++) ghosts += AP::GhostNet::Get(n).active;
        s += " ghosts=" + std::to_string(ghosts);
        s += AP::Music::StateField();
        s += AP::JukeboxPane::StateField(fmwk != nullptr && fmwk->paused != 0);
        s += AP::MapListField();
        s += AP::Test::StateFields();
        return s;
    }

    // Test-only commands. Returns false if the command must wait for the player.
    bool HandleDebug(const std::vector<std::string>& a) {
        if (a.size() < 2) return true;
        const std::string& cmd = a[1];
        if (cmd == "state") {
            AP::Send(StateLine());
        } else if (cmd == "flag" && a.size() >= 3) {
            AP::Send("FLAG " + a[2] + " " + (EventFlags::CheckFlag(a[2]) ? "1" : "0"));
        } else if (cmd == "setflag" && a.size() >= 4) {
            EventFlags::SetFlag(a[2], a[3] == "1");
            AP::Send("FLAG " + a[2] + " " + (EventFlags::CheckFlag(a[2]) ? "1" : "0"));
        } else if (cmd == "give" && a.size() >= 5) {
            // Raw AddItemID through the LASR hook: DBG give <id> <count> <index>
            if (!PlayerReady()) return false;
            Inventory::AddItemID(atoi(a[2].c_str()), atoi(a[3].c_str()), atoi(a[4].c_str()));
            AP::Send("OK give");
        } else if (cmd == "grant" && a.size() >= 5) {
            // Same path as ITEM without touching the received counter: DBG grant <key> <count> <dungeon> [show 0|1]
            if (!PlayerReady()) return false;
            int id = GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
            if (IsTrapKey(a[2])) QueueTrap(a[2], "", "");
            else if (id >= 0 && (a.size() < 6 || a[5] != "0")) QueueShow(Show{id, "", "", ""});
            AP::Send("OK grant");
        } else if (cmd == "heldangle" && a.size() >= 6) {
            // An NPC model's item-get angle until the game restarts: DBG heldangle <model> <pitch> <yaw> <roll>
            for (auto& fix : g_HeldModelFixes) {
                if (a[2] != fix.model) continue;
                fix.angle = {(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
                AP::Send("OK heldangle");
                return true;
            }
            AP::Send("ERR heldangle unknown model " + a[2]);
        } else if (cmd == "popsway" && a.size() >= 3) {
            // DBG popsway <on|off>: whether the popup's model turns from side to side, until the game restarts
            AP::Popup::Sway(a[2] != "off");
            AP::Send("OK popsway");
        } else if (cmd == "popframes" && a.size() >= 3) {
            // DBG popframes <count>: record the popup's next poses; DBG popframes dump: send them
            if (a[2] == "dump") {
                for (const auto& line : AP::Popup::RecordedFrames()) AP::Send(line);
                AP::Send("OK popframes");
            } else {
                AP::Popup::RecordFrames(static_cast<size_t>(atoi(a[2].c_str())));
                AP::Send("OK popframes");
            }
        } else if (cmd == "heldpose") {
            // DBG heldpose: the world rotations (quaternions) of Link's model, the held NPC model's parent node and
            // the model itself during an item-get, and the model's local rotation
            auto quat = [](const Game::Vec4& r) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.3f,%.3f,%.3f,%.3f", r.x, r.y, r.z, r.t);
                return std::string(buf);
            };
            Game::Actors::Player* player = Game::GetPlayer();
            Game::RootComp* link = player != nullptr ? player->modelComp : nullptr;
            auto* held = reinterpret_cast<Game::RootComp*>(g_HeldModel);
            std::string out = "HELDPOSE link=" + (link ? quat(link->coords.rotate) : std::string("-"));
            if (held != nullptr && HeldModelAlive()) {
                out += " parent=" + (held->otherComp ? quat(held->otherComp->coords.rotate) : std::string("-"));
                out += " model=" + quat(held->coords.rotate) + " local=" + quat(held->coordsNew.rotate);
            }
            // the camera's view rotation
            const Game::Camera* camera = Game::GetCamera();
            if (camera != nullptr) {
                out += " view=";
                for (int i = 0; i < 12; i++) {
                    char buf[16];
                    snprintf(buf, sizeof(buf), i ? ",%.3f" : "%.3f", camera->view[i]);
                    out += buf;
                }
            }
            AP::Send(out);
        } else if (cmd == "warp" && a.size() >= 6) {
            // DBG warp <level> <x> <y> <z>
            if (!PlayerReady()) return false;
            LevelSystem::LoadLevel(a[2].c_str(), Game::Vec3{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())});
            AP::Send("OK warp");
        } else if (cmd == "pos" && a.size() >= 5) {
            // Moves the player within the current level: DBG pos <x> <y> <z>
            if (!PlayerReady()) return false;
            // Setting the respawn point too means a fall into a room that is not loaded yet lands at the target.
            auto* player = Game::GetFramework()->player;
            auto* col = player->playerCollision;
            Game::Vec3 target{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
            col->coordsNew.pos = target;
            col->coords.pos = target;
            col->needsCoordUpdate = true;
            player->respawnCoords.pos = target;
            AP::Send("OK pos");
        } else if (cmd == "setrecv" && a.size() >= 3) {
            SetRecvCount(atoi(a[2].c_str()));
            AP::Send("RECV " + std::to_string(GetRecvCount()));
        } else if (cmd == "clearchecks") {
            std::fill(g_Checks.begin(), g_Checks.end(), 0);
            SaveChecks();
            AP::Send("OK clearchecks");
        } else if (cmd == "map") {
            if (a.size() >= 3) AP::SetMapMode(atoi(a[2].c_str()));
            AP::Send(AP::MapState());
        } else if (cmd == "maplist") {
            AP::Send(AP::MapListInfo(a));
        } else if (cmd == "mapdot") {
            AP::Send(AP::MapDotInfo(a));
        } else if (cmd == "heap") {
            // DBG heap: the module's heap (exl::setting::HeapSize): bytes taken from it, of those in use and free
            struct mallinfo mi = mallinfo();
            AP::Send("OK heap arena=" + std::to_string(mi.arena) + " used=" + std::to_string(mi.uordblks) +
                     " free=" + std::to_string(mi.fordblks) + " size=" + std::to_string(exl::setting::HeapSize));
        } else if (cmd == "maplabel") {
            AP::Send("MAPLABEL " + AP::MapLabelText(a.size() >= 3 ? atoi(a[2].c_str()) : AP::MapHoverGroup()));
        } else if (cmd == "text") {
            AP::Send(a.size() >= 3 ? AP::SeenText(a[2]) : AP::TextState());
        } else if (cmd == "music") {
            // DBG music: the jukebox's STATE fields and the last BGM requests
            AP::Send("MUSIC" + AP::Music::StateField() + " " + AP::Music::LogText());
        } else if (cmd == "jukebox") {
            // DBG jukebox: the Jukebox page's state
            AP::Send(AP::JukeboxPane::Info());
        } else if (cmd == "name") {
            // DBG name: the player's name as the game holds it for the save (UTF-16, up to 8 letters, at +0x8 of the
            // save header), set by a new file's name entry on OK
            const u8* info = *Game::SaveHeader_ptr;
            if (info == nullptr) {
                AP::Send("ERR name: no object");
                return true;
            }
            const char16_t* name = reinterpret_cast<const char16_t*>(info + 0x8);
            std::string utf8;
            for (int i = 0; i < 8 && name[i] != 0; i++) {
                char16_t c = name[i];
                if (c < 0x80) {
                    utf8 += static_cast<char>(c);
                } else if (c < 0x800) {
                    utf8 += static_cast<char>(0xc0 | (c >> 6));
                    utf8 += static_cast<char>(0x80 | (c & 0x3f));
                } else {
                    utf8 += static_cast<char>(0xe0 | (c >> 12));
                    utf8 += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
                    utf8 += static_cast<char>(0x80 | (c & 0x3f));
                }
            }
            AP::Send("PLAYERNAME " + utf8);
        } else if (cmd == "peek" && a.size() >= 5) {
            // DBG peek <p|f|i|b|m|h> <hex offset> <hex length>: bytes of the player, the framework, the inventory, the BGM
            // manager, its first playing instance or that instance's sound
            Game::Framework* fmwk = Game::GetFramework();
            const u8* base = nullptr;
            if (a[2] == "p") base = fmwk ? reinterpret_cast<const u8*>(fmwk->player) : nullptr;
            else if (a[2] == "f") base = reinterpret_cast<const u8*>(fmwk);
            else if (a[2] == "i") base = reinterpret_cast<const u8*>(Game::Data::Inventory::Items());
            else if (a[2] == "b" || a[2] == "m") base = AP::Music::PeekBase(a[2] == "m");
            else if (a[2] == "h" && AP::Music::PeekBase(true) != nullptr) {
                base = *reinterpret_cast<const u8* const*>(AP::Music::PeekBase(true) + 8);  // the sound
            }
            if (base == nullptr) {
                AP::Send("ERR peek: no object");
                return true;
            }
            size_t off = strtoull(a[3].c_str(), nullptr, 16), len = std::min<size_t>(strtoull(a[4].c_str(), nullptr, 16), 0x2000);
            std::string line = "PEEK " + a[2] + " " + a[3] + " ";
            char hex[3];
            for (size_t i = 0; i < len; i++) {
                snprintf(hex, sizeof(hex), "%02x", base[off + i]);
                line += hex;
                if (line.size() > 1800) {
                    AP::Send(line);
                    line = "PEEK+ ";
                }
            }
            AP::Send(line);
        } else if (cmd == "poke" && a.size() >= 4) {
            // DBG poke <hex offset> <hex bytes>: writes bytes into the inventory
            u8* base = reinterpret_cast<u8*>(Game::Data::Inventory::Items());
            size_t off = strtoull(a[2].c_str(), nullptr, 16);
            const std::string& hex = a[3];
            if (base == nullptr || hex.size() % 2 != 0 || off + hex.size() / 2 > sizeof(Game::Data::Inventory::InventoryItems)) {
                AP::Send("ERR poke");
                return true;
            }
            for (size_t i = 0; i < hex.size() / 2; i++) {
                base[off + i] = static_cast<u8>(strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16));
            }
            AP::Send("OK poke");
        } else if (cmd == "itemget" && a.size() >= 3) {
            // DBG itemget <item key or id> [message entry]: the item event, without granting or waiting in the queue.
            // Only for looking at item models: it skips the checks that hold received items back, so it can break a
            // running flow. Tests of received items use grant or ITEM lines.
            if (!PlayerReady()) return false;
            if (g_ItemEvent != ItemEvent::None) {
                AP::Send("ERR itemget: a display is running");
                return true;
            }
            bool numeric = !a[2].empty() && a[2].find_first_not_of("0123456789") == std::string::npos;
            int id = numeric ? std::stoi(a[2]) : ItemIdFromKey(a[2]);
            g_Showing = Show{-1, "", "", ""};
            bool ok = id >= 0 && StartItemEvent(id, a.size() >= 4 ? a[3] : "");
            AP::Send(std::string(ok ? "OK" : "ERR") + " itemget " + std::to_string(id));
        } else if (cmd == "pickup" && a.size() >= 3) {
            // DBG pickup <item key or id>: what an item-get of it does with Instant Pickups, without the event
            if (!PlayerReady()) return false;
            bool numeric = a[2].find_first_not_of("0123456789") == std::string::npos;
            int id = numeric ? std::stoi(a[2]) : ItemIdFromKey(a[2]);
            if (id >= 0) InstantPickup(id);
            AP::Send(std::string(id >= 0 ? "OK" : "ERR") + " pickup " + std::to_string(id));
        } else if (cmd == "popmodel" && a.size() >= 3) {
            // DBG popmodel <item key or id>: the received-item popup's model alone, without the toast or the queue
            if (!PlayerReady()) return false;
            bool numeric = a[2].find_first_not_of("0123456789") == std::string::npos;
            int id = numeric ? std::stoi(a[2]) : ItemIdFromKey(a[2]);
            bool ok = id >= 0 && AP::Popup::Show(id);
            AP::Send(std::string(ok ? "OK" : "ERR") + " popmodel " + std::to_string(id));
        } else if (cmd == "dumpmain") {
            // Writes the loaded, decrypted main module (text..data) to sd:/lasap/main.bin for reverse engineering.
            const auto& info = exl::util::GetMainModuleInfo();
            uintptr_t start = info.m_Total.m_Start;
            size_t size = info.m_Data.m_Start + info.m_Data.m_Size - start;
            nn::fs::CreateDirectory("sd:/lasap");
            nn::fs::DeleteFile("sd:/lasap/main.bin");
            nn::fs::CreateFile("sd:/lasap/main.bin", size);
            nn::fs::FileHandle h{};
            if (R_FAILED(nn::fs::OpenFile(&h, "sd:/lasap/main.bin", nn::fs::OpenMode_Write))) {
                AP::Send("ERR cannot open main.bin");
                return true;
            }
            nn::fs::WriteFile(h, 0, reinterpret_cast<void*>(start), size, nn::fs::WriteOption{nn::fs::WriteOptionFlag_Flush});
            nn::fs::CloseFile(h);
            AP::Send("OK dumpmain base=" + HexU64(start) + " size=" + HexU64(size) +
                     " text=" + HexU64(info.m_Text.m_Size) + " rodata=" + HexU64(info.m_Rodata.m_Start - start) +
                     " data=" + HexU64(info.m_Data.m_Start - start));
        } else if (cmd == "savepopup") {
            // Shows the game's own autosave popup, to check that toasts give its text back
            Game::Throbber_Show(true);
            AP::Send("OK savepopup");
        } else if (cmd == "collect" && a.size() >= 3) {
            // Simulates picking up a placeholder: DBG collect <local_id>
            AP::OnPlaceholderCollected(atoi(a[2].c_str()));
        } else if (!AP::Test::Command(a)) {
            AP::Send("ERR unknown debug command");
        }
        return true;
    }
#endif

    // Returns false if the line must be retried on a later frame.
    bool HandleLine(const std::string& line) {
        auto a = Split(line);
        if (a.empty()) return true;
        if (a[0] == "ITEM" && a.size() >= 5) {
            if (!PlayerReady()) return false;
            int n = atoi(a[1].c_str());
            int have = GetRecvCount();
            if (n == have + 1) {
                // ITEM <n> <key> <count> <dungeon> [<location> <item name>|<sender>]
                int location = a.size() >= 6 ? atoi(a[5].c_str()) : -1;
                const KeptLocation* kept = FindKept(location);
                int id = kept ? GrantKept(*kept) : GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
                std::string names = RestAfter(line, 6);
                size_t bar = names.find('|');
                std::string item = bar == std::string::npos ? names : names.substr(0, bar);
                std::string sender = bar == std::string::npos ? "" : names.substr(bar + 1);
                // An own item found in this save since it loaded was named by the found text already.
                bool namedWhenFound = sender.empty() && g_FoundSinceLoad.count(location) != 0 &&
                                      AP::HasLocationName(location);
                if (IsTrapKey(a[2])) QueueTrap(a[2], item, sender);
                else if (id >= 0 && !namedWhenFound) QueueShow(Show{id, item, sender, ""});
                SetRecvCount(n);
                have = n;
            }
            AP::Send("RECV " + std::to_string(have));
        } else if (a[0] == "SYNC") {
            if (!PlayerReady()) return false;
            AP::Send("RECV " + std::to_string(GetRecvCount()));
            SendAllChecks();
            if (EventFlags::CheckFlag(kGoalFlag) || LevelSystem::GetCurrentLevel() == kGoalLevel) AP::Send("GOAL");
            if (EventFlags::CheckFlag(AP::SIGN_FLAG)) AP::Send("SIGN");
            ReportHints(true);
        } else if (a[0] == "DEATH") {
            // A death from another player. Without a save (intro, title, file menus) or with Link dead already it
            // is dropped; otherwise it waits until Link is free (no event, message or pause menu) and no item display
            // is starting or showing.
            Game::Framework* fmwk = Game::GetFramework();
            auto* items = Game::Data::Inventory::Items();
            if (NoSaveLoaded(fmwk)) {
                Logging.Log("[AP] DeathLink dropped: no save loaded");
                AP::Send("DEATHDROPPED");
                return true;
            }
            if (fmwk->player == nullptr || items == nullptr) return false;
            if (items->Health <= 0) return true;
            if (!PlayerFree() || g_ItemEvent != ItemEvent::None) return false;
            // The game over flow skips the Secret Medicine and reports no death for it (patcher, APDeathLink).
            EventFlags::SetFlag(AP::DEATHLINK_FLAG, true);
            g_DeathLinkAt = NowMs();
            items->Health = 0;
            std::string source = RestAfter(line, 1);
            QueueNotice(source.empty() ? "DeathLink" : "DeathLink|from " + source);
            Logging.Log("[AP] DeathLink applied");
        } else if (a[0] == "NAME" && a.size() >= 2) {
            // NAME <local id> <item name>|<player name, empty for an own item>
            std::string names = RestAfter(line, 2);
            size_t bar = names.find('|');
            AP::SetLocationName(atoi(a[1].c_str()), names.substr(0, bar),
                                bar == std::string::npos ? "" : names.substr(bar + 1));
        } else if (a[0] == "CHECKED") {
            // CHECKED <local id>...: locations the server has as checked
            for (size_t i = 1; i < a.size(); i++) {
                int id = atoi(a[i].c_str());
                if (id >= 0 && id < (int)(kChecksBytes * 8)) g_ServerChecks[id / 8] |= (1 << (id % 8));
            }
            AP::MapDataChanged();
        } else if (a[0] == "INLOGIC") {
            // INLOGIC CLEAR, then INLOGIC <local id>...: the locations in logic for the items received
            if (a.size() >= 2 && a[1] == "CLEAR") {
                std::fill(g_InLogic.begin(), g_InLogic.end(), 0);
                g_HasLogic = true;
            }
            for (size_t i = 1; i < a.size(); i++) {
                int id = atoi(a[i].c_str());
                if (a[i] != "CLEAR" && id >= 0 && id < (int)(kChecksBytes * 8)) g_InLogic[id / 8] |= (1 << (id % 8));
            }
            AP::MapDataChanged();
        } else if (a[0] == "SERVER" && a.size() >= 2) {
            // SERVER 1|0: whether the client is logged into the slot
            g_ServerConnected = a[1] == "1";
        } else if (a[0] == "NOTICE" && a.size() >= 2) {
            // NOTICE <text>: a toast in the autosave popup; '|' starts a new line
            QueueNotice(RestAfter(line, 1));
        } else if (a[0] == "GHOST" || a[0] == "GHOSTS" || a[0] == "GHOSTNAMES") {
            AP::GhostNet::HandleLine(line);
        } else if (a[0] == "MUSIC") {
            AP::Music::HandleLine(a);
        } else if (a[0] == "MSG") {
            Logging.Log("[AP] %s", line.c_str() + 4);
#ifdef LASAP_TEST
        } else if (a[0] == "DBG") {
            return HandleDebug(a);
#endif
        } else {
            AP::Send("ERR unknown command " + a[0]);
        }
        return true;
    }
}

namespace AP {
    void Init(const std::string& seed_hash, int port) {
        if (!seed_hash.empty()) g_SeedHash = seed_hash;
        InstallTextHooks();
        Puppet::InstallHooks();
        Music::InstallHooks();
        ItemGetSequence::InstallAtFuncPtr(Game::Player_ItemGetSequence.Ptr());
        ItemModelTask::InstallAtFuncPtr(Game::ItemGet_ModelTask.Ptr());
        ModelResourceGet::InstallAtFuncPtr(Game::ModelResource_Get.Ptr());
        NodeMarkDirty::InstallAtFuncPtr(Game::Node_MarkDirty.Ptr());
        Popup::InstallHooks();
        exl::patch::CodePatcher(Game::Offset(kItemGetTextDelay)).WriteInst(exl::armv8::InstBitSet(kTextDelayQuarter));
        ItemGetFanfareDone::InstallAtFuncPtr(Game::ItemGet_FanfareDone.Ptr());
        LoadLocationNames();
        LoadMap();
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
        RecordCheck(local_id);
    }

    void OnItemAdded(int item_id, int index) {
        if (g_Granting) return;
        for (const auto& kept : g_Kept) {
            if (kept.item != item_id || (kept.index >= 0 && kept.index != index)) continue;
            RecordCheck(kept.local_id);
            g_KeptAddedItem = item_id;
            g_KeptAddedLocation = kept.local_id;
            g_KeptAddedAt = NowMs();
        }
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

    bool HasLogic() {
        return g_HasLogic;
    }

    bool IsServerConnected() {
        return g_ServerConnected;
    }

    bool IsInLogic(int local_id) {
        return local_id >= 0 && local_id < (int)(kChecksBytes * 8) && ((g_InLogic[local_id / 8] >> (local_id % 8)) & 1);
    }

#ifdef LASAP_TEST
    bool HandleTestLine(const std::string& line) { return HandleLine(line); }
    bool TestPlayerFree() { return PlayerFree(); }
    bool TestItemDisplayPending() { return !g_ShowQueue.empty() || g_ItemEvent != ItemEvent::None; }
    bool TestPlayerCarrying() { return PlayerCarrying(); }
    std::string TestScreenName() { return ScreenName(Game::GetFramework()); }
#endif

    bool IsLocationChecked(int local_id) {
        if (local_id < 0 || local_id >= (int)(kChecksBytes * 8)) return false;
        if (!g_ChecksLoaded) LoadChecks();
        return ((g_Checks[local_id / 8] | g_ServerChecks[local_id / 8]) >> (local_id % 8)) & 1;
    }

    bool IsLocationSkipped(int local_id) {
        if (local_id < 0 || local_id >= (int)(kChecksBytes * 8)) return false;
        if (!g_ChecksLoaded) LoadChecks();
        return (g_Skipped[local_id / 8] >> (local_id % 8)) & 1;
    }

    void SetLocationSkipped(int local_id, bool skipped) {
        if (local_id < 0 || local_id >= (int)(kChecksBytes * 8)) return;
        if (!g_ChecksLoaded) LoadChecks();
        u8 bit = 1 << (local_id % 8);
        g_Skipped[local_id / 8] = skipped ? g_Skipped[local_id / 8] | bit : g_Skipped[local_id / 8] & ~bit;
        WriteBits(g_SkippedPath, g_Skipped);
    }

}

namespace {
    // Levels where a warp would break what runs there: the final dungeon, the Chamber Dungeons and their editor, the
    // fishing pond, the trendy game, the ending and Marin's event levels.
    bool WarpMenuBlockedLevel(const std::string& level) {
        static const char* kBlocked[] = {"Lv09WindFishsEgg", "PanelEdit", "FishingPond", kCraneGameLevel, "Ending",
                                         "EventMarin01", "EventMarin02"};
        if (level.starts_with("Panel")) return true;
        for (const char* b : kBlocked) {
            if (level == b) return true;
        }
        return false;
    }

    // Gives a waiting Secret Medicine once Link, free and alive, has none (he used his, or it was never given).
    void TickMedicine() {
        auto* items = Game::Data::Inventory::Items();
        if (items == nullptr || items->ExtraItems.Medicine || NowMs() - g_MedicinePolled < kMedicinePollMs) return;
        g_MedicinePolled = NowMs();
        if (!PlayerReady() || !PlayerFree() || LinkDead()) return;
        int waiting = GetMedicineWaiting();
        if (waiting == 0) return;
        SetMedicineWaiting(waiting - 1);
        g_Granting = true;
        Inventory::AddItemID(kSecretMedicine, 1, -1);
        g_Granting = false;
        AP::Log("Secret Medicine given (" + std::to_string(waiting - 1) + " waiting)");
        QueueNotice("Secret Medicine|from your stock");
    }

    // Minus held long enough: opens the warp menu when the player is free, holds nothing and no item display
    // waits to start. The game does not tick this while paused (the map), so the menu needs Minus still held.
    void TickWarpMenu() {
        if (!g_WarpMenuWanted) return;
        g_WarpMenuWanted = false;
        if (g_MinusHeld && PlayerFree() && !PlayerCarrying() && !LinkDead() && g_ItemEvent == ItemEvent::None &&
            !WarpMenuBlockedLevel(LevelSystem::GetCurrentLevel())) {
            g_WarpMenuShown = RequestCommonEvent(AP::WARP_MENU_ENTRY);
            Logging.Log("[AP] warp menu %s", g_WarpMenuShown ? "opened" : "could not be requested");
        }
    }
}

namespace AP {
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
            if (!g_ChecksLoaded) LoadChecks();
            SendAllChecks();
            Send("LOG level " + g_LastLevel);  // the client passes it on to trackers
            for (auto& hint : g_Hints) hint.reported = false;  // sent again once the save is loaded
            GhostNet::Reset();
            Music::Report();  // the game's choice first; the client sends its own only when the game has none
        }

        TickConnection();
        GhostNet::Tick();
        Puppet::Tick();
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
        TickMap();
        Music::Tick(PlayerFree());
#ifdef LASAP_TEST
        Test::Tick();
#endif

        // Lines that need a loaded save wait in g_Deferred; everything else runs right away.
        if (!g_Deferred.empty() && PlayerReady()) {
            std::deque<std::string> retry;
            retry.swap(g_Deferred);
            for (const auto& line : retry) {
                if (!HandleLine(line)) Defer(line);
            }
        }
        std::string line;
        // up to 16 lines a frame, GHOST lines not counted (30 ghosts send up to 600 a second; each is cheap)
        for (int i = 0, all = 0; i < 16 && all < 1024 && PopLine(line); all++) {
            if (line.compare(0, 6, "GHOST ") != 0) i++;
            if (!HandleLine(line)) Defer(line);
        }
    }

    void ApplyInjectedInput(nn::hid::NpadBaseState* state) {
        u64 now = NowMs();
#ifdef LASAP_TEST
        Test::ApplyInput(state);
#endif
        ListUi::OnPad(state->mButtons._storage[0]);
        if (MapListTakesPad(state)) {
            // A Minus held as the list opened is let go: not a tap once released, nor the warp menu.
            g_MinusHeld = false;
            g_MinusTapUntil = 0;
            return;
        }
        // The game does not see Minus while it is held (TickWarpMenu). Each controller style is polled, so only a
        // poll that sees Minus counts; its release is noticed when no poll has seen it for a moment.
        u64& buttons = state->mButtons._storage[0];
        if (g_MinusHeld && !(buttons & kButtonMinus) && now - g_MinusLastSeen > kMinusReleasedMs) {
            // Released: a short press reaches the game unless the menu opened.
            g_MinusHeld = false;
            if (!g_WarpMenuShown) g_MinusTapUntil = now + kMinusTapMs;
        }
        if (buttons & kButtonMinus) {
            if (!g_MinusHeld) {
                g_MinusHeld = true;
                g_MinusLong = false;
                g_WarpMenuShown = false;
                g_MinusDownSince = now;
            }
            g_MinusLastSeen = now;
            if (!g_MinusLong && now - g_MinusDownSince >= kWarpMenuHoldMs) {
                g_MinusLong = true;
                g_WarpMenuWanted = true;
            }
            buttons &= ~kButtonMinus;
        }
        if (now < g_MinusTapUntil) buttons |= kButtonMinus;
    }
}
