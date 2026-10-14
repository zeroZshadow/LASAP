// Test build only (LASAP_TEST): the ghosts of multiplayer ghosts (DESIGN.md, Ghosts). One puppet per ghost slot of
// AP::GhostNet, and one for DBG puppet: an NPC actor spawned at runtime through the game's actor factory (as a chest
// pops its enemy), drawn with Link's model (its Npc row, set up by ghost_puppet.py) dithered by alpha-to-coverage,
// and moved by hand every frame: steered toward the positions it is fed, playing Link's animations by its speed.
// The actor system owns the actors, and they outlive level changes: each puppet keeps a weak reference to its actor
// and reuses it for the whole session, hidden while its ghost is not shown.
#ifdef LASAP_TEST
#include "AP/ap.hpp"
#include "AP/ap_debug.hpp"
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
    constexpr u16 kGhostActor = 0x179;     // NpcGhost, whose Npc row ghost_puppet.py points at the ghost model
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
    struct NameView { const char* ptr; u64 len; };
    using PlayByNameFn = void (*)(void* comp, NameView* name, u8 mode, float blend, float frame);
    constexpr uintptr_t kPlayByName[] = {0x949260, 0x9df590};
    constexpr size_t kActorAnimComp = 0x2b0, kAnimCompPlayer = 0xe8, kAnimPlayerModel = 0x90;
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

    // Steering: a puppet is fed target positions (a ghost's lines, or Link's own path for DBG puppet) and moves
    // toward them each frame: dead reckoning along the target's velocity plus a correction toward it, capped; it
    // snaps across jumps (warps, respawns). It turns toward where it goes, or the target's facing when still. The
    // target's velocity spans the targets of the last kVelocityFrames: lines arrive unevenly (sent on a timer, carried
    // by the network, read once a frame), and the last two alone give a speed that jumps.
    constexpr float kFrame = 1.0f / 60, kSnap = 4, kMaxSpeed = 12, kGain = 4, kTurnRate = 12;  // per second
    constexpr u32 kVelocityFrames = 18;
    u32 g_MinVelocityFrames = 6;  // DBG puppet window: 1 takes the last two targets
    constexpr int kFeeds = 16;
    struct Feeding { u32 frame; Game::Vec3 pos; };
    struct Steer {
        bool valid = false;
        Game::Vec3 pos{}, target{}, targetVel{};
        float yaw = 0, targetYaw = 0, speed = 0, age = 0;  // yaw in radians, 0 = +z
        u32 lastFeed = 0;
        int gait = -1;  // index in kGaits, -1: none played yet
        Feeding feeds[kFeeds];  // the last targets, newest at feeds[(count - 1) % kFeeds]
        u32 count = 0;
        float moved[60] = {};   // distance moved in each of the last 60 frames (DBG puppet: smoothness)
        u32 frames = 0;
    };
    // Animations by speed (units per second): each from its threshold on, with a margin either side so a speed near
    // one does not switch back and forth.
    constexpr const char* kGaits[] = {"wait", "walk", "run", "dash_lp"};
    constexpr float kGaitFrom[] = {0, 0.4f, 3.5f, 7.5f}, kGaitMargin = 0.3f;
    int GaitAt(float speed) {
        int g = 0;
        while (g + 1 < 4 && speed >= kGaitFrom[g + 1]) g++;
        return g;
    }

    // Puppets 0..SLOTS-1 show GhostNet's ghost slots; the last is DBG puppet's.
    constexpr int kGhosts = AP::GhostNet::SLOTS, kTest = kGhosts, kPuppets = kGhosts + 1;
    // A ghost whose last line is older than this is not shown (the relay and the client send offs; this covers a
    // lost one). A still player's game sends a position twice a second.
    constexpr u64 kStaleMs = 5000;
    constexpr int kFlagWarped = 1, kFlagHidden = 2;
    constexpr u32 kRespawnFrames = 60;  // after a failed spawn

    enum class Look { Unknown, Ghost, Hidden };
    struct Puppet {
        void* actor = nullptr;
        void* ctrl = nullptr;    // a weak reference to the actor
        u8* model = nullptr;     // once loaded
        Look look = Look::Unknown;
        bool shown = false;
        u32 spawnAfter = 0;      // frame
        Steer steer;
        u32 framesSeen = 0;      // the ghost's line count fed so far
        std::vector<std::string> animLog;  // its plays, for DBG puppet animlog
    };
    Puppet g_Puppets[kPuppets];
    // Each shown puppet's blocks as GPU addresses [lo, hi), for the render thread's hooks; hi 0: none.
    struct Span { std::atomic<u64> lo{0}, hi{0}; };
    Span g_Spans[kPuppets];

    struct Sample { Game::Vec3 pos; Game::Vec4 rot; };
    // DBG puppet: Link's path g_Delay frames late, fed g_SteerHz times a second (0: written as it is)
    constexpr int kHistory = 1024;
    int g_Delay = -2;  // -1: stand at g_At; -2: not driven
    int g_SteerHz = 0;
    Game::Vec3 g_At{};
    Sample g_History[kHistory];
    u32 g_HistoryFrame = 0, g_LastFrame = 0;
    bool g_HistoryValid = false;
    std::string g_AnimPending;  // from DBG puppet anim, played on the game thread

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    Game::Actors::Player* Player() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->mPlayer : nullptr;
    }

    std::string Hex(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

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
        std::vector<std::string> log = std::move(p.animLog);
        p = Puppet{};
        p.animLog = std::move(log);
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

    void Hide(Puppet& p) {
        ClearSpan(IndexOf(p));
        if (Alive(p)) SetLook(p, Look::Hidden);
        p.shown = false;
        p.steer = Steer{};
    }

    void Place(Puppet& p, const Game::Vec3& pos, const Game::Vec4* rot);

    // Creates actor <id> at pos as PopEnemy does, with the player as owner, and keeps a weak reference to it; a
    // puppet that has an actor shows it there again.
    bool Spawn(Puppet& p, u16 id, const Game::Vec3& pos, u32 frame, std::string& err) {
        if (Alive(p)) {
            Place(p, pos, nullptr);
            p.shown = true;
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
        auto* system = reinterpret_cast<SharedPtr*>(reinterpret_cast<u8*>(Game::GetFramework()) + kFrameworkActorSystem);
        SharedPtr out{};
        GameFn<CreateFn>(kCreateActor)(&out, system, params);

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
        p.animLog.clear();
        if (!Alive(p)) {
            err = "the actor system did not keep the actor";
            Forget(p);
            p.spawnAfter = frame + kRespawnFrames;
            return false;
        }
        p.shown = true;
        return true;
    }

    void PlayAnim(Puppet& p, const char* name, float blend) {
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
    }

    float YawOf(const Game::Vec4& q) { return 2 * std::atan2(q.y, q.t); }
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

    // One frame of steering toward the puppet's target; picks the animation from the speed.
    void SteerFrame(Puppet& p) {
        Steer& st = p.steer;
        st.age += kFrame;
        Game::Vec3 aim{st.target.x + st.targetVel.x * st.age, st.target.y + st.targetVel.y * st.age,
                       st.target.z + st.targetVel.z * st.age};
        float ex = aim.x - st.pos.x, ey = aim.y - st.pos.y, ez = aim.z - st.pos.z;
        if (ex * ex + ey * ey + ez * ez > kSnap * kSnap) {
            st.pos = aim;
            ex = ey = ez = 0;
        }
        float vx = st.targetVel.x + ex * kGain, vy = st.targetVel.y + ey * kGain, vz = st.targetVel.z + ez * kGain;
        float sp = std::sqrt(vx * vx + vz * vz);
        if (sp > kMaxSpeed) {
            vx *= kMaxSpeed / sp;
            vz *= kMaxSpeed / sp;
            sp = kMaxSpeed;
        }
        st.pos.x += vx * kFrame;
        st.pos.y += vy * kFrame;
        st.pos.z += vz * kFrame;
        st.moved[st.frames++ % 60] = sp * kFrame;
        st.speed = st.speed * 0.8f + sp * 0.2f;
        float want = st.speed > 0.5f ? std::atan2(vx, vz) : st.targetYaw;
        float d = std::remainder(want - st.yaw, 2 * static_cast<float>(M_PI));
        float step = kTurnRate * kFrame;
        st.yaw += std::clamp(d, -step, step);
        Game::Vec4 q = QuatOf(st.yaw);
        Place(p, st.pos, &q);
        // From the model's load on: the NPC plays its own idle as it loads.
        int gait = st.gait < 0 ? GaitAt(st.speed) : std::clamp(st.gait, GaitAt(st.speed - kGaitMargin),
                                                                 GaitAt(st.speed + kGaitMargin));
        if (gait != st.gait && p.model != nullptr) {
            PlayAnim(p, kGaits[gait], 6);
            st.gait = gait;
        }
    }

    // A new target; snap: jump there (a warp, or shown again) instead of steering.
    void Feed(Puppet& p, const Game::Vec3& pos, float yaw, u32 frame, bool snap) {
        Steer& st = p.steer;
        float jump = std::hypot(pos.x - st.target.x, pos.z - st.target.z);
        if (!st.valid || snap || jump > kSnap) {
            int gait = st.gait;
            bool keepPos = st.valid && !snap;  // a jump: steer there from where it stands (snaps if far)
            Game::Vec3 at = st.pos;
            float facing = st.yaw;
            st = Steer{};
            st.valid = true;
            st.pos = keepPos ? at : pos;
            st.yaw = keepPos ? facing : yaw;
            st.target = pos;
            st.targetYaw = yaw;
            st.lastFeed = frame;
            st.gait = gait;
            st.feeds[0] = {frame, pos};
            st.count = 1;
            return;
        }
        st.feeds[st.count % kFeeds] = {frame, pos};
        st.count++;
        // the oldest target within the window, at least kMinVelocityFrames back
        const Feeding* from = nullptr;
        for (u32 k = 1; k < std::min<u32>(st.count, kFeeds); k++) {
            const Feeding& f = st.feeds[(st.count - 1 - k) % kFeeds];
            if (frame - f.frame > kVelocityFrames) break;
            if (frame - f.frame >= g_MinVelocityFrames) from = &f;
            if (from != nullptr && g_MinVelocityFrames <= 1) break;
        }
        if (from != nullptr) {
            float dt = (frame - from->frame) * kFrame;
            st.targetVel = Game::Vec3{(pos.x - from->pos.x) / dt, (pos.y - from->pos.y) / dt, (pos.z - from->pos.z) / dt};
        } else if (frame - st.lastFeed > kVelocityFrames) {
            st.targetVel = Game::Vec3{};  // after a pause
        }
        st.target = pos;
        st.targetYaw = yaw;
        st.age = 0;
        st.lastFeed = frame;
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
        p.look = Look::Unknown;
        p.steer.gait = -1;
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
    // last line is recent; fed at each new line, steered every frame. A puppet not shown stays hidden for reuse.
    void DriveGhost(int n, const std::string& level, u32 frame, u64 now) {
        Puppet& p = g_Puppets[n];
        const AP::GhostNet::Ghost& g = AP::GhostNet::Get(n);
        bool shown = AP::IsClientConnected() && g.active && !(g.flags & kFlagHidden) && g.level == level &&
                     now - g.receivedMs < kStaleMs;
        if (!shown) {
            if (p.shown) Hide(p);
            return;
        }
        Game::Vec3 pos{g.x, g.y, g.z};
        bool snap = !p.shown || (g.flags & kFlagWarped) != 0;
        if (!p.shown) {
            std::string err;
            if (frame < p.spawnAfter || !Spawn(p, kGhostActor, pos, frame, err)) return;
        }
        if (g.frames != p.framesSeen || snap) {
            Feed(p, pos, g.yaw * static_cast<float>(M_PI) / 180, frame, snap);
            p.framesSeen = g.frames;
        }
        SteerFrame(p);
    }

    // DBG puppet: on Link's path g_Delay frames late, exactly or steered; or standing at g_At.
    void DriveTest(u32 frame) {
        Puppet& p = g_Puppets[kTest];
        if (p.ctrl == nullptr) return;
        if (!g_AnimPending.empty()) {
            PlayAnim(p, g_AnimPending.c_str(), 6);
            g_AnimPending.clear();
        }
        if (g_Delay >= 0 && g_HistoryValid && static_cast<u32>(g_Delay) < g_HistoryFrame) {
            const Sample& s = g_History[(g_HistoryFrame - 1 - g_Delay) % kHistory];
            if (g_SteerHz <= 0) {
                Place(p, s.pos, &s.rot);
            } else {
                u32 every = std::max(1, 60 / g_SteerHz);
                if (!p.steer.valid || frame - p.steer.lastFeed >= every) Feed(p, s.pos, YawOf(s.rot), frame, false);
                SteerFrame(p);
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
                        " dithered=" + std::to_string(g_GhostDraws);
        for (auto& p : g_Puppets) {
            if (!Alive(p)) continue;
            int n = IndexOf(p);
            s += " | " + (n == kTest ? std::string("test") : std::to_string(n)) + " actor=" +
                 Hex(reinterpret_cast<uintptr_t>(p.actor)) + (p.shown ? "" : " hidden");
            if (auto* root = At<Game::RootComp*>(p.actor, kActorRoot)) s += " pos=" + PosText(root->coordsNew.pos);
            s += " model=" + (p.model != nullptr ? Hex(reinterpret_cast<uintptr_t>(p.model)) : "-");
            s += " anim=" + std::string(p.steer.gait >= 0 ? kGaits[p.steer.gait] : "-");
            if (p.steer.frames >= 60) {
                // speed over the last second: mean and standard deviation, units per second
                float sum = 0, sq = 0;
                for (float m : p.steer.moved) {
                    sum += m / kFrame;
                    sq += (m / kFrame) * (m / kFrame);
                }
                char buf[48];
                snprintf(buf, sizeof(buf), " speed=%.2f~%.2f", sum / 60, std::sqrt(std::max(0.0f, sq / 60 - (sum / 60) * (sum / 60))));
                s += buf;
            }
        }
        return s;
    }
}

// The plays by name on the puppets' animation components (DBG puppet animlog).
HOOK_DEFINE_TRAMPOLINE(PlayByName) {
    static void Callback(void* comp, NameView* name, u8 mode, float blend, float frame) {
        if (mode == 0 && comp != nullptr) {
            for (auto& p : g_Puppets) {
                if (comp == AnimComp(p) && p.animLog.size() < 40) p.animLog.emplace_back(name->ptr, name->len);
            }
        }
        Orig(comp, name, mode, blend, frame);
    }
};

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
        PlayByName::InstallAtOffset(offset_manager.Offset(kPlayByName[0]));
        BindUniformBuffer::InstallAtOffset(offset_manager.Offset(kBindUniformBuffer));
        SetRasterState::InstallAtOffset(offset_manager.Offset(kSetRasterState));
        DrawElementsBaseVertex::InstallAtOffset(offset_manager.Offset(kDrawElementsBaseVertex));
        DrawElementsInstanced::InstallAtOffset(offset_manager.Offset(kDrawElementsInstanced));
    }

    // Once per input poll; samples Link and moves the puppets once per frame.
    void Tick() {
        auto* player = Player();
        Game::Framework* fmwk = Game::GetFramework();
        if (player == nullptr || fmwk == nullptr) {
            // a level loads
            for (int n = 0; n < kPuppets; n++) ClearSpan(n);
            g_HistoryValid = false;
            return;
        }
        u32 frame = fmwk->mFrameCount;
        if (frame == g_LastFrame) return;
        g_LastFrame = frame;
        for (auto& p : g_Puppets) {
            if (p.ctrl != nullptr && !Alive(p)) Forget(p);
            if (p.ctrl != nullptr) RefreshModel(p);
        }
        if (player->playerCollision != nullptr) {
            auto& c = player->playerCollision->coordsNew;
            g_History[g_HistoryFrame % kHistory] = Sample{c.pos, c.rotate};
            g_HistoryFrame++;
            g_HistoryValid = true;
        }
        std::string level = LevelSystem::GetCurrentLevel();
        u64 now = NowMs();
        for (int n = 0; n < kGhosts; n++) DriveGhost(n, level, frame, now);
        DriveTest(frame);
        for (auto& p : g_Puppets) {
            if (Alive(p)) Update(p, frame);
        }
    }

    int Shown() {
        int n = 0;
        for (int i = 0; i < kGhosts; i++) n += g_Spans[i].hi.load() != 0;
        return n;
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
            // DBG puppet steer <hz>: with follow, the path fed this many times a second and steered; 0: exact
            g_SteerHz = atoi(a[3].c_str());
            p.steer.valid = false;
        } else if (sub == "window" && a.size() >= 4) {
            // DBG puppet window <frames>: the least span of targets the steering takes a velocity over (1: the
            // last two)
            g_MinVelocityFrames = std::clamp(atoi(a[3].c_str()), 1, static_cast<int>(kVelocityFrames));
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
            // DBG puppet drop: hides DBG puppet's actor (spawn shows it again)
            Hide(p);
            g_Delay = -2;
        }
        AP::Send(Info());
        return true;
    }
}
#endif
