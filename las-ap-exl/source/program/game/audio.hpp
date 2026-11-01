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

    struct Sound;

    // nn::atk's ambient callbacks
    struct AmbientParamUpdateCallback {
        struct Vtable {
            void (*dtor)(AmbientParamUpdateCallback*);
            void (*deletingDtor)(AmbientParamUpdateCallback*);
            // param: the sound's ambient parameters, copied from its +0x68 and back
            void (*update)(AmbientParamUpdateCallback*, const void* arg, u32 soundId, void* param);
        };
        const Vtable* vtable;
    };
    struct AmbientArgUpdateCallback {
        struct Vtable {
            void (*dtor)(AmbientArgUpdateCallback*);
            void (*deletingDtor)(AmbientArgUpdateCallback*);
            void (*update)(AmbientArgUpdateCallback*, void* arg, const Sound* sound);
        };
        const Vtable* vtable;
    };
    struct AmbientArgAllocatorCallback {
        struct Vtable {
            void (*dtor)(AmbientArgAllocatorCallback*);
            void (*deletingDtor)(AmbientArgAllocatorCallback*);
            void* (*alloc)(AmbientArgAllocatorCallback*, size_t size);
            void (*free)(AmbientArgAllocatorCallback*, void* arg, const Sound* sound);
        };
        const Vtable* vtable;
    };

    // nn::atk's AmbientInfo. BasicSound_SetAmbientInfo copies the given arg into one argAllocator makes; Finalize frees
    // it.
    struct AmbientInfo {
        AmbientParamUpdateCallback* paramUpdate;
        AmbientArgUpdateCallback* argUpdate;  // 0x8
        AmbientArgAllocatorCallback* argAllocator;  // 0x10
        void* arg;                 // 0x18
        size_t argSize;            // 0x20
    };
    static_assert(sizeof(AmbientInfo) == 0x28);

    // nn::atk's OutputAmbientParam: the main output's part of the ambient parameters.
    struct OutputAmbientParam {
        float volume;
        float pan;                 // 0x4
        float surroundPan;         // 0x8
        float fxSend[3];           // 0xc: aux buses A to C
    };
    static_assert(sizeof(OutputAmbientParam) == 0x18);

    // The ambient parameters a sound keeps (nn::atk's BasicSound::AmbientParam): the param callback's result.
    struct AmbientParam {
        float volume;
        float pitch;               // 0x4
        float lpf;                 // 0x8
        float biquadFilterValue;   // 0xc
        s32 biquadFilterType;      // 0x10: -1 none
        s32 priority;              // 0x14: added to the sound's playerPriority
        u32 outputLineFlag;        // 0x18: -1 all
        u32 userData;              // 0x1c
        OutputAmbientParam tvParam;  // 0x20
    };
    static_assert(offsetof(AmbientParam, priority) == 0x14 && sizeof(AmbientParam) == 0x38);

    // nn::atk's SoundActorParam: the sound actor's parameters, copied from its +0xb0 each update.
    struct SoundActorParam {
        float volume;
        float pitch;               // 0x4
        float tvVolume;            // 0x8
        float tvPan;               // 0xc
        float lpf;                 // 0x10
        s32 biquadFilterType;      // 0x14: -1 none
        float biquadFilterValue;   // 0x18
    };
    static_assert(sizeof(SoundActorParam) == 0x1c);

    // nn::atk's OutputParam: the mix for one output device.
    struct OutputParam {
        float volume;
        u32 mixMode;               // 0x4: 0 pan, 1 mixParameter
        float mixParameter[2][6];  // 0x8: per wave channel, a gain per output channel
        float pan;                 // 0x38
        float surroundPan;         // 0x3c
        float mainSend;            // 0x40
        float fxSend[3];           // 0x44
    };
    static_assert(sizeof(OutputParam) == 0x50);

    // nn::util's IntrusiveListNode.
    struct IntrusiveListNode {
        IntrusiveListNode* prev;
        IntrusiveListNode* next;   // 0x8
    };

    // A playing sound: nn::atk::detail::BasicSound (BasicSound_Ctor, BasicSound_vtable 0x1c53248; 0x210 bytes, the
    // base of StreamSound, WaveSound, SequenceSound and AdvancedWaveSound). Sound_Stop fades it out over stopFade;
    // Sound_SetVolume(sound, frames, volume) sets the volume's fade.
    struct Sound {
        const uintptr_t* vtable;   // +0x18 Initialize, +0x20 Finalize, +0x40 the driver player, +0x48 the stop's
        void* playerHeap;          // 0x8
        void* handles[2];          // 0x10: back-links to the handles that hold it (general, temporary)
        void* player;              // 0x20: its nn::atk SoundPlayer, which keeps its sounds in priority order
        void* soundActor;          // 0x28
        void* extPlayer;           // 0x30: Finalize removes the sound from it
        const void* soundArchive;  // 0x38: the nn::atk SoundArchive it plays from
        AmbientInfo ambientInfo;   // 0x40
        AmbientParam ambientParam; // 0x68
        SoundActorParam actorParam;  // 0xa0
        MoveValue stopFade;        // 0xbc: Sound_FadeFramesLeft is frames - framesGone
        MoveValue pauseFade;       // 0xcc
        MoveValue muteFade;        // 0xdc
        bool startRequested;       // 0xec: the next update starts it once prepared
        bool started;              // 0xed
        bool autoStop;             // 0xee: Sound_Stop clears it
        bool fadingOut;            // 0xef: set by Sound_Stop
        bool playerAvailable;      // 0xf0: the driver sets it once its player is set up
        bool unpause;              // 0xf1: Sound_Stop clears it
        u8 pad_f2[0x2];
        u32 pauseMode;             // 0xf4: 1 pauses at once; Sound_Stop clears it
        u8 playerPriority;         // 0xf8: with ambientParam.priority, the player's sort key
        s8 biquadFilterType;       // 0xf9: -1 none
        u8 pad_fa[0x2];
        u32 lifecycle;             // 0xfc: 1 initialized, 2 finalized, 3 destroyed
        u8 playerState;            // 0x100: 1 playing, 2 stopped
        u8 pauseState;             // 0x101: 1 pausing, 2 paused (Sound_Stop finalizes at once), 3 unpausing
        u8 muteState;              // 0x102: as pauseState, for muteFade
        u8 pad_103;
        s32 autoStopCounter;       // 0x104
        u32 updateCounter;         // 0x108
        u32 playFrames;            // 0x10c: frames played unpaused; the SE player's restart limits compare it
        s32 id;                    // 0x110: the sound archive item (the BgmTable id)
        u32 instanceId;            // 0x114: a global counter
        u64 setupTick;             // 0x118
        float initVolume;          // 0x120: 1
        float pitch;               // 0x124: 1
        float lpfFreq;             // 0x128
        float biquadFilterValue;   // 0x12c
        u32 outputLineFlag;        // 0x130: 1
        u8 pad_134[0x4];
        void* output;              // 0x138: Initialize's nn::atk OutputReceiver
        MoveValue volume;          // 0x140
        u32 mixMode;               // 0x150
        float pan;                 // 0x154
        float surroundPan;         // 0x158
        float send[4];             // 0x15c: the main send, then aux buses A to C
        OutputParam outputParam;   // 0x16c
        u8 pad_1bc[0x4];
        void* outputAdditionalParam;  // 0x1c0
        void* userParam;           // 0x1c8: Initialize zeroes userParamSize bytes of it
        size_t userParamSize;      // 0x1d0
        void (*stopCallback)();    // 0x1d8: Finalize calls and clears it
        IntrusiveListNode playerPlayLink;      // 0x1e0
        IntrusiveListNode playerPriorityLink;  // 0x1f0: SoundPlayer_SortPriority's
        IntrusiveListNode extPlayerLink;       // 0x200
    };
    static_assert(offsetof(Sound, player) == 0x20 && offsetof(Sound, ambientInfo) == 0x40);
    static_assert(offsetof(Sound, actorParam) == 0xa0 && offsetof(Sound, stopFade) == 0xbc);
    static_assert(offsetof(Sound, pauseFade) == 0xcc && offsetof(Sound, startRequested) == 0xec);
    static_assert(offsetof(Sound, playerPriority) == 0xf8 && offsetof(Sound, lifecycle) == 0xfc);
    static_assert(offsetof(Sound, pauseState) == 0x101 && offsetof(Sound, playFrames) == 0x10c);
    static_assert(offsetof(Sound, id) == 0x110 && offsetof(Sound, output) == 0x138);
    static_assert(offsetof(Sound, volume) == 0x140 && offsetof(Sound, outputParam) == 0x16c);
    static_assert(offsetof(Sound, stopCallback) == 0x1d8 && offsetof(Sound, playerPriorityLink) == 0x1f0);
    static_assert(sizeof(Sound) == 0x210);

    // The base of the BGM and SE instances (SoundHandle_vtable 0x1c30a90): the sound it holds and its name.
    struct SoundHandle {
        const uintptr_t* vtable;
        Sound* sound;              // 0x8
        String name;               // 0x10
        u8 unused_28[0x28];        // 0x28: zeroed by the instances' constructors, never read
        float unused_50;           // 0x50: 1 from the constructors, never read
        float unused_54;           // 0x54: 1 from the constructors, never read
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
        float pitch;               // 0x100: BgmInstance_Start gives it to the sound
        u32 startDelay;            // 0x104: frames the starting list waits before it starts the stream
        u8 pad_108[0x8];
        Vec4 position;             // 0x110: kind 4's (BgmInstance_SetPosition)
        float radius;              // 0x120: kind 4: full volume within
        float fadeRange;           // 0x124: kind 4: silent this far past radius
        u32 startOffset;           // 0x128: the stream's start offset when not 0; the game only zeroes it
        u8 prepareNow;             // 0x12c: BgmManager_Play's last argument
        u8 ducked;                 // 0x12d: while a jingle plays
        u8 noPositional;           // 0x12e: the positional update gives volume 0; NpcMarin's singing sets it
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

    // A pitch wobble: value swings between min and max by step each frame (onUpdate2) and goes to apply.
    struct PitchWobble {
        Function apply;            // void(f32): sets the SE's sound's pitch
        u32 falling;               // 0x30
        float value;               // 0x34
        float step;                // 0x38
        float max;                 // 0x3c
        float min;                 // 0x40
        u8 pad_44[0xc];
    };
    static_assert(offsetof(PitchWobble, falling) == 0x30 && sizeof(PitchWobble) == 0x50);

    // A sound effect that plays or is about to (SeInstance_vtable 0x1b59260; SeInstance_Setup, SeInstance_Release).
    struct SeInstance {
        SoundHandle handle;
        void* actor;               // 0x60: a caller's nn::atk 3D actor
        void* ownActor;            // 0x68: owned, made for a 3D SE without one
        Function onUpdate;         // 0x70: each frame
        Function onUpdate2;        // 0xa0: each frame
        PitchWobble wobble;        // 0xd0: a 3D one-shot's in the echo area, unless flags bit 13
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
        u32 unused_154;            // 0x154: zeroed by SePlayer_Ctor and SeInstance_Release, never read
        bool active;               // 0x158
        bool is3D;                 // 0x159
        bool echoArea;             // 0x15a
        u8 pad_15b[0x5];
    };
    static_assert(offsetof(SeInstance, onUpdate) == 0x70 && offsetof(SeInstance, wobble) == 0xd0);
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

    // The aux buses' effects: nn::atk EffectReverb (kinds 1, 2) or EffectI3dl2Reverb (3, 4) on bus 0, EffectDelay
    // (5, 6) on bus 1.
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
        u8 mode;                   // 0xf0: 0 slot 0, 2 the others (SoundLoader_Request's slot modes); never read
        u8 state;                  // 0xf1: 0 free, 1 claimed, 2 loaded
        bool dropLoad;             // 0xf2: a load that ends with it set is undone and the slot freed; never set
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
        bool async;                // 0x9c: loaded by the worker task, else at once; SoundLoader_Request sets 1
        bool playAfter;            // 0x9d
        bool playEnvSe;            // 0x9e: BgmManager_PlayEnvSe runs once the group is loaded
        u8 pad_9f;
    };
    static_assert(offsetof(LoadRequest, groupId) == 0x90 && sizeof(LoadRequest) == 0xa0);

    // The sound data loader (SoundLoader_Ctor; 0x570 bytes): its heaps and the loads a worker task runs.
    struct SoundLoader {
        u8 pad_0[0x8];
        SoundSlot slots[4];        // 0x8
        Vector<LoadRequest> requests;  // 0x3e8
        ThreadPool task;           // 0x400: one thread; runs the async loads
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
        bool quiet;                         // 0x9419: everything at 0.2; never set, only cleared
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
