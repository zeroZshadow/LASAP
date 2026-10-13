// Game-thread side of the AP bridge: applies received items, reports collected locations,
// and runs test-only debug commands. Tick() runs once per input poll on the game thread.
#include "AP/ap.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Hooks/Common/inventory.hpp"
#include "Hooks/Common/level.hpp"
#include "Game/framework.hpp"
#include "Game/Data/inventory.hpp"
#include "nn/fs.hpp"
#include <algorithm>
#include <bit>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace {
    constexpr int kRecvBits = 16;
    constexpr size_t kChecksBytes = 65536 / 8;
    // Bits for received heart pieces and seashells (the game has 32 and 50). The patcher gives the heart piece and
    // seashell actors their own bits above these, so receiving never hides one; the game counts over all 64 bits.
    constexpr int kHeartPieceBits = 32;
    constexpr int kSeashellBits = 50;
    // Time to wait after a level change before granting items.
    constexpr u64 kSettleMs = 1500;

    // The game polls input several times per frame, so timing uses the system counter (19.2 MHz).
    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    std::string g_SeedHash = "00000000";
    std::string g_ChecksPath;
    std::vector<u8> g_Checks(kChecksBytes, 0);
    bool g_ChecksLoaded = false;

    std::deque<std::string> g_Deferred;

    void Defer(const std::string& line) {
        // The client resends SYNC and ITEM lines, so keep one copy of each.
        if (std::find(g_Deferred.begin(), g_Deferred.end(), line) != g_Deferred.end()) return;
        if (g_Deferred.size() >= 64) g_Deferred.pop_front();
        g_Deferred.push_back(line);
    }

    std::string g_LastLevel;
    int g_LastHealth = -1;
    u64 g_LevelSince = 0;

    // Injected controller input (DBG input)
    u64 g_InjectButtons = 0;
    s32 g_InjectLX = 0, g_InjectLY = 0;
    u64 g_InjectUntil = 0;

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
        for (const auto& [id, name] : itemsDict) {
            if (name == key) return id;
        }
        return -1;
    }

    bool PlayerReady() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr && fmwk->mPlayer != nullptr && NowMs() - g_LevelSince >= kSettleMs;
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

    void Damage(int quarterHearts) {
        auto* items = *Game::Data::Inventory::Items;
        items->Health = items->Health > quarterHearts ? items->Health - quarterHearts : 1;
    }

    // Native item-get (Link holds the item up and the item text shows). This is the body of the event action
    // Link.GenericItemGetSequenceByKey; it only displays, the inventory is changed separately.
    // It returns the running action as a libc++ shared_ptr.
    struct StrView { const char* data; size_t len; };
    struct SharedPtr { void* ptr; void* ctrl; };
    using ReleaseWeakFn = void (*)(void* ctrl);
    constexpr uintptr_t kItemGetSequence = 0xd795d0;
    constexpr uintptr_t kSharedWeakReleaseWeak = 0x14311f0;
    // Offsets found by memory comparison (TESTING.md, "Reverse engineering").
    constexpr size_t kPlayerEventState = 0x4ed;   // u8, 0 while the player is free (no talk, event or cutscene)
    constexpr size_t kPlayerCarried = 0x19b0;     // pointer, set while Link holds something over his head
    constexpr size_t kFrameworkPaused = 0x510;    // u8, 1 while the pause menu is open
    constexpr size_t kFrameworkScreen = 0xb0;     // u8: 2 intro and title, 5 file menus, 0x0b level load, 0x17 level
    constexpr size_t kFrameworkIntro = 0x6d0;     // pointer, set only while the intro plays

    // Which screen the game shows, for test scripts.
    std::string ScreenName(Game::Framework* fmwk) {
        if (fmwk == nullptr) return "none";
        const u8* f = reinterpret_cast<const u8*>(fmwk);
        switch (f[kFrameworkScreen]) {
            case 0x02: return *reinterpret_cast<const u64*>(f + kFrameworkIntro) != 0 ? "intro" : "title";
            case 0x05: return "menu";
            case 0x0b: return "loading";
            case 0x17: return "level";
            default: return std::to_string(f[kFrameworkScreen]);
        }
    }
    constexpr u64 kItemGetTimeoutMs = 120000;
    constexpr u64 kItemEventStartMs = 3000;      // a requested item event that has not started by then is retried
    constexpr size_t kShowQueueMax = 16;
    constexpr size_t kShowFillerMax = 3;          // with this many waiting, filler is no longer shown
    constexpr u64 kFreeSettleMs = 600;            // the player must be free this long before an item-get starts
    constexpr int kItemEventTries = 3;

    int g_ItemEventTry = 0;
    u64 g_FreeSince = 0;
    bool g_WasFree = false;
    // A received item waiting to be shown; with a sender, it shows "You got <item> from <sender>!".
    // A trap (trap = its key) plays its effect instead; id is then -1.
    struct Show { int id; std::string item; std::string sender; std::string trap; };
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

    // Client connection notices
    constexpr u64 kNoClientNoticeMs = 20000;   // in a level this long without a client: say so, once per boot
    bool g_WasConnected = false;
    bool g_EverConnected = false;
    bool g_NoClientShown = false;

    void ReleaseShared(SharedPtr& sp) {
        if (sp.ctrl != nullptr) {
            auto* owners = reinterpret_cast<long*>(reinterpret_cast<u8*>(sp.ctrl) + 8);
            if (__atomic_fetch_sub(owners, 1, __ATOMIC_ACQ_REL) == 0) {
                auto** vtable = *reinterpret_cast<void (***)(void*)>(sp.ctrl);
                vtable[2](sp.ctrl);  // __on_zero_shared
                reinterpret_cast<ReleaseWeakFn>(g_BaseAddress + offset_manager.Offset(kSharedWeakReleaseWeak))(sp.ctrl);
            }
        }
        sp = SharedPtr{};
    }

    // Starting an event of Common.bfevfl, as the game does for its game over:
    // load the flowchart resource, make an event for the entry, prepare it and hand it to the event manager.
    constexpr uintptr_t kResourceManager = 0x1cc6db8;   // global: resource manager pointer
    constexpr uintptr_t kEventManager = 0x1cb24d8;      // global: event manager pointer
    constexpr uintptr_t kLoadResource = 0xfbbd80;       // (out handle, manager, StringView* path)
    constexpr uintptr_t kWaitResource = 0x3dc20;        // (resource)
    constexpr uintptr_t kResourceFlowchart = 0x3d9f0;   // (out flowchart, handle*)
    constexpr uintptr_t kCreateEvent = 0x80c290;        // (out event, flowchart*, StringView* entry)
    constexpr uintptr_t kPrepareEvent = 0x80db00;       // (event)
    constexpr uintptr_t kRequestEvent = 0x81f2e0;       // (out, event manager, event*)

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    void* GameGlobal(uintptr_t offset) {
        return *reinterpret_cast<void**>(g_BaseAddress + offset_manager.Offset(offset));
    }

    bool RequestCommonEvent(const char* entry) {
        void* resources = GameGlobal(kResourceManager);
        void* events = GameGlobal(kEventManager);
        if (resources == nullptr || events == nullptr) return false;
        static const char kPath[] = "event/Common.bfevfl";
        StrView path{kPath, sizeof(kPath) - 1};
        StrView name{entry, strlen(entry)};
        SharedPtr handle{}, flowchart{}, event{}, request{};
        GameFn<void (*)(SharedPtr*, void*, StrView*)>(kLoadResource)(&handle, resources, &path);
        if (handle.ptr != nullptr) GameFn<void (*)(void*)>(kWaitResource)(handle.ptr);
        GameFn<void (*)(SharedPtr*, SharedPtr*)>(kResourceFlowchart)(&flowchart, &handle);
        bool ok = false;
        if (flowchart.ptr != nullptr) {
            GameFn<void (*)(SharedPtr*, SharedPtr*, StrView*)>(kCreateEvent)(&event, &flowchart, &name);
            if (event.ptr != nullptr) {
                GameFn<void (*)(void*)>(kPrepareEvent)(event.ptr);
                GameFn<void (*)(SharedPtr*, void*, SharedPtr*)>(kRequestEvent)(&request, events, &event);
                ok = true;
            }
        }
        ReleaseShared(request);
        ReleaseShared(event);
        ReleaseShared(flowchart);
        ReleaseShared(handle);
        return ok;
    }

    // Plays an item-get of itemId with that message entry ("" for the item's own text) as the item event.
    bool StartItemEvent(int itemId, const std::string& messageEntry) {
        g_ItemEventId = itemId;
        g_ItemEventMessage = messageEntry;
        g_ItemEventShown = -1;
        Logging.Log("[AP] item event " + std::to_string(itemId) + " request");
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
        auto* items = *Game::Data::Inventory::Items;
        if (damage < 0) {
            Damage(4);
            return true;
        }
        if (global_config.damage.mode == "normal" && items->Health <= damage) items->Health = damage + 1;
        std::string entry = std::string(AP::TRAP_EVENT_PREFIX) + key;
        g_ItemEventId = -1;
        g_ItemEventSwap = false;
        Logging.Log("[AP] trap event " + key + " request");
        if (!RequestCommonEvent(entry.c_str())) return false;
        g_ItemEvent = ItemEvent::Requested;
        g_ItemEventSince = NowMs();
        return true;
    }

    // True when the player can move: no talk, event, cutscene or pause menu.
    bool PlayerFree() {
        Game::Framework* fmwk = Game::GetFramework();
        if (!PlayerReady()) return false;
        if (reinterpret_cast<u8*>(fmwk)[kFrameworkPaused] != 0) return false;
        return reinterpret_cast<u8*>(fmwk->mPlayer)[kPlayerEventState] == 0;
    }

    // An item-get takes what Link holds over his head out of the game, so items wait until he puts it down.
    bool PlayerCarrying() {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->mPlayer == nullptr) return false;
        return *reinterpret_cast<void**>(reinterpret_cast<u8*>(fmwk->mPlayer) + kPlayerCarried) != nullptr;
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

    // A toast in the autosave popup; '|' starts a new line.
    void QueueNotice(std::string text) {
        for (auto& c : text) if (c == '|') c = '\n';
        AP::QueueToast(text);
    }

    // Tells the player when the client connects, goes away, or never came.
    void TickConnection() {
        bool connected = AP::IsClientConnected();
        if (g_WasConnected && !connected) {
            QueueNotice("Archipelago client|disconnected");
        }
        if (connected) g_EverConnected = true;
        g_WasConnected = connected;
        if (!g_EverConnected && !g_NoClientShown && PlayerFree() && g_LevelSince != 0 &&
            NowMs() - g_LevelSince > kNoClientNoticeMs) {
            g_NoClientShown = true;
            QueueNotice("Archipelago client|not connected");
        }
    }

    void QueueShow(const Show& show) {
        if (g_ShowQueue.size() >= kShowQueueMax) return;
        if (g_ShowQueue.size() >= kShowFillerMax && IsFiller(show.id)) return;
        g_ShowQueue.push_back(show);
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
                Logging.Log("[AP] item event " + std::to_string(g_ItemEventId) + " did not start");
                if ((g_Showing.id >= 0 || !g_Showing.trap.empty()) && ++g_ItemEventTry < kItemEventTries) {
                    g_ShowQueue.push_front(g_Showing);
                }
                else g_ItemEventTry = 0;
                break;
            case ItemEvent::Running:
                if (!free && since < kItemGetTimeoutMs) return true;
                Logging.Log("[AP] item event " + std::to_string(g_ItemEventId) + " ended after " + std::to_string(since) + " ms");
                g_ItemEventTry = 0;
                break;
        }
        g_ItemEvent = ItemEvent::None;
        g_ItemEventSwap = false;
        return false;
    }

    // Shows queued items one at a time, each as an event, once the player is free and holds nothing.
    void TickItemGet() {
        bool free = PlayerFree();
        if (free && !g_WasFree) g_FreeSince = NowMs();
        if (free) AP::ExpireFoundLocation();
        g_WasFree = free;
        if (ItemEventActive(free)) {
            g_WasFree = false;  // the settle time starts again after each item-get
            return;
        }
        if (g_ShowQueue.empty() || !free || PlayerCarrying() || NowMs() - g_FreeSince < kFreeSettleMs) return;
        g_Showing = g_ShowQueue.front();
        g_ShowQueue.pop_front();
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

    // The item event's item-get shows the placeholder with RECEIVED_LABEL as message entry; the requested item
    // and its message take their place.
    HOOK_DEFINE_TRAMPOLINE(ItemGetSequence) {
        static void Callback(SharedPtr* out, void* player, int itemId, bool keepCarry, StrView* messageEntry) {
            size_t len = strlen(AP::RECEIVED_LABEL);
            if (g_ItemEventSwap && g_ItemEvent != ItemEvent::None && itemId == AP::PLACEHOLDER_ITEM_ID &&
                messageEntry != nullptr && messageEntry->len == len &&
                memcmp(messageEntry->data, AP::RECEIVED_LABEL, len) == 0) {
                StrView message{g_ItemEventMessage.c_str(), g_ItemEventMessage.size()};
                g_ItemEventSwap = false;
                g_ItemEventShown = g_ItemEventId;
                if (g_ItemEvent == ItemEvent::Requested) g_ItemEventSince = NowMs();
                g_ItemEvent = ItemEvent::Running;
                Logging.Log("[AP] item event " + std::to_string(g_ItemEventId) + " start");
                Orig(out, player, g_ItemEventId, keepCarry, &message);
                return;
            }
            Orig(out, player, itemId, keepCarry, messageEntry);
        }
    };

    bool IsTrapKey(const std::string& key) { return key.ends_with("Trap") || key == "$ENEMY"; }

    // A received trap waits like an item display, then plays its effect (StartTrapEvent).
    void QueueTrap(const std::string& key, const std::string& item, const std::string& sender) {
        QueueShow(Show{-1, item.empty() ? key : item, sender, key});
    }

    // Grants one item the way an in-game pickup would, including LASR's progressive items.
    // Returns the item id granted, or -1 for traps (QueueTrap) and unknown keys.
    int GrantItem(const std::string& key, int count, int dungeon) {
        auto* items = *Game::Data::Inventory::Items;
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
            case 22: index = LowestClearBit(items->Seashells, kSeashellBits); break; // Seashell
            case 23: index = LowestClearBit(items->GoldenLeaves, 5); break;    // GoldenLeaf
            case 58: index = LowestClearBit(items->HeartPieces, kHeartPieceBits); break; // HeartPiece
            case 59: index = LowestClearBit(items->HeartContainers, 16); break; // HeartContainer
            case 44: case 45: case 47: case 48: case 49: index = dungeon; break; // dungeon items
            default: break;
        }
        Inventory::AddItemID(id, count, index);
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

    void LoadChecks() {
        g_ChecksPath = "sd:/lasap/" + g_SeedHash + ".chk";
        nn::fs::CreateDirectory("sd:/lasap");
        nn::fs::FileHandle h{};
        if (R_SUCCEEDED(nn::fs::OpenFile(&h, g_ChecksPath.c_str(), nn::fs::OpenMode_Read))) {
            nn::fs::ReadFile(h, 0, g_Checks.data(), kChecksBytes);
            nn::fs::CloseFile(h);
        }
        g_ChecksLoaded = true;
    }

    void SaveChecks() {
        nn::fs::FileHandle h{};
        if (R_FAILED(nn::fs::OpenFile(&h, g_ChecksPath.c_str(), nn::fs::OpenMode_Write))) {
            nn::fs::CreateFile(g_ChecksPath.c_str(), kChecksBytes);
            if (R_FAILED(nn::fs::OpenFile(&h, g_ChecksPath.c_str(), nn::fs::OpenMode_Write))) {
                AP::Log("cannot write " + g_ChecksPath);
                return;
            }
        }
        nn::fs::WriteFile(h, 0, g_Checks.data(), kChecksBytes, nn::fs::WriteOption{nn::fs::WriteOptionFlag_Flush});
        nn::fs::CloseFile(h);
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

    std::string HexU64(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

    std::string StateLine() {
        Game::Framework* fmwk = Game::GetFramework();
        auto* items = *Game::Data::Inventory::Items;
        std::string s = "STATE level=" + LevelSystem::GetCurrentLevel();
        s += " screen=" + ScreenName(fmwk);
        s += " ms_in_level=" + std::to_string(NowMs() - g_LevelSince);
        s += " player=" + std::string(fmwk && fmwk->mPlayer ? "1" : "0");
        if (items != nullptr) {
            s += " health=" + std::to_string(items->Health);
            s += " rupees=" + std::to_string(items->Rupees);
            s += " shells=" + HexU64(items->Seashells);
            s += " hp=" + HexU64(items->HeartPieces);
            s += " hc=" + std::to_string(items->HeartContainers);
            s += " leaves=" + std::to_string(items->GoldenLeaves);
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
        if (fmwk && fmwk->mPlayer && fmwk->mPlayer->playerCollision) {
            auto& p = fmwk->mPlayer->playerCollision->coordsNew.pos;
            char buf[96];
            snprintf(buf, sizeof(buf), " pos=%.2f,%.2f,%.2f", p.x, p.y, p.z);
            s += buf;
        }
        s += " recv=" + std::to_string(GetRecvCount());
        if (fmwk && fmwk->mPlayer) {
            s += " event=" + std::to_string(reinterpret_cast<u8*>(fmwk->mPlayer)[kPlayerEventState]);
            s += " paused=" + std::to_string(reinterpret_cast<u8*>(fmwk)[kFrameworkPaused]);
            s += std::string(" carry=") + (PlayerCarrying() ? "1" : "0");
        }
        s += " showq=" + std::to_string(g_ShowQueue.size()) + (g_ItemEvent != ItemEvent::None ? "+1" : "");
        s += " shown=" + std::to_string(g_ItemEventShown);
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
        } else if (cmd == "warp" && a.size() >= 6) {
            // DBG warp <level> <x> <y> <z>
            if (!PlayerReady()) return false;
            LevelSystem::LoadLevel(a[2].c_str(), Game::Vec3{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())});
            AP::Send("OK warp");
        } else if (cmd == "pos" && a.size() >= 5) {
            // Moves the player within the current level: DBG pos <x> <y> <z>
            if (!PlayerReady()) return false;
            // Setting the respawn point too means a fall into a room that is not loaded yet lands at the target.
            auto* player = Game::GetFramework()->mPlayer;
            auto* col = player->playerCollision;
            Game::Vec3 target{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
            col->coordsNew.pos = target;
            col->coords.pos = target;
            col->needsCoordUpdate = true;
            player->respawnCoords.pos = target;
            AP::Send("OK pos");
        } else if (cmd == "input" && a.size() >= 4) {
            // DBG input <buttons hex> <milliseconds> [lx ly]
            g_InjectButtons = strtoull(a[2].c_str(), nullptr, 16);
            g_InjectUntil = NowMs() + atoi(a[3].c_str());
            g_InjectLX = a.size() >= 6 ? atoi(a[4].c_str()) : 0;
            g_InjectLY = a.size() >= 6 ? atoi(a[5].c_str()) : 0;
            AP::Send("OK input");
        } else if (cmd == "setrecv" && a.size() >= 3) {
            SetRecvCount(atoi(a[2].c_str()));
            AP::Send("RECV " + std::to_string(GetRecvCount()));
        } else if (cmd == "clearchecks") {
            std::fill(g_Checks.begin(), g_Checks.end(), 0);
            SaveChecks();
            AP::Send("OK clearchecks");
        } else if (cmd == "text") {
            AP::Send(AP::TextState());
        } else if (cmd == "peek" && a.size() >= 5) {
            // DBG peek <p|f|i> <hex offset> <hex length>: bytes of the player, the framework or the inventory
            Game::Framework* fmwk = Game::GetFramework();
            const u8* base = nullptr;
            if (a[2] == "p") base = fmwk ? reinterpret_cast<const u8*>(fmwk->mPlayer) : nullptr;
            else if (a[2] == "f") base = reinterpret_cast<const u8*>(fmwk);
            else if (a[2] == "i") base = *reinterpret_cast<const u8**>(g_symbols["InventoryPtr"]);
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
            u8* base = *reinterpret_cast<u8**>(g_symbols["InventoryPtr"]);
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
            reinterpret_cast<void (*)(bool)>(g_BaseAddress + offset_manager.Offset(0xf895c0))(true);
            AP::Send("OK savepopup");
        } else if (cmd == "collect" && a.size() >= 3) {
            // Simulates picking up a placeholder: DBG collect <local_id>
            AP::OnPlaceholderCollected(atoi(a[2].c_str()));
        } else {
            AP::Send("ERR unknown debug command");
        }
        return true;
    }

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
                int id = GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
                int location = a.size() >= 6 ? atoi(a[5].c_str()) : -1;
                std::string names = RestAfter(line, 6);
                size_t bar = names.find('|');
                std::string item = bar == std::string::npos ? names : names.substr(0, bar);
                std::string sender = bar == std::string::npos ? "" : names.substr(bar + 1);
                // An own item found here was named by the found text already.
                bool namedWhenFound = sender.empty() && location >= 0 && AP::HasLocationName(location);
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
            if (EventFlags::CheckFlag("ShadowClear")) AP::Send("GOAL");
        } else if (a[0] == "DEATH") {
            if (!PlayerReady()) return false;
            (*Game::Data::Inventory::Items)->Health = 0;
        } else if (a[0] == "NAME" && a.size() >= 2) {
            // NAME <local id> <item name>|<player name, empty for an own item>
            std::string names = RestAfter(line, 2);
            size_t bar = names.find('|');
            AP::SetLocationName(atoi(a[1].c_str()), names.substr(0, bar),
                                bar == std::string::npos ? "" : names.substr(bar + 1));
        } else if (a[0] == "NOTICE" && a.size() >= 2) {
            // NOTICE <text>: a toast in the autosave popup; '|' starts a new line
            QueueNotice(RestAfter(line, 1));
        } else if (a[0] == "MSG") {
            Logging.Log("[AP] " + line.substr(4));
        } else if (a[0] == "DBG") {
            return HandleDebug(a);
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
        ItemGetSequence::InstallAtOffset(offset_manager.Offset(kItemGetSequence));
        LoadLocationNames();
        StartNetwork(port > 0 ? port : PORT);
    }

    std::string HelloLine() {
        return "HELLO " + std::to_string(PROTOCOL_VERSION) + " " + g_SeedHash;
    }

    void Log(const std::string& text) {
        Logging.Log("[AP] " + text);
        Send("LOG " + text);
    }

    void OnPlaceholderCollected(int local_id) {
        SetFoundLocation(local_id);
        if (!g_ChecksLoaded) LoadChecks();
        if (local_id >= 0 && local_id < (int)(kChecksBytes * 8)) {
            g_Checks[local_id / 8] |= (1 << (local_id % 8));
            SaveChecks();
        }
        Logging.Log("[AP] collected location " + std::to_string(local_id));
        Send("CHECK " + std::to_string(local_id));
    }

    void OnGoal() {
        Logging.Log("[AP] goal reached");
        Send("GOAL");
    }

}

namespace {
    // Levels where a warp would break what runs there: the final dungeon, the Chamber Dungeons and their editor, the
    // fishing pond, the trendy game, the ending and Marin's event levels.
    bool WarpMenuBlockedLevel(const std::string& level) {
        static const char* kBlocked[] = {"Lv09WindFishsEgg", "PanelEdit", "FishingPond", "CraneGame", "Ending",
                                         "EventMarin01", "EventMarin02"};
        if (level.starts_with("Panel")) return true;
        for (const char* b : kBlocked) {
            if (level == b) return true;
        }
        return false;
    }

    // Minus held long enough: opens the warp menu when the player is free, holds nothing and no item display
    // waits to start. The game does not tick this while paused (the map), so the menu needs Minus still held.
    void TickWarpMenu() {
        if (!g_WarpMenuWanted) return;
        g_WarpMenuWanted = false;
        if (g_MinusHeld && PlayerFree() && !PlayerCarrying() && g_ItemEvent == ItemEvent::None &&
            !WarpMenuBlockedLevel(LevelSystem::GetCurrentLevel())) {
            g_WarpMenuShown = RequestCommonEvent(AP::WARP_MENU_ENTRY);
            Logging.Log(std::string("[AP] warp menu ") + (g_WarpMenuShown ? "opened" : "could not be requested"));
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
        }

        // DeathLink: report the moment health reaches zero.
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk != nullptr && fmwk->mPlayer != nullptr && *Game::Data::Inventory::Items != nullptr) {
            int health = (*Game::Data::Inventory::Items)->Health;
            if (health <= 0 && g_LastHealth > 0) Send("DIED");
            g_LastHealth = health;
        }

        if (TakeNewConnection()) {
            if (!g_ChecksLoaded) LoadChecks();
            SendAllChecks();
            Send("LOG level " + g_LastLevel);  // the client passes it on to trackers
        }

        TickConnection();
        AP::TickToasts(PlayerReady());
        TickItemGet();
        TickWarpMenu();

        // Lines that need a loaded save wait in g_Deferred; everything else runs right away.
        if (!g_Deferred.empty() && PlayerReady()) {
            std::deque<std::string> retry;
            retry.swap(g_Deferred);
            for (const auto& line : retry) {
                if (!HandleLine(line)) Defer(line);
            }
        }
        std::string line;
        for (int i = 0; i < 16 && PopLine(line); i++) {
            if (!HandleLine(line)) Defer(line);
        }
    }

    void ApplyInjectedInput(nn::hid::NpadBaseState* state) {
        u64 now = NowMs();
        if (now < g_InjectUntil) {
            state->mButtons._storage[0] |= g_InjectButtons;
            if (g_InjectLX != 0 || g_InjectLY != 0) {
                state->mAnalogStickL.mX = g_InjectLX;
                state->mAnalogStickL.mY = g_InjectLY;
            }
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
