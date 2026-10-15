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
    constexpr size_t kParamRoom = 0xc;     // u32 copied from the owner's +0xb0
    constexpr size_t kParamPos = 0x180;    // Vec4
    constexpr size_t kParamRot = 0x190;    // quaternion; PopEnemy copies the identity from a global
    constexpr size_t kParamScale = 0x1a0;  // Vec4 of ones, from a global
    constexpr uintptr_t kIdentityRot = 0x1c93de8, kOneScale = 0x1c93df0;  // globals: pointers to 16 bytes
    // What PopEnemy releases afterwards: a weak_ptr's control block at +0x1b8, shared_ptr control blocks at +0x168
    // and +0x158, then three member destructors.
    constexpr uintptr_t kParamsDtor0 = 0x1cf70, kParamsDtor1 = 0x1d190, kParamsDtor2 = 0x1d3b0;
    constexpr size_t kParamsDtor0At = 0xd0, kParamsDtor1At = 0x50, kParamsDtor2At = 0x10;
    constexpr uintptr_t kReleaseWeak = 0x14311f0;       // std::__shared_weak_count::__release_weak
    // libc++ control block: +8 shared owners - 1 (-1: the object is gone), +0x10 weak owners - 1
    constexpr size_t kCtrlOwners = 8, kCtrlWeakOwners = 0x10;
    constexpr size_t kActorRoom = 0xb0;
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

    // Replay: a puppet is fed frames (a ghost's lines, or Link's own path for DBG puppet), each stamped with the
    // sender's clock, and shows them on that timeline g_BufferMs behind: its position between the two frames around
    // that time, its facing that of the last frame before it (Link turns in steps: a turn is not smoothed). The
    // sender's clock maps to this one by the least delay seen, drifting up slowly, so a line that arrives late waits
    // less instead of moving the ghost faster. Past the newest frame it goes on along the last segment for up to
    // kExtrapolateMs, unless that frame stood still, then holds. A warped frame is jumped to at its time. A frame more
    // than kResetMs behind the newest starts a new timeline. When new lines move the replay's position (the sender
    // turned or stopped while the replay ran on past its newest line), the puppet does not jump: the difference is
    // added to where it shows and fades out with the time constant kCorrectMs (unless it is over kCorrectMax units).
    constexpr int kTrack = 32;
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
    constexpr float kTagHeight = 1.3f;  // the name tag's point above the ghost's feet
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
    // An actor spawned in a level's first frames can stay inactive (entity flags without bit 2, (f & 0x40000017) !=
    // 0x17): never updated or drawn. One inactive this long is made again; an active one stays active through
    // pauses and level changes.
    constexpr u32 kActiveMask = 0x40000017, kActive = 0x17;
    constexpr u32 kInactiveFrames = 30;
    u32 g_Remade = 0;  // DBG puppet: actors made again

    enum class Look { Unknown, Ghost, Hidden };
    struct Puppet {
        void* actor = nullptr;
        void* ctrl = nullptr;    // a weak reference to the actor
        u8* model = nullptr;     // once loaded
        Look look = Look::Unknown;
        u32 gear = AP::GhostNet::LOOK_DEFAULT;  // the look of the frame replayed
        int tunic = -1;                          // the tunic set, -1: none yet, kNoTunic: the model has none
        void* tunicAnim = nullptr;               // the animation on the tunic's slot when it was set
        bool shown = false;
        u64 hiddenSince = 0;     // ms
        u32 spawnAfter = 0;      // frame
        Track track;
        u32 framesSeen = 0;      // the ghost's lines fed so far
        u32 inactiveFrames = 0;  // shown with an inactive actor
        int detail = 2;          // 2: its whole look drawn, 1: only kCrowdShapes, 0: none (crowds, Rank)
#ifdef LASAP_TEST
        std::vector<std::string> animLog;  // its plays and model loads, for DBG puppet animlog
        u32 shownAt = 0;                   // frame it was last shown
#endif
    };
    Puppet g_Puppets[kPuppets];
    // Each shown puppet's blocks as GPU addresses [lo, hi), for the render thread's hooks; hi 0: none.
    struct Span { std::atomic<u64> lo{0}, hi{0}; };
    Span g_Spans[kPuppets];

    u32 g_LastFrame = 0;

    // Crowds. Each shape a ghost draws costs a draw call in each of two render passes, and on an emulator the draw
    // calls limit the frame rate (2026-10-15, Ryubing: 60 fps up to about 120 ghost shapes a pass, whatever their
    // size; a ghost in its usual look draws about 6). The nearest ghosts draw their whole look, the next ones only
    // kCrowdShapes (hat, face, tunic, hair and skin), the farthest none. How many of each follows the frame rate: down by a
    // quarter after a second under kSlowFps, up by one after kRaiseMs at kSteadyFps or more, but back to a count that
    // was too slow only after kRetryMs. The kMinWhole nearest are always whole and the kMinDrawn nearest always drawn
    // (the game's own slow moments, as after a level load, which kSettleMs skips, are no reason to hide a few).
    // Ranked by distance, less g_KeepMargin per step of detail a ghost has: one near a limit keeps its detail until
    // another is that much nearer, so ghosts walking about at about the same distance do not switch back and forth.
    constexpr u32 kCrowdShapes = (1u << 3) | (1u << 4) | (1u << 5) | (1u << 11);
    constexpr float kSlowFps = 55, kSteadyFps = 59;
    constexpr u64 kRaiseMs = 3000, kRetryMs = 30000, kSettleMs = 3000;
    constexpr int kMinWhole = 6, kMinDrawn = 16;
    int g_FullLimit = 8;         // ghosts drawn whole
    int g_DrawnLimit = kGhosts;  // ghosts drawn at all
    u32 g_CrowdMask = kCrowdShapes;
    float g_KeepMargin = 2;      // DBG puppet keep
    u32 g_DetailChanges = 0;     // DBG puppet: ghosts' detail changed by Rank
    bool g_Adapt = true;         // DBG puppet lod <full> <drawn> fixes the limits, lod auto adapts them again
    u64 g_FpsStart = 0, g_LastChange = 0, g_TooSlowAt = 0, g_SettleUntil = 0;
    int g_TooSlowFull = kGhosts + 1, g_TooSlowDrawn = kGhosts + 1;  // the counts last found too slow
    u32 g_FpsFrame = 0;
    float g_Fps = 60;

#ifdef LASAP_TEST
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

    // The puppet's model once it is loaded: its animation player's model.
    u8* ModelOf(const Puppet& p) { return At<u8*>(At<void*>(AnimComp(p), kAnimCompPlayer), kAnimPlayerModel); }

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

    // Draws only the materials a ghost shows, or (hidden) none.
    void SetLook(Puppet& p, Look look) {
        if (p.model == nullptr || p.look == look) return;
        u16 count = At<u16>(p.model, kModelMatCount);
        u8* mats = At<u8*>(p.model, kModelMats);
        u32* visible = At<u32*>(p.model, kModelMatVis);
        for (u16 i = 0; i < count; i++) {
            std::string_view name;
            if (const u8* res = At<const u8*>(mats + i * kMatStride, 0)) {
                const u8* s = At<const u8*>(res, 8);
                name = std::string_view(reinterpret_cast<const char*>(s + 2), *reinterpret_cast<const u16*>(s));
            }
            bool shown = look == Look::Ghost && std::any_of(std::begin(kShownMaterials), std::end(kShownMaterials),
                                                            [&](const char* m) { return name == m; });
            if (shown) visible[i / 32] |= 1u << (i % 32);
            else visible[i / 32] &= ~(1u << (i % 32));
        }
        p.look = look;
    }

    void Hide(Puppet& p, u64 now) {
        ClearSpan(IndexOf(p));
        if (Alive(p)) SetLook(p, Look::Hidden);
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

    // Whether the Npc sheet has the APGhost row (the patcher adds it with the ghosts' model).
    bool HasGhostRow() {
        void* sheet = **reinterpret_cast<void***>(g_BaseAddress + kNpcSheets);
        return sheet != nullptr && GameFn<void* (*)(void*, StringView*, u64)>(kFindRow)(sheet, &g_GhostRow, kNpcRowSize) != nullptr;
    }

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
        u8* owner = reinterpret_cast<u8*>(player);
        alignas(16) u8 params[kParamsSize] = {};
        *reinterpret_cast<u16*>(params + kParamId) = id;
        *reinterpret_cast<u32*>(params + kParamRoom) = *reinterpret_cast<u32*>(owner + kActorRoom);
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
        p.gear = fa.look;
        // From the model's load on: the NPC plays its own idle as it loads.
        if (p.model == nullptr) return;
        if (fa.anim > 0) {
            bool started = (fa.flags & kFlagStarted) != 0 && tr.started != a;
            if (fa.anim != tr.anim || started) {
                if (const char* name = AP::GhostNet::AnimName(fa.anim)) PlayAnim(p, name, g_AnimBlend, fa.animFrame / 10.0f);
                tr.anim = fa.anim;
                tr.started = a;
                tr.gait = -1;
            }
            return;
        }
        int gait = tr.gait < 0 ? GaitAt(speed) : std::clamp(tr.gait, GaitAt(speed - kGaitMargin),
                                                             GaitAt(speed + kGaitMargin));
        if (gait != tr.gait) {
            PlayAnim(p, kGaits[gait], kGaitBlend, 0);
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
        p.look = Look::Unknown;
        p.tunic = -1;
        p.tunicAnim = nullptr;
        p.track.gait = -1;
        p.track.anim = -1;
    }

    // The look's shapes on the puppet's model.
    void SetShapes(Puppet& p) {
        u8* instance = At<u8*>(At<void*>(AnimComp(p), kAnimCompPlayer), kAnimPlayerInstance);
        if (At<u8*>(At<u8*>(instance, kInstanceModel), kModelObject) != p.model) return;
        u8* shapes = At<u8*>(instance, kInstanceShapes);
        if (shapes == nullptr) return;
        int count = std::min<int>(At<u16>(p.model, kModelShapeCount), AP::GhostNet::LOOK_SHAPES);
        u32 mask = p.detail == 2 ? g_ShapeMask : g_ShapeMask & g_CrowdMask;
        for (int i = 0; i < count; i++) shapes[i] = (p.gear & mask) >> i & 1;
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
        int tunic = std::min<int>((p.gear >> AP::GhostNet::LOOK_TUNIC_SHIFT) & 3, 2);
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

    // Last each frame for each puppet with an actor: drawn as a ghost (dithered) or hidden. The model's setup sets
    // the visibility bits some frames after it appears, so they are written again every half second.
    void Update(Puppet& p, u32 frame) {
        if (p.model == nullptr) return;
        if (frame % 30 == 0) p.look = Look::Unknown;
        bool draw = p.shown && g_DrawGhosts && p.detail > 0;
        SetLook(p, draw ? Look::Ghost : Look::Hidden);
        if (!p.shown) return;
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

    bool IsPuppetBlock(u64 address) {
        for (auto& s : g_Spans) {
            u64 hi = s.hi.load(std::memory_order_acquire);
            if (hi != 0 && address >= s.lo.load(std::memory_order_acquire) && address < hi) return true;
        }
        return false;
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
        int i = WrapperIndex(wrapper);
        if (i >= 0) g_DrawsBy[i]++;
        g_GhostPending = nullptr;
        u8* context = ContextOf(wrapper);
        if (context == nullptr) return;
        void* state = *reinterpret_cast<void**>(context + kContextRasterCache);
        if (state != nullptr) GameFn<void (*)(void*, void*)>(kBindRasterState)(wrapper, state);
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
        if ((At<u32>(p.actor, kActorFlags) & kActiveMask) != kActive) {
            if (++p.inactiveFrames >= kInactiveFrames) {
                g_Remade++;
                Remove(p);  // spawned again next frame
                return;
            }
        } else {
            p.inactiveFrames = 0;
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
                                        YawOf(s.rot) * 180 / static_cast<float>(M_PI), anim.anim, anim.frame, flags,
                                        AP::GhostNet::LinkLook()});
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
            const char* anim = p.track.anim > 0 ? AP::GhostNet::AnimName(p.track.anim)
                                                : p.track.gait >= 0 ? kGaits[p.track.gait] : nullptr;
            s += " anim=" + std::string(anim != nullptr ? anim : "-");
            int slot;
            TunicSlot(p, slot);
            s += " gear=" + Hex(p.gear) + " tunic=" + std::to_string(p.tunic) + "@" + std::to_string(slot);
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

#ifdef LASAP_TEST
// The plays by name on the puppets' animation components (DBG puppet animlog).
HOOK_DEFINE_TRAMPOLINE(PlayByName) {
    static void Callback(void* comp, NameView* name, u8 mode, float blend, float frame) {
        if (mode == 0 && comp != nullptr) {
            for (auto& p : g_Puppets) {
                if (comp == AnimComp(p) && p.animLog.size() < 40)
                    p.animLog.push_back(std::string(name->ptr, name->len) + "@" + std::to_string(g_LastFrame));
            }
        }
        Orig(comp, name, mode, blend, frame);
    }
};
#endif

HOOK_DEFINE_TRAMPOLINE(BindUniformBuffer) {
    static void Callback(u8* wrapper, int stage, int slot, u64* bufferOffset, u64 size) {
        Orig(wrapper, stage, slot, bufferOffset, size);
        if (bufferOffset == nullptr || !IsPuppetBlock(bufferOffset[0] + bufferOffset[1])) return;
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
        BindUniformBuffer::InstallAtOffset(offset_manager.Offset(kBindUniformBuffer));
        SetRasterState::InstallAtOffset(offset_manager.Offset(kSetRasterState));
        DrawElementsBaseVertex::InstallAtOffset(offset_manager.Offset(kDrawElementsBaseVertex));
        DrawElementsInstanced::InstallAtOffset(offset_manager.Offset(kDrawElementsInstanced));
    }

    // Once per input poll (after GhostNet's); moves the puppets once per frame.
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
            for (int n = 0; n < kPuppets; n++) ClearSpan(n);
            g_SettleUntil = NowMs() + kSettleMs;
            AP::NameTag::DropAll();
#ifdef LASAP_TEST
            g_HistoryValid = false;
#endif
            return;
        }
        u32 frame = fmwk->mFrameCount;
        if (frame == g_LastFrame) return;
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
            if (Alive(p)) Update(p, frame);
        }
        // the name over the head of each ghost drawn whole (each tag's text costs draws too; the crowd limits count them)
        for (int n = 0; n < kGhosts; n++) {
            const auto& p = g_Puppets[n];
            if (g_NameTags && AP::GhostNet::ShowNames() && p.shown && p.detail == 2 && p.model != nullptr && Alive(p)) {
                const Game::Vec3& at = p.track.pos;
                AP::NameTag::Show(n, Game::Vec3{at.x, at.y + kTagHeight, at.z}, AP::GhostNet::Get(n).name);
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
        // and ghost slot 0's look on its model: the shapes and tunic last set
        const auto& p = g_Puppets[0];
        std::string gear = p.model != nullptr && p.shown ? Hex(p.gear) + "/" + std::to_string(p.tunic) : "-";
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
            // *(0x1cc8b30)[1], +0x8169: its vsync watcher runs; the frame's end then clears and waits for its event)
            u8* graphics = *reinterpret_cast<u8**>(g_BaseAddress + 0x1cc8b30);
            u8* system = graphics != nullptr ? *reinterpret_cast<u8**>(graphics + 8) : nullptr;
            if (system != nullptr) system[0x8169] = a[3] == "on";
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
