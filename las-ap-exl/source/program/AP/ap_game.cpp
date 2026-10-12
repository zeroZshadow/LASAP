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
    using ItemGetSequenceFn = void (*)(SharedPtr* out, void* player, int itemId, bool keepCarry, StrView* messageEntry);
    using ReleaseWeakFn = void (*)(void* ctrl);
    constexpr uintptr_t kItemGetSequence = 0xd795d0;
    constexpr uintptr_t kSharedWeakReleaseWeak = 0x14311f0;
    // Offsets found by memory comparison (TESTING.md, "Reverse engineering").
    constexpr size_t kPlayerEventState = 0x4ed;   // u8, 0 while the player is free (no talk, event or cutscene)
    constexpr size_t kFrameworkPaused = 0x510;    // u8, 1 while the pause menu is open
    constexpr size_t kActionState = 0x38;         // u32 of the item-get action
    constexpr u32 kActionDone = 0x2a;             // ... once its text is closed
    constexpr u64 kItemGetTimeoutMs = 120000;
    constexpr size_t kShowQueueMax = 16;
    constexpr size_t kShowFillerMax = 3;          // with this many waiting, filler is no longer shown
    constexpr u64 kFreeSettleMs = 600;            // the player must be free this long before an item-get starts
    constexpr u64 kItemGetMinMs = 1500;           // an item-get done sooner was cancelled by the game
    constexpr int kItemGetTries = 3;
    constexpr u64 kTextButtons = 0x3;             // A, B
    constexpr u64 kButtonB = 0x2;

    SharedPtr g_ItemGetAction{};
    int g_ItemGetId = -1;
    int g_ItemGetTry = 0;
    u64 g_ItemGetSince = 0;
    u64 g_FreeSince = 0;
    bool g_WasFree = false;
    // A received item waiting to be shown; with a sender, it shows "You got <item> from <sender>!".
    struct Show { int id; std::string item; std::string sender; };
    std::deque<Show> g_ShowQueue;
    Show g_Showing{};

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

    bool StartItemGet(int itemId, const std::string& messageEntry) {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->mPlayer == nullptr) return false;
        auto fn = reinterpret_cast<ItemGetSequenceFn>(g_BaseAddress + offset_manager.Offset(kItemGetSequence));
        StrView msg{messageEntry.c_str(), messageEntry.size()};
        SharedPtr out{};
        fn(&out, fmwk->mPlayer, itemId, false, &msg);
        if (out.ptr == nullptr) {
            ReleaseShared(out);
            return false;
        }
        g_ItemGetAction = out;
        g_ItemGetSince = NowMs();
        return true;
    }

    bool ItemGetRunning() {
        if (g_ItemGetAction.ptr == nullptr) return false;
        u32 state = *reinterpret_cast<u32*>(reinterpret_cast<u8*>(g_ItemGetAction.ptr) + kActionState);
        u64 ran = NowMs() - g_ItemGetSince;
        if (state != kActionDone && ran < kItemGetTimeoutMs) return true;
        ReleaseShared(g_ItemGetAction);
        Logging.Log("[AP] item-get " + std::to_string(g_ItemGetId) + " ended after " + std::to_string(ran) + " ms, state " + std::to_string(state));
        if (state == kActionDone && ran < kItemGetMinMs && ++g_ItemGetTry < kItemGetTries) {
            g_ShowQueue.push_front(g_Showing);
        } else {
            g_ItemGetTry = 0;
        }
        return false;
    }

    // True when the player can move: no talk, event, cutscene or pause menu.
    bool PlayerFree() {
        Game::Framework* fmwk = Game::GetFramework();
        if (!PlayerReady()) return false;
        if (reinterpret_cast<u8*>(fmwk)[kFrameworkPaused] != 0) return false;
        return reinterpret_cast<u8*>(fmwk->mPlayer)[kPlayerEventState] == 0;
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

    void QueueShow(const Show& show) {
        if (g_ShowQueue.size() >= kShowQueueMax) return;
        if (g_ShowQueue.size() >= kShowFillerMax && IsFiller(show.id)) return;
        g_ShowQueue.push_back(show);
    }

    // Shows queued items one at a time while the player is free.
    void TickItemGet() {
        bool free = PlayerFree();
        if (free && !g_WasFree) g_FreeSince = NowMs();
        if (free) AP::ExpireFoundLocation();
        g_WasFree = free;
        if (ItemGetRunning()) {
            g_WasFree = false;  // the settle time starts again after each item-get
            return;
        }
        if (g_ShowQueue.empty() || !free || NowMs() - g_FreeSince < kFreeSettleMs) return;
        g_Showing = g_ShowQueue.front();
        g_ShowQueue.pop_front();
        int id = g_Showing.id;
        g_ItemGetId = id;
        Logging.Log("[AP] item-get " + std::to_string(id) + " start");
        std::string message;
        if (!g_Showing.sender.empty() || HasSourceText(id)) {
            AP::SetReceivedText(g_Showing.item, g_Showing.sender);
            message = AP::RECEIVED_LABEL;
        }
        if (!StartItemGet(id, message)) AP::Log("item-get " + std::to_string(id) + " did not start");
    }

    // Grants one item the way an in-game pickup would, including LASR's progressive items.
    // Returns the item id granted, or -1 for traps and unknown keys.
    int GrantItem(const std::string& key, int count, int dungeon) {
        auto* items = *Game::Data::Inventory::Items;
        std::string k = key;
        if (k.ends_with("Trap") || k == "$ENEMY") {
            // Traps from other worlds cost one heart; they never kill.
            Damage(4);
            return -1;
        }
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
        }
        s += " showq=" + std::to_string(g_ShowQueue.size()) + (g_ItemGetAction.ptr ? "+1" : "");
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
            if (id >= 0 && (a.size() < 6 || a[5] != "0")) QueueShow(Show{id, "", ""});
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
            // DBG peek <p|f|i|a|c> <hex offset> <hex length>: bytes of the player, the framework the level-load data, or the last item-get action and its control block
            Game::Framework* fmwk = Game::GetFramework();
            const u8* base = nullptr;
            if (a[2] == "p") base = fmwk ? reinterpret_cast<const u8*>(fmwk->mPlayer) : nullptr;
            else if (a[2] == "f") base = reinterpret_cast<const u8*>(fmwk);
            else if (a[2] == "i") base = *reinterpret_cast<const u8**>(g_symbols["InventoryPtr"]);
            else if (a[2] == "a") base = reinterpret_cast<const u8*>(g_ItemGetAction.ptr);
            else if (a[2] == "c") base = reinterpret_cast<const u8*>(g_ItemGetAction.ctrl);
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
            // DBG itemget <item key> [message entry]
            if (!PlayerReady()) return false;
            if (ItemGetRunning()) {
                AP::Send("ERR itemget: a display is running");
                return true;
            }
            int id = ItemIdFromKey(a[2]);
            bool ok = id >= 0 && StartItemGet(id, a.size() >= 4 ? a[3] : "");  // bypasses the queue
            AP::Send(std::string(ok ? "OK" : "ERR") + " itemget " + std::to_string(id) + " action=" + HexU64((u64)g_ItemGetAction.ptr));
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
                if (id >= 0 && !namedWhenFound) QueueShow(Show{id, item, sender});
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
    void Init(const std::string& seed_hash) {
        if (!seed_hash.empty()) g_SeedHash = seed_hash;
        InstallTextHooks();
        StartNetwork();
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

    void Tick() {
        std::string level = LevelSystem::GetCurrentLevel();
        if (level != g_LastLevel) {
            g_LastLevel = level;
            g_LevelSince = NowMs();
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
        }

        TickItemGet();

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
        if (NowMs() < g_InjectUntil) {
            state->mButtons._storage[0] |= g_InjectButtons;
            if (g_InjectLX != 0 || g_InjectLY != 0) {
                state->mAnalogStickL.mX = g_InjectLX;
                state->mAnalogStickL.mY = g_InjectLY;
            }
        }
        // An item-get started outside an event does not lock the player, so only the text buttons get through.
        // A becomes B: the press that closes the text would otherwise also talk to or lift what Link faces.
        if (g_ItemGetAction.ptr != nullptr) {
            u64 buttons = state->mButtons._storage[0];
            state->mButtons._storage[0] = (buttons & kTextButtons) ? kButtonB : 0;
            state->mAnalogStickL.mX = state->mAnalogStickL.mY = 0;
            state->mAnalogStickR.mX = state->mAnalogStickR.mY = 0;
        }
    }
}
