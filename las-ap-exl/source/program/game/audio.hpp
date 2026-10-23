#pragma once
// The game's music: the BGM manager (the static Game::BgmManager), its instances and their sounds.
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Audio {
    // A playing sound: its volume's fade, which Sound_SetVolume(sound, frames, volume) sets.
    struct Sound {
        u8 pad_0[0x140];
        float volume;              // 0x140
        float target;              // 0x144
    };
    static_assert(offsetof(Sound, volume) == 0x140 && offsetof(Sound, target) == 0x144);

    // A BGM that plays or is about to.
    struct BgmInstance {
        u8 pad_0[0x8];
        Sound* sound;              // 0x8
        String name;               // 0x10
        u8 pad_28[0x30];
        u32 fadeOutFrames;         // 0x58
        u8 pad_5c[0xf0 - 0x5c];
        u8 kind;                   // 0xf0: 1 zone and event music, 2 jingle, 3 buff
        u8 pad_f1[0x13];
        u32 fadeInFrames;          // 0x104: the frames it fades in over once it starts
    };
    static_assert(offsetof(BgmInstance, sound) == 0x8 && offsetof(BgmInstance, name) == 0x10);
    static_assert(offsetof(BgmInstance, fadeOutFrames) == 0x58 && offsetof(BgmInstance, kind) == 0xf0);
    static_assert(offsetof(BgmInstance, fadeInFrames) == 0x104);
    constexpr u8 kZoneMusic = 1;

    struct BgmManager {
        u8 pad_0[0x6268];
        Vector<BgmInstance*> starting;      // 0x6268: streams still being prepared, which no fade or stop reaches yet
        Vector<BgmInstance*> playing;       // 0x6280
        u8 pad_6298[0x9250 - 0x6298];
        char zoneTrack[64];                 // 0x9250: the zone's track
        u64 zoneTrackLength;                // 0x9290
    };
    static_assert(offsetof(BgmManager, starting) == 0x6268 && offsetof(BgmManager, playing) == 0x6280);
    static_assert(offsetof(BgmManager, zoneTrack) == 0x9250 && offsetof(BgmManager, zoneTrackLength) == 0x9290);
}
