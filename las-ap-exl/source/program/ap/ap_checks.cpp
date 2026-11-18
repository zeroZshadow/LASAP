// Checks (DESIGN.md, Map tracker; In-game hints; Persistence): the locations collected here (saved per save and
// reported), those the server has as checked or in logic and those the player marked skipped, for the map tracker;
// and the in-game hints read and the dungeons entered.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "ap/ap_patcher.hpp"
#include "config/seed_file.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "hooks.hpp"
#include "loggers.hpp"
#include "nn/fs.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
#include <cstdlib>

namespace AP::Bridge {
namespace {
    using Game::NowMs;

    constexpr size_t kChecksBytes = 65536 / 8;

    // The live save's files for the installed seed, sd:/lasap/<its SaveId>-<seed hash>.chk and .skp, and the id they
    // are loaded for. A save's id is in each save file it wrote, a file and the autosave alike, so they share its files.
    std::string g_ChecksPath;
    std::vector<u8> g_Checks(kChecksBytes, 0);
    std::optional<Game::SaveId> g_LoadedId;
    // Locations the player marked as skipped on the map (map tracker), kept per save next to the checks.
    std::string g_SkippedPath;
    std::vector<u8> g_Skipped(kChecksBytes, 0);
    // The save whose checks went to the client since it was loaded
    std::optional<Game::SaveId> g_SentId;
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

    // The files in sd:/lasap
    std::vector<std::string> CheckFileNames() {
        std::vector<std::string> names;
        nn::fs::DirectoryHandle dir{};
        if (R_FAILED(nn::fs::OpenDirectory(&dir, "sd:/lasap", nn::fs::OpenDirectoryMode_File))) return names;
        s64 count = 0;
        nn::fs::GetDirectoryEntryCount(&count, dir);
        std::vector<nn::fs::DirectoryEntry> entries(static_cast<size_t>(std::max<s64>(count, 0)));
        s64 read = 0;
        if (!entries.empty()) nn::fs::ReadDirectory(&read, entries.data(), dir, count);
        nn::fs::CloseDirectory(dir);
        for (s64 i = 0; i < read; i++) names.emplace_back(entries[i].m_Name);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool FileThere(const std::string& path) {
        nn::fs::FileHandle h{};
        if (R_FAILED(nn::fs::OpenFile(&h, path.c_str(), nn::fs::OpenMode_Read))) return false;
        nn::fs::CloseFile(h);
        return true;
    }

    bool SameId(const Game::SaveId& a, const Game::SaveId& b) {
        return std::memcmp(a.bytes, b.bytes, sizeof a.bytes) == 0;
    }

    // A save's id in hex
    std::string IdName(const Game::SaveId& id) {
        static constexpr char kHex[] = "0123456789abcdef";
        std::string name;
        for (u8 b : id.bytes) {
            name += kHex[b >> 4];
            name += kHex[b & 0xf];
        }
        return name;
    }

    constexpr size_t kIdNameSize = 32, kSeedHashSize = 8;

    // A save's files go by its id and the seed's hash, so a seed never reads another seed's checks; those of a mod
    // before files went by save by the seed's hash alone
    std::string SaveStem(const Game::SaveId& id) {
        return IdName(id) + "-" + g_SeedHash;
    }

    std::string FilePath(std::string_view stem, std::string_view ext) {
        return "sd:/lasap/" + std::string(stem) + "." + std::string(ext);
    }

    Game::SaveId& HeaderId(int slot) {
        return *reinterpret_cast<Game::SaveId*>(&(*Game::FileHeaders)[slot][8]);
    }

    // Files 1-3; the autosave (slot 3) holds the save of the file played last
    constexpr int kFiles = 3;

    // A save file the boot read, or one written since, the autosave included, has the id
    bool InSaveFile(const Game::SaveId& id) {
        for (int slot = 0; slot <= kFiles; slot++) {
            if (SameId(HeaderId(slot), id)) return true;
        }
        return false;
    }

    u64 SplitMix(u64 z) {
        z += 0x9e3779b97f4a7c15;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        return z ^ (z >> 31);
    }

    // An id for a new save: the game's (nn::util::GenerateUuid) repeats across boots on Ryujinx, so the system
    // counter is mixed in, and an id a save file or a check file has is drawn again
    Game::SaveId NewSaveId(u64 lo, u64 hi) {
        const std::vector<std::string> files = CheckFileNames();
        for (u64 n = 0;; n++) {
            Game::SaveId id;
            const u64 words[2] = {lo ^ SplitMix(Game::Ticks() + n), hi ^ SplitMix(Game::Ticks() ^ (n << 32))};
            std::memcpy(id.bytes, words, sizeof id.bytes);
            const std::string name = IdName(id);
            if (InSaveFile(id) || std::any_of(files.begin(), files.end(), [&](const std::string& f) {
                    return f.starts_with(name);
                })) continue;
            return id;
        }
    }

    // The live save's files. A save without them that a save file holds is from before they went by save: it takes
    // the seed's files (CleanUp deletes those once every save has its own). A new game's are empty.
    void LoadChecks() {
        const Game::SaveId id = *Game::LiveSaveId;
        g_LoadedId = id;
        g_ChecksPath = FilePath(SaveStem(id), "chk");
        g_SkippedPath = FilePath(SaveStem(id), "skp");
        std::fill(g_Checks.begin(), g_Checks.end(), 0);
        std::fill(g_Skipped.begin(), g_Skipped.end(), 0);
        nn::fs::CreateDirectory("sd:/lasap");
        if (FileThere(g_ChecksPath)) {
            ReadBits(g_ChecksPath, g_Checks);
            ReadBits(g_SkippedPath, g_Skipped);
            return;
        }
        if (InSaveFile(id) && FileThere(FilePath(g_SeedHash, "chk"))) {
            ReadBits(FilePath(g_SeedHash, "chk"), g_Checks);
            if (FileThere(FilePath(g_SeedHash, "skp"))) {
                ReadBits(FilePath(g_SeedHash, "skp"), g_Skipped);
                WriteBits(g_SkippedPath, g_Skipped);
            }
            Logging.Log("[AP] checks: save %s takes the seed's files", IdName(id).c_str());
        }
        WriteBits(g_ChecksPath, g_Checks);
    }

    void EnsureLoaded() {
        if (!g_LoadedId || !SameId(*g_LoadedId, *Game::LiveSaveId)) LoadChecks();
    }

    // Deletes the check files of saves that are gone: those named by an id that no file (1-3) holds and that is not
    // the save in play's (one not written yet), so an erased file's go though the autosave holds its save; the
    // files named by an id alone, which do not say their seed; and the seed-named files of other seeds, or of this
    // seed once every save has its own. A save's files of other seeds stay while it does. The file menu's live save
    // is no save in play: a copy loads its source there.
    void CleanUp() {
        if (*Game::FileHeadersRead == 0) return;
        std::vector<std::string> saves;
        for (s8 slot = 0; slot < kFiles; slot++) {
            if (Game::SaveFile_Exists(slot)) saves.push_back(IdName(HeaderId(slot)));
        }
        bool allOwn = std::all_of(saves.begin(), saves.end(), [](const std::string& s) {
            return FileThere(FilePath(s + "-" + g_SeedHash, "chk"));
        });
        if (g_SentId) saves.push_back(IdName(*g_SentId));

        int deleted = 0;
        for (const std::string& name : CheckFileNames()) {
            size_t dot = name.rfind('.');
            if (dot == std::string_view::npos) continue;
            std::string_view stem = std::string_view(name).substr(0, dot), ext = std::string_view(name).substr(dot + 1);
            if (ext != "chk" && ext != "skp") continue;
            bool gone;
            if (stem.size() == kIdNameSize + 1 + kSeedHashSize && stem[kIdNameSize] == '-') {
                gone = std::find(saves.begin(), saves.end(), stem.substr(0, kIdNameSize)) == saves.end();
            } else if (stem.size() == kIdNameSize) gone = true;
            else if (stem.size() == kSeedHashSize) gone = stem != g_SeedHash || allOwn;
            else continue;
            if (gone && R_SUCCEEDED(nn::fs::DeleteFile(FilePath(stem, ext).c_str()))) deleted++;
        }
        if (deleted > 0) Logging.Log("[AP] checks: deleted %d files of saves that are gone", deleted);
    }

    // The file menu opens (FileOperate_Setup, as it clears the current file), and a file was erased (DeleteDataEnd)
    HOOK_DEFINE_INLINE(FileMenuOpen) {
        static void Callback(exl::hook::nx64::InlineCtx*) { CleanUp(); }
    };
    HOOK_DEFINE_INLINE(FileErased) {
        static void Callback(exl::hook::nx64::InlineCtx*) { CleanUp(); }
    };
    // Each write of a save file (SaveImage_Write (task, u8 slot, bool)): the file menu's copy of its header gets the
    // save's id, which the game puts there only from the boot's read
    HOOK_DEFINE_INLINE(SaveWrite) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            u8 slot = static_cast<u8>(ctx->W[1]);
            if (slot < 4) HeaderId(slot) = *Game::LiveSaveId;
        }
    };
    // The file menu's copy, as it writes the source it loaded into the live save (CopyDataSave): the copy gets an id
    // of its own and a copy of the source's files for the installed seed
    HOOK_DEFINE_INLINE(FileCopy) {
        static void Callback(exl::hook::nx64::InlineCtx*) {
            const Game::SaveId source = *Game::LiveSaveId;
            const Game::SaveId drawn = Game::GenerateUuid();
            u64 words[2];
            std::memcpy(words, drawn.bytes, sizeof words);
            *Game::LiveSaveId = NewSaveId(words[0], words[1]);
            for (const char* ext : {"chk", "skp"}) {
                std::string from = FilePath(SaveStem(source), ext);
                if (!FileThere(from)) continue;
                std::vector<u8> bits(kChecksBytes, 0);
                ReadBits(from, bits);
                WriteBits(FilePath(SaveStem(*Game::LiveSaveId), ext), bits);
            }
        }
    };
    // GlobalSave_Reset (a new game, the boot, $QUIT), as it stores the id GenerateUuid drew (stp x0, x1 into the file
    // header's SaveId), before the header's CRC: the save gets NewSaveId's in its place
    HOOK_DEFINE_INLINE(ResetId) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            const Game::SaveId id = NewSaveId(ctx->X[0], ctx->X[1]);
            u64 words[2];
            std::memcpy(words, id.bytes, sizeof words);
            ctx->X[0] = words[0];
            ctx->X[1] = words[1];
        }
    };
    constexpr uintptr_t kFileMenuOpen = 0xe603e8, kFileErased = 0xe65220, kSaveWrite = 0xdfccf0, kFileCopy = 0xe66ff0,
                        kResetId = 0xdfac00;

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
        EnsureLoaded();
        if (std::optional<Bit> bit = BitOf(location)) {
            g_Checks[bit->byte] |= bit->mask;
            SaveChecks();
        }
        Logging.Log("[AP] collected location %d", std::to_underlying(location));
        AP::Send("CHECK " + ToString(location));
    }

    bool CollectedHere(LocationId location) {
        if (!BitOf(location)) return false;
        EnsureLoaded();
        return Has(g_Checks, location);
    }

    void SendAllChecks() {
        if (!g_SentId) return;  // no save in play
        EnsureLoaded();
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

    void TickSaveChecks(bool inPlay) {
        if (!inPlay) {
            g_SentId.reset();
            return;
        }
        if (!PlayerReady() || (g_SentId && SameId(*g_SentId, *Game::LiveSaveId))) return;
        g_SentId = *Game::LiveSaveId;
        SendAllChecks();
    }

    void InstallSaveChecks() {
        FileMenuOpen::InstallAtOffset(Game::Offset(kFileMenuOpen));
        FileErased::InstallAtOffset(Game::Offset(kFileErased));
        SaveWrite::InstallAtOffset(Game::Offset(kSaveWrite));
        FileCopy::InstallAtOffset(Game::Offset(kFileCopy));
        ResetId::InstallAtOffset(Game::Offset(kResetId));
    }

#ifdef LASAP_TEST
    void ClearChecks() {
        EnsureLoaded();
        std::fill(g_Checks.begin(), g_Checks.end(), 0);
        SaveChecks();
    }

    std::string CheckFiles() {
        std::string line;
        for (const std::string& name : CheckFileNames()) line += " " + name;
        return line;
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
        EnsureLoaded();
        return Has(g_Checks, location) || Has(g_ServerChecks, location);
    }

    bool IsLocationSkipped(LocationId location) {
        if (!BitOf(location)) return false;
        EnsureLoaded();
        return Has(g_Skipped, location);
    }

    void SetLocationSkipped(LocationId location, bool skipped) {
        std::optional<Bit> bit = BitOf(location);
        if (!bit) return;
        EnsureLoaded();
        g_Skipped[bit->byte] = skipped ? g_Skipped[bit->byte] | bit->mask : g_Skipped[bit->byte] & ~bit->mask;
        WriteBits(g_SkippedPath, g_Skipped);
    }
}
