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
    // 0x9df590): (component, {name, length}*, mode, blend frames, frame). Mode 0 looks the name up in its animation
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
    // Link's materials drawn for a ghost: the body and the first sword and shield. The others are the items (rod,
    // ocarina, shovel, hookshot, flippers, second sword and shields) the player shows only in use.
    const char* const kShownMaterials[] = {"MI_body", "MI_boots", "MI_ear", "MI_eye", "MI_mouth", "MI_hair",
                                           "linkShieldA_MI_shieldA", "linkSwordA_MI_sowrdA", "linkSwordA_MI_sowrdAball"};

    // Replay: a puppet is fed frames (a ghost's lines, or Link's own path for DBG puppet), each stamped with the
    // sender's clock, and shows them on that timeline g_BufferMs behind: its position between the two frames around
    // that time, its facing that of the last frame before it (Link turns in steps: a turn is not smoothed). The
    // sender's clock maps to this one by the least delay seen, drifting up slowly, so a line that arrives late waits
    // less instead of moving the ghost faster. Past the newest frame it goes on along the last segment for up to
    // kExtrapolateMs, unless that frame stood still, then holds. A warped frame is jumped to at its time. A frame more
    // than kResetMs behind the newest starts a new timeline.
    constexpr int kTrack = 32;
    constexpr double kExtrapolateMs = 100, kOffsetRise = 0.02;
    constexpr s64 kResetMs = 1000;
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
        double lastNow = 0;
        float moved[60] = {};  // speed in each of the last 60 frames (DBG puppet: smoothness)
        u32 shownFrames = 0;
    };
    // Animations: the sender's (its PlayerAnimation.gsheet key, named in this game's sheet, the same FSKA in the
    // puppet's model), from the frame it started at, played when the replay reaches that frame; for a sender that
    // sends none (anim id 0), by speed (units per second): each from its threshold on, with a margin either side so a
    // speed near one does not switch back and forth.
    constexpr float kAnimBlend = 4;
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
    constexpr u32 kNoRowFrames = 600;   // after a spawn without the APGhost row

    enum class Look { Unknown, Ghost, Hidden };
    struct Puppet {
        void* actor = nullptr;
        void* ctrl = nullptr;    // a weak reference to the actor
        u8* model = nullptr;     // once loaded
        Look look = Look::Unknown;
        bool shown = false;
        u64 hiddenSince = 0;     // ms
        u32 spawnAfter = 0;      // frame
        Track track;
        u32 framesSeen = 0;      // the ghost's lines fed so far
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

    // a + (b - a) * t
    Game::Vec3 Along(const Frame& a, const Frame& b, float t) {
        return Game::Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }

    // One frame of the replay; picks the animation from the speed.
    void ReplayFrame(Puppet& p, double now) {
        Track& tr = p.track;
        if (tr.count == 0) return;
        double t = now - tr.offset - g_BufferMs;
        u32 oldest = tr.count > kTrack ? tr.count - kTrack : 0;
        // the last frame at or before t (the oldest while t is before them all)
        u32 a = tr.count - 1;
        while (a > oldest && static_cast<double>(tr.frames[a % kTrack].timeMs) > t) a--;
        const Frame& fa = tr.frames[a % kTrack];
        Game::Vec3 pos = PosOf(fa);
        float speed = 0;
        if (static_cast<double>(fa.timeMs) <= t) {
            if (a + 1 < tr.count) {
                const Frame& fb = tr.frames[(a + 1) % kTrack];
                float span = static_cast<float>(fb.timeMs - fa.timeMs);
                if (!(fb.flags & kFlagWarped) && span > 0) {
                    pos = Along(fa, fb, static_cast<float>(t - fa.timeMs) / span);
                    speed = std::hypot(fb.x - fa.x, fb.z - fa.z) * 1000 / span;
                }
            } else if (!(fa.flags & (kFlagStill | kFlagWarped)) && a > oldest && t - fa.timeMs < kExtrapolateMs) {
                const Frame& fp = tr.frames[(a - 1) % kTrack];
                float span = static_cast<float>(fa.timeMs - fp.timeMs);
                if (span > 0) {
                    pos = Along(fp, fa, 1 + static_cast<float>(t - fa.timeMs) / span);
                    speed = std::hypot(fa.x - fp.x, fa.z - fp.z) * 1000 / span;
                }
            }
        }
        double dt = now - tr.lastNow;
        if (tr.shownFrames > 0 && dt > 0)
            tr.moved[tr.shownFrames % 60] = static_cast<float>(std::hypot(pos.x - tr.pos.x, pos.z - tr.pos.z) * 1000 / dt);
        tr.shownFrames++;
        tr.lastNow = now;
        tr.pos = pos;
        Game::Vec4 q = QuatOf(fa.yaw * static_cast<float>(M_PI) / 180);
        Place(p, pos, &q);
        // From the model's load on: the NPC plays its own idle as it loads.
        if (p.model == nullptr) return;
        if (fa.anim > 0) {
            bool started = (fa.flags & kFlagStarted) != 0 && tr.started != a;
            if (fa.anim != tr.anim || started) {
                if (const char* name = AP::GhostNet::AnimName(fa.anim)) PlayAnim(p, name, kAnimBlend, fa.animFrame / 10.0f);
                tr.anim = fa.anim;
                tr.started = a;
                tr.gait = -1;
            }
            return;
        }
        int gait = tr.gait < 0 ? GaitAt(speed) : std::clamp(tr.gait, GaitAt(speed - kGaitMargin),
                                                             GaitAt(speed + kGaitMargin));
        if (gait != tr.gait) {
            PlayAnim(p, kGaits[gait], 6, 0);
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
        if (model != nullptr && p.animLog.size() < 40) p.animLog.push_back("model@" + std::to_string(g_LastFrame - p.shownAt));
#endif
        p.look = Look::Unknown;
        p.track.gait = -1;
        p.track.anim = -1;
    }

    // Last each frame for each puppet with an actor: drawn as a ghost (dithered) or hidden. The model's setup sets
    // the visibility bits some frames after it appears, so they are written again every half second.
    void Update(Puppet& p, u32 frame) {
        if (p.model == nullptr) return;
        if (frame % 30 == 0) p.look = Look::Unknown;
        SetLook(p, p.shown ? Look::Ghost : Look::Hidden);
        if (p.shown) PublishSpan(IndexOf(p), p.model);
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

    // After a puppet's draw on this wrapper: bind the raster state its context has cached.
    void EndGhostDraw(void* wrapper) {
        g_GhostDraws++;
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
            PlayAnim(p, g_AnimPending.c_str(), 6, 0);
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
                                        YawOf(s.rot) * 180 / static_cast<float>(M_PI), anim.anim, anim.frame, flags});
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
                        " dithered=" + std::to_string(g_GhostDraws);
        for (auto& p : g_Puppets) {
            if (!Alive(p)) continue;
            int n = IndexOf(p);
            const char* name = reinterpret_cast<const char*>(p.actor) + kActorName;
            s += " | " + (n == kTest ? std::string("test") : std::to_string(n)) + " actor=" +
                 Hex(reinterpret_cast<uintptr_t>(p.actor)) + (p.shown ? "" : " hidden") + " name=" +
                 std::string(name, strnlen(name, 0x20));
            if (auto* root = At<Game::RootComp*>(p.actor, kActorRoot)) s += " pos=" + PosText(root->coordsNew.pos);
            s += " model=" + (p.model != nullptr ? Hex(reinterpret_cast<uintptr_t>(p.model)) : "-");
            const char* anim = p.track.anim > 0 ? AP::GhostNet::AnimName(p.track.anim)
                                                : p.track.gait >= 0 ? kGaits[p.track.gait] : nullptr;
            s += " anim=" + std::string(anim != nullptr ? anim : "-");
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
                    p.animLog.push_back(std::string(name->ptr, name->len) + "@" + std::to_string(g_LastFrame - p.shownAt));
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
        Orig(wrapper, a, b, c, d, e, f);
        if (g_GhostPending != nullptr && g_GhostPending == wrapper) EndGhostDraw(wrapper);
    }
};

HOOK_DEFINE_TRAMPOLINE(DrawElementsInstanced) {
    static void Callback(void* wrapper, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) {
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
    void Tick() {
        auto* player = Player();
        Game::Framework* fmwk = Game::GetFramework();
        if (player == nullptr || fmwk == nullptr) {
            // a level loads
            for (int n = 0; n < kPuppets; n++) ClearSpan(n);
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
        for (int n = 0; n < kGhosts; n++) DriveGhost(n, level, frame, now, nowFine);
#ifdef LASAP_TEST
        DriveTest(frame, now, nowFine);
#endif
        for (auto& p : g_Puppets) {
            if (Alive(p)) Update(p, frame);
        }
    }

#ifdef LASAP_TEST
    std::string StateField() {
        int shown = 0, actors = 0;
        for (int i = 0; i < kGhosts; i++) {
            shown += g_Spans[i].hi.load() != 0;
            actors += g_Puppets[i].ctrl != nullptr;
        }
        return std::to_string(shown) + "/" + std::to_string(actors);
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
