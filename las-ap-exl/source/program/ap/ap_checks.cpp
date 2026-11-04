// Checks (DESIGN.md, Map tracker; In-game hints; Persistence): the locations collected here (saved per seed and
// reported), those the server has as checked or in logic and those the player marked skipped, for the map tracker;
// and the in-game hints read and the dungeons entered.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "config/seed_file.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/time.hpp"
#include "nn/fs.hpp"
#include <algorithm>
#include <optional>
#include <cstdlib>

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
    // Locations holding a server hint not yet found (HINTED lines), for the map tracker.
    std::vector<u8> g_Hinted(kChecksBytes, 0);

    // Flags of the save reported to the client by a line once they are set: once per connection, and all again on
    // SYNC.
    struct ReportedFlag { std::string flag; std::string line; bool reported; };
    // In-game hints the client makes server hints of (the seed file's hints): the flag showing the hint sets (APHint<n>),
    // reported as HINT <slot> <location id>.
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

    // The line's ids, up to the first field that is not one, into bits.
    void SetBits(Fields& f, std::vector<u8>& bits) {
        while (std::optional<int> id = f.Num<int>()) {
            if (*id >= 0 && *id < (int)(kChecksBytes * 8)) bits[*id / 8] |= (1 << (*id % 8));
        }
    }

    // A first field CLEAR empties the bits before the ids are set; whether it did.
    bool NoteBits(Fields& f, std::vector<u8>& bits) {
        Fields rest = f;
        bool clear = rest.Str() == "CLEAR";
        if (clear) {
            std::fill(bits.begin(), bits.end(), 0);
            f = rest;
        }
        SetBits(f, bits);
        return clear;
    }

    void NoteServerChecks(Fields& f) {
        SetBits(f, g_ServerChecks);
    }

    void NoteInLogic(Fields& f) {
        if (NoteBits(f, g_InLogic)) g_HasLogic = true;
    }

    void NoteDoors(Fields& f) {
        NoteBits(f, g_DoorOpen);
    }

    void NoteHinted(Fields& f) {
        NoteBits(f, g_Hinted);
    }

    // The seed file's hints' "S <n> <slot> <location id>" lines: reading source n's hint makes a server hint of that
    // location (patcher ap_hints.py); showing the hint sets the flag (ap_text.cpp).
    void LoadHints() {
        SeedFile::ForEach("hints", [](Fields& f) {
            if (f.Str() != "S") return;
            std::string_view n = f.Str(), slot = f.Str(), location = f.Str();
            if (location.empty()) return;
            std::string line = "HINT " + std::string(slot) + " " + std::string(location);
            g_Hints.push_back({std::string(AP::HINT_PREFIX) + std::string(n), std::move(line), false});
        });
        Logging.Log("[AP] hints: %zu", g_Hints.size());
    }

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

    bool IsHinted(LocationId location) {
        return Has(g_Hinted, location);
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
