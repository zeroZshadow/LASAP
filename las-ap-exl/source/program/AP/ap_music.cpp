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
#include "AP/ap_tracks.hpp"
#include "Game/framework.hpp"
#include "offsets.hpp"
#include "loggers.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    struct StrView { const char* data; size_t len; };

    // The BGM manager (game 1.0.0): a static of the game, made on its first use (guard bit 0 set then).
    // BgmManager_Play(f32 volume, manager*, StringView* name, bool, bool, bool force) returns the new instance, or
    // null for an unknown name or a track that plays already. A new instance starts its stream from the beginning,
    // intro included. Instances: +0x10 the name (libc++ std::string), +0xf0 kind (1: zone and event BGM, which the
    // zone update fades out when the zone's track changes), +0x104 the frames it fades in over once it starts. The
    // manager: +0x6268 the instances about to start, +0x6280 those that play (begin, end), +0x9250 the zone's track
    // (char[64], length +0x9290). A zone change (the zone update) fades the other kind 1 instances out over 90 frames
    // and the new track in over 150 (none right after a level load); PlayZoneBgm fades out the same but starts its
    // track at full volume, so a change made here sets the fade in itself.
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
    // The event action PlayBGM's call, and the event songs that stand in for the zone's music, which no PlayZoneBGM
    // follows: the washed-up sword's (SinkingSword.bfevfl) and the new game's wake-up (PlayerStart.bfevfl).
    constexpr uintptr_t kEventPlayCall = 0x805bc4;
    constexpr const char* kZoneStandIns[] = {"BGM_FIELD_NORMAL_INTRO", "BGM_HOUSE_FIRST"};
    // The manager's instances about to start (+0x6268, begin and end: a track whose stream is still being prepared,
    // which no fade or stop reaches yet) and those that play (+0x6280).
    constexpr size_t kStarting = 0x6268, kActive = 0x6280, kZoneTrack = 0x9250, kZoneTrackLen = 0x9290;
    // Instance: name, kind, fade-in frames, the sound (+0x8). Sound: its volume's fade (+0x140 the volume, +0x144 the
    // target, frames, frames gone), set by Sound_SetVolume(sound, frames, volume); the pause menu sets every BGM's
    // target to 0.2, and 1 again as it closes.
    constexpr size_t kInstName = 0x10, kInstKind = 0xf0, kInstFadeIn = 0x104, kInstSound = 0x8, kSoundTarget = 0x144;
    constexpr uintptr_t kSoundSetVolume = 0x10ea8c0;
    constexpr u32 kFadeFrames = 90, kFadeInFrames = 150;  // the zone update's
    constexpr int kLoudFrames = 8;  // as the pause menu sets the volume back

    // The chosen track ("" none), its index in AP::Tracks::TRACKS (-1: none, or a track not in the list), and whether
    // the game should switch to it (or back) on the next tick in a level.
    char g_Track[64] = {};
    size_t g_TrackLen = 0;
    int g_TrackIndex = -1;
    bool g_Pending = false;
    // How long the chosen track has played (Tick, playback below).
    const u8* g_Instance = nullptr;  // the chosen track's instance as last seen playing
    u64 g_PlayedMs = 0, g_LastMs = 0;
    bool g_Ended = false;            // the chosen track (one that does not loop) ended while the player was free
    const u8* g_Started = nullptr;   // the instance the last change started (compared, read only while it plays)
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

    bool IsZoneCall(uintptr_t ret, const StrView* name) {
        for (uintptr_t call : kZoneCalls) {
            if (ret == offset_manager.Offset(call)) return true;
        }
        if (ret != offset_manager.Offset(kEventPlayCall) || name == nullptr) return false;
        for (const char* standIn : kZoneStandIns) {
            if (std::string_view(name->data, name->len) == standIn) return true;
        }
        return false;
    }

    // Makes `track` ("" none) the choice; false when it is the choice already or too long.
    int TrackIndex(const std::string& id);

    bool Set(const std::string& track) {
        if (track.size() >= sizeof(g_Track)) {
            AP::Log("music: track name too long");
            return false;
        }
        if (track == std::string(g_Track, g_TrackLen)) return false;
        if (g_TrackLen > 0) {
            if (!g_Pending) g_Previous.assign(g_Track, g_TrackLen);  // what plays until the change applies
        }
        memcpy(g_Track, track.c_str(), track.size() + 1);
        g_TrackLen = track.size();
        g_TrackIndex = TrackIndex(track);
        g_Instance = nullptr;
        g_PlayedMs = 0;
        g_Ended = false;
        g_Pending = true;
        AP::Log(track.empty() ? "music off" : "music " + track);
        return true;
    }

    // A libc++ std::string in the game's memory: its characters and length, without a copy.
    std::string_view GameString(const u8* s) {
        if ((s[0] & 1) == 0) return std::string_view(reinterpret_cast<const char*>(s + 1), s[0] >> 1);
        return std::string_view(*reinterpret_cast<const char* const*>(s + 0x10), *reinterpret_cast<const size_t*>(s + 8));
    }

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    // Zone or event music (kind 1) other than the chosen track plays: a boss's, an event's.
    bool OtherMusic(const u8* manager) {
        u8* const* it = *reinterpret_cast<u8* const* const*>(manager + kActive);
        u8* const* end = *reinterpret_cast<u8* const* const*>(manager + kActive + 8);
        for (; it != end; it++) {
            if ((*it)[kInstKind] != 1) continue;
            if (GameString(*it + kInstName) != std::string_view(g_Track, g_TrackLen)) return true;
        }
        return false;
    }

    // Playback (the Jukebox page's controls, DESIGN.md "Jukebox page"). With repeat one (on at the start) the chosen
    // track loops, as the game loops its music. With it off, once the chosen track has played through (its length in
    // ap_tracks.hpp, counted while it plays, from the start again whenever the game starts it again) the next one
    // follows. Next and previous step through one order, round: the list's, or with shuffle a random order of the
    // list drawn when shuffle turns on, so every track plays once before any plays again and previous retraces next.
    bool g_RepeatOne = true, g_Shuffle = false;
    int g_Order[AP::Tracks::COUNT] = {};  // shuffle's order: indexes into AP::Tracks::TRACKS
    int g_Place[AP::Tracks::COUNT] = {};  // each track's place in it
    u32 g_Random = 0;

    int TrackIndex(const std::string& id) {
        for (int i = 0; i < AP::Tracks::COUNT; i++) {
            if (id == AP::Tracks::TRACKS[i].id) return i;
        }
        return -1;
    }

    u32 Random() {
        if (g_Random == 0) g_Random = static_cast<u32>(NowMs()) | 1;
        g_Random ^= g_Random << 13;
        g_Random ^= g_Random >> 17;
        g_Random ^= g_Random << 5;
        return g_Random;
    }

    // A new shuffle order (Fisher-Yates).
    void DrawOrder() {
        for (int i = 0; i < AP::Tracks::COUNT; i++) g_Order[i] = i;
        for (int i = AP::Tracks::COUNT - 1; i > 0; i--) std::swap(g_Order[i], g_Order[Random() % (i + 1)]);
        for (int i = 0; i < AP::Tracks::COUNT; i++) g_Place[g_Order[i]] = i;
    }

    // The track `step` places from `current` (-1: a track not in the list, before the first) in the order.
    int StepFrom(int current, int step) {
        constexpr int n = AP::Tracks::COUNT;
        if (current < 0) return g_Shuffle ? g_Order[step > 0 ? 0 : n - 1] : step > 0 ? 0 : n - 1;
        if (!g_Shuffle) return ((current + step) % n + n) % n;
        return g_Order[((g_Place[current] + step) % n + n) % n];
    }

    // The chosen track's instance among those that play, or null.
    const u8* PlayingInstance(const u8* manager) {
        if (g_TrackLen == 0) return nullptr;
        u8* const* it = *reinterpret_cast<u8* const* const*>(manager + kActive);
        u8* const* end = *reinterpret_cast<u8* const* const*>(manager + kActive + 8);
        for (; it != end; it++) {
            if (GameString(*it + kInstName) == std::string_view(g_Track, g_TrackLen)) return *it;
        }
        return nullptr;
    }

#ifdef LASAP_TEST
    // DBG music log: the last BGM requests, oldest first.
    constexpr size_t kLogSize = 24;
    std::string g_Log[kLogSize];
    size_t g_LogCount = 0;
    void Record(const std::string& entry) { g_Log[g_LogCount++ % kLogSize] = entry; }
#endif

    // Doors: a door's event stops all music (Audio.StopAllBGM) and fades the zone's out, and the level load stops what
    // is left (BgmManager_StopAll with 0 frames); the new level then starts the chosen track again from its beginning.
    // The module keeps the chosen track out of those stops and decides afterwards: a level load follows (another
    // player, or none): it plays on, and the new level finds it playing; the game asks for the zone's music again
    // (an event's PlayZoneBGM): it plays on; another track starts (an event's song, a fanfare): it stops then; the
    // player stays free with none of these (an event that meant silence): it stops then. Stairs into a room of the same
    // level (a 2D room and back) hold it, free the player for a frame and then ask for the zone's music: the player
    // counts as free again after kHeldFreeFrames frames in a row.
    // BgmManager_StopAll(u32 frames), the zone music's fade (void: the kind 1 instances over 90 frames),
    // Sound_Stop(sound, u32 frames); an instance's fade-out frames at +0x58.
    constexpr uintptr_t kBgmStopAll = 0x9b550, kBgmFadeZone = 0xa6510, kSoundStop = 0x10e97f0;
    constexpr size_t kInstFadeOut = 0x58;
    constexpr u32 kHeldFadeFrames = 30;
    constexpr int kHeldFreeFrames = 10;
    int g_FreeFrames = 0;               // ticks in a row with the player free
    const u8* g_HoldSound = nullptr;    // the chosen track's sound while a stop of the game's runs
    bool g_HoldSkipped = false;         // that stop reached it
    bool g_Held = false;                // a stop left it playing: what follows decides
    bool g_Carried = false;             // held into a level load: it plays on unless another track starts
    const void* g_HeldPlayer = nullptr;

    // Runs a stop of the game's with the chosen track left playing.
    template<typename F>
    void Holding(F stop) {
        u8* manager = Manager();
        u8* instance = manager != nullptr ? const_cast<u8*>(PlayingInstance(manager)) : nullptr;
        u8* sound = instance != nullptr ? *reinterpret_cast<u8**>(instance + kInstSound) : nullptr;
        if (sound == nullptr) return stop();
        u32 fadeOut = *reinterpret_cast<u32*>(instance + kInstFadeOut);
        g_HoldSound = sound;
        g_HoldSkipped = false;
        stop();
        g_HoldSound = nullptr;
#ifdef LASAP_TEST
        Record(std::string("hold ") + (g_HoldSkipped ? "kept" : "missed"));
#endif
        if (!g_HoldSkipped) return;
        *reinterpret_cast<u32*>(instance + kInstFadeOut) = fadeOut;
        Game::Framework* fmwk = Game::GetFramework();
        g_Held = true;
        g_HeldPlayer = fmwk != nullptr ? fmwk->mPlayer : nullptr;
    }

    // The held track stops after all.
    void StopHeld() {
#ifdef LASAP_TEST
        Record(std::string("held stop") + (g_Carried ? " carried" : ""));
#endif
        g_Held = g_Carried = false;
        u8* manager = Manager();
        const u8* instance = manager != nullptr ? PlayingInstance(manager) : nullptr;
        u8* sound = instance != nullptr ? *reinterpret_cast<u8* const*>(instance + kInstSound) : nullptr;
        if (sound != nullptr) GameFn<void (*)(void*, u32)>(kSoundStop)(sound, kHeldFadeFrames);
    }

}

HOOK_DEFINE_TRAMPOLINE(BgmManagerPlay) {
    static void* Callback(float volume, void* manager, StrView* name, bool b2, bool b3, bool force) {
        uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - g_BaseAddress;
        StrView chosen{g_Track, g_TrackLen};
        bool swap = g_TrackLen > 0 && IsZoneCall(ret, name);
        void* instance = Orig(volume, manager, swap ? &chosen : name, b2, b3, force);
        if ((g_Held || g_Carried) && !swap && instance != nullptr) StopHeld();  // an event's song, a fanfare
        if (swap) g_Held = false;  // the zone's music again: it plays on
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

HOOK_DEFINE_TRAMPOLINE(BgmStopAll) {
    static void Callback(u32 frames) { Holding([frames] { Orig(frames); }); }
};

HOOK_DEFINE_TRAMPOLINE(BgmFadeZone) {
    static void Callback() { Holding([] { Orig(); }); }
};

HOOK_DEFINE_TRAMPOLINE(SoundStop) {
    static void Callback(u8* sound, u32 frames) {
        if (sound != nullptr && sound == g_HoldSound) {
            g_HoldSkipped = true;
            return;
        }
        Orig(sound, frames);
    }
};

namespace AP::Music {
    void InstallHooks() {
        BgmManagerPlay::InstallAtOffset(offset_manager.Offset(kBgmPlay));
        BgmStopAll::InstallAtOffset(offset_manager.Offset(kBgmStopAll));
        BgmFadeZone::InstallAtOffset(offset_manager.Offset(kBgmFadeZone));
        SoundStop::InstallAtOffset(offset_manager.Offset(kSoundStop));
#ifdef LASAP_TEST
        BgmManagerStop::InstallAtOffset(offset_manager.Offset(kBgmStop));
#endif
    }

    void HandleLine(const std::vector<std::string>& a) {
        // MUSIC <track> | MUSIC off; for a name refused as too long, the client learns what plays instead
        std::string track = a.size() >= 2 && a[1] != "off" ? a[1] : "";
        if (!Set(track) && track.size() >= sizeof(g_Track)) Report();
    }

    void Choose(const std::string& track) {
        if (Set(track == "off" ? "" : track)) Report();
    }

    void Report() {
        AP::Send(std::string("MUSIC ") + (g_TrackLen > 0 ? g_Track : "off"));
    }

    void Tick(bool free) {
        Game::Framework* fmwk = Game::GetFramework();
        u8* manager = Manager();
        if (fmwk == nullptr || fmwk->mPlayer == nullptr || manager == nullptr) {
            if (g_Held) {
                g_Held = false;  // held into a level load: it plays on
                g_Carried = true;
            }
            if (!g_Carried) g_Instance = nullptr;  // a level load starts the music again
            g_FreeFrames = 0;
            return;
        }
        g_FreeFrames = free ? g_FreeFrames + 1 : 0;
        if (g_Held && fmwk->mPlayer != g_HeldPlayer) {
            g_Held = false;  // another level: it plays on
        } else if (g_Held && g_FreeFrames >= kHeldFreeFrames) {
            StopHeld();  // an event that meant silence is over
        }
        g_Carried = false;
        // How long the chosen track has played: from its start, while it plays (also under the pause menu, which
        // does not tick this). With repeat one off, the next track once it has played through, only while the player
        // is free: a change fades out the event songs too. A track that does not loop is gone once it has played: it
        // has played through when it goes, with the player free, within a second of its length (an event that stops
        // it does so with the player not free, and music that cuts it in plays on).
        if (g_TrackIndex >= 0) {
            u64 now = NowMs();
            const u8* instance = PlayingInstance(manager);
            u64 length = AP::Tracks::TRACKS[g_TrackIndex].ms;
            if (instance == nullptr && g_Instance != nullptr && free && g_PlayedMs + 1000 >= length &&
                !OtherMusic(manager)) {
                g_Ended = true;
            }
            if (instance != g_Instance) {
                g_PlayedMs = 0;
            } else if (instance != nullptr) {
                g_PlayedMs += now - g_LastMs;
            }
            if (instance != nullptr) g_Ended = false;
            g_Instance = instance;
            g_LastMs = now;
            if (free && !g_RepeatOne && !g_Pending && (g_Ended || (instance != nullptr && g_PlayedMs >= length))) {
                Skip(1);
            }
        }
        if (!g_Pending) return;
        // A track still being prepared would start after the change and play on beside it: the change waits, and
        // applies the latest choice.
        if (*reinterpret_cast<u8* const*>(manager + kStarting) != *reinterpret_cast<u8* const*>(manager + kStarting + 8)) {
            return;
        }
        g_Pending = false;
        if (g_TrackLen > 0 && !KnownTrack(g_Track)) {
            AP::Log(std::string("music: unknown track ") + g_Track);
            g_Track[0] = 0;
            g_TrackLen = 0;
            g_TrackIndex = -1;
        }
        // The zone's music is asked for again: the chosen track (or the zone's own) starts from its beginning and
        // fades in as on a zone change, and the rest fades out.
        u8* started = GameFn<u8* (*)(bool)>(kPlayZoneBgm)(true);
        if (started != nullptr) *reinterpret_cast<u32*>(started + kInstFadeIn) = kFadeInFrames;
        g_Started = started;
        // PlayZoneBgm fades nothing when nothing new starts (the zone has no track, or a buff's music plays): the
        // track chosen before stops by name, unless it is what plays now (the chosen track, or the zone's own with none
        // chosen).
        bool keep = g_Previous == std::string(g_Track, g_TrackLen) || (g_TrackLen == 0 && g_Previous == ::ZoneTrack(manager));
        if (!g_Previous.empty() && !keep) {
            StrView view{g_Previous.c_str(), g_Previous.size()};
            GameFn<void (*)(StrView*, u32)>(kBgmStop)(&view, kFadeFrames);
        }
        g_Previous.clear();
    }

    std::string Skip(int step) {
        int next = StepFrom(TrackIndex(g_TrackLen > 0 ? Track() : ZoneTrack()), step > 0 ? 1 : -1);
        Choose(AP::Tracks::TRACKS[next].id);
        std::string name = AP::Tracks::TRACKS[next].name;
        AP::ShowMusicToast("\xe2\x99\xaa " + name);
        return name;
    }

    void Loud() {
        u8* manager = Manager();
        if (manager == nullptr || g_Started == nullptr) return;
        u8* const* it = *reinterpret_cast<u8* const* const*>(manager + kActive);
        u8* const* end = *reinterpret_cast<u8* const* const*>(manager + kActive + 8);
        for (; it != end; it++) {
            if (*it != g_Started) continue;
            u8* sound = *reinterpret_cast<u8* const*>(*it + kInstSound);
            if (sound != nullptr && *reinterpret_cast<const float*>(sound + kSoundTarget) < 1.0f) {
                GameFn<void (*)(void*, int, float)>(kSoundSetVolume)(sound, kLoudFrames, 1.0f);
            }
            return;
        }
    }

    bool RepeatOne() { return g_RepeatOne; }
    bool Shuffle() { return g_Shuffle; }
    void SetRepeatOne(bool on) { g_RepeatOne = on; }
    void SetShuffle(bool on) {
        if (on && !g_Shuffle) DrawOrder();
        g_Shuffle = on;
    }

    std::string Track() { return std::string(g_Track, g_TrackLen); }

    bool IsChosen(const char* track) { return strcmp(track, g_Track) == 0; }

    bool HasMusic() {
        const u8* manager = Manager();
        return g_TrackLen > 0 || (manager != nullptr && *reinterpret_cast<const size_t*>(manager + kZoneTrackLen) > 0);
    }

    std::string ZoneTrack() {
        const u8* manager = Manager();
        return manager != nullptr ? ::ZoneTrack(manager) : std::string();
    }

#ifdef LASAP_TEST
    std::string StateField() {
        const u8* manager = Manager();
        std::string s = std::string(" music=") + (g_TrackLen > 0 ? g_Track : "-");
        if (manager == nullptr) return s + " zonebgm=- bgm=- bgmvol=-";
        s += " zonebgm=" + ::ZoneTrack(manager) + " bgm=";
        u8* const* it = *reinterpret_cast<u8* const* const*>(manager + kActive);
        u8* const* end = *reinterpret_cast<u8* const* const*>(manager + kActive + 8);
        if (it == end) return s + "- bgmvol=-";
        std::string volumes;
        for (bool first = true; it != end; it++, first = false) {
            s += (first ? "" : ",") + std::string(GameString(*it + kInstName)) + ":" + std::to_string((*it)[kInstKind]);
            const u8* sound = *reinterpret_cast<u8* const*>(*it + kInstSound);
            float target = sound != nullptr ? *reinterpret_cast<const float*>(sound + kSoundTarget) : 0.0f;
            volumes += (first ? "" : ",") + std::to_string(static_cast<int>(target * 100.0f + 0.5f));
        }
        return s + " bgmvol=" + volumes;
    }

    const u8* PeekBase(bool instance) {
        const u8* manager = Manager();
        if (manager == nullptr || !instance) return manager;
        u8* const* it = *reinterpret_cast<u8* const* const*>(manager + kActive);
        u8* const* end = *reinterpret_cast<u8* const* const*>(manager + kActive + 8);
        return it != end ? *it : nullptr;
    }

    std::string LogText() {
        std::string s = "MUSICLOG";
        size_t from = g_LogCount > kLogSize ? g_LogCount - kLogSize : 0;
        for (size_t i = from; i < g_LogCount; i++) s += " | " + g_Log[i % kLogSize];
        return s;
    }
#endif
}
