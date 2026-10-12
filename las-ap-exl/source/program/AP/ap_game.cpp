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

    // Grants one item the way an in-game pickup would, including LASR's progressive items.
    void GrantItem(const std::string& key, int count, int dungeon) {
        auto* items = *Game::Data::Inventory::Items;
        std::string k = key;
        if (k.ends_with("Trap") || k == "$ENEMY") {
            // Traps from other worlds cost one heart; they never kill.
            Damage(4);
            return;
        }
        if (k == "SwordLv1" && EventFlags::CheckFlag("SwordFoundFlag")) k = "SwordLv2";
        else if (k == "Shield" && EventFlags::CheckFlag("ShieldFoundFlag")) k = "MirrorShield";
        else if (k == "PowerBraceletLv1" && EventFlags::CheckFlag("BraceletFoundFlag")) k = "PowerBraceletLv2";
        else if (k == "ClothesRed") EventFlags::SetFlag("RedTunicFoundFlag", true);
        else if (k == "ClothesBlue") EventFlags::SetFlag("BlueTunicFoundFlag", true);

        int id = ItemIdFromKey(k);
        if (id < 0) {
            AP::Log("unknown item key " + key);
            return;
        }

        int index = -1;
        switch (id) {
            case 22: index = LowestClearBit(items->Seashells, 64); break;      // Seashell
            case 23: index = LowestClearBit(items->GoldenLeaves, 5); break;    // GoldenLeaf
            case 58: index = LowestClearBit(items->HeartPieces, 64); break;    // HeartPiece
            case 59: index = LowestClearBit(items->HeartContainers, 16); break; // HeartContainer
            case 44: case 45: case 47: case 48: case 49: index = dungeon; break; // dungeon items
            default: break;
        }
        Inventory::AddItemID(id, count, index);
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
            // Same path as ITEM without touching the received counter: DBG grant <key> <count> <dungeon>
            if (!PlayerReady()) return false;
            GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
            AP::Send("OK grant");
        } else if (cmd == "warp" && a.size() >= 6) {
            // DBG warp <level> <x> <y> <z>
            if (!PlayerReady()) return false;
            LevelSystem::LoadLevel(a[2].c_str(), Game::Vec3{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())});
            AP::Send("OK warp");
        } else if (cmd == "pos" && a.size() >= 5) {
            // Moves the player within the current level: DBG pos <x> <y> <z>
            if (!PlayerReady()) return false;
            auto* col = Game::GetFramework()->mPlayer->playerCollision;
            col->coordsNew.pos = Game::Vec3{(float)atof(a[2].c_str()), (float)atof(a[3].c_str()), (float)atof(a[4].c_str())};
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
                GrantItem(a[2], atoi(a[3].c_str()), atoi(a[4].c_str()));
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
        if (NowMs() >= g_InjectUntil) return;
        state->mButtons._storage[0] |= g_InjectButtons;
        if (g_InjectLX != 0 || g_InjectLY != 0) {
            state->mAnalogStickL.mX = g_InjectLX;
            state->mAnalogStickL.mY = g_InjectLY;
        }
    }
}
