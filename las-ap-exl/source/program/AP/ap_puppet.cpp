// The ghosts of multiplayer ghosts (DESIGN.md, Ghosts). One puppet per ghost slot of AP::GhostNet, and in the test
// build one for DBG puppet: an NPC actor spawned at runtime through the game's actor factory (as a chest
// pops its enemy), drawn with Link's model (its Npc row, set up by ghost_puppet.py) dithered by alpha-to-coverage,
// and moved by hand every frame: replaying the positions and animations it is fed on their sender's clock.
// The actor system owns the actors, and they outlive level changes: each puppet keeps a weak reference to its actor
// and reuses it for the whole session, hidden while its ghost is not shown.
#include "AP/ap.hpp"
#ifdef LASAP_TEST
#include "AP/ap_debug.hpp"
#endif
#include "Game/framework.hpp"
#include "Hooks/Common/level.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
    struct SharedPtr { void* ptr; void* ctrl; };

    // Actor factory (game 1.0.0): void(SharedPtr* out, SharedPtr* owner, Params*). The player's hookshot calls it with
    // the actor system as owner and these params, zeroed but for:
    constexpr uintptr_t kCreateActor = 0x8f5ea0;
    constexpr size_t kParamsSize = 0x1c0;
    constexpr size_t kParamId = 0x8;       // u16 actor id (the actor table's index, as in LEB files)
    constexpr size_t kParamRoom = 0xc;     // s32 the actor's zone (+0xb0); the hookshot copies its owner's
    constexpr size_t kParamPos = 0x180;    // Vec4
    constexpr size_t kParamRot = 0x190;    // quaternion; PopEnemy copies the identity from a global
    constexpr size_t kParamScale = 0x1a0;  // Vec4 of ones, from a global
    constexpr uintptr_t kIdentityRot = 0x1c93de8, kOneScale = 0x1c93df0;  // globals: pointers to 16 bytes
    // What PopEnemy releases afterwards: a weak_ptr's control block at +0x1b8, shared_ptr control blocks at +0x168
    // and +0x158, then three member destructors.
    constexpr uintptr_t kParamsDtor0 = 0x1cf70, kParamsDtor1 = 0x1d190, kParamsDtor2 = 0x1d3b0;
    constexpr size_t kParamsDtor0At = 0xd0, kParamsDtor1At = 0x50, kParamsDtor2At = 0x10;
    constexpr uintptr_t kReleaseWeak = 0x14311f0;       // std::__shared_weak_count::__release_weak
    constexpr uintptr_t kGameNew = 0x1431170, kGameDelete = 0x1431110;  // the game's operator new and delete
    // libc++ control block: +8 shared owners - 1 (-1: the object is gone), +0x10 weak owners - 1
    constexpr size_t kCtrlOwners = 8, kCtrlWeakOwners = 0x10;
    constexpr size_t kFrameworkActorSystem = 0x498;  // shared_ptr
    constexpr size_t kActorRoot = 0x80;    // the root component (coordinates), as PopEnemy reads it
    // Ghosts are NpcGhost actors (0x179) with an Npc row of their own. Each NPC class passes its row's symbol, a
    // literal in its code ("NpcGhost" at 0x16ebdf4), to the NPC component constructor (0x943e90: component, args,
    // {name, length}*, ?), which looks the row up (0xfa6370: sheet, {name, length}*, row size 0x220; null when
    // missing) in the Npc sheet (*(*0x1cb8360)). All of it runs inside the factory call, so while the module's own
    // spawn runs the key is APGhost, if the sheet has that row (ghost_puppet.py adds it; a missing row crashes).
    constexpr u16 kGhostActor = 0x179;
    constexpr uintptr_t kNpcComponentCtor = 0x943e90, kFindRow = 0xfa6370, kNpcSheets = 0x1cb8360;
    constexpr uintptr_t kNpcGhostKey = 0x16ebdf4;
    constexpr size_t kNpcRowSize = 0x220;
    struct StringView { const char* ptr; u64 len; };
    StringView g_GhostRow{"APGhost", 7};
    bool g_SpawningGhost = false;
    // Removal as the game does it (0x8e3440): RemoveChild (0xfb00c0: parent, name, length, immediate) from the actor
    // system, keyed by the actor's name (+0x18, length +0x38); not immediate, the entity manager destroys it at its
    // next flush. Actor +0x40 flags: bit 30 destroy requested, bit 31 destroyed.
    constexpr uintptr_t kRemoveChild = 0xfb00c0;
    constexpr size_t kActorName = 0x18, kActorNameLength = 0x38, kActorFlags = 0x40;
    constexpr u64 kHiddenRemoveMs = 10000;  // a ghost hidden this long loses its actor
    using CreateFn = void (*)(SharedPtr*, SharedPtr*, u8*);
    using DtorFn = void (*)(u8*);
    using ReleaseWeakFn = void (*)(void*);

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    long* CtrlCount(void* ctrl, size_t at) { return reinterpret_cast<long*>(reinterpret_cast<u8*>(ctrl) + at); }

    void ReleaseShared(void* ctrl) {
        if (ctrl == nullptr) return;
        if (__atomic_fetch_sub(CtrlCount(ctrl, kCtrlOwners), 1, __ATOMIC_ACQ_REL) == 0) {
            auto** vtable = *reinterpret_cast<void (***)(void*)>(ctrl);
            vtable[2](ctrl);  // __on_zero_shared
            GameFn<ReleaseWeakFn>(kReleaseWeak)(ctrl);
        }
    }

    // Animations by name: the NPC's animation component (actor +0x2b0) has a virtual play (0x949260, overridden by
    // 0x9df590): (component, {name, length}*, mode, blend seconds, frame). Mode 0 looks the name up in its animation
    // set (+0x1e8, cache +0x218) and hands it to its animation player (+0xe8) at frame 0 and rate 0 (the player's
    // virtual +0xc0 frame, +0xd8 rate); mode 2 sets the rate 1. The slot is found in the component's vtable at its
    // first use. The animation player's +0x90 is the model object it animates.
    using NameView = StringView;
    using PlayByNameFn = void (*)(void* comp, NameView* name, u8 mode, float blend, float frame);
    constexpr uintptr_t kPlayByName[] = {0x949260, 0x9df590};
    constexpr size_t kActorAnimComp = 0x2b0, kAnimCompPlayer = 0xe8, kAnimPlayerModel = 0x90;
    constexpr size_t kSetFrameSlot = 0xc0 / 8;
    int g_PlaySlot = -1;  // -1: not looked up; -2: not in the component's vtable
    // The upper body, as Link has it: every NPC component class makes its animation player through the same
    // NpcComponent_MakeAnimator (0x94b310, vtable +0x208: out, component, {name, length}*, the model component's
    // shared_ptr*), a single-layer SkeletalAnimationComponent, each time the actor loads its model. For a ghost's
    // component (its row, +0x88, APGhost's) the module makes Link's kind instead (Player_InitAnimator):
    // LayeredAnimation_Make (0x67ba70: out, the component's entity +0x58, {name, length}*, model component sp*, int*
    // layers) with two layers, layer 1 masked to the bones from spine_a up (0x1035390: layers +0x1b0, layer, bone
    // name, true) and off (0x10355f0: layers +0x1b0, layer, on). Playing on layer 0 stays the NPC's (the layered
    // player's set-animation is layer 0's); layer 1 takes the sender's upper-body animation as Link's controller
    // hands it over (PlayerAnimation_SetSlot): LayeredAnimation_SetSlot (0x1059dd0: player, layer, animation sp*,
    // blend, start frame) with the bound animation by name (NpcAnimation_Get 0x9670d0: out sp, NPC component,
    // {name, length}*), then the layer's virtual +0x30 (loops) and +0x48 (0), and the layer turned on. Like the
    // single-layer player (NpcAnimator_Make), the layered one is then started (the entity manager's start task) and
    // made a child of the component's entity (0xfa9e40: entity, child), which binds it to the model.
    constexpr uintptr_t kMakeAnimator = 0x94b310, kLayeredMake = 0x67ba70, kLayerBoneMask = 0x1035390,
                        kLayerEnable = 0x10355f0, kLayeredSetSlot = 0x1059dd0, kNpcAnimGet = 0x9670d0,
                        kSpineA = 0x16b6ecb, kLayeredVtable = 0x1c49ee8, kAddChild = 0xfa9e40;
    constexpr size_t kNpcRow = 0x88, kNpcEntity = 0x58, kSacLayerSet = 0x1b0, kSacLayerCount = 0x1c8,
                     kSacLayers = 0x1d0, kLayerSetLoop = 0x30 / 8, kLayerSetStart = 0x48 / 8;
    constexpr int kUpperLayer = 1;

    // Model object: +0 the FMDL, +0x10 u32* material visibility bits, +0x30 u16 shape count, +0x32 u16 material
    // count, +0x40 shapes (0x70 apart; +0x18 pool offset of the shape's block), +0x48 materials (0x80 apart), +0x68
    // the memory pool object (+0x50 its GPU address). Material: +0 FMAT, +0x20 u32 block copies, +0x38 pool offset
    // of its shader parameter block, +0x60 block size; the copies are the size rounded up to 0x100 apart.
    constexpr size_t kModelMatVis = 0x10, kModelShapeCount = 0x30, kModelMatCount = 0x32, kModelShapes = 0x40,
                     kModelMats = 0x48, kModelPool = 0x68;
    constexpr size_t kShapeStride = 0x70, kShapeBlock = 0x18;
    constexpr size_t kMatStride = 0x80, kMatCopies = 0x20, kMatBlock = 0x38, kMatBlockSize = 0x60;
    constexpr size_t kPoolGpu = 0x50;
    // Link's materials drawn for a ghost: his body and the items on his model. Which of its shapes draw is the
    // ghost's look (SetGear).
    const char* const kShownMaterials[] = {"MI_body", "MI_boots", "MI_ear", "MI_eye", "MI_mouth", "MI_hair",
                                           "linkShieldA_MI_shieldA", "linkSwordA_MI_sowrdA", "linkSwordA_MI_sowrdAball",
                                           "MI_flippers", "MagicRod_MI_magicRod", "Ocarina_MI_ocarina", "Shovel_MI_shovel",
                                           "linkHookShot_MI_hookShot", "linkShieldB_MI_shieldB",
                                           "linkShieldB_MI_shieldBMirror", "linkSwordB_MI_sowrdB",
                                           "linkSwordB_MI_sowrdBball"};
    // The look's shapes: the animation player's model instance (+0x88) has one byte per shape (+0x378), 1 drawn; a
    // shape draws only with its byte, its material's visibility bit and its bone's all set. Nothing but the model's
    // creation and the shape setters writes them.
    constexpr size_t kAnimPlayerInstance = 0x88, kInstanceShapes = 0x378, kInstanceModel = 0x60, kModelObject = 0x10;
    // The look's tunic: the material animation "clothes" swaps MI_body's albedo (frames 0 green, 1 red, 3 blue).
    // NpcComponent_PlayMaterialAnimation (0x9c1b30: out shared_ptr, NPC component, {name, length}*, at once) puts it
    // on a slot of the NPC's material animation component (NPC component +0x1b8; slots +0xa0, count +0xb8; a slot's
    // +0x8 its animation); the slot is the facial ones' count (NPC component +0x198 -> +0xf0: +0x38, one more each
    // for +0x18 and +0x28 set) unless +0x198 is missing or its +0x43 bit 6 is set, then 0. Slot virtuals +0x48 set
    // frame, +0x60 set rate.
    constexpr uintptr_t kPlayMaterialAnim = 0x9c1b30;
    constexpr size_t kNpcFacial = 0x198, kFacialFlags = 0x43, kFacialSet = 0xf0, kFacialCount = 0x38,
                     kFacialExtraA = 0x18, kFacialExtraB = 0x28;
    constexpr size_t kNpcMaterialAnim = 0x1b8, kMatAnimSlots = 0xa0, kMatAnimSlotCount = 0xb8, kSlotAnim = 0x8;
    constexpr size_t kSlotSetFrame = 0x48 / 8, kSlotSetRate = 0x60 / 8;
    constexpr float kTunicFrames[] = {0, 1, 3};
    constexpr int kNoTunic = -2;
    // The face: Link's eyes, mouth and ears are material animations held at a frame ("eye", "mouth", "ear"). The
    // APGhost row's facial row (Facial.gsheet, ap_player_ghost.py) names them, so the NPC's material animation
    // component has a slot for each (before the tunic's), at frame 0 with no blinking; the module finds each by its
    // animation's name (slot +0x8, its resource at +0x18, the name at +0x8) and holds its frame (+0x24).
    // The instanced face (Instanced models) draws as the model draws without these animations, which is frame 0 of
    // each; a ghost with another face draws its own face shapes, the frames set on its slots.
    constexpr size_t kMatAnimRes = 0x18, kResName = 0x8, kSlotFrame = 0x24;
    constexpr const char* kFaceAnims[] = {"eye", "mouth", "ear"};
    constexpr int kFaceParts = 3;
    constexpr int kFaceFrames[kFaceParts] = {14, 14, 2};  // the FMAAs' frames
    constexpr u32 kFaceShapes = 0x380;               // the ear, eyes and mouth
    bool g_LayeredGhosts = true;  // DBG puppet layered on|off: new ghost models get the layered animation player

    // Replay: a puppet is fed frames (a ghost's lines, or Link's own path for DBG puppet), each stamped with the
    // sender's clock, and shows them on that timeline g_BufferMs behind: its position between the two frames around
    // that time, its facing that of the last frame before it (Link turns in steps: a turn is not smoothed). The
    // sender's clock maps to this one by the least delay seen, drifting up slowly, so a line that arrives late waits
    // less instead of moving the ghost faster. Past the newest frame it goes on along the last segment for up to
    // kExtrapolateMs, unless that frame stood still, then holds. A warped frame is jumped to at its time. A frame more
    // than kResetMs behind the newest starts a new timeline. When new lines move the replay's position (the sender
    // turned or stopped while the replay ran on past its newest line), the puppet does not jump: the difference is
    // added to where it shows and fades out with the time constant kCorrectMs (unless it is over kCorrectMax units).
    // A ghost that arrives (a warped frame: another level, a respawn, a warp) stays hidden from that frame's time
    // until a frame after it moved, started or changed the animation, or kArriveHoldMs pass on its clock: a player
    // entering a level stands still through its own screen's fade-in first (falling into the well: on the floor, then
    // fall_from_top drops the model from above while the position sinks). It shows again with that frame, whose
    // animation then plays unblended.
    constexpr int kTrack = 32;
    constexpr double kArriveHoldMs = 1000;
    constexpr float kArriveMove = 0.05f;
    constexpr double kExtrapolateMs = 100, kOffsetRise = 0.02, kCorrectMs = 100;
    constexpr float kCorrectMax = 2;
    constexpr s64 kResetMs = 1000;
    // Two lines' time (20 a second while moving), so the next line has mostly arrived when the replay needs it: a
    // shorter buffer runs on past the newest line and corrects when the next one comes in, which shows as jerks
    // (2026-10-15, 30 ghosts: per-frame speed spread 1.06 at 33 ms, 0.31 at 66, 0.24 at 100).
    double g_BufferMs = 100;  // DBG puppet buffer
    using Frame = AP::GhostNet::Frame;
    struct Track {
        Frame frames[kTrack];  // newest at frames[(count - 1) % kTrack]
        u32 count = 0;
        double offset = 0;     // this clock minus the sender's (ms), the least seen
        int gait = -1;         // index in kGaits, -1: none played yet
        int anim = -1;         // the sender's animation id playing, -1: none played yet
        u32 started = ~0u;     // the frame (count) whose animation start was played last
        Game::Vec3 pos{};
        Game::Vec3 error{};    // added to the replay's position, fading out (kCorrectMs)
        u32 replayed = 0;      // count at the last frame
        u32 arrival = 0;       // the frame (count) of the last warped frame + 1, 0: none waiting (ArrivalHeld)
        bool arriving = false; // the replay holds the ghost hidden at its arrival
        double lastNow = 0;
        float moved[60] = {};  // speed in each of the last 60 frames (DBG puppet: smoothness)
        u32 shownFrames = 0;
    };
    // Animations: the sender's (its PlayerAnimation.gsheet key, named in this game's sheet, the same FSKA in the
    // puppet's model), from the frame it started at, played when the replay reaches that frame; for a sender that
    // sends none (anim id 0), by speed (units per second): each from its threshold on, with a margin either side so a
    // speed near one does not switch back and forth.
    // Blend times in seconds (the layer adds the frame's time to its progress): Link's own (PlayerAnimation's
    // default, 0x16afc44) for the sender's animations, a little longer between gaits.
    float g_AnimBlend = 0.0667f;  // DBG puppet blend
    constexpr float kGaitBlend = 0.1f;
    constexpr int kFlagStarted = 8;
    constexpr const char* kGaits[] = {"wait", "walk", "run", "dash_lp"};
    constexpr float kGaitFrom[] = {0, 0.4f, 3.5f, 7.5f}, kGaitMargin = 0.3f;
    int GaitAt(float speed) {
        int g = 0;
        while (g + 1 < 4 && speed >= kGaitFrom[g + 1]) g++;
        return g;
    }

    // Puppets 0..SLOTS-1 show GhostNet's ghost slots; the last is DBG puppet's.
#ifdef LASAP_TEST
    constexpr int kGhosts = AP::GhostNet::SLOTS, kTest = kGhosts, kPuppets = kGhosts + 1;
#else
    constexpr int kGhosts = AP::GhostNet::SLOTS, kPuppets = kGhosts;
#endif
    // A ghost whose last line is older than this is not shown (the relay and the client send offs; this covers a
    // lost one). A still player's game sends a position twice a second.
    constexpr u64 kStaleMs = 5000;
    constexpr int kFlagWarped = 1, kFlagHidden = 2, kFlagStill = 4;
    constexpr u32 kRespawnFrames = 60;  // after a failed spawn
    // The name tag's point: above the head bone of the ghost's model (where Link's head is drawn, so it follows a
    // fall from above, a jump or a warp's lift), or above its feet while the model has no skeleton yet.
    constexpr int kHeadBone = 28;
    constexpr float kTagAboveHead = 0.68f, kTagHeight = 1.3f;  // kTagHeight: above the feet (the head standing + 0.68)
    bool g_NameTags = true;             // DBG puppet tags on|off
    bool g_DrawGhosts = true;           // DBG puppet draw off: models hidden, all else runs (finding the cost)
    bool g_Dither = true;               // DBG puppet dither off: drawn without the dither state
    u32 g_ShapeMask = 0xfffff;          // DBG puppet shapes: the look's shapes the puppets may draw
    u64 g_TickTicks[60] = {};           // the module's Tick per frame (system ticks), for DBG puppet
#ifdef LASAP_TEST
    u32 g_InfoDraws[8] = {};            // g_DrawsBy at the last DBG puppet
    u32 g_InfoFrame = 0;
#endif
    constexpr u32 kNoRowFrames = 600;   // after a spawn without the APGhost row
    // An inactive actor (entity flags (f & 0x40000017) != 0x17) is neither updated nor drawn. A shown puppet whose
    // actor was never active this long is made again (a backstop: zone -1 keeps the zones from suspending it).
    constexpr u32 kActiveMask = 0x40000017, kActive = 0x17;
    constexpr u32 kInactiveFrames = 30;
    u32 g_Remade = 0;  // DBG puppet: actors made again

    struct Puppet {
        void* actor = nullptr;
        void* ctrl = nullptr;    // a weak reference to the actor
        u8* model = nullptr;     // once loaded
        AP::GhostNet::Look look;                 // the look of the frame replayed
        int tunic = -1;                          // the tunic set, -1: none yet, kNoTunic: the model has none
        void* tunicAnim = nullptr;               // the animation on the tunic's slot when it was set
        int upper = -1;                          // the anim id on the upper body's layer, -1: not set on this model
        int faceSlots[kFaceParts] = {-1, -1, -1};  // kFaceAnims' slots, -1: not found (yet)
        int faceSet[kFaceParts] = {-1, -1, -1};    // the frames set on them
        bool ownFace = false;                    // draws its own face shapes (another face than the instanced one)
        bool shown = false;
        u64 hiddenSince = 0;     // ms
        u32 spawnAfter = 0;      // frame
        Track track;
        u32 framesSeen = 0;      // the ghost's lines fed so far
        u32 inactiveFrames = 0;  // shown with an inactive actor
        bool wasActive = false;  // its actor has been active
        int detail = 2;          // 2: its whole look drawn, 1: only kCrowdShapes, 0: none (crowds, Rank)
        u32 shownMats[4] = {};   // the model's material visibility words a ghost draws with (kShownMaterials)
        int matWords = -1;       // their count, -1: not computed for this model
        u32 readyFrames = 0;     // frames its body has drawn in a row (visibility kept, actor active)
        bool ready = false;      // its body draws: its name tag and instanced shapes may show
#ifdef LASAP_TEST
        u32 visFixes = 0;        // frames whose visibility the module had to write again
        std::vector<std::string> animLog;  // its plays and model loads, for DBG puppet animlog
        u32 shownAt = 0;                   // frame it was last shown
#endif
    };
    Puppet g_Puppets[kPuppets];
    // Each shown puppet's blocks as GPU addresses [lo, hi), for the render thread's hooks; hi 0: none.
    struct Span { std::atomic<u64> lo{0}, hi{0}; };
    // one per puppet and one per instanced model
    constexpr int kInstSpan = kPuppets, kInstSpans = 9;
    Span g_Spans[kPuppets + kInstSpans];

    u32 g_LastFrame = 0;

    // Crowds. Each shape a ghost draws costs a draw call in each of two render passes, and on an emulator the draw
    // calls limit the frame rate (2026-10-15, Ryubing: 60 fps up to about 120 ghost shapes a pass, whatever their
    // size; a ghost in its usual look draws about 6). The nearest ghosts draw their whole look, the next ones only
    // kCrowdShapes (arms, tunic, face, hair), the farthest none. How many of each follows the frame rate: down by a
    // quarter after a second under kSlowFps, up by one after kRaiseMs at kSteadyFps or more, but back to a count that
    // was too slow only after kRetryMs. The kMinWhole nearest are always whole and the kMinDrawn nearest always drawn
    // (the game's own slow moments, as after a level load, which kSettleMs skips, are no reason to hide a few).
    // Ranked by distance, less g_KeepMargin per step of detail a ghost has: one near a limit keeps its detail until
    // another is that much nearer, so ghosts walking about at about the same distance do not switch back and forth.
    constexpr u32 kCrowdShapes = (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 11);
    constexpr float kSlowFps = 55, kSteadyFps = 59;
    constexpr u64 kRaiseMs = 3000, kRetryMs = 30000, kSettleMs = 3000;
    constexpr int kMinWhole = 6, kMinDrawn = 16;
    int g_FullLimit = 8;         // ghosts drawn whole
    int g_DrawnLimit = kGhosts;  // ghosts drawn at all
    u32 g_CrowdMask = kCrowdShapes;
    // The shapes the instanced models draw (InstShapes): the puppets draw none of them.
    constexpr u32 kInstancedShapes = 0xff387;
    float g_KeepMargin = 2;      // DBG puppet keep
    u32 g_DetailChanges = 0;     // DBG puppet: ghosts' detail changed by Rank
    bool g_Adapt = true;         // DBG puppet lod <full> <drawn> fixes the limits, lod auto adapts them again
    u64 g_FpsStart = 0, g_LastChange = 0, g_TooSlowAt = 0, g_SettleUntil = 0;
    int g_TooSlowFull = kGhosts + 1, g_TooSlowDrawn = kGhosts + 1;  // the counts last found too slow
    u32 g_FpsFrame = 0;
    float g_Fps = 60;

#ifdef LASAP_TEST
    // DBG puppet framelog: per frame of the module's tick, the pause flag (Framework +0x510), the ghost draws and the
    // instanced models' draws recorded so far, the instanced records written and the ghosts ready
    struct FrameRow { u32 frame; u8 paused; u32 draws, instDraws, records, ready; };
    constexpr u32 kFrameLog = 120;
    constexpr size_t kFrameworkPaused = 0x510;
    FrameRow g_FrameLog[kFrameLog];
    u32 g_FrameLogCount = 0;
    struct Sample { Game::Vec3 pos; Game::Vec4 rot; };
    // DBG puppet: Link's path g_Delay frames late, fed g_SteerHz times a second (0: written as it is)
    constexpr int kHistory = 1024;
    int g_Delay = -2;  // -1: stand at g_At; -2: not driven
    int g_SteerHz = 0;
    Game::Vec3 g_At{};
    Sample g_History[kHistory];
    u32 g_HistoryFrame = 0, g_TestFedFrame = 0, g_TestStarts = 0;
    bool g_HistoryValid = false;
    std::string g_AnimPending;  // from DBG puppet anim, played on the game thread
#endif

    u64 Ticks() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks;
    }
    u64 NowMs() { return Ticks() / 19200; }
    double NowMsFine() { return static_cast<double>(Ticks()) / 19200; }

    Game::Actors::Player* Player() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->mPlayer : nullptr;
    }

#ifdef LASAP_TEST
    std::string Hex(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }
#endif

    int IndexOf(const Puppet& p) { return static_cast<int>(&p - g_Puppets); }

    template<typename T>
    T At(const void* base, size_t offset) {
        return base != nullptr ? *reinterpret_cast<const T*>(reinterpret_cast<const u8*>(base) + offset) : T{};
    }

    bool Alive(const Puppet& p) {
        return p.ctrl != nullptr && __atomic_load_n(CtrlCount(p.ctrl, kCtrlOwners), __ATOMIC_ACQUIRE) >= 0;
    }

    void* AnimComp(const Puppet& p) { return Alive(p) ? At<void*>(p.actor, kActorAnimComp) : nullptr; }

    // The animation player's model instance (+0x88) and model object (+0x90); a layered player (the ghosts', with
    // Link's upper-body layer) keeps them with its layers, at +0x1b8 and +0x1c0.
    constexpr size_t kLayeredInstance = 0x1b8, kLayeredModel = 0x1c0;
    bool IsLayered(const void* player) { return At<uintptr_t>(player, 0) == g_BaseAddress + kLayeredVtable; }
    u8* PlayerInstance(const void* player) {
        return At<u8*>(player, IsLayered(player) ? kLayeredInstance : kAnimPlayerInstance);
    }

    // The puppet's model once it is loaded: its animation player's model.
    u8* ModelOf(const Puppet& p) {
        void* player = At<void*>(AnimComp(p), kAnimCompPlayer);
        return At<u8*>(player, IsLayered(player) ? kLayeredModel : kAnimPlayerModel);
    }

    void ClearSpan(int n) {
        g_Spans[n].hi.store(0, std::memory_order_release);
        g_Spans[n].lo.store(0, std::memory_order_release);
    }

    // Lets the actor go, once the game has destroyed it.
    void Forget(Puppet& p) {
        ClearSpan(IndexOf(p));
        if (p.ctrl != nullptr) GameFn<ReleaseWeakFn>(kReleaseWeak)(p.ctrl);
#ifdef LASAP_TEST
        std::vector<std::string> log = std::move(p.animLog);
        p = Puppet{};
        p.animLog = std::move(log);
#else
        p = Puppet{};
#endif
    }

    // The model's visibility words a ghost draws with: the materials it shows (kShownMaterials).
    void ComputeShownMats(Puppet& p) {
        u16 count = std::min<u16>(At<u16>(p.model, kModelMatCount), 32 * 4);
        const u8* mats = At<const u8*>(p.model, kModelMats);
        std::fill(std::begin(p.shownMats), std::end(p.shownMats), 0);
        for (u16 i = 0; i < count; i++) {
            std::string_view name;
            if (const u8* res = At<const u8*>(mats + i * kMatStride, 0)) {
                const u8* s = At<const u8*>(res, 8);
                name = std::string_view(reinterpret_cast<const char*>(s + 2), *reinterpret_cast<const u16*>(s));
            }
            auto named = [&](const char* m) { return name == m; };
            if (std::any_of(std::begin(kShownMaterials), std::end(kShownMaterials), named))
                p.shownMats[i / 32] |= 1u << (i % 32);
        }
        p.matWords = (count + 31) / 32;
    }

    // Draws only the materials a ghost shows, or (hidden) none; true when they already were. The game sets the
    // visibility again at times (the model's setup some frames after it appears, the actor coming back after a
    // dungeon room's scroll), so it is checked every frame.
    bool WriteLook(Puppet& p, bool draw) {
        if (p.model == nullptr) return false;
        if (p.matWords < 0) ComputeShownMats(p);
        u16 count = std::min<u16>(At<u16>(p.model, kModelMatCount), 32 * 4);
        u32* visible = At<u32*>(p.model, kModelMatVis);
        if (visible == nullptr) return false;
        bool kept = true;
        for (int w = 0; w < p.matWords; w++) {
            u32 valid = count - 32 * w >= 32 ? ~0u : (1u << (count - 32 * w)) - 1;
            u32 want = (draw ? p.shownMats[w] : 0) | (visible[w] & ~valid);
            if (visible[w] != want) {
                visible[w] = want;
                kept = false;
            }
        }
#ifdef LASAP_TEST
        if (!kept) {
            p.visFixes++;
            if (p.animLog.size() < 40)
                p.animLog.push_back(std::string(draw ? "vis" : "hide") + "@" + std::to_string(g_LastFrame));
        }
#endif
        return kept;
    }

    void Hide(Puppet& p, u64 now) {
        ClearSpan(IndexOf(p));
        if (Alive(p)) WriteLook(p, false);
        p.readyFrames = 0;
        p.ready = false;
        p.wasActive = false;
        if (p.shown) p.hiddenSince = now;
        p.shown = false;
        p.track = Track{};
    }

    // Has the game destroy the actor (at the entity manager's next flush) and lets it go.
    void Remove(Puppet& p) {
        ClearSpan(IndexOf(p));
        if (Alive(p) && (At<u32>(p.actor, kActorFlags) & 0xc0000000) == 0) {
            void* system = *reinterpret_cast<void**>(reinterpret_cast<u8*>(Game::GetFramework()) + kFrameworkActorSystem);
            GameFn<bool (*)(void*, const char*, u64, bool)>(kRemoveChild)(
                system, reinterpret_cast<const char*>(p.actor) + kActorName, At<u64>(p.actor, kActorNameLength), false);
        }
        Forget(p);
    }

    void Place(Puppet& p, const Game::Vec3& pos, const Game::Vec4* rot);

    // The Npc sheet's APGhost row (the patcher adds it with the ghosts' model), or nullptr.
    const void* GhostRow() {
        void* sheet = **reinterpret_cast<void***>(g_BaseAddress + kNpcSheets);
        return sheet != nullptr ? GameFn<void* (*)(void*, StringView*, u64)>(kFindRow)(sheet, &g_GhostRow, kNpcRowSize) : nullptr;
    }

    bool HasGhostRow() { return GhostRow() != nullptr; }

    // Creates actor <id> at pos as PopEnemy does, with the player as owner, and keeps a weak reference to it; a
    // puppet that has an actor shows it there again.
    bool Spawn(Puppet& p, u16 id, const Game::Vec3& pos, u32 frame, std::string& err) {
        if (Alive(p)) {
            Place(p, pos, nullptr);
            p.shown = true;
#ifdef LASAP_TEST
            p.shownAt = frame;
#endif
            return true;
        }
        auto* player = Player();
        if (player == nullptr) {
            err = "no player";
            return false;
        }
        alignas(16) u8 params[kParamsSize] = {};
        *reinterpret_cast<u16*>(params + kParamId) = id;
        // Zone -1: NpcGhost is zone-scoped, and an actor of a zone Link has left is suspended until he enters it
        // again (ACTORS.md, Activity); a ghost goes where its player goes, so it belongs to no zone.
        *reinterpret_cast<s32*>(params + kParamRoom) = -1;
        float* f = reinterpret_cast<float*>(params + kParamPos);
        f[0] = pos.x;
        f[1] = pos.y;
        f[2] = pos.z;
        f[3] = 0;
        void* rot = *reinterpret_cast<void**>(g_BaseAddress + kIdentityRot);
        void* scale = *reinterpret_cast<void**>(g_BaseAddress + kOneScale);
        if (rot == nullptr || scale == nullptr) {
            err = "no identity globals";
            return false;
        }
        std::memcpy(params + kParamRot, rot, 16);
        std::memcpy(params + kParamScale, scale, 16);

        // The owner is the framework's actor system (a shared_ptr at Framework +0x498), as the player's hookshot
        // passes it.
        if (id == kGhostActor && !HasGhostRow()) {
            err = "no APGhost row (a mod patched before ghosts)";
            p.spawnAfter = frame + kNoRowFrames;
            return false;
        }
        auto* system = reinterpret_cast<SharedPtr*>(reinterpret_cast<u8*>(Game::GetFramework()) + kFrameworkActorSystem);
        SharedPtr out{};
        g_SpawningGhost = id == kGhostActor;
        GameFn<CreateFn>(kCreateActor)(&out, system, params);
        g_SpawningGhost = false;

        void* weak = *reinterpret_cast<void**>(params + 0x1b8);
        if (weak != nullptr) GameFn<ReleaseWeakFn>(kReleaseWeak)(weak);
        ReleaseShared(*reinterpret_cast<void**>(params + 0x168));
        ReleaseShared(*reinterpret_cast<void**>(params + 0x158));
        GameFn<DtorFn>(kParamsDtor0)(params + kParamsDtor0At);
        GameFn<DtorFn>(kParamsDtor1)(params + kParamsDtor1At);
        GameFn<DtorFn>(kParamsDtor2)(params + kParamsDtor2At);

        if (out.ptr == nullptr || out.ctrl == nullptr) {
            err = "factory returned nothing";
            p.spawnAfter = frame + kRespawnFrames;
            return false;
        }
        Forget(p);
        __atomic_fetch_add(CtrlCount(out.ctrl, kCtrlWeakOwners), 1, __ATOMIC_ACQ_REL);
        ReleaseShared(out.ctrl);
        p.actor = out.ptr;
        p.ctrl = out.ctrl;
#ifdef LASAP_TEST
        p.animLog.clear();
#endif
        if (!Alive(p)) {
            err = "the actor system did not keep the actor";
            Forget(p);
            p.spawnAfter = frame + kRespawnFrames;
            return false;
        }
        p.shown = true;
#ifdef LASAP_TEST
        p.shownAt = frame;
#endif
        return true;
    }

    // Plays an animation of the puppet's model by name from a frame (the animation player's virtual +0xc0).
    void PlayAnim(Puppet& p, const char* name, float blend, float frame) {
        void* comp = AnimComp(p);
        if (comp == nullptr) return;
        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(comp);
        if (g_PlaySlot == -1) {
            g_PlaySlot = -2;
            for (int i = 0; i < 0x40 && g_PlaySlot < 0; i++)
                for (uintptr_t f : kPlayByName)
                    if (vtable[i] == g_BaseAddress + f) g_PlaySlot = i;
        }
        if (g_PlaySlot < 0) return;
        NameView view{name, strlen(name)};
        auto play = reinterpret_cast<PlayByNameFn>(vtable[g_PlaySlot]);
        play(comp, &view, 0, blend, 0);
        play(comp, &view, 2, blend, 0);
        void* player = At<void*>(comp, kAnimCompPlayer);
        if (frame > 0 && player != nullptr) {
            auto setFrame = reinterpret_cast<void (*)(void*, float)>((*reinterpret_cast<uintptr_t**>(player))[kSetFrameSlot]);
            setFrame(player, frame);
        }
    }

    // The sender's upper-body animation (anim id, 0 none) on layer 1 of the puppet's layered animation player, from
    // its start; nothing on a model whose player is not layered.
    void SetUpper(Puppet& p, int anim) {
        if (anim == p.upper) return;
        p.upper = anim;
        void* comp = AnimComp(p);
        u8* sac = At<u8*>(comp, kAnimCompPlayer);
        if (sac == nullptr || !IsLayered(sac) || At<s32>(sac, kSacLayerCount) <= kUpperLayer) return;
        SharedPtr played{};
        const char* name = anim > 0 ? AP::GhostNet::AnimName(anim) : nullptr;
        if (name != nullptr) {
            NameView view{name, strlen(name)};
            GameFn<void (*)(SharedPtr*, void*, NameView*)>(kNpcAnimGet)(&played, comp, &view);
        }
        // none: the layer is only turned off
        if (played.ptr != nullptr) {
            GameFn<void (*)(void*, int, SharedPtr*, float, float)>(kLayeredSetSlot)(sac, kUpperLayer, &played,
                                                                                    g_AnimBlend, 0);
            u8* layer = At<u8**>(sac, kSacLayers)[kUpperLayer];
            if (layer != nullptr) {
                uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(layer);
                reinterpret_cast<void (*)(void*, bool)>(vtable[kLayerSetLoop])(layer, AP::GhostNet::AnimLoops(anim));
                reinterpret_cast<void (*)(void*, float)>(vtable[kLayerSetStart])(layer, 0);
            }
        }
        GameFn<void (*)(void*, int, bool)>(kLayerEnable)(sac + kSacLayerSet, kUpperLayer, played.ptr != nullptr);
        ReleaseShared(played.ctrl);
#ifdef LASAP_TEST
        if (p.animLog.size() < 40)
            p.animLog.push_back("upper:" + std::string(name != nullptr ? name : "-") + (played.ptr != nullptr ? "" : "?") +
                                "@" + std::to_string(g_LastFrame));
#endif
    }

#ifdef LASAP_TEST
    float YawOf(const Game::Vec4& q) { return 2 * std::atan2(q.y, q.t); }
#endif
    Game::Vec4 QuatOf(float yaw) { return Game::Vec4{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}; }

    void Place(Puppet& p, const Game::Vec3& pos, const Game::Vec4* rot) {
        auto* root = Alive(p) ? At<Game::RootComp*>(p.actor, kActorRoot) : nullptr;
        if (root == nullptr) return;
        root->coordsNew.pos = pos;
        root->coords.pos = pos;
        if (rot != nullptr) {
            root->coordsNew.rotate = *rot;
            root->coords.rotate = *rot;
        }
        root->needsCoordUpdate = true;
    }

    void ResetTrack(Track& tr) {
        int gait = tr.gait, anim = tr.anim;
        tr = Track{};
        tr.gait = gait;
        tr.anim = anim;
    }

    void Push(Track& tr, const Frame& f) {
        if (tr.count > 0) {
            const Frame& newest = tr.frames[(tr.count - 1) % kTrack];
            if (f.timeMs < newest.timeMs - kResetMs) ResetTrack(tr);
            else if (f.timeMs <= newest.timeMs) return;
        }
        double delay = static_cast<double>(f.receivedMs) - static_cast<double>(f.timeMs);
        if (tr.count == 0 || delay < tr.offset) tr.offset = delay;
        else tr.offset += (delay - tr.offset) * kOffsetRise;
        tr.frames[tr.count % kTrack] = f;
        tr.count++;
        if (f.flags & kFlagWarped) tr.arrival = tr.count;
    }

    Game::Vec3 PosOf(const Frame& f) { return Game::Vec3{f.x, f.y, f.z}; }

#ifdef LASAP_TEST
    // DBG puppet trace <slot>: the next kTraceRows replay frames of that ghost slot (DBG puppet tracedump)
    // and, per frame, how many ghosts ran past their newest line (extrapolating, or holding it)
    struct TraceRow { double now, t; s64 at; u32 a, count; char mode; float x, z; int past, held, lines; };
    constexpr u32 kTraceRows = 180;
    TraceRow g_Trace[kTraceRows];
    TraceRow g_TraceNext{};
    u32 g_TraceCount = 0;
    int g_TraceSlot = -1, g_Past = 0, g_Held = 0;
    u32 g_LinesSeen = 0;  // GHOST lines of all slots, at the last frame
#endif

    // a + (b - a) * t
    Game::Vec3 Along(const Frame& a, const Frame& b, float t) {
        return Game::Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }

    // The replay at time t from the track's first count frames: the position, the speed, the frame at or before t and
    // how it was found ('i' between two frames, 'e' on past the last, 'h' holding the last, 'b' before them all).
    struct Replayed {
        Game::Vec3 pos;
        float speed = 0;
        u32 a = 0;
        char mode = 'b';
    };
    Replayed ReplayAt(const Track& tr, double t, u32 count) {
        Replayed r;
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        // the last frame at or before t (the oldest while t is before them all)
        r.a = count - 1;
        while (r.a > oldest && static_cast<double>(tr.frames[r.a % kTrack].timeMs) > t) r.a--;
        const Frame& fa = tr.frames[r.a % kTrack];
        r.pos = PosOf(fa);
        if (static_cast<double>(fa.timeMs) > t) return r;
        r.mode = 'h';
        if (r.a + 1 < count) {
            r.mode = 'i';
            const Frame& fb = tr.frames[(r.a + 1) % kTrack];
            float span = static_cast<float>(fb.timeMs - fa.timeMs);
            if (!(fb.flags & kFlagWarped) && span > 0) {
                r.pos = Along(fa, fb, static_cast<float>(t - fa.timeMs) / span);
                r.speed = std::hypot(fb.x - fa.x, fb.z - fa.z) * 1000 / span;
            }
        } else if (!(fa.flags & (kFlagStill | kFlagWarped)) && r.a > oldest && t - fa.timeMs < kExtrapolateMs) {
            const Frame& fp = tr.frames[(r.a - 1) % kTrack];
            float span = static_cast<float>(fa.timeMs - fp.timeMs);
            if (span > 0) {
                r.mode = 'e';
                r.pos = Along(fp, fa, 1 + static_cast<float>(t - fa.timeMs) / span);
                r.speed = std::hypot(fa.x - fp.x, fa.z - fp.z) * 1000 / span;
            }
        }
        return r;
    }

    // Whether the replay at time t (frame r.a) holds the ghost hidden at its arrival (kArriveHoldMs).
    bool ArrivalHeld(Track& tr, const Replayed& r, double t) {
        if (tr.arrival == 0) return false;
        u32 w = tr.arrival - 1;
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        if (w < oldest) {
            tr.arrival = 0;
            return false;
        }
        if (r.a < w) return false;
        const Frame& fw = tr.frames[w % kTrack];
        bool released = t - static_cast<double>(fw.timeMs) >= kArriveHoldMs;
        for (u32 k = w + 1; k <= r.a && !released; k++) {
            const Frame& f = tr.frames[k % kTrack];
            released = (f.flags & kFlagStarted) != 0 || f.anim != fw.anim ||
                       std::hypot(f.x - fw.x, f.y - fw.y, f.z - fw.z) > kArriveMove;
        }
        if (released) tr.arrival = 0;
        return !released;
    }

    // One frame of the replay; picks the animation from the speed.
    void ReplayFrame(Puppet& p, double now) {
        Track& tr = p.track;
        if (tr.count == 0) return;
        double t = now - tr.offset - g_BufferMs;
        Replayed r = ReplayAt(tr, t, tr.count);
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        if (tr.replayed > oldest && tr.replayed < tr.count) {
            // new lines: where the replay had the puppet without them, less where it has it now
            bool warped = false;
            for (u32 k = tr.replayed; k < tr.count; k++) warped |= (tr.frames[k % kTrack].flags & kFlagWarped) != 0;
            Game::Vec3 was = ReplayAt(tr, t, tr.replayed).pos;
            if (!warped) tr.error = Game::Vec3{tr.error.x + was.x - r.pos.x, tr.error.y + was.y - r.pos.y,
                                               tr.error.z + was.z - r.pos.z};
            else tr.error = Game::Vec3{};
        }
        tr.replayed = tr.count;
        bool held = tr.arriving;
        tr.arriving = ArrivalHeld(tr, r, t);
        // shown again: the animation from its start, not blended from the pose it held hidden
        float blend = held && !tr.arriving ? 0 : g_AnimBlend;
        double dt = now - tr.lastNow;
        float fade = tr.shownFrames > 0 && dt > 0 ? static_cast<float>(std::exp(-dt / kCorrectMs)) : 0;
        tr.error = Game::Vec3{tr.error.x * fade, tr.error.y * fade, tr.error.z * fade};
        if (std::fabs(tr.error.x) + std::fabs(tr.error.y) + std::fabs(tr.error.z) > kCorrectMax) tr.error = Game::Vec3{};
        Game::Vec3 pos{r.pos.x + tr.error.x, r.pos.y + tr.error.y, r.pos.z + tr.error.z};
        const Frame& fa = tr.frames[r.a % kTrack];
        u32 a = r.a;
        float speed = r.speed;
        [[maybe_unused]] char mode = r.mode;
        if (tr.shownFrames > 0 && dt > 0)
            tr.moved[tr.shownFrames % 60] = static_cast<float>(std::hypot(pos.x - tr.pos.x, pos.z - tr.pos.z) * 1000 / dt);
        tr.shownFrames++;
        tr.lastNow = now;
#ifdef LASAP_TEST
        if (mode == 'e') g_Past++;
        if (mode == 'h' && !(fa.flags & kFlagStill)) g_Held++;
        if (IndexOf(p) == g_TraceSlot) g_TraceNext = TraceRow{now, t, fa.timeMs, a, tr.count, mode, pos.x, pos.z, 0, 0, 0};
#endif
        tr.pos = pos;
        Game::Vec4 q = QuatOf(fa.yaw * static_cast<float>(M_PI) / 180);
        Place(p, pos, &q);
        p.look = fa.look;
        // From the model's load on: the NPC plays its own idle as it loads.
        if (p.model == nullptr) return;
        SetUpper(p, fa.upperAnim);
        if (fa.anim > 0) {
            bool started = (fa.flags & kFlagStarted) != 0 && tr.started != a;
            if (fa.anim != tr.anim || started) {
                if (const char* name = AP::GhostNet::AnimName(fa.anim)) PlayAnim(p, name, blend, fa.animFrame / 10.0f);
                tr.anim = fa.anim;
                tr.started = a;
                tr.gait = -1;
            }
            return;
        }
        int gait = tr.gait < 0 ? GaitAt(speed) : std::clamp(tr.gait, GaitAt(speed - kGaitMargin),
                                                             GaitAt(speed + kGaitMargin));
        if (gait != tr.gait) {
            PlayAnim(p, kGaits[gait], blend == 0 ? 0 : kGaitBlend, 0);
            tr.gait = gait;
            tr.anim = -1;
        }
    }

    bool IsLinkModel(const u8* model) {
        const u8* res = At<const u8*>(model, 0);
        if (res == nullptr || std::memcmp(res, "FMDL", 4) != 0) return false;
        const u8* s = At<const u8*>(res, 8);
        return std::string_view(reinterpret_cast<const char*>(s + 2), *reinterpret_cast<const u16*>(s)) == "link";
    }

    // The model's blocks (its materials' parameter blocks and its shapes' blocks, one span in its pool) as GPU
    // addresses, for the render thread.
    void PublishSpan(int n, const u8* model) {
        u64 gpu = At<u64>(At<const u8*>(model, kModelPool), kPoolGpu);
        u64 lo = ~0ull, hi = 0;
        u16 count = At<u16>(model, kModelMatCount);
        const u8* mats = At<const u8*>(model, kModelMats);
        for (u16 i = 0; i < count; i++) {
            const u8* mat = mats + i * kMatStride;
            u64 at = At<u64>(mat, kMatBlock);
            u32 copies = std::min(At<u32>(mat, kMatCopies), 4u);
            u64 size = (At<u64>(mat, kMatBlockSize) + 0xff) & ~0xffull;
            lo = std::min(lo, at);
            hi = std::max(hi, at + copies * size);
        }
        u16 shapes = At<u16>(model, kModelShapeCount);
        const u8* shape = At<const u8*>(model, kModelShapes);
        for (u16 i = 0; i < shapes; i++) {
            u64 at = At<u64>(shape + i * kShapeStride, kShapeBlock);
            lo = std::min(lo, at);
            hi = std::max(hi, at + 0x1000);
        }
        if (hi <= lo) return;
        if (g_Spans[n].lo.load(std::memory_order_relaxed) == gpu + lo &&
            g_Spans[n].hi.load(std::memory_order_relaxed) == gpu + hi) return;
        g_Spans[n].hi.store(0, std::memory_order_release);
        g_Spans[n].lo.store(gpu + lo, std::memory_order_release);
        g_Spans[n].hi.store(gpu + hi, std::memory_order_release);
    }

    // First each frame for each puppet with an actor: its model. The actor loads its model again in each level, as a
    // new model object with its own animation player.
    void RefreshModel(Puppet& p) {
        u8* model = ModelOf(p);
        if (model != nullptr && !IsLinkModel(model)) model = nullptr;
        if (model == p.model) return;
        ClearSpan(IndexOf(p));
        p.model = model;
#ifdef LASAP_TEST
        if (model != nullptr && p.animLog.size() < 40) p.animLog.push_back("model@" + std::to_string(g_LastFrame));
#endif
        p.tunic = -1;
        p.tunicAnim = nullptr;
        p.upper = -1;
        std::fill(std::begin(p.faceSlots), std::end(p.faceSlots), -1);
        std::fill(std::begin(p.faceSet), std::end(p.faceSet), -1);
        p.ownFace = false;
        p.matWords = -1;
        p.readyFrames = 0;
        p.ready = false;
        p.track.gait = -1;
        p.track.anim = -1;
    }

    // The look's shapes on the puppet's model.
    void SetShapes(Puppet& p) {
        u8* instance = PlayerInstance(At<void*>(AnimComp(p), kAnimCompPlayer));
        if (At<u8*>(At<u8*>(instance, kInstanceModel), kModelObject) != p.model) return;
        u8* shapes = At<u8*>(instance, kInstanceShapes);
        if (shapes == nullptr) return;
        int count = std::min<int>(At<u16>(p.model, kModelShapeCount), AP::GhostNet::SHAPE_COUNT);
        u32 mask = p.detail == 2 ? g_ShapeMask : g_ShapeMask & g_CrowdMask;
        mask &= ~(kInstancedShapes & ~(p.ownFace ? kFaceShapes : 0));
        for (int i = 0; i < count; i++) shapes[i] = (p.look.shapes & mask) >> i & 1;
    }

    void* TunicSlot(const Puppet& p, int& index) {
        u8* npc = static_cast<u8*>(AnimComp(p));
        u8* mat = At<u8*>(npc, kNpcMaterialAnim);
        const u8* facial = At<const u8*>(npc, kNpcFacial);
        index = 0;
        if (facial != nullptr && !((At<u8>(facial, kFacialFlags) >> 6) & 1)) {
            const u8* set = At<const u8*>(facial, kFacialSet);
            index = At<s32>(set, kFacialCount) + (At<s32>(set, kFacialExtraA) != 0) + (At<s32>(set, kFacialExtraB) != 0);
        }
        if (mat == nullptr || index < 0 || index >= At<s32>(mat, kMatAnimSlotCount)) return nullptr;
        return At<void**>(mat, kMatAnimSlots)[index];
    }

    // The look's tunic: "clothes" on its slot (again when something else took the slot), held at the tunic's frame.
    void SetTunic(Puppet& p) {
        int tunic = std::clamp(p.look.tunic, AP::GhostNet::TUNIC_GREEN, AP::GhostNet::TUNIC_BLUE);
        int index;
        void* slot = TunicSlot(p, index);
        if (slot == nullptr || p.tunic == kNoTunic) return;
        void* playing = At<void*>(slot, kSlotAnim);
        if (p.tunicAnim == nullptr || playing != p.tunicAnim) {
            SharedPtr out{};
            StringView name{"clothes", 7};
            GameFn<void (*)(SharedPtr*, void*, StringView*, bool)>(kPlayMaterialAnim)(&out, AnimComp(p), &name, true);
            ReleaseShared(out.ctrl);
            p.tunicAnim = At<void*>(slot, kSlotAnim);
            // not put there (the model has no "clothes"): no tunic for this model
            p.tunic = p.tunicAnim == nullptr || p.tunicAnim == playing ? kNoTunic : -1;
            if (p.tunic == kNoTunic) return;
        }
        if (tunic == p.tunic) return;
        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(slot);
        reinterpret_cast<void (*)(void*, float)>(vtable[kSlotSetRate])(slot, 0);
        reinterpret_cast<void (*)(void*, float)>(vtable[kSlotSetFrame])(slot, kTunicFrames[tunic]);
        p.tunic = tunic;
    }

    // The slot of the puppet's material animation component that holds the animation of that name, or -1.
    int MatSlotOf(const Puppet& p, std::string_view name) {
        const u8* mat = At<const u8*>(AnimComp(p), kNpcMaterialAnim);
        const u8* const* slots = At<const u8* const*>(mat, kMatAnimSlots);
        int count = At<s32>(mat, kMatAnimSlotCount);
        for (int i = 0; slots != nullptr && i < count; i++) {
            const u8* s = At<const u8*>(At<const u8*>(At<const u8*>(slots[i], kSlotAnim), kMatAnimRes), kResName);
            if (s != nullptr && std::string_view(reinterpret_cast<const char*>(s + 2), At<u16>(s, 0)) == name) return i;
        }
        return -1;
    }

    // The sender's face on the face's slots, written again whenever a slot's frame differs; the puppet draws its own
    // face shapes while it differs from the instanced face (all frames 0). A model without all three slots (an older
    // mod's row) keeps the instanced face; a slot not found is looked for again each frame.
    void SetFace(Puppet& p) {
        bool all = true;
        for (int k = 0; k < kFaceParts; k++) {
            if (p.faceSlots[k] < 0) p.faceSlots[k] = MatSlotOf(p, kFaceAnims[k]);
            all &= p.faceSlots[k] >= 0;
        }
        int frames[kFaceParts] = {p.look.eye, p.look.mouth, p.look.ear};
        for (int k = 0; k < kFaceParts; k++)
            if (frames[k] < 0 || frames[k] >= kFaceFrames[k]) frames[k] = 0;
        p.ownFace = all && (frames[0] | frames[1] | frames[2]) != 0;
        if (!all) return;
        u8* mat = At<u8*>(AnimComp(p), kNpcMaterialAnim);
        u8** slots = At<u8**>(mat, kMatAnimSlots);
        for (int k = 0; k < kFaceParts; k++) {
            if (p.faceSlots[k] >= At<s32>(mat, kMatAnimSlotCount)) continue;
            u8* slot = slots[p.faceSlots[k]];
            if (frames[k] == p.faceSet[k] && At<float>(slot, kSlotFrame) == static_cast<float>(frames[k])) continue;
            uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(slot);
            reinterpret_cast<void (*)(void*, float)>(vtable[kSlotSetRate])(slot, 0);
            reinterpret_cast<void (*)(void*, float)>(vtable[kSlotSetFrame])(slot, static_cast<float>(frames[k]));
            p.faceSet[k] = frames[k];
        }
    }

    // Last each frame for each puppet with an actor: drawn as a ghost (dithered) or hidden. It is ready (its name tag
    // and instanced shapes show) once its body has drawn kReadyFrames whole frames: the visibility kept and the actor
    // active (an inactive actor is neither updated nor drawn: one spawned in a level's first frames), so a tag or a
    // face never shows without the body; the instanced shapes take the bones the frame before posed, which in the
    // first frame are those of the pose it held hidden (on the floor, before a fall from above).
    constexpr int kReadyFrames = 2;
    void Update(Puppet& p) {
        if (p.model == nullptr) {
            p.readyFrames = 0;
            p.ready = false;
            return;
        }
        bool active = (At<u32>(p.actor, kActorFlags) & kActiveMask) == kActive;
        bool draw = p.shown && g_DrawGhosts && p.detail > 0 && !p.track.arriving;
        bool kept = WriteLook(p, draw);
        p.readyFrames = draw && kept && active ? p.readyFrames + 1 : 0;
        p.ready = p.readyFrames >= kReadyFrames;
        if (!p.shown) return;
        SetFace(p);
        SetShapes(p);
        SetTunic(p);
        if (draw) PublishSpan(IndexOf(p), p.model);
        else ClearSpan(IndexOf(p));
    }

    // Screen-space dither on the puppets only: the model file's shown albedos carry a constant alpha (render pass
    // 1), and their draws get an NVN multisample state with alpha-to-coverage. The renderer binds a material's
    // parameter block through the command buffer wrapper's bind-uniform-buffer (0x113fd70: wrapper, stage, slot, {GPU
    // address, offset}*, size); an address inside a puppet's blocks marks its draw. The context's cached raster-state
    // setter (0xff9430: context, state; cache at +0xa0, which other code reads, the wrapper at +8) would bind the
    // material's own state over it, so it binds ours again until the draw (0x113e950 / 0x113e9d0); after the draw the
    // cached state is bound again (0x113ea70: wrapper, state), as the context believes it is.
    constexpr uintptr_t kBindUniformBuffer = 0x113fd70, kSetRasterState = 0xff9430;
    constexpr uintptr_t kDrawElementsBaseVertex = 0x113e950, kDrawElementsInstanced = 0x113e9d0;
    constexpr uintptr_t kBindRasterState = 0x113ea70;
    constexpr size_t kWrapperCmdBuf = 0xb8, kContextWrapper = 8, kContextRasterCache = 0xa0;
    // GOT slots of the game's NVN function pointers (each holds the address of a global with the pointer)
    constexpr uintptr_t kGotMsSetDefaults = 0x1c951e8, kGotMsSetA2C = 0x1c95200, kGotBindMs = 0x1c947c0;
    alignas(16) u8 g_GhostMs[64];
    bool g_GhostMsReady = false;
    // The renderer records several command buffers (one wrapper each, a context each): the wrapper whose next
    // draw is a puppet's, and each wrapper's context.
    void* g_GhostPending = nullptr;
    std::pair<void*, u8*> g_Contexts[8] = {};
    u32 g_GhostDraws = 0;
    u32 g_InstDraws = 0;           // of them the instanced models' (DBG puppet framelog)
    bool g_PendingInst = false;    // the pending draw is an instanced model's
    u32 g_DrawsBy[8] = {};    // puppet draws per wrapper (g_Contexts' order), for DBG puppet
    u32 g_SkipMask = 0;       // DBG puppet skip: wrappers whose puppet draws are dropped (finding the cost)
    void* g_SkipPending = nullptr;

    template<typename T>
    T NvnFn(uintptr_t got) {
        void** global = *reinterpret_cast<void***>(g_BaseAddress + got);
        return reinterpret_cast<T>(*global);
    }

    void BindGhostMs(void* cmdBuf) {
        if (!g_GhostMsReady) {
            NvnFn<void (*)(void*)>(kGotMsSetDefaults)(g_GhostMs);
            NvnFn<void (*)(void*, bool)>(kGotMsSetA2C)(g_GhostMs, true);
            g_GhostMsReady = true;
        }
        NvnFn<void (*)(void*, const void*)>(kGotBindMs)(cmdBuf, g_GhostMs);
    }

    // The span (puppet, or kInstSpan + instanced group) whose blocks hold this address, or -1.
    int PuppetBlock(u64 address) {
        for (int n = 0; n < kPuppets + kInstSpans; n++) {
            u64 hi = g_Spans[n].hi.load(std::memory_order_acquire);
            if (hi != 0 && address >= g_Spans[n].lo.load(std::memory_order_acquire) && address < hi) return n;
        }
        return -1;
    }

    u8* ContextOf(void* wrapper) {
        for (auto& c : g_Contexts) if (c.first == wrapper) return c.second;
        return nullptr;
    }

    void NoteContext(u8* context) {
        void* wrapper = *reinterpret_cast<void**>(context + kContextWrapper);
        for (auto& c : g_Contexts) {
            if (c.first == wrapper) { c.second = context; return; }
        }
        for (auto& c : g_Contexts) {
            if (c.first == nullptr) { c = {wrapper, context}; return; }
        }
    }

    int WrapperIndex(void* wrapper) {
        for (int i = 0; i < 8; i++) {
            if (g_Contexts[i].first == wrapper) return i;
        }
        return -1;
    }

    // After a puppet's draw on this wrapper: bind the raster state its context has cached.
    void EndGhostDraw(void* wrapper) {
        g_GhostDraws++;
        if (g_PendingInst) g_InstDraws++;
        int i = WrapperIndex(wrapper);
        if (i >= 0) g_DrawsBy[i]++;
        g_GhostPending = nullptr;
        u8* context = ContextOf(wrapper);
        if (context == nullptr) return;
        void* state = *reinterpret_cast<void**>(context + kContextRasterCache);
        if (state != nullptr) GameFn<void (*)(void*, void*)>(kBindRasterState)(wrapper, state);
    }

    // Equipment and faces, instanced (DESIGN.md, Ghosts): the shapes bound to one bone each draw once for all ghosts.
    // Per group of them one InstancedModel component (the game's for lawn grass and floor tiles) on Link, with one
    // instance per drawn ghost whose look has the group's shapes written each frame; the puppets draw none of them. Made as FieldTip makes its own (0xb846e0): the component (0x310 bytes,
    // constructor 0x104d540: component, {name, length, enabled, owner, owner's control block}*, the model resource
    // {resource, control block, model} as a model component holds it at +0x168, a cache key, the shapes' mask, the
    // model list at Framework +0x540), a control block of its own (vtable 0x1b9cf50) as its shared-from-this, the
    // entity manager's closures (virtual +0x78) and its model instance M (virtual +0x20, in the model list at once),
    // the entity manager's start task (0xfae2d0: manager *0x1cc6d98, shared_ptr*) and the actor's child map (0xfaa8a0:
    // actor, component, root). +0x2f9 set and +0x2fa clear keep the game from adding instances. M's instances: a
    // vector of 0x30-byte records at M +0x398 (begin, end, capacity: {position, rotation quaternion, scale}), turned
    // into a storage buffer at each draw (0xfe4020, up to 512). M is drawn in its bind pose with an identity world,
    // so a shape bound to one bone shows at a ghost's bone with the ghost's bone world times the inverse of M's.
    // g3d: model object +0x38 the skeleton, its +0x20 the bones' world matrices (0x40 each: X, Y, Z axes, translation).
    constexpr uintptr_t kInstCtor = 0x104d540, kLockOwner = 0x21470, kSharedCtrlVtable = 0x1b9cf50,
                        kStartTask = 0xfae2d0, kEntityManager = 0x1cc6d98, kAttachChild = 0xfaa8a0;
    constexpr size_t kInstSize = 0x310, kInstFlags = 0x40, kInstNoPush = 0x2f9, kInstVisible = 0x2fa, kInstM = 0x148,
                     kInstSelf = 0x8, kInstSelfCtrl = 0x10, kAnimSmc = 0x168, kSmcResource = 0x168, kMRecords = 0x398,
                     kFrameworkModels = 0x540, kModelSkeleton = 0x38, kSkeletonWorld = 0x20;
    constexpr u32 kInstMode = 0x4c41;  // cache keys (+ group) no game component uses: models of their own
    constexpr int kInstCapacity = 32;
    struct NameArg { const char* ptr; u64 len; u8 enabled; void* owner; void* ownerCtrl; };
    struct Record { float pos[4], rot[4], scale[3], pad; };
    // The shapes bound to one bone each (vertex skin count 1), as the look shows them together (a group's model draws
    // all its shapes for each instance): sword A, sword B, the Magic Rod, the shovel, the hookshot (bone 12, itemA_L),
    // shield A, the mirror shield, the ocarina (bone 24, itemA_R), the face's ear, eyes and mouth (bone 28, head).
    struct InstGroup { u32 shapes; int bone; };
    constexpr InstGroup kInstGroups[] = {{0x30000, 12}, {0xc0000, 12}, {0x1, 12}, {0x4, 12}, {0x1000, 12},
                                         {0x2000, 24}, {0xc000, 24}, {0x2, 24}, {0x380, kHeadBone}};
    constexpr int kInstGroupCount = sizeof(kInstGroups) / sizeof(kInstGroups[0]);
    constexpr u32 InstShapes() {
        u32 shapes = 0;
        for (const InstGroup& g : kInstGroups) shapes |= g.shapes;
        return shapes;
    }
    static_assert(InstShapes() == kInstancedShapes && kInstGroupCount == kInstSpans);
    // A component belongs to its host's child map, as the game's own do: it goes with Link at a level change.
    struct Inst {
        u8* comp = nullptr;
        void* host = nullptr;
        u32 drawn = 0;  // records written last frame
    };
    Inst g_Inst[kInstGroupCount];
    int g_InstMade = 0;
    char g_InstName[16];

    const float* BoneWorld(const u8* modelObject, int bone) {
        const u8* skeleton = At<const u8*>(modelObject, kModelSkeleton);
        const float* world = At<const float*>(skeleton, kSkeletonWorld);
        return world != nullptr ? world + bone * 16 : nullptr;
    }

    // Columns c[0..2] of a 3x3 (each 4 floats apart) times the inverse of another's, and the translation.
    void Compose(const float* g, const float* m, float axes[3][3], float t[3]) {
        // the inverse of m's 3x3 (columns m[0], m[4], m[8])
        float a[3][3] = {{m[0], m[4], m[8]}, {m[1], m[5], m[9]}, {m[2], m[6], m[10]}};  // a[row][col]
        float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                    a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
        float inv[3][3];
        float k = std::fabs(det) > 1e-8f ? 1 / det : 0;
        inv[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * k;
        inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * k;
        inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * k;
        inv[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * k;
        inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * k;
        inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * k;
        inv[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * k;
        inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * k;
        inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * k;
        float gr[3][3] = {{g[0], g[4], g[8]}, {g[1], g[5], g[9]}, {g[2], g[6], g[10]}};
        float r[3][3];
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r[i][j] = gr[i][0] * inv[0][j] + gr[i][1] * inv[1][j] + gr[i][2] * inv[2][j];
        for (int i = 0; i < 3; i++) {
            t[i] = g[12 + i] - (r[i][0] * m[12] + r[i][1] * m[13] + r[i][2] * m[14]);
            for (int j = 0; j < 3; j++) axes[j][i] = r[i][j];  // axes[column][row]
        }
    }

    void ToRecord(const float axes[3][3], const float t[3], Record& out) {
        float sx = std::sqrt(axes[0][0] * axes[0][0] + axes[0][1] * axes[0][1] + axes[0][2] * axes[0][2]);
        float sy = std::sqrt(axes[1][0] * axes[1][0] + axes[1][1] * axes[1][1] + axes[1][2] * axes[1][2]);
        float sz = std::sqrt(axes[2][0] * axes[2][0] + axes[2][1] * axes[2][1] + axes[2][2] * axes[2][2]);
        float m00 = axes[0][0] / sx, m10 = axes[0][1] / sx, m20 = axes[0][2] / sx;
        float m01 = axes[1][0] / sy, m11 = axes[1][1] / sy, m21 = axes[1][2] / sy;
        float m02 = axes[2][0] / sz, m12 = axes[2][1] / sz, m22 = axes[2][2] / sz;
        float q[4];
        float tr = m00 + m11 + m22;
        if (tr > 0) {
            float k = std::sqrt(tr + 1) * 2;
            q[3] = k / 4; q[0] = (m21 - m12) / k; q[1] = (m02 - m20) / k; q[2] = (m10 - m01) / k;
        } else if (m00 > m11 && m00 > m22) {
            float k = std::sqrt(1 + m00 - m11 - m22) * 2;
            q[3] = (m21 - m12) / k; q[0] = k / 4; q[1] = (m01 + m10) / k; q[2] = (m02 + m20) / k;
        } else if (m11 > m22) {
            float k = std::sqrt(1 + m11 - m00 - m22) * 2;
            q[3] = (m02 - m20) / k; q[0] = (m01 + m10) / k; q[1] = k / 4; q[2] = (m12 + m21) / k;
        } else {
            float k = std::sqrt(1 + m22 - m00 - m11) * 2;
            q[3] = (m10 - m01) / k; q[0] = (m02 + m20) / k; q[1] = (m12 + m21) / k; q[2] = k / 4;
        }
        out = Record{{t[0], t[1], t[2], 1}, {q[0], q[1], q[2], q[3]}, {sx, sy, sz}, 0};
    }

    u8* InstModel(const Inst& inst) { return inst.comp != nullptr ? At<u8*>(inst.comp, kInstM) : nullptr; }

    // Stops drawing a group's instances (host alive: the component stays on it, empty) and forgets the component.
    void InstDrop(int g, bool hostAlive) {
        Inst& inst = g_Inst[g];
        if (inst.comp == nullptr) return;
        if (u8* m = hostAlive ? InstModel(inst) : nullptr)
            *reinterpret_cast<u8**>(m + kMRecords + 8) = *reinterpret_cast<u8**>(m + kMRecords);
        ClearSpan(kInstSpan + g);
        inst = Inst{};
    }

    void InstDropAll(bool hostAlive) {
        for (int g = 0; g < kInstGroupCount; g++) InstDrop(g, hostAlive);
    }

    bool InstMake(int g, void* host, u8* smc) {
        u8* fmwk = reinterpret_cast<u8*>(Game::GetFramework());
        u8* comp = GameFn<u8* (*)(size_t)>(kGameNew)(kInstSize);
        SharedPtr owner{};
        GameFn<void (*)(SharedPtr*, void*, void*)>(kLockOwner)(&owner, At<void*>(host, 8), At<void*>(host, 0x10));
        snprintf(g_InstName, sizeof(g_InstName), "APGhostInst%d", g_InstMade++);
        NameArg name{g_InstName, strlen(g_InstName), 1, owner.ptr, owner.ctrl};
        GameFn<void (*)(u8*, NameArg*, void*, u32, u32, void*)>(kInstCtor)(
            comp, &name, smc + kSmcResource, kInstMode + g, kInstGroups[g].shapes, fmwk + kFrameworkModels);
        ReleaseShared(owner.ctrl);
        void** ctrl = GameFn<void** (*)(size_t)>(kGameNew)(0x20);
        ctrl[0] = reinterpret_cast<void*>(g_BaseAddress + offset_manager.Offset(kSharedCtrlVtable));
        ctrl[1] = nullptr;                      // one owner: the module
        ctrl[2] = reinterpret_cast<void*>(1);   // and the component's own weak reference
        ctrl[3] = comp;
        if (At<void*>(comp, kInstSelfCtrl) == nullptr) {
            *reinterpret_cast<void**>(comp + kInstSelf) = comp;
            *reinterpret_cast<void**>(comp + kInstSelfCtrl) = ctrl;
        }
        comp[kInstVisible] = 0;
        comp[kInstNoPush] = 1;
        u32& flags = *reinterpret_cast<u32*>(comp + kInstFlags);
        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(comp);
        if (!(flags & 0x40000008)) {
            reinterpret_cast<void (*)(u8*)>(vtable[0x78 / 8])(comp);
            reinterpret_cast<void (*)(u8*)>(vtable[0x20 / 8])(comp);
            flags |= 8;
        }
        SharedPtr sp{comp, ctrl};
        __atomic_fetch_add(CtrlCount(ctrl, kCtrlOwners), 1, __ATOMIC_ACQ_REL);
        GameFn<void (*)(void*, SharedPtr*)>(kStartTask)(*reinterpret_cast<void**>(g_BaseAddress + kEntityManager), &sp);
        ReleaseShared(ctrl);
        bool attached = GameFn<bool (*)(void*, u8*, void*)>(kAttachChild)(host, comp, At<void*>(host, kActorRoot));
        u8* m = attached ? At<u8*>(comp, kInstM) : nullptr;
        if (m != nullptr) {
            // room for the records before the render thread first draws them
            u8** vec = reinterpret_cast<u8**>(m + kMRecords);
            u8* old = vec[0];
            vec[0] = GameFn<u8* (*)(size_t)>(kGameNew)(kInstCapacity * sizeof(Record));
            vec[1] = vec[0];
            vec[2] = vec[0] + kInstCapacity * sizeof(Record);
            if (old != nullptr) GameFn<void (*)(void*)>(kGameDelete)(old);
            g_Inst[g] = Inst{comp, host, 0};
        }
        ReleaseShared(ctrl);  // the module's reference: the host's child map holds its own
        return m != nullptr;
    }

    // Each frame: each group's component on Link (made once a ghost's model gives the resource), with one record per
    // drawn ghost whose look has the group's shapes.
    void InstTick() {
        auto* host = Player();
        for (int g = 0; g < kInstGroupCount; g++)
            if (g_Inst[g].comp != nullptr && g_Inst[g].host != host) InstDrop(g, false);  // Link was made again
        if (host == nullptr) return;
        u8* smc = nullptr;
        for (int i = 0; i < kGhosts && smc == nullptr; i++)
            if (g_Puppets[i].model != nullptr && Alive(g_Puppets[i]))
                smc = At<u8*>(At<void*>(AnimComp(g_Puppets[i]), kAnimCompPlayer), kAnimSmc);
        for (int g = 0; g < kInstGroupCount; g++) {
            Inst& inst = g_Inst[g];
            if (inst.comp == nullptr && (smc == nullptr || !InstMake(g, host, smc))) continue;
            u8* m = InstModel(inst);
            const u8* mObject = At<const u8*>(At<const u8*>(m, kInstanceModel), kModelObject);
            const float* mBone = mObject != nullptr ? BoneWorld(mObject, kInstGroups[g].bone) : nullptr;
            // M's first calculation comes with its first draw: until then its bone is not set
            if (mBone == nullptr || std::fabs(mBone[0]) + std::fabs(mBone[1]) + std::fabs(mBone[2]) < 1e-6f) continue;
            PublishSpan(kInstSpan + g, mObject);
            u8** vec = reinterpret_cast<u8**>(m + kMRecords);
            if (vec[2] - vec[0] < static_cast<long>(kInstCapacity * sizeof(Record))) continue;
            Record* out = reinterpret_cast<Record*>(vec[0]);
            u32 n = 0;
            for (int i = 0; i < kGhosts && n < static_cast<u32>(kInstCapacity); i++) {
                const Puppet& p = g_Puppets[i];
                if (!p.ready || p.model == nullptr || p.detail == 0 || !Alive(p)) continue;
                if (p.ownFace && kInstGroups[g].shapes == kFaceShapes) continue;
                u32 shapes = p.look.shapes & (p.detail == 2 ? g_ShapeMask : g_ShapeMask & g_CrowdMask);
                if (!(shapes & kInstGroups[g].shapes)) continue;
                const float* bone = BoneWorld(p.model, kInstGroups[g].bone);
                if (bone == nullptr) continue;
                float axes[3][3], t[3];
                Compose(bone, mBone, axes, t);
                ToRecord(axes, t, out[n++]);
            }
            vec[1] = vec[0] + n * sizeof(Record);
            inst.drawn = n;
        }
    }

    // GhostNet's slot n: shown while the client is connected, the ghost is in Link's level and not hidden, and its
    // last line is recent; fed its new lines, replayed every frame. A puppet not shown stays hidden for reuse.
    void DriveGhost(int n, const std::string& level, u32 frame, u64 now, double nowFine) {
        Puppet& p = g_Puppets[n];
        const AP::GhostNet::Ghost& g = AP::GhostNet::Get(n);
        bool shown = AP::IsClientConnected() && g.active && !(g.flags & kFlagHidden) && g.level == level &&
                     now - g.receivedMs < kStaleMs;
        if (!shown) {
            if (p.shown) Hide(p, now);
            else if (p.ctrl != nullptr && now - p.hiddenSince >= kHiddenRemoveMs) Remove(p);
            return;
        }
        if (!p.shown) {
            std::string err;
            if (frame < p.spawnAfter || !Spawn(p, kGhostActor, Game::Vec3{g.x, g.y, g.z}, frame, err)) return;
            p.framesSeen = g.timelineFrom;
        }
        u32 flags = At<u32>(p.actor, kActorFlags);
        if ((flags & kActiveMask) != kActive) {
            if (!p.wasActive && ++p.inactiveFrames >= kInactiveFrames) {
                g_Remade++;
                Remove(p);  // spawned again next frame
                return;
            }
        } else {
            p.inactiveFrames = 0;
            p.wasActive = true;
        }
        if (p.framesSeen < g.timelineFrom) {
            // the sender's clock started over
            ResetTrack(p.track);
            p.framesSeen = g.timelineFrom;
        }
        constexpr u32 kKept = AP::GhostNet::HISTORY;
        for (u32 k = std::max(p.framesSeen, g.frames > kKept ? g.frames - kKept : 0); k < g.frames; k++)
            Push(p.track, g.history[k % kKept]);
        p.framesSeen = g.frames;
        ReplayFrame(p, nowFine);
    }

#ifdef LASAP_TEST
    // DBG puppet: on Link's path g_Delay frames late, exactly or replayed from frames taken g_SteerHz times a second;
    // or standing at g_At.
    void DriveTest(u32 frame, u64 now, double nowFine) {
        Puppet& p = g_Puppets[kTest];
        if (p.ctrl == nullptr) return;
        if (!g_AnimPending.empty()) {
            PlayAnim(p, g_AnimPending.c_str(), kGaitBlend, 0);
            g_AnimPending.clear();
        }
        if (g_Delay >= 0 && g_HistoryValid && static_cast<u32>(g_Delay) < g_HistoryFrame) {
            const Sample& s = g_History[(g_HistoryFrame - 1 - g_Delay) % kHistory];
            if (g_SteerHz <= 0) {
                Place(p, s.pos, &s.rot);
            } else {
                u32 every = std::max(1, 60 / g_SteerHz);
                if (p.track.count == 0 || frame - g_TestFedFrame >= every) {
                    const Frame* last = p.track.count > 0 ? &p.track.frames[(p.track.count - 1) % kTrack] : nullptr;
                    bool still = last != nullptr && last->x == s.pos.x && last->y == s.pos.y && last->z == s.pos.z;
                    // Link's animation now (with follow, ahead of his path by the delay)
                    AP::GhostNet::Anim anim = AP::GhostNet::LinkAnim();
                    int flags = (still ? kFlagStill : 0) | (anim.starts != g_TestStarts ? kFlagStarted : 0);
                    g_TestStarts = anim.starts;
                    Push(p.track, Frame{static_cast<s64>(now), now, s.pos.x, s.pos.y, s.pos.z,
                                        YawOf(s.rot) * 180 / static_cast<float>(M_PI), anim.anim, anim.frame,
                                        anim.upper, flags, AP::GhostNet::LinkLook()});
                    g_TestFedFrame = frame;
                }
                ReplayFrame(p, nowFine);
            }
        } else if (g_Delay == -1) {
            Place(p, g_At, nullptr);
        }
    }

    std::string PosText(const Game::Vec3& v) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f", v.x, v.y, v.z);
        return buf;
    }

    // DBG puppet: "PUPPET ... | <n> ..." per puppet with an actor (n = ghost slot, test for DBG puppet's).
    std::string Info() {
        std::string s = "PUPPET delay=" + std::to_string(g_Delay) + " steer=" + std::to_string(g_SteerHz) +
                        " buffer=" + std::to_string(static_cast<int>(g_BufferMs)) +
                        " blend=" + std::to_string(g_AnimBlend).substr(0, 6) +
                        " dithered=" + std::to_string(g_GhostDraws) + " remade=" + std::to_string(g_Remade) + " fps=" + std::to_string(static_cast<int>(g_Fps + 0.5f)) +
                        " lod=" + std::to_string(g_FullLimit) + "/" + std::to_string(g_DrawnLimit) + (g_Adapt ? "" : "fixed") +
                        " changes=" + std::to_string(g_DetailChanges);
        s += " inst=";
        for (int g = 0; g < kInstGroupCount; g++) s += (g ? "," : "") + std::to_string(g_Inst[g].drawn);
        u64 sum = 0, most = 0;
        for (u64 t : g_TickTicks) {
            sum += t;
            most = std::max(most, t);
        }
        s += " tick_us=" + std::to_string(sum * 10 / 192 / 60) + "/" + std::to_string(most * 10 / 192);
        // puppet draws per frame on each command buffer since the last DBG puppet
        u32 frames = std::max(1u, g_LastFrame - g_InfoFrame);
        s += " draws/frame=";
        for (int i = 0; i < 8; i++) {
            if (g_Contexts[i].first == nullptr) continue;
            s += std::to_string(i) + ":" + std::to_string((g_DrawsBy[i] - g_InfoDraws[i]) / frames) + ",";
            g_InfoDraws[i] = g_DrawsBy[i];
        }
        g_InfoFrame = g_LastFrame;
        for (auto& p : g_Puppets) {
            if (!Alive(p)) continue;
            int n = IndexOf(p);
            const char* name = reinterpret_cast<const char*>(p.actor) + kActorName;
            s += " | " + (n == kTest ? std::string("test") : std::to_string(n)) + " actor=" +
                 Hex(reinterpret_cast<uintptr_t>(p.actor)) + (p.shown ? "" : " hidden") + " name=" +
                 std::string(name, strnlen(name, 0x20));
            if (auto* root = At<Game::RootComp*>(p.actor, kActorRoot)) s += " pos=" + PosText(root->coordsNew.pos);
            s += " model=" + (p.model != nullptr ? Hex(reinterpret_cast<uintptr_t>(p.model)) : "-");
            s += " flags=" + Hex(At<u32>(p.actor, kActorFlags));
            s += " zone=" + std::to_string(At<s32>(p.actor, 0xb0));
            const char* anim = p.track.anim > 0 ? AP::GhostNet::AnimName(p.track.anim)
                                                : p.track.gait >= 0 ? kGaits[p.track.gait] : nullptr;
            s += " anim=" + std::string(anim != nullptr ? anim : "-");
            int slot;
            TunicSlot(p, slot);
            s += " shapes=" + Hex(p.look.shapes) + " tunic=" + std::to_string(p.tunic) + "@" + std::to_string(slot);
            s += " ready=" + std::to_string(p.ready) + " visfix=" + std::to_string(p.visFixes) + " face=" +
                 std::to_string(p.look.eye) + "," + std::to_string(p.look.mouth) + "," + std::to_string(p.look.ear) + "@" +
                 std::to_string(p.faceSlots[0]) + "," + std::to_string(p.faceSlots[1]) + "," +
                 std::to_string(p.faceSlots[2]) + (p.ownFace ? "own" : "") + " upper=" + std::to_string(p.upper) +
                 (p.track.arriving ? " arriving" : "");
            if (p.track.shownFrames > 60) {
                // speed over the last 60 frames: mean and standard deviation, units per second
                float sum = 0, sq = 0;
                for (float m : p.track.moved) {
                    sum += m;
                    sq += m * m;
                }
                char buf[48];
                snprintf(buf, sizeof(buf), " speed=%.2f~%.2f", sum / 60, std::sqrt(std::max(0.0f, sq / 60 - (sum / 60) * (sum / 60))));
                s += buf;
            }
        }
        return s;
    }
#endif
}

// While the module spawns a ghost: NpcGhost's component looks up the APGhost row, if the sheet has it.
HOOK_DEFINE_TRAMPOLINE(NpcComponentCtor) {
    static void* Callback(void* self, void* args, StringView* rowKey, void* x3) {
        if (g_SpawningGhost && rowKey != nullptr && rowKey->ptr == reinterpret_cast<const char*>(g_BaseAddress + kNpcGhostKey)) {
            if (HasGhostRow()) rowKey = &g_GhostRow;
        }
        return Orig(self, args, rowKey, x3);
    }
};

// A ghost's NPC component (the APGhost row) gets a layered animation player with Link's upper-body layer. The
// player comes back through x8 (a shared_ptr), so the hook returns a type that is not trivial.
struct SharedOut {
    void* ptr = nullptr;
    void* ctrl = nullptr;
    ~SharedOut() {}
};

HOOK_DEFINE_TRAMPOLINE(MakeAnimator) {
    static SharedOut Callback(u8* comp, StringView* name, SharedPtr* model) {
        const void* row = comp != nullptr ? At<const void*>(comp, kNpcRow) : nullptr;
        if (g_LayeredGhosts && row != nullptr && model != nullptr && model->ptr != nullptr && row == GhostRow()) {
            SharedOut out;
            int layers = 2;
            GameFn<void (*)(SharedOut*, void*, StringView*, SharedPtr*, int*)>(kLayeredMake)(
                &out, At<void*>(comp, kNpcEntity), name, model, &layers);
            if (out.ptr != nullptr) {
                u8* set = static_cast<u8*>(out.ptr) + kSacLayerSet;
                GameFn<void (*)(void*, int, const char*, bool)>(kLayerBoneMask)(
                    set, kUpperLayer, reinterpret_cast<const char*>(g_BaseAddress + kSpineA), true);
                GameFn<void (*)(void*, int, bool)>(kLayerEnable)(set, kUpperLayer, false);
                // started and made the entity's child, as NpcAnimator_Make does with its player
                void* entity = At<void*>(comp, kNpcEntity);
                void* manager = *reinterpret_cast<void**>(g_BaseAddress + kEntityManager);
                if (manager != nullptr) {
                    SharedPtr sp{out.ptr, out.ctrl};
                    if (sp.ctrl != nullptr) __atomic_fetch_add(CtrlCount(sp.ctrl, kCtrlOwners), 1, __ATOMIC_ACQ_REL);
                    GameFn<void (*)(void*, SharedPtr*)>(kStartTask)(manager, &sp);
                    ReleaseShared(sp.ctrl);
                }
                GameFn<bool (*)(void*, void*)>(kAddChild)(entity, out.ptr);
                return out;
            }
        }
        return Orig(comp, name, model);
    }
};

#ifdef LASAP_TEST
// The plays by name on the puppets' animation components (DBG puppet animlog).
HOOK_DEFINE_TRAMPOLINE(PlayByName) {
    static void Callback(void* comp, NameView* name, u8 mode, float blend, float frame) {
        if (mode == 0 && comp != nullptr) {
            for (auto& p : g_Puppets) {
                if (comp == AnimComp(p) && p.animLog.size() < 40)
                    p.animLog.push_back(std::string(name->ptr, name->len) + (blend == 0 ? "/0" : "") + "@" +
                                        std::to_string(g_LastFrame));
            }
        }
        Orig(comp, name, mode, blend, frame);
    }
};
#endif

HOOK_DEFINE_TRAMPOLINE(BindUniformBuffer) {
    static void Callback(u8* wrapper, int stage, int slot, u64* bufferOffset, u64 size) {
        Orig(wrapper, stage, slot, bufferOffset, size);
        int span = bufferOffset != nullptr ? PuppetBlock(bufferOffset[0] + bufferOffset[1]) : -1;
        if (span < 0) return;
        g_PendingInst = span >= kInstSpan;
        int i = WrapperIndex(wrapper);
        if (i >= 0 && (g_SkipMask >> i & 1)) {
            g_SkipPending = wrapper;
            return;
        }
        if (!g_Dither) {
            g_DrawsBy[i >= 0 ? i : 0]++;
            return;
        }
        BindGhostMs(*reinterpret_cast<void**>(wrapper + kWrapperCmdBuf));
        g_GhostPending = wrapper;
    }
};

HOOK_DEFINE_TRAMPOLINE(SetRasterState) {
    static void Callback(u8* context, void* state) {
        NoteContext(context);
        Orig(context, state);
        u8* wrapper = *reinterpret_cast<u8**>(context + kContextWrapper);
        if (g_GhostPending == nullptr || g_GhostPending != wrapper) return;
        BindGhostMs(*reinterpret_cast<void**>(wrapper + kWrapperCmdBuf));
    }
};

HOOK_DEFINE_TRAMPOLINE(DrawElementsBaseVertex) {
    static void Callback(void* wrapper, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) {
        if (g_SkipPending != nullptr && g_SkipPending == wrapper) {
            g_SkipPending = nullptr;
            return;
        }
        Orig(wrapper, a, b, c, d, e, f);
        if (g_GhostPending != nullptr && g_GhostPending == wrapper) EndGhostDraw(wrapper);
    }
};

HOOK_DEFINE_TRAMPOLINE(DrawElementsInstanced) {
    static void Callback(void* wrapper, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) {
        if (g_SkipPending != nullptr && g_SkipPending == wrapper) {
            g_SkipPending = nullptr;
            return;
        }
        Orig(wrapper, a, b, c, d, e, f);
        if (g_GhostPending != nullptr && g_GhostPending == wrapper) EndGhostDraw(wrapper);
    }
};

namespace AP::Puppet {
    void InstallHooks() {
#ifdef LASAP_TEST
        PlayByName::InstallAtOffset(offset_manager.Offset(kPlayByName[0]));
#endif
        NpcComponentCtor::InstallAtOffset(offset_manager.Offset(kNpcComponentCtor));
        MakeAnimator::InstallAtOffset(offset_manager.Offset(kMakeAnimator));
        BindUniformBuffer::InstallAtOffset(offset_manager.Offset(kBindUniformBuffer));
        SetRasterState::InstallAtOffset(offset_manager.Offset(kSetRasterState));
        DrawElementsBaseVertex::InstallAtOffset(offset_manager.Offset(kDrawElementsBaseVertex));
        DrawElementsInstanced::InstallAtOffset(offset_manager.Offset(kDrawElementsInstanced));
    }

    // Once a second: the frame rate, and the crowd limits after it.
    void Adapt(u32 frame, u64 now, int shown) {
        // a window spanning a level load (no ticks then) starts again
        if (g_FpsStart == 0 || now < g_FpsStart || frame < g_FpsFrame || now - g_FpsStart > 2000) {
            g_FpsStart = now;
            g_FpsFrame = frame;
            return;
        }
        if (now - g_FpsStart < 1000) return;
        g_Fps = static_cast<float>(frame - g_FpsFrame) * 1000 / static_cast<float>(now - g_FpsStart);
        g_FpsStart = now;
        g_FpsFrame = frame;
        if (!g_Adapt || shown == 0 || now < g_SettleUntil) return;
        int full = std::min(g_FullLimit, shown), drawn = std::min(g_DrawnLimit, shown);
        bool retry = now - g_TooSlowAt >= kRetryMs;
        if (g_Fps < kSlowFps) {
            if (full > kMinWhole) {
                g_TooSlowFull = full;
                g_FullLimit = std::max(kMinWhole, full - full / 4 - 1);
            } else if (drawn > kMinDrawn) {
                g_TooSlowDrawn = drawn;
                g_DrawnLimit = std::max(kMinDrawn, drawn - drawn / 4 - 1);
            }
            g_TooSlowAt = g_LastChange = now;
        } else if (g_Fps >= kSteadyFps && now - g_LastChange >= kRaiseMs) {
            if (drawn < shown && (drawn + 1 < g_TooSlowDrawn || retry)) g_DrawnLimit = drawn + 1;
            else if (drawn >= shown && full < shown && (full + 1 < g_TooSlowFull || retry)) g_FullLimit = full + 1;
            g_LastChange = now;
        }
    }

    // Each frame: the shown ghosts by distance from Link, nearest first, get their detail.
    void Rank(Game::Actors::Player* player, u32 frame, u64 now) {
        int order[kGhosts];
        int count = 0;
        for (int n = 0; n < kGhosts; n++) {
            if (g_Puppets[n].shown && g_Puppets[n].model != nullptr) order[count++] = n;
        }
        if (player->playerCollision != nullptr) {
            const Game::Vec3 at = player->playerCollision->coordsNew.pos;
            float key[kGhosts];
            for (int i = 0; i < count; i++) {
                const auto& p = g_Puppets[order[i]];
                key[order[i]] = std::hypot(p.track.pos.x - at.x, p.track.pos.z - at.z) - g_KeepMargin * p.detail;
            }
            std::sort(order, order + count, [&](int a, int b) { return key[a] < key[b]; });
        }
        for (int i = 0; i < count; i++) {
            int& detail = g_Puppets[order[i]].detail;
            int now = i < g_FullLimit ? 2 : i < g_DrawnLimit ? 1 : 0;
            if (now != detail) g_DetailChanges++;
            detail = now;
        }
        Adapt(frame, now, count);
    }

    void TickPuppets();

    // Once per input poll (after GhostNet's); moves the puppets once per frame.
    void Tick() {
        u64 t0 = Ticks();
        u32 before = g_LastFrame;
        TickPuppets();
        if (g_LastFrame != before) g_TickTicks[g_LastFrame % 60] = Ticks() - t0;
    }

    void TickPuppets() {
        auto* player = Player();
        Game::Framework* fmwk = Game::GetFramework();
        if (player == nullptr || fmwk == nullptr) {
            // a level loads
            for (int n = 0; n < kPuppets; n++) {
                ClearSpan(n);
                g_Puppets[n].wasActive = false;
            }
            g_SettleUntil = NowMs() + kSettleMs;
            AP::NameTag::DropAll();
            InstDropAll(false);
#ifdef LASAP_TEST
            g_HistoryValid = false;
#endif
            return;
        }
        u32 frame = fmwk->mFrameCount;
        if (frame == g_LastFrame) {
            // the game paused (a menu opens): its frame count stays and the actors stand, but the instanced models'
            // records are emptied each frame drawn, so they are written again
            InstTick();
            return;
        }
        g_LastFrame = frame;
        for (auto& p : g_Puppets) {
            if (p.ctrl != nullptr && !Alive(p)) Forget(p);
            if (p.ctrl != nullptr) RefreshModel(p);
        }
#ifdef LASAP_TEST
        if (player->playerCollision != nullptr) {
            auto& c = player->playerCollision->coordsNew;
            g_History[g_HistoryFrame % kHistory] = Sample{c.pos, c.rotate};
            g_HistoryFrame++;
            g_HistoryValid = true;
        }
#endif
        std::string level = LevelSystem::GetCurrentLevel();
        u64 now = NowMs();
        double nowFine = NowMsFine();
#ifdef LASAP_TEST
        g_Past = g_Held = 0;
        g_TraceNext.now = 0;
#endif
        for (int n = 0; n < kGhosts; n++) DriveGhost(n, level, frame, now, nowFine);
#ifdef LASAP_TEST
        if (g_TraceNext.now != 0 && g_TraceCount < kTraceRows) {
            g_TraceNext.past = g_Past;
            g_TraceNext.held = g_Held;
            u32 lines = 0;
            for (int n = 0; n < kGhosts; n++) lines += AP::GhostNet::Get(n).frames;
            g_TraceNext.lines = static_cast<int>(lines - g_LinesSeen);
            g_LinesSeen = lines;
            g_Trace[g_TraceCount++] = g_TraceNext;
        }
        DriveTest(frame, now, nowFine);
#endif
        Rank(player, frame, now);
        for (auto& p : g_Puppets) {
            if (Alive(p)) Update(p);
        }
        InstTick();
#ifdef LASAP_TEST
        {
            u32 records = 0, ready = 0;
            for (auto& inst : g_Inst) records += inst.drawn;
            for (int n = 0; n < kGhosts; n++) ready += g_Puppets[n].ready;
            g_FrameLog[g_FrameLogCount++ % kFrameLog] =
                FrameRow{frame, reinterpret_cast<u8*>(fmwk)[kFrameworkPaused], g_GhostDraws, g_InstDraws, records, ready};
        }
#endif
        // the name over the head of each ghost drawn whole, once its body draws (each tag's text costs draws too; the
        // crowd limits count them)
        for (int n = 0; n < kGhosts; n++) {
            const auto& p = g_Puppets[n];
            if (g_NameTags && AP::GhostNet::ShowNames() && p.ready && p.detail == 2 && Alive(p)) {
                const float* head = BoneWorld(p.model, kHeadBone);
                const Game::Vec3& at = p.track.pos;
                Game::Vec3 tag = head != nullptr ? Game::Vec3{head[12], head[13] + kTagAboveHead, head[14]}
                                                 : Game::Vec3{at.x, at.y + kTagHeight, at.z};
                AP::NameTag::Show(n, tag, AP::GhostNet::Get(n).name);
            } else {
                AP::NameTag::Hide(n);
            }
        }
    }

#ifdef LASAP_TEST
    std::string StateField() {
        int shown = 0, actors = 0;
        for (int i = 0; i < kGhosts; i++) {
            shown += g_Spans[i].hi.load() != 0;
            actors += g_Puppets[i].ctrl != nullptr;
        }
        // and ghost slot 0's look on its model: the shapes, tunic, face frames and upper-body animation last set
        const auto& p = g_Puppets[0];
        std::string gear = p.model != nullptr && p.shown
                               ? Hex(p.look.shapes) + "/" + std::to_string(p.tunic) + "/" + std::to_string(p.faceSet[0]) +
                                     "," + std::to_string(p.faceSet[1]) + "," + std::to_string(p.faceSet[2]) + "/" +
                                     std::to_string(p.upper)
                               : "-";
        return std::to_string(shown) + "/" + std::to_string(actors) + " gear=" + gear + " " + AP::NameTag::State();
    }

    bool Command(const std::vector<std::string>& a) {
        if (a[1] != "puppet") return false;
        const std::string sub = a.size() >= 3 ? a[2] : "";
        auto& p = g_Puppets[kTest];
        auto* player = Player();
        if (sub == "spawn" && a.size() >= 4) {
            // DBG puppet spawn <actor id hex> [dx dz]: beside Link
            if (player == nullptr || player->playerCollision == nullptr) {
                AP::Send("ERR puppet: no player");
                return true;
            }
            Game::Vec3 pos = player->playerCollision->coordsNew.pos;
            if (a.size() >= 6) {
                pos.x += atof(a[4].c_str());
                pos.z += atof(a[5].c_str());
            } else {
                pos.x += 1.5f;
            }
            std::string err;
            if (!Spawn(p, static_cast<u16>(strtoul(a[3].c_str(), nullptr, 16)), pos, g_LastFrame, err)) {
                AP::Send("ERR puppet: " + err);
                return true;
            }
            g_At = pos;
            g_Delay = -2;
        } else if (sub == "at" && a.size() >= 6) {
            g_At = Game::Vec3{(float)atof(a[3].c_str()), (float)atof(a[4].c_str()), (float)atof(a[5].c_str())};
            g_Delay = -1;
        } else if (sub == "follow" && a.size() >= 4) {
            // DBG puppet follow <frames>: Link's path (position and rotation) that many frames late
            g_Delay = std::min(atoi(a[3].c_str()), kHistory - 1);
        } else if (sub == "steer" && a.size() >= 4) {
            // DBG puppet steer <hz>: with follow, the path taken this many times a second and replayed; 0: exact
            g_SteerHz = atoi(a[3].c_str());
            ResetTrack(p.track);
        } else if (sub == "draw" && a.size() >= 4) {
            // DBG puppet draw on|off: the ghosts' models drawn or hidden; actors, animation and lines run on
            g_DrawGhosts = a[3] == "on";
        } else if (sub == "lod" && a.size() >= 4) {
            // DBG puppet lod auto | lod <whole> <drawn>: the crowd limits follow the frame rate, or are fixed
            g_Adapt = a[3] == "auto";
            if (!g_Adapt && a.size() >= 5) {
                g_FullLimit = std::clamp(atoi(a[3].c_str()), 0, kGhosts);  // below the minimums: for measuring
                g_DrawnLimit = std::clamp(atoi(a[4].c_str()), 0, kGhosts);
            }
        } else if (sub == "keep" && a.size() >= 4) {
            // DBG puppet keep <units>: the crowd ranking's margin for a ghost's present detail
            g_KeepMargin = static_cast<float>(atof(a[3].c_str()));
        } else if (sub == "crowdshapes" && a.size() >= 4) {
            // DBG puppet crowdshapes <hex mask>: the shapes of ghosts drawn as part of a crowd
            g_CrowdMask = strtoul(a[3].c_str(), nullptr, 16);
        } else if (sub == "shapes" && a.size() >= 4) {
            // DBG puppet shapes <hex mask>: only those of the look's shapes drawn
            g_ShapeMask = strtoul(a[3].c_str(), nullptr, 16);
        } else if (sub == "skip" && a.size() >= 4) {
            // DBG puppet skip <hex mask>: drops the puppets' draws on those command buffers (draws/frame's order)
            g_SkipMask = strtoul(a[3].c_str(), nullptr, 16);
        } else if (sub == "dither" && a.size() >= 4) {
            // DBG puppet dither on|off: the ghosts drawn with or without the dither state
            g_Dither = a[3] == "on";
        } else if (sub == "tagstyle" && a.size() >= 9) {
            // DBG puppet tagstyle <scale> <text alpha> <window alpha> <raise> <margin x> <margin y>: the tags' look
            AP::NameTag::Style(static_cast<float>(atof(a[3].c_str())), atoi(a[4].c_str()), atoi(a[5].c_str()),
                               static_cast<float>(atof(a[6].c_str())), static_cast<float>(atof(a[7].c_str())),
                               static_cast<float>(atof(a[8].c_str())));
        } else if (sub == "tags" && a.size() >= 4) {
            // DBG puppet tags on|off: the ghosts' name tags
            g_NameTags = a[3] == "on";
        } else if (sub == "tags") {
            // DBG puppet tags: the tags' text extents
            AP::Send(AP::NameTag::Info());
            return true;
        } else if (sub == "blend" && a.size() >= 4) {
            // DBG puppet blend <s>: the blend time of the sender's animations on the puppets
            g_AnimBlend = std::clamp(static_cast<float>(atof(a[3].c_str())), 0.0f, 2.0f);
        } else if (sub == "buffer" && a.size() >= 4) {
            // DBG puppet buffer <ms>: how far behind the newest frame (on the sender's clock) puppets replay
            g_BufferMs = std::clamp(atof(a[3].c_str()), 0.0, 1000.0);
        } else if (sub == "anim" && a.size() >= 4) {
            // DBG puppet anim <name>: plays one of the model's animations (on the game thread)
            g_AnimPending = a[3];
        } else if (sub == "animlog") {
            // DBG puppet animlog: the animations played on each puppet since its spawn or the last animlog
            std::string line = "ANIMLOG";
            for (auto& q : g_Puppets) {
                if (q.animLog.empty()) continue;
                int n = IndexOf(q);
                line += " | " + (n == kTest ? std::string("test") : std::to_string(n));
                for (auto& l : q.animLog) line += " " + l;
                q.animLog.clear();
            }
            AP::Send(line);
            return true;
        } else if (sub == "vsyncwait" && a.size() >= 4) {
            // DBG puppet vsyncwait on|off: whether each frame ends by waiting for the next vsync (the graphics system,
            // *(0x1cc8b30)[1], +0x8169: its vsync watcher runs; the frame's end then clears and waits for its event).
            // Only with a watcher (+0x81b0 its thread): No 30 FPS Drops starts none, and nothing would signal the event.
            u8* graphics = *reinterpret_cast<u8**>(g_BaseAddress + 0x1cc8b30);
            u8* system = graphics != nullptr ? *reinterpret_cast<u8**>(graphics + 8) : nullptr;
            if (system != nullptr && *reinterpret_cast<void**>(system + 0x81b0) != nullptr) system[0x8169] = a[3] == "on";
        } else if (sub == "trace" && a.size() >= 4) {
            // DBG puppet trace <slot>: records that ghost's next replay frames
            g_TraceSlot = atoi(a[3].c_str());
            g_TraceCount = 0;
        } else if (sub == "tracedump") {
            // DBG puppet tracedump: "TRACE <now ms> <replay time ms> <frame's time> <frame>/<frames> <mode> <x> <z> <past> <held> <lines>"
            // per recorded frame
            for (u32 i = 0; i < g_TraceCount; i++) {
                const TraceRow& r = g_Trace[i];
                char buf[96];
                snprintf(buf, sizeof(buf), "TRACE %.2f %.2f %lld %u/%u %c %.3f %.3f %d %d %d", r.now, r.t,
                         static_cast<long long>(r.at), r.a, r.count, r.mode, r.x, r.z, r.past, r.held, r.lines);
                AP::Send(buf);
            }
            AP::Send("TRACE end");
            return true;
        } else if (sub == "layered" && a.size() >= 4) {
            // DBG puppet layered on|off: whether new ghost models get the layered animation player
            g_LayeredGhosts = a[3] == "on";
        } else if (sub == "framelog") {
            // DBG puppet framelog: "FRAMELOG <frame> <paused> <ghost draws> <instanced draws> <records> <ready>" per
            // logged frame (the draws since the frame before), oldest first
            u32 from = g_FrameLogCount > kFrameLog ? g_FrameLogCount - kFrameLog : 0;
            for (u32 i = from; i < g_FrameLogCount; i++) {
                const FrameRow& r = g_FrameLog[i % kFrameLog];
                const FrameRow& q = g_FrameLog[(i > from ? i - 1 : i) % kFrameLog];
                char buf[96];
                snprintf(buf, sizeof(buf), "FRAMELOG %u %u %u %u %u %u", r.frame, r.paused, r.draws - q.draws,
                         r.instDraws - q.instDraws, r.records, r.ready);
                AP::Send(buf);
            }
            AP::Send("FRAMELOG end");
            return true;
        } else if (sub == "drop") {
            // DBG puppet drop: removes DBG puppet's actor
            Remove(p);
            g_Delay = -2;
        }
        AP::Send(Info());
        return true;
    }
#endif
}
