// Checks (DESIGN.md, Checks): the locations collected here (saved per seed and reported), those the server has as
// checked or in logic and those the player marked skipped, for the map tracker; and the in-game hints read and the
// dungeons entered.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/time.hpp"
#include "nn/fs.hpp"
#include <algorithm>
#include <optional>
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
    // Dungeon locations whose dungeon's door is reachable for the items received (DOORS lines), for the map tracker.
    std::vector<u8> g_DoorOpen(kChecksBytes, 0);

    // Flags of the save reported to the client by a line once they are set: once per connection, and all again on
    // SYNC.
    struct ReportedFlag { std::string flag; std::string line; bool reported; };
    // In-game hints the client makes server hints of (HINTS_PATH): the flag reading the hint sets, reported as
    // HINT <slot> <location id>.
    std::vector<ReportedFlag> g_Hints;
    // The dungeons entered: the flag the game sets the first time it shows a dungeon's name on entering it, which also
    // shows its name on the map (map tracker), reported as DUNGEON <n> by the dungeon's level number (Lv<nn>). The
    // client tells trackers the door that leads into it.
    std::vector<ReportedFlag> g_Dungeons = {
        {"UI_FieldMapIcon_Lv01Dungeon", "DUNGEON 1", false}, {"UI_FieldMapIcon_Lv02Dungeon", "DUNGEON 2", false},
        {"UI_FieldMapIcon_Lv03Dungeon", "DUNGEON 3", false}, {"UI_FieldMapIcon_Lv04Dungeon", "DUNGEON 4", false},
        {"UI_FieldMapIcon_Lv05Dungeon", "DUNGEON 5", false}, {"UI_FieldMapIcon_Lv06Dungeon", "DUNGEON 6", false},
        {"UI_FieldMapIcon_Lv07Dungeon", "DUNGEON 7", false}, {"UI_FieldMapIcon_Lv08Dungeon", "DUNGEON 8", false},
        {"UI_FieldMapIcon_Lv10Dungeon", "DUNGEON 10", false},
    };
    u64 g_FlagsPolled = 0;
    constexpr u64 kFlagPollMs = 500;

    void Report(std::vector<ReportedFlag>& flags, bool all) {
        for (auto& f : flags) {
            if ((all || !f.reported) && EventFlags::CheckFlag(f.flag.c_str())) {
                AP::Send(f.line);
                f.reported = true;
            }
        }
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

    // A location's bit in the bit sets: its byte's index and its mask; none for an id out of their range
    struct Bit { size_t byte; u8 mask; };
    std::optional<Bit> BitOf(LocationId location) {
        int i = std::to_underlying(location);
        if (i < 0 || i >= (int)(kChecksBytes * 8)) return std::nullopt;
        return Bit{static_cast<size_t>(i / 8), static_cast<u8>(1 << (i % 8))};
    }

    bool Has(const std::vector<u8>& bits, LocationId location) {
        std::optional<Bit> bit = BitOf(location);
        return bit && (bits[bit->byte] & bit->mask) != 0;
    }
}

    // Saves a location collected here and reports it.
    void RecordCheck(LocationId location) {
        if (!g_ChecksLoaded) LoadChecks();
        if (std::optional<Bit> bit = BitOf(location)) {
            g_Checks[bit->byte] |= bit->mask;
            SaveChecks();
        }
        Logging.Log("[AP] collected location %d", std::to_underlying(location));
        AP::Send("CHECK " + ToString(location));
    }

    bool CollectedHere(LocationId location) {
        if (!BitOf(location)) return false;
        if (!g_ChecksLoaded) LoadChecks();
        return Has(g_Checks, location);
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

    void NoteBits(const std::vector<std::string>& a, std::vector<u8>& bits) {
        if (a.size() >= 2 && a[1] == "CLEAR") std::fill(bits.begin(), bits.end(), 0);
        for (size_t i = 1; i < a.size(); i++) {
            int id = atoi(a[i].c_str());
            if (a[i] != "CLEAR" && id >= 0 && id < (int)(kChecksBytes * 8)) bits[id / 8] |= (1 << (id % 8));
        }
    }

    void NoteInLogic(const std::vector<std::string>& a) {
        if (a.size() >= 2 && a[1] == "CLEAR") g_HasLogic = true;
        NoteBits(a, g_InLogic);
    }

    void NoteDoors(const std::vector<std::string>& a) {
        NoteBits(a, g_DoorOpen);
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
            if (ss >> n >> flag >> slot >> location) g_Hints.push_back({flag, "HINT " + slot + " " + location, false});
        }
        Logging.Log("[AP] hints: %zu", g_Hints.size());
    }

    // Sends HINT and DUNGEON for the hints read and the dungeons entered (their flag set) and not yet reported on
    // this connection, or for all.
    void ReportFlags(bool all) {
        if (!AP::IsClientConnected()) return;
        Report(g_Hints, all);
        Report(g_Dungeons, all);
    }

    void TickReportedFlags() {
        if (!PlayerReady() || NowMs() - g_FlagsPolled < kFlagPollMs) return;
        g_FlagsPolled = NowMs();
        ReportFlags(false);
    }

    void ResetReportedFlags() {
        for (auto* flags : {&g_Hints, &g_Dungeons}) {
            for (auto& f : *flags) f.reported = false;
        }
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

    bool IsInLogic(LocationId location) {
        return Has(g_InLogic, location);
    }

    bool IsDoorOpen(LocationId location) {
        return Has(g_DoorOpen, location);
    }

    bool IsLocationChecked(LocationId location) {
        if (!BitOf(location)) return false;
        if (!g_ChecksLoaded) LoadChecks();
        return Has(g_Checks, location) || Has(g_ServerChecks, location);
    }

    bool IsLocationSkipped(LocationId location) {
        if (!BitOf(location)) return false;
        if (!g_ChecksLoaded) LoadChecks();
        return Has(g_Skipped, location);
    }

    void SetLocationSkipped(LocationId location, bool skipped) {
        std::optional<Bit> bit = BitOf(location);
        if (!bit) return;
        if (!g_ChecksLoaded) LoadChecks();
        g_Skipped[bit->byte] = skipped ? g_Skipped[bit->byte] | bit->mask : g_Skipped[bit->byte] & ~bit->mask;
        WriteBits(g_SkippedPath, g_Skipped);
    }
}
