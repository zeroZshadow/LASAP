// Checks (DESIGN.md, Checks): the locations collected here (saved per seed and reported), those the server has as
// checked or in logic and those the player marked skipped, for the map tracker; and the in-game hints read.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/time.hpp"
#include "nn/fs.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace AP::Bridge {
namespace {
    using Game::NowMs;

    constexpr size_t kChecksBytes = 65536 / 8;

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

    // In-game hints the client makes server hints of (HINTS_PATH): the flag reading the hint sets, and the hinted
    // location as "<slot> <location id>". Reported once per connection, and all again on SYNC.
    struct HintFlag { std::string flag; std::string target; bool reported; };
    std::vector<HintFlag> g_Hints;
    u64 g_HintFlagsPolled = 0;
    constexpr u64 kHintFlagPollMs = 500;

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

    void SendAllChecks() {
        if (!g_ChecksLoaded) LoadChecks();
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

    void NoteServerChecks(const std::vector<std::string>& a) {
        for (size_t i = 1; i < a.size(); i++) {
            int id = atoi(a[i].c_str());
            if (id >= 0 && id < (int)(kChecksBytes * 8)) g_ServerChecks[id / 8] |= (1 << (id % 8));
        }
    }

    void NoteInLogic(const std::vector<std::string>& a) {
        if (a.size() >= 2 && a[1] == "CLEAR") {
            std::fill(g_InLogic.begin(), g_InLogic.end(), 0);
            g_HasLogic = true;
        }
        for (size_t i = 1; i < a.size(); i++) {
            int id = atoi(a[i].c_str());
            if (a[i] != "CLEAR" && id >= 0 && id < (int)(kChecksBytes * 8)) g_InLogic[id / 8] |= (1 << (id % 8));
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
        if (g_Hints.empty() || !PlayerReady() || NowMs() - g_HintFlagsPolled < kHintFlagPollMs) return;
        g_HintFlagsPolled = NowMs();
        ReportHints(false);
    }

    void ResetHints() {
        for (auto& hint : g_Hints) hint.reported = false;
    }

#ifdef LASAP_TEST
    void ClearChecks() {
        std::fill(g_Checks.begin(), g_Checks.end(), 0);
        SaveChecks();
    }
#endif
}

namespace AP {
    using namespace Bridge;

    bool HasLogic() {
        return g_HasLogic;
    }

    bool IsInLogic(int local_id) {
        return local_id >= 0 && local_id < (int)(kChecksBytes * 8) && ((g_InLogic[local_id / 8] >> (local_id % 8)) & 1);
    }

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
