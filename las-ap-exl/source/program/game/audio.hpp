#pragma once
// The game's audio: its audio manager (the static Game::BgmManager), the BGM and sound effect instances, their nn::atk
// sounds, the environment sounds and the sound data loader (scripts/re/FRAMEWORK.md, Audio).
#include "game/actor.hpp"
#include "game/state_machine.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Audio {
    // A value moving to a target over frames (nn::atk's MoveValue): a fade.
    struct MoveValue {
        float value;
        float target;              // 0x4
        s32 frames;                // 0x8
        s32 framesGone;            // 0xc
    };
    static_assert(sizeof(MoveValue) == 0x10);

    // A playing sound: nn::atk::detail::BasicSound (BasicSound_Ctor, BasicSound_vtable 0x1c53248; 0x210 bytes, the
    // base of StreamSound, WaveSound, SequenceSound and AdvancedWaveSound). Sound_Stop fades it out over stopFade;
    // Sound_SetVolume(sound, frames, volume) sets the volume's fade.
    struct Sound {
        const uintptr_t* vtable;   // +0x18 Initialize, +0x20 Finalize, +0x48 the stop Sound_Stop calls
        void* playerHeap;          // 0x8
        void* handles[2];          // 0x10: back-links to the handles that hold it
        void* player;              // 0x20: its nn::atk SoundPlayer, which keeps its sounds in priority order
        void* soundActor;          // 0x28
        void* extPlayer;           // 0x30: Finalize removes the sound from it
        void* field_38;            // 0x38
        u8 ambientInfo[0x28];      // 0x40: {param, arg and allocator callbacks, arg, arg size}; Finalize frees arg
        float field_68[2];         // 0x68: 1, 1
        u64 field_70;              // 0x70
        s32 field_78;              // 0x78: -1
        s32 priority;              // 0x7c: with priorityBonus, the player's sort key
        s32 field_80;              // 0x80: -1
        u32 field_84;              // 0x84
        float field_88;            // 0x88: 1
        u8 field_8c[0x14];         // 0x8c
        float field_a0[3];         // 0xa0: 1, 1, 1
        u32 field_ac[3];           // 0xac: 0, 0, -1
        u32 field_b8;              // 0xb8
        MoveValue stopFade;        // 0xbc: Sound_FadeFramesLeft is frames - framesGone
        MoveValue pauseFade;       // 0xcc
        MoveValue fade2;           // 0xdc
        u8 active[2];              // 0xec: 1 started; Sound_Stop fades only while one is set
        u8 field_ee;               // 0xee: Sound_Stop clears it
        bool fadingOut;            // 0xef: set by Sound_Stop
        u8 field_f0;               // 0xf0
        u8 field_f1;               // 0xf1: Sound_Stop clears it
        u8 pad_f2[0x2];
        u32 field_f4;              // 0xf4
        u8 priorityBonus;          // 0xf8
        u8 field_f9;               // 0xf9: 0xff
        u8 pad_fa[0x2];
        u32 lifecycle;             // 0xfc: 1 initialized, 2 finalized, 3 destroyed
        u8 field_100;              // 0x100: 2 once finalized
        u8 state;                  // 0x101: 2 (likely paused) makes Sound_Stop finalize at once
        u8 field_102;              // 0x102
        u8 pad_103;
        u32 field_104[2];          // 0x104
        u32 playFrames;            // 0x10c: the SE player's restart limits compare it
        s32 id;                    // 0x110: the sound archive item (the BgmTable id)
        u32 instanceId;            // 0x114: a global counter
        u64 field_118;             // 0x118
        float field_120;           // 0x120: 1
        float field_124;           // 0x124: 1
        float field_128;           // 0x128
        float field_12c;           // 0x12c
        u32 field_130;             // 0x130: 1
        u8 pad_134[0x4];
        void* output;              // 0x138: Initialize's
        MoveValue volume;          // 0x140
        u8 params_150[0xc0];       // 0x150: more parameters, a gain per bus (+0x160) and the list nodes (+0x1e0,
                                   // +0x1f0 the player's priority link, +0x200)
    };
    static_assert(offsetof(Sound, player) == 0x20 && offsetof(Sound, ambientInfo) == 0x40);
    static_assert(offsetof(Sound, priority) == 0x7c && offsetof(Sound, stopFade) == 0xbc);
    static_assert(offsetof(Sound, pauseFade) == 0xcc && offsetof(Sound, active) == 0xec);
    static_assert(offsetof(Sound, priorityBonus) == 0xf8 && offsetof(Sound, lifecycle) == 0xfc);
    static_assert(offsetof(Sound, state) == 0x101 && offsetof(Sound, playFrames) == 0x10c);
    static_assert(offsetof(Sound, id) == 0x110 && offsetof(Sound, output) == 0x138);
    static_assert(offsetof(Sound, volume) == 0x140 && sizeof(Sound) == 0x210);

    // The base of the BGM and SE instances (SoundHandle_vtable 0x1c30a90): the sound it holds and its name.
    struct SoundHandle {
        const uintptr_t* vtable;
        Sound* sound;              // 0x8
        String name;               // 0x10
        u8 field_28[0x28];         // 0x28: zeroed by the instances' constructors, not read
        float field_50;            // 0x50: 1, not read
        float field_54;            // 0x54: 1, not read
        u32 fadeOutFrames;         // 0x58: every Sound_Stop of the handle's
        u8 pad_5c[0x4];
    };
    static_assert(offsetof(SoundHandle, sound) == 0x8 && offsetof(SoundHandle, name) == 0x10);
    static_assert(offsetof(SoundHandle, fadeOutFrames) == 0x58 && sizeof(SoundHandle) == 0x60);

    // A BGM instance's kind: zone and event music, an item's jingle, a buff's music, a positional track (the Great
    // Fairy's sets it after BgmManager_Play).
    enum class BgmKind : u8 { Zone = 1, Jingle = 2, Buff = 3, Positional = 4 };

    // A BGM that plays or is about to (vtable 0x1b58cf0; 0x130 bytes; BgmPlayer::instances).
    struct BgmInstance {
        SoundHandle handle;        // the track's name and sound
        Function onStart;          // 0x60: once, when it starts (BgmTable row flags bit 0)
        Function onUpdate[2];      // 0x90: each frame while playing ([0] by flags bit 1, or the positional update)
        BgmKind kind;              // 0xf0
        u8 pad_f1[0x3];
        u32 flags;                 // 0xf4: the BgmTable row's; bit 2 a buff's music
        s32 trackId;               // 0xf8: the BgmTable row, -1 none
        float volume;              // 0xfc
        float pitch;               // 0x100
        u32 startDelay;            // 0x104: frames the starting list waits before it starts the stream
        u8 pad_108[0x8];
        Vec4 position;             // 0x110: kind 4's (BgmInstance_SetPosition)
        float radius;              // 0x120: kind 4: full volume within
        float fadeRange;           // 0x124: kind 4: silent this far past radius
        u32 startOffset;           // 0x128: the stream's start offset when not 0
        u8 prepareNow;             // 0x12c: BgmManager_Play's last argument
        u8 ducked;                 // 0x12d: while a jingle plays
        u8 noPositional;           // 0x12e: the positional update gives volume 0
        u8 pad_12f;
    };
    static_assert(offsetof(BgmInstance, onStart) == 0x60 && offsetof(BgmInstance, onUpdate) == 0x90);
    static_assert(offsetof(BgmInstance, kind) == 0xf0 && offsetof(BgmInstance, trackId) == 0xf8);
    static_assert(offsetof(BgmInstance, startDelay) == 0x104 && offsetof(BgmInstance, position) == 0x110);
    static_assert(offsetof(BgmInstance, startOffset) == 0x128 && offsetof(BgmInstance, prepareNow) == 0x12c);
    static_assert(sizeof(BgmInstance) == 0x130);

    // The BGM player (BgmPlayer_Ctor; 0x9f0 bytes): eight instances and the lists they move through.
    struct BgmPlayer {
        u8 pad_0[0x10];
        BgmInstance instances[8];           // 0x10
        Vector<BgmInstance*> free;          // 0x990: BgmManager_Play takes the last
        Vector<BgmInstance*> starting;      // 0x9a8: streams still being prepared, which no fade or stop reaches yet
        Vector<BgmInstance*> playing;       // 0x9c0
        u8 pad_9d8[0x18];
    };
    static_assert(offsetof(BgmPlayer, instances) == 0x10 && offsetof(BgmPlayer, free) == 0x990);
    static_assert(offsetof(BgmPlayer, playing) == 0x9c0 && sizeof(BgmPlayer) == 0x9f0);

    // A sound effect that plays or is about to (SeInstance_vtable 0x1b59260; SeInstance_Setup, SeInstance_Release).
    struct SeInstance {
        SoundHandle handle;
        void* actor;               // 0x60: a caller's nn::atk 3D actor
        void* ownActor;            // 0x68: owned, made for a 3D SE without one
        Function onUpdate;         // 0x70: each frame
        Function onUpdate2;        // 0xa0: each frame
        Function function_d0;      // 0xd0
        float params3d[5];         // 0x100
        u8 pad_114[0xc];
        Vec4 position;             // 0x120
        s32 serial;                // 0x130: the play's, -1 free
        s32 soundId;               // 0x134
        u8 kind;                   // 0x138: 2 a 3D one-shot, 3 ambient line or wind, 4 zone environment, 5 its _EX
        u8 pad_139[0x3];
        u32 flags;                 // 0x13c: the SE table's (SePlayer_Admit)
        u32 flags2;                // 0x140: bit 0 keeps the volume
        s32 keepAlive;             // 0x144: Sound_PlayAmbient sets 4
        float volume;              // 0x148
        float pitch;               // 0x14c
        float volumeScale;         // 0x150: 1, the duck volume, or a cap
        u32 field_154;             // 0x154
        bool active;               // 0x158
        bool is3D;                 // 0x159
        bool echoArea;             // 0x15a
        u8 pad_15b[0x5];
    };
    static_assert(offsetof(SeInstance, onUpdate) == 0x70 && offsetof(SeInstance, params3d) == 0x100);
    static_assert(offsetof(SeInstance, position) == 0x120 && offsetof(SeInstance, serial) == 0x130);
    static_assert(offsetof(SeInstance, flags) == 0x13c && offsetof(SeInstance, volumeScale) == 0x150);
    static_assert(offsetof(SeInstance, active) == 0x158 && sizeof(SeInstance) == 0x160);

    // A play of an SE (Sound_PlayAt's out argument): valid while the instance's serial is still its.
    struct SeRef {
        SeInstance* instance;
        s32 serial;                // 0x8
        u8 pad_c[0x4];
    };
    static_assert(sizeof(SeRef) == 0x10);

    // The sound effects' player (SePlayer_Ctor; 0x5870 bytes): 64 instances, the free ones, the playing ones and the
    // ones to start at the next update.
    struct SePlayer {
        u8 pad_0[0x10];
        SeInstance instances[0x40];  // 0x10
        Vector<SeInstance*> free;  // 0x5810: Sound_Play takes the last
        Vector<SeInstance*> playing;  // 0x5828
        Vector<SeInstance*> queued;   // 0x5840: started (or dropped) by the next update
        s32 cooldown8;             // 0x5858: frames SEs with flags bit 3 are refused
        s32 cooldown16;            // 0x585c: bit 4
        s32 cooldown32;            // 0x5860: bit 5
        float duckVolume;          // 0x5864: 0.2 after an SE with flags bit 8, back up to 1
        s32 duckHold;              // 0x5868: frames before duckVolume rises
        s32 serial;                // 0x586c: the last play's
    };
    static_assert(offsetof(SePlayer, instances) == 0x10 && offsetof(SePlayer, free) == 0x5810);
    static_assert(offsetof(SePlayer, queued) == 0x5840 && offsetof(SePlayer, cooldown8) == 0x5858);
    static_assert(offsetof(SePlayer, serial) == 0x586c && sizeof(SePlayer) == 0x5870);

    // A countdown in frames.
    struct FrameTimer {
        float step;                // 1 / frames
        s16 elapsed;               // 0x4
        s16 frames;                // 0x6: -1 unset
    };
    static_assert(sizeof(FrameTimer) == 0x8);

    // The aux buses' effects: a reverb (kinds 1 to 4, bus 0) and a delay (kinds 5, 6, bus 1).
    struct AudioEffects {
        u8 reverbState;            // 0 none, 1 on, 2 being removed
        u8 delayState;             // 0x1
        u8 reverbKind;             // 0x2
        u8 nextReverbKind;         // 0x3: applied once the current one is removed
        u8 delayKind;              // 0x4
        u8 nextDelayKind;          // 0x5
        u8 pad_6[0x2];
        FrameTimer delayCooldown;  // 0x8: between automatic delay changes
    };
    static_assert(offsetof(AudioEffects, delayCooldown) == 0x8 && sizeof(AudioEffects) == 0x10);

    // An SE_AMB_LINE_BG line source: its volume falls with the listener's distance from the segment.
    struct AmbientLine {
        FixedString<0x40> name;
        u8 pad_48[0x8];
        Vec4 start;                // 0x50
        Vec4 end;                  // 0x60
        Vec4 nearest;              // 0x70: the segment's point nearest the listener
        float distance;            // 0x80
        float radius;              // 0x84: full volume within
        float fadeRange;           // 0x88
        float volume;              // 0x8c
    };
    static_assert(offsetof(AmbientLine, start) == 0x50 && offsetof(AmbientLine, distance) == 0x80);
    static_assert(sizeof(AmbientLine) == 0x90);

    // An SE at a position: a forest insect spot or a torch (EnvSe_AddLamp).
    struct EnvSeEmitter {
        FixedString<0x40> name;
        u8 pad_48[0x8];
        Vec4 position;             // 0x50
        SeRef ref;                 // 0x60
        s32 id;                    // 0x70: the owner's
        s32 zone;                  // 0x74: lamps play while Link is in it
        u8 pad_78[0x8];
    };
    static_assert(offsetof(EnvSeEmitter, ref) == 0x60 && offsetof(EnvSeEmitter, zone) == 0x74);
    static_assert(sizeof(EnvSeEmitter) == 0x80);

    // The environment's SEs (EnvSe_Ctor; 0x2900 bytes): line sources, insects, torches, height SEs, the cucco
    // attack, the frogs and the cove.
    struct EnvSePlayer {
        AmbientLine lines[12];
        EnvSeEmitter insects[3];   // 0x6c0
        Vector<EnvSeEmitter*> insectFree;    // 0x840
        Vector<EnvSeEmitter*> insectActive;  // 0x858
        EnvSeEmitter lamps[64];    // 0x870
        Vector<EnvSeEmitter*> lampFree;      // 0x2870
        Vector<EnvSeEmitter*> lampActive;    // 0x2888
        SeRef wind[3];             // 0x28a0: played while Link is high up
        u8 windPhase[3];           // 0x28d0
        u8 pad_28d3;
        s16 windTimer[3];          // 0x28d4
        bool windOff;              // 0x28da: an event action's
        u8 pad_28db;
        u32 coveProbe;             // 0x28dc: the 5x5 grid point ray-tested this frame
        u32 coveHits;              // 0x28e0: SE_AMB_COVE_BG's volume after 25 probes
        s32 cuccoHits;             // 0x28e4: SE_MAP_CUCCO_ATTACK loops from 4
        FrameTimer cuccoTimer;     // 0x28e8
        u32 frame;                 // 0x28f0
        FrameTimer frogTimer;      // 0x28f4
        u8 pad_28fc[0x4];
    };
    static_assert(offsetof(EnvSePlayer, insects) == 0x6c0 && offsetof(EnvSePlayer, lamps) == 0x870);
    static_assert(offsetof(EnvSePlayer, wind) == 0x28a0 && offsetof(EnvSePlayer, windTimer) == 0x28d4);
    static_assert(offsetof(EnvSePlayer, coveProbe) == 0x28dc && offsetof(EnvSePlayer, frogTimer) == 0x28f4);
    static_assert(sizeof(EnvSePlayer) == 0x2900);

    // A sound heap of the loader: slot 0 holds GROUP_COMMON, slots 1 to 3 a zone's groups.
    struct SoundSlot {
        u8 heap[0x50];             // nn::atk::SoundHeap
        void* memory;              // 0x50
        bool memoryMade;           // 0x58
        u8 pad_59[0x7];
        FixedString<0x40> group;   // 0x60
        FixedString<0x40> playLabel;  // 0xa8: the SE to play once loaded
        u8 field_f0;               // 0xf0: 0 slot 0, 2 the others
        u8 state;                  // 0xf1: 0 free, 1 claimed, 2 loaded
        u8 field_f2;               // 0xf2
        u8 pad_f3[0x5];
    };
    static_assert(offsetof(SoundSlot, group) == 0x60 && offsetof(SoundSlot, state) == 0xf1);
    static_assert(sizeof(SoundSlot) == 0xf8);

    // A load the loader is asked for (SoundLoader_Request).
    struct LoadRequest {
        FixedString<0x40> playLabel;
        FixedString<0x40> group;   // 0x48
        s32 groupId;               // 0x90
        s32 slot;                  // 0x94
        float volume;              // 0x98
        u8 field_9c;               // 0x9c: 1
        bool playAfter;            // 0x9d
        u16 field_9e;              // 0x9e
    };
    static_assert(offsetof(LoadRequest, groupId) == 0x90 && sizeof(LoadRequest) == 0xa0);

    // The sound data loader (SoundLoader_Ctor; 0x570 bytes): its heaps and the loads a worker task runs.
    struct SoundLoader {
        u8 pad_0[0x8];
        SoundSlot slots[4];        // 0x8
        Vector<LoadRequest> requests;  // 0x3e8
        u8 task[0x78];             // 0x400: the worker
        u8 mutex[0x20];            // 0x478
        bool running;              // 0x498
        bool busy;                 // 0x499: the shutdown waits while set
        u8 pad_49a[0x6];
        StateMachine<2> machine;   // 0x4a0
    };
    static_assert(offsetof(SoundLoader, requests) == 0x3e8 && offsetof(SoundLoader, running) == 0x498);
    static_assert(offsetof(SoundLoader, machine) == 0x4a0 && sizeof(SoundLoader) == 0x570);

    // The audio manager (BgmManager_Ctor; 0x9420 bytes): the listener, the effects, sound effects, music, the
    // environment's SEs, the loader and the outputs.
    struct BgmManager {
        Vec4 cameraEye;                     // AudioCamera_Update's
        Vec4 cameraTarget;                  // 0x10
        Vec4 listenerPos;                   // 0x20: 0.7 of the eye and 0.3 of Link
        float cameraFov;                    // 0x30: 20 when made
        u8 pad_34[0xc];
        AudioEffects effects;               // 0x40
        SePlayer se;                        // 0x50: Sound_Play
        BgmPlayer bgm;                      // 0x58c0
        EnvSePlayer envSePlayer;            // 0x62b0
        SoundLoader loader;                 // 0x8bb0
        void* audioHeap;                    // 0x9120
        void* audioHeap2;                   // 0x9128
        FixedString<0x40> outputTv;         // 0x9130
        FixedString<0x40> outputSpeaker;    // 0x9178
        FixedString<0x40> outputJack;       // 0x91c0
        FixedString<0x40> outputUsb;        // 0x9208
        FixedString<0x40> zoneTrack;        // 0x9250: the zone's track
        FixedString<0x40> envSe;            // 0x9298: the environment SE (the zone's, or an event's)
        FixedString<0x40> envGroup;         // 0x92e0: the sound group envSe needs
        u8 zoneEffect;                      // 0x9328: AudioEffects kind, 0 none
        u8 pad_9329[0x3];
        float zoneVolume;                   // 0x932c
        u32 eventStartSub;                  // 0x9330: signal handles
        u32 eventEndSub;                    // 0x9334
        u32 zoneSub;                        // 0x9338: BgmManager_OnZoneChanged
        u8 pad_933c[0x4];
        FixedString<0x40> holdTrack;        // 0x9340: BgmManager_FadeZone fades nothing while it plays
        FixedString<0x40> savedEnvSe;       // 0x9388: while an event's environment SE plays
        FixedString<0x40> savedEnvGroup;    // 0x93d0
        u8 initialized;                     // 0x9418
        bool quiet;                         // 0x9419: everything at 0.2
        bool eventQuiet;                    // 0x941a: SEs at 0.2 during an event
        bool zoneBgmNow;                    // 0x941b: the next zone change starts its BGM without delay
        u8 pad_941c[0x4];
    };
    static_assert(offsetof(BgmManager, listenerPos) == 0x20 && offsetof(BgmManager, effects) == 0x40);
    static_assert(offsetof(BgmManager, se) == 0x50 && offsetof(BgmManager, bgm) == 0x58c0);
    static_assert(offsetof(BgmManager, envSePlayer) == 0x62b0 && offsetof(BgmManager, loader) == 0x8bb0);
    static_assert(offsetof(BgmManager, audioHeap) == 0x9120 && offsetof(BgmManager, zoneTrack) == 0x9250);
    static_assert(offsetof(BgmManager, envSe) == 0x9298 && offsetof(BgmManager, zoneEffect) == 0x9328);
    static_assert(offsetof(BgmManager, zoneVolume) == 0x932c && offsetof(BgmManager, eventStartSub) == 0x9330);
    static_assert(offsetof(BgmManager, holdTrack) == 0x9340 && offsetof(BgmManager, savedEnvSe) == 0x9388);
    static_assert(offsetof(BgmManager, initialized) == 0x9418 && offsetof(BgmManager, zoneBgmNow) == 0x941b);
    static_assert(sizeof(BgmManager) == 0x9420);
}
