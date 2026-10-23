#pragma once
// The game's audio: its audio manager (the static Game::BgmManager), the BGM instances and their sounds
// (scripts/re/FRAMEWORK.md, Audio).
#include "game/actor.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Audio {
    // A playing sound (its size is not known): its stop fade (Sound_Stop) and its volume's fade, which
    // Sound_SetVolume(sound, frames, volume) sets.
    struct Sound {
        const uintptr_t* vtable;   // +0x48 stop
        u8 pad_8[0x8];
        void* handles[2];          // 0x10: back-links to the handles that hold it
        void* owner;               // 0x20: told when it stops
        u8 pad_28[0x94];
        float stopVolume;          // 0xbc
        float stopTarget;          // 0xc0
        s32 stopFrames;            // 0xc4
        s32 stopFramesGone;        // 0xc8: Sound_FadeFramesLeft is stopFrames - stopFramesGone
        u8 pad_cc[0x20];
        u8 active[2];              // 0xec
        u8 pad_ee[0x13];
        u8 state;                  // 0x101: 2 stopping
        u8 pad_102[0x3e];
        float volume;              // 0x140
        float target;              // 0x144
        s32 volumeFrames;          // 0x148
        s32 volumeFramesGone;      // 0x14c
    };
    static_assert(offsetof(Sound, owner) == 0x20 && offsetof(Sound, stopVolume) == 0xbc);
    static_assert(offsetof(Sound, stopFramesGone) == 0xc8 && offsetof(Sound, active) == 0xec);
    static_assert(offsetof(Sound, state) == 0x101 && offsetof(Sound, volume) == 0x140);
    static_assert(offsetof(Sound, target) == 0x144 && offsetof(Sound, volumeFramesGone) == 0x14c);

    // A BGM that plays or is about to (vtable 0x1b58cf0; 0x130 bytes; BgmPlayer::instances).
    struct BgmInstance {
        const uintptr_t* vtable;
        Sound* sound;              // 0x8
        String name;               // 0x10: the track
        u8 field_28[0x28];         // 0x28
        float field_50;            // 0x50: 1
        float field_54;            // 0x54: 1
        u32 fadeOutFrames;         // 0x58
        u8 pad_5c[0x4];
        Function onEvent[3];       // 0x60: by the BgmTable row's flags
        u8 kind;                   // 0xf0: 1 zone and event music, 2 jingle, 3 buff
        u8 pad_f1[0x3];
        u32 flags;                 // 0xf4: the BgmTable row's; bit 2 a buff's music
        s32 trackId;               // 0xf8: the BgmTable row, -1 none
        float volume;              // 0xfc
        float pitch;               // 0x100
        u32 fadeInFrames;          // 0x104: the frames it fades in over once it starts
        u8 field_108[0x8];         // 0x108
        Vec4 position;             // 0x110
        u8 field_120[0x8];         // 0x120
        u32 field_128;             // 0x128
        u8 prepareNow;             // 0x12c: BgmManager_Play's last argument
        u8 pad_12d;
        u8 field_12e;              // 0x12e
        u8 pad_12f;
    };
    static_assert(offsetof(BgmInstance, sound) == 0x8 && offsetof(BgmInstance, name) == 0x10);
    static_assert(offsetof(BgmInstance, fadeOutFrames) == 0x58 && offsetof(BgmInstance, onEvent) == 0x60);
    static_assert(offsetof(BgmInstance, kind) == 0xf0 && offsetof(BgmInstance, trackId) == 0xf8);
    static_assert(offsetof(BgmInstance, fadeInFrames) == 0x104 && offsetof(BgmInstance, position) == 0x110);
    static_assert(offsetof(BgmInstance, prepareNow) == 0x12c && sizeof(BgmInstance) == 0x130);
    constexpr u8 kZoneMusic = 1;

    // The BGM player (BgmPlayer_Ctor; 0x9f0 bytes): eight instances and the lists they move through.
    struct BgmPlayer {
        u8 pad_0[0x10];
        BgmInstance instances[8];           // 0x10
        Vector<BgmInstance*> free;          // 0x990: BgmManager_Play takes the last
        Vector<BgmInstance*> starting;      // 0x9a8: streams still being prepared, which no fade or stop reaches yet
        Vector<BgmInstance*> playing;       // 0x9c0
        Vector<BgmInstance*> vector_9d8;    // 0x9d8
    };
    static_assert(offsetof(BgmPlayer, instances) == 0x10 && offsetof(BgmPlayer, free) == 0x990);
    static_assert(offsetof(BgmPlayer, playing) == 0x9c0 && sizeof(BgmPlayer) == 0x9f0);

    // The audio manager (BgmManager_Ctor; 0x9420 bytes): sound effects, music, ambience and the outputs.
    struct BgmManager {
        u8 field_0[0x10];
        Vec4 field_10;                      // 0x10
        u8 field_20[0x10];                  // 0x20
        float field_30;                     // 0x30: 20
        u8 pad_34[0xc];
        u8 field_40[0x10];                  // 0x40
        u8 se[0x5870];                      // 0x50: the sound effects' players (Sound_Play)
        BgmPlayer bgm;                      // 0x58c0
        u8 field_62b0[0x2900];              // 0x62b0
        u8 ambience[0x570];                 // 0x8bb0: the zone's ambience
        void* audioHeap;                    // 0x9120
        void* audioHeap2;                   // 0x9128
        FixedString<0x40> outputTv;         // 0x9130
        FixedString<0x40> outputSpeaker;    // 0x9178
        FixedString<0x40> outputJack;       // 0x91c0
        FixedString<0x40> outputUsb;        // 0x9208
        FixedString<0x40> zoneTrack;        // 0x9250: the zone's track
        u8 field_9298[0x90];                // 0x9298
        u8 field_9328;                      // 0x9328
        u8 pad_9329[0x3];
        float zoneVolume;                   // 0x932c
        u8 field_9330[0x10];                // 0x9330
        FixedString<0x40> holdTrack;        // 0x9340: BgmManager_FadeZone fades nothing while it plays
        FixedString<0x40> field_9388;       // 0x9388
        FixedString<0x40> field_93d0;       // 0x93d0
        u8 initialized;                     // 0x9418
        u8 pad_9419[0x7];
    };
    static_assert(offsetof(BgmManager, se) == 0x50 && offsetof(BgmManager, bgm) == 0x58c0);
    static_assert(offsetof(BgmManager, ambience) == 0x8bb0 && offsetof(BgmManager, audioHeap) == 0x9120);
    static_assert(offsetof(BgmManager, zoneTrack) == 0x9250 && offsetof(BgmManager, zoneVolume) == 0x932c);
    static_assert(offsetof(BgmManager, holdTrack) == 0x9340 && offsetof(BgmManager, initialized) == 0x9418);
    static_assert(sizeof(BgmManager) == 0x9420);
}
