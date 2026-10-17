// Jukebox (DESIGN.md, Music): a MUSIC line from the client, or the Jukebox page (Choose), picks a BGM that plays
// instead of the zone's music until MUSIC off. The game tells the client its choice: on each connection and on each
// choice made in the game.
//
// Zone music (each room's track from its level file) reaches BgmManager_Play from two places: the zone update, when
// Link enters a zone whose track differs from the last one, and PlayZoneBgm (the event action PlayZoneBGM, which
// events call to bring the zone's music back). While a track is chosen, those requests ask for it instead; the game
// then keeps it playing through room and level changes as it keeps one zone track playing. Every other request
// (event songs, instrument songs, fanfares, jingles, boss and buff music) plays as usual, and the chosen track
// comes back where the zone's music would.
#include "AP/ap.hpp"
#include "Game/framework.hpp"
#include "offsets.hpp"
#include "loggers.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace {
    struct StrView { const char* data; size_t len; };

    // The BGM manager (game 1.0.0): a static of the game, made on its first use (guard bit 0 set then).
    // BgmManager_Play(f32 volume, manager*, StringView* name, bool, bool, bool force) returns the new instance, or
    // null for an unknown name or a track that plays already. Instances: +0x10 the name (libc++ std::string), +0xf0
    // kind (1: zone and event BGM, which the zone update fades out when the zone's track changes). The manager:
    // +0x6280 the instances that play (begin, end), +0x9250 the zone's track (char[64], length +0x9290).
    constexpr uintptr_t kBgmManager = 0x1ca1d70, kBgmManagerGuard = 0x1cab240;
    constexpr uintptr_t kBgmPlay = 0x9fd60;
    // PlayZoneBgm(bool fadeOthers): plays the zone's track (+0x9250) and, when it starts, fades out the other kind 1
    // instances; while a Guardian Acorn's or Piece of Power's music plays it starts the track without fading.
    constexpr uintptr_t kPlayZoneBgm = 0xa6790;
    // BgmManager_StopByName(StringView* name, u32 fade frames)
    constexpr uintptr_t kBgmStop = 0x9b7b0;
    // BgmTable_Find(table, const char* name): the track's id, -1 for an unknown name; the table is a static (guard
    // bit 0) that BgmManager_Play makes.
    constexpr uintptr_t kBgmTableFind = 0x10e23d0, kBgmTable = 0x1caba28, kBgmTableGuard = 0x1cac378;
    // Return addresses of the zone music's BgmManager_Play calls: the zone update, PlayZoneBgm (twice).
    constexpr uintptr_t kZoneCalls[] = {0x9f008, 0xa688c, 0xa6988};
    constexpr size_t kActive = 0x6280, kZoneTrack = 0x9250, kZoneTrackLen = 0x9290;
    constexpr size_t kInstName = 0x10, kInstKind = 0xf0;
    constexpr u32 kFadeFrames = 60;

    // The chosen track ("" none) and whether the game should switch to it (or back) on the next tick in a level.
    char g_Track[64] = {};
    size_t g_TrackLen = 0;
    bool g_Pending = false;
    // The track chosen before the last change, stopped once the change applies.
    std::string g_Previous;

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    u8* Manager() {
        if ((*reinterpret_cast<volatile u8*>(g_BaseAddress + kBgmManagerGuard) & 1) == 0) return nullptr;
        return reinterpret_cast<u8*>(g_BaseAddress + kBgmManager);
    }

    std::string ZoneTrack(const u8* manager) {
        size_t len = *reinterpret_cast<const size_t*>(manager + kZoneTrackLen);
        return std::string(reinterpret_cast<const char*>(manager + kZoneTrack), len < 64 ? len : 0);
    }

    bool KnownTrack(const char* name) {
        if ((*reinterpret_cast<volatile u8*>(g_BaseAddress + kBgmTableGuard) & 1) == 0) return true;  // not known yet
        return GameFn<int (*)(void*, const char*)>(kBgmTableFind)(
                   reinterpret_cast<void*>(g_BaseAddress + kBgmTable), name) != -1;
    }

    bool IsZoneCall(uintptr_t ret) {
        for (uintptr_t call : kZoneCalls) {
            if (ret == offset_manager.Offset(call)) return true;
        }
        return false;
    }

    // Makes `track` ("" none) the choice; false when it is the choice already or too long.
    bool Set(const std::string& track) {
        if (track.size() >= sizeof(g_Track)) {
            AP::Log("music: track name too long");
            return false;
        }
        if (track == std::string(g_Track, g_TrackLen)) return false;
        if (g_TrackLen > 0) g_Previous.assign(g_Track, g_TrackLen);
        memcpy(g_Track, track.c_str(), track.size() + 1);
        g_TrackLen = track.size();
        g_Pending = true;
        AP::Log(track.empty() ? "music off" : "music " + track);
        return true;
    }

#ifdef LASAP_TEST
    // A libc++ std::string in the game's memory.
    std::string GameString(const u8* s) {
        if ((s[0] & 1) == 0) return std::string(reinterpret_cast<const char*>(s + 1), s[0] >> 1);
        return std::string(*reinterpret_cast<const char* const*>(s + 0x10), *reinterpret_cast<const size_t*>(s + 8));
    }

    // DBG music log: the last BGM requests, oldest first.
    constexpr size_t kLogSize = 24;
    std::string g_Log[kLogSize];
    size_t g_LogCount = 0;
    void Record(const std::string& entry) { g_Log[g_LogCount++ % kLogSize] = entry; }
#endif
}

HOOK_DEFINE_TRAMPOLINE(BgmManagerPlay) {
    static void* Callback(float volume, void* manager, StrView* name, bool b2, bool b3, bool force) {
        uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_BaseAddress;
        StrView chosen{g_Track, g_TrackLen};
        bool swap = g_TrackLen > 0 && IsZoneCall(ret);
        void* instance = Orig(volume, manager, swap ? &chosen : name, b2, b3, force);
#ifdef LASAP_TEST
        char entry[160];
        snprintf(entry, sizeof(entry), "play %.*s%s%s +%lx %d%d%d %s", (int)(name ? name->len : 0),
                 name ? name->data : "", swap ? "->" : "", swap ? g_Track : "", ret, b2, b3, force,
                 instance != nullptr ? "new" : "-");
        Record(entry);
#endif
        return instance;
    }
};

#ifdef LASAP_TEST
HOOK_DEFINE_TRAMPOLINE(BgmManagerStop) {
    static void Callback(StrView* name, u32 frames) {
        uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_BaseAddress;
        char entry[120];
        snprintf(entry, sizeof(entry), "stop %.*s +%lx %u", (int)(name ? name->len : 0), name ? name->data : "", ret,
                 frames);
        Record(entry);
        Orig(name, frames);
    }
};
#endif

namespace AP::Music {
    void InstallHooks() {
        BgmManagerPlay::InstallAtOffset(offset_manager.Offset(kBgmPlay));
#ifdef LASAP_TEST
        BgmManagerStop::InstallAtOffset(offset_manager.Offset(kBgmStop));
#endif
    }

    void HandleLine(const std::vector<std::string>& a) {
        // MUSIC <track> | MUSIC off
        Set(a.size() >= 2 && a[1] != "off" ? a[1] : "");
    }

    void Choose(const std::string& track) {
        if (Set(track == "off" ? "" : track)) Report();
    }

    void Report() {
        AP::Send(std::string("MUSIC ") + (g_TrackLen > 0 ? g_Track : "off"));
    }

    void Tick() {
        if (!g_Pending) return;
        Game::Framework* fmwk = Game::GetFramework();
        u8* manager = Manager();
        if (fmwk == nullptr || fmwk->mPlayer == nullptr || manager == nullptr) return;
        g_Pending = false;
        if (g_TrackLen > 0 && !KnownTrack(g_Track)) {
            AP::Log(std::string("music: unknown track ") + g_Track);
            g_Track[0] = 0;
            g_TrackLen = 0;
        }
        // The zone's music is asked for again: the chosen track (or the zone's own) starts and the rest fades out.
        GameFn<void* (*)(bool)>(kPlayZoneBgm)(true);
        // PlayZoneBgm fades nothing when nothing new starts (the zone has no track, or a buff's music plays): the
        // track chosen before stops by name, unless it is what plays now.
        if (!g_Previous.empty() && g_Previous != std::string(g_Track, g_TrackLen) && g_Previous != ::ZoneTrack(manager)) {
            StrView view{g_Previous.c_str(), g_Previous.size()};
            GameFn<void (*)(StrView*, u32)>(kBgmStop)(&view, kFadeFrames);
        }
        g_Previous.clear();
    }

    std::string Track() { return std::string(g_Track, g_TrackLen); }

    std::string ZoneTrack() {
        const u8* manager = Manager();
        return manager != nullptr ? ::ZoneTrack(manager) : std::string();
    }

#ifdef LASAP_TEST
    std::string StateField() {
        const u8* manager = Manager();
        std::string s = std::string(" music=") + (g_TrackLen > 0 ? g_Track : "-");
        if (manager == nullptr) return s + " zonebgm=- bgm=-";
        s += " zonebgm=" + ::ZoneTrack(manager) + " bgm=";
        u8* const* it = *reinterpret_cast<u8* const* const*>(manager + kActive);
        u8* const* end = *reinterpret_cast<u8* const* const*>(manager + kActive + 8);
        if (it == end) return s + "-";
        for (bool first = true; it != end; it++, first = false) {
            s += (first ? "" : ",") + GameString(*it + kInstName) + ":" + std::to_string((*it)[kInstKind]);
        }
        return s;
    }

    std::string LogText() {
        std::string s = "MUSICLOG";
        size_t from = g_LogCount > kLogSize ? g_LogCount - kLogSize : 0;
        for (size_t i = from; i < g_LogCount; i++) s += " | " + g_Log[i % kLogSize];
        return s;
    }
#endif
}
