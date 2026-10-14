// Test build only (LASAP_TEST): a ghost puppet for multiplayer ghosts. Spawns an NPC actor at runtime through the
// game's actor factory (as a chest pops its enemy), draws it with Link's model (its Npc row, set up for the test)
// dithered by alpha-to-coverage, and moves it by hand every frame: on Link's own path some frames late, optionally
// fed at a network-like rate and steered, playing Link's animations by its speed. DBG puppet ...
#ifdef LASAP_TEST
#include "AP/ap.hpp"
#include "AP/ap_debug.hpp"
#include "Game/framework.hpp"
#include "Hooks/Common/level.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include <algorithm>
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
    constexpr size_t kActorRoom = 0xb0;
    constexpr size_t kFrameworkActorSystem = 0x498;  // shared_ptr
    constexpr size_t kActorRoot = 0x80;    // the root component (coordinates), as PopEnemy reads it
    using CreateFn = void (*)(SharedPtr*, SharedPtr*, u8*);
    using DtorFn = void (*)(u8*);
    using ReleaseWeakFn = void (*)(void*);

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    void ReleaseShared(void* ctrl) {
        if (ctrl == nullptr) return;
        auto* owners = reinterpret_cast<long*>(reinterpret_cast<u8*>(ctrl) + 8);
        if (__atomic_fetch_sub(owners, 1, __ATOMIC_ACQ_REL) == 0) {
            auto** vtable = *reinterpret_cast<void (***)(void*)>(ctrl);
            vtable[2](ctrl);  // __on_zero_shared
            GameFn<ReleaseWeakFn>(kReleaseWeak)(ctrl);
        }
    }

    // The puppet's model (an NpcGhost whose Npc row draws Link's model "link") loads after the spawn. Each of its
    // materials' setup calls the shader option setter (0xff3dd0) with its option selector, whose +0x70 is the model
    // object (+0 the FMDL); the first model named "link" set up after a spawn is the puppet's. Model object: +0x10
    // u32* material visibility bits, +0x30 u16 shape count, +0x32 u16 material count, +0x40 shapes (0x70 apart; +0x18
    // pool offset of the shape's block), +0x48 materials (0x80 apart), +0x68 the memory pool object (+8 its CPU
    // address, +0x50 its GPU address). Material: +0 FMAT, +0x20 u32 block copies, +0x38 pool offset of its shader
    // parameter block, +0x60 block size; the copies are the size rounded up to 0x100 apart.
    constexpr uintptr_t kSetShaderOption = 0xff3dd0;  // void(selector, int option, u32 value)
    constexpr int kSetupOption = 9;                     // dither_mode, set once per material at setup
    constexpr size_t kSelectorModel = 0x70;
    constexpr size_t kModelMatVis = 0x10, kModelShapeCount = 0x30, kModelMatCount = 0x32, kModelShapes = 0x40,
                     kModelMats = 0x48, kModelPool = 0x68;
    constexpr size_t kShapeStride = 0x70, kShapeBlock = 0x18;
    constexpr size_t kMatStride = 0x80, kMatCopies = 0x20, kMatBlock = 0x38, kMatBlockSize = 0x60;
    constexpr size_t kPoolGpu = 0x50;
    // Link's materials drawn for a ghost: the body and the first sword and shield. The others are the items (rod,
    // ocarina, shovel, hookshot, flippers, second sword and shields) the player shows only in use.
    const char* const kShownMaterials[] = {"MI_body", "MI_boots", "MI_ear", "MI_eye", "MI_mouth", "MI_hair",
                                           "linkShieldA_MI_shieldA", "linkSwordA_MI_sowrdA", "linkSwordA_MI_sowrdAball"};
    bool g_ModelArmed = false;  // a puppet was spawned and its model is not set up yet
    u8* g_PuppetModel = nullptr;
    u8* g_HiddenModel = nullptr;  // the model the item materials were hidden on

    struct Sample { Game::Vec3 pos; Game::Vec4 rot; };

    // Steering (DBG puppet steer <hz>): the puppet is fed a target (Link's path g_Delay frames late) g_SteerHz times
    // a second, as a network would, and moves toward it each frame: dead reckoning along the target's velocity plus
    // a correction toward it, capped; it snaps across jumps (warps, respawns). It turns toward where it goes, or the
    // target's facing when still. 0: positions written as they are.
    int g_SteerHz = 0;
    constexpr float kFrame = 1.0f / 60, kSnap = 4, kMaxSpeed = 12, kGain = 4, kTurnRate = 12;  // per second
    struct Steer {
        bool valid = false;
        Game::Vec3 pos{}, target{}, targetVel{};
        float yaw = 0, targetYaw = 0, speed = 0, age = 0;
        u32 lastFeed = 0;
        const char* anim = nullptr;
    } g_Steer;

    // Animations by name: the NPC's animation component (actor +0x2b0) has a virtual play (0x949260, overridden by
    // 0x9df590): (component, {name, length}*, mode, blend frames, frame). Mode 0 looks the name up in its animation
    // set (+0x1e8, cache +0x218) and hands it to its animation player (+0xe8) at frame 0 and rate 0 (the player's
    // virtual +0xc0 frame, +0xd8 rate); mode 2 sets the rate 1. The slot is found in the component's vtable at its
    // first use.
    struct NameView { const char* ptr; u64 len; };
    using PlayByNameFn = void (*)(void* comp, NameView* name, u8 mode, float blend, float frame);
    constexpr uintptr_t kPlayByName[] = {0x949260, 0x9df590}, kActorAnimComp = 0x2b0;
    int g_PlaySlot = -1;  // -1: not looked up; -2: not in the component's vtable
    std::string g_AnimPending;  // from DBG puppet anim, played on the game thread
    std::vector<std::string> g_AnimLog;  // the puppet's plays, for DBG puppet animlog

    constexpr int kHistory = 1024;

    SharedPtr g_Puppet{};
    void* g_PuppetPlayer = nullptr;  // the player the puppet was made beside; a new one means a new level
    int g_Delay = -2;                // follow Link this many frames late; -1: stand at g_At; -2: not driven
    Game::Vec3 g_At{};
    Sample g_History[kHistory];
    u32 g_HistoryFrame = 0, g_LastFrame = 0;
    bool g_HistoryValid = false;
    u32 g_Drives = 0;

    Game::Actors::Player* Player() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->mPlayer : nullptr;
    }

    std::string Hex(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

    void Drop() {
        ReleaseShared(g_Puppet.ctrl);
        g_Puppet = SharedPtr{};
        g_PuppetPlayer = nullptr;
        g_ModelArmed = false;
        g_PuppetModel = nullptr;
        g_HiddenModel = nullptr;
        g_Steer.valid = false;
        g_PlaySlot = -1;
        g_Delay = -2;
    }

    // Creates actor <id> at pos as PopEnemy does, with the player as owner.
    bool Spawn(u16 id, const Game::Vec3& pos, std::string& err) {
        auto* player = Player();
        if (player == nullptr) {
            err = "no player";
            return false;
        }
        u8* owner = reinterpret_cast<u8*>(player);
        alignas(16) u8 params[kParamsSize] = {};
        *reinterpret_cast<u16*>(params + kParamId) = id;
        *reinterpret_cast<u32*>(params + kParamRoom) = *reinterpret_cast<u32*>(owner + kActorRoom);
        float* p = reinterpret_cast<float*>(params + kParamPos);
        p[0] = pos.x;
        p[1] = pos.y;
        p[2] = pos.z;
        p[3] = 0;
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

        if (out.ptr == nullptr) {
            err = "factory returned nothing";
            return false;
        }
        Drop();
        g_Puppet = out;
        g_PuppetPlayer = player;
        g_ModelArmed = true;
        g_AnimLog.clear();
        return true;
    }

    Game::RootComp* PuppetRoot() {
        if (g_Puppet.ptr == nullptr) return nullptr;
        return *reinterpret_cast<Game::RootComp**>(reinterpret_cast<u8*>(g_Puppet.ptr) + kActorRoot);
    }

    void* PuppetAnimComp() {
        if (g_Puppet.ptr == nullptr) return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<u8*>(g_Puppet.ptr) + kActorAnimComp);
    }

    void PlayAnim(const char* name, float blend) {
        void* comp = PuppetAnimComp();
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

    void Place(const Game::Vec3& pos, const Game::Vec4* rot) {
        Game::RootComp* root = PuppetRoot();
        if (root == nullptr) return;
        root->coordsNew.pos = pos;
        root->coords.pos = pos;
        if (rot != nullptr) {
            root->coordsNew.rotate = *rot;
            root->coords.rotate = *rot;
        }
        root->needsCoordUpdate = true;
        g_Drives++;
    }

    // One frame of steering toward g_Steer's target; picks the animation from the speed.
    void SteerFrame() {
        Steer& st = g_Steer;
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
        st.speed = st.speed * 0.8f + sp * 0.2f;
        float want = st.speed > 0.5f ? std::atan2(vx, vz) : st.targetYaw;
        float d = std::remainder(want - st.yaw, 2 * static_cast<float>(M_PI));
        float step = kTurnRate * kFrame;
        st.yaw += std::clamp(d, -step, step);
        Game::Vec4 q = QuatOf(st.yaw);
        Place(st.pos, &q);
        const char* anim = st.speed < 0.4f ? "wait" : st.speed < 3.5f ? "walk" : st.speed < 7.5f ? "run" : "dash_lp";
        if (anim != st.anim) {
            PlayAnim(anim, 6);
            st.anim = anim;
        }
    }

    void Feed(const Sample& s, u32 frame) {
        Steer& st = g_Steer;
        if (!st.valid) {
            st = Steer{};
            st.valid = true;
            st.pos = st.target = s.pos;
            st.yaw = st.targetYaw = YawOf(s.rot);
            st.lastFeed = frame;
            return;
        }
        float dt = (frame - st.lastFeed) * kFrame;
        if (dt > 0) {
            st.targetVel = Game::Vec3{(s.pos.x - st.target.x) / dt, (s.pos.y - st.target.y) / dt, (s.pos.z - st.target.z) / dt};
            float jump = std::sqrt(st.targetVel.x * st.targetVel.x + st.targetVel.z * st.targetVel.z) * dt;
            if (jump > kSnap) st.targetVel = Game::Vec3{};
        }
        st.target = s.pos;
        st.targetYaw = YawOf(s.rot);
        st.age = 0;
        st.lastFeed = frame;
    }

    u8* PuppetModel() { return g_Puppet.ptr != nullptr ? g_PuppetModel : nullptr; }

    // A bfres string: u16 length, then the characters.
    std::string_view ResName(const u8* res) {
        const u8* s = *reinterpret_cast<const u8* const*>(res + 8);
        return std::string_view(reinterpret_cast<const char*>(s + 2), *reinterpret_cast<const u16*>(s));
    }

    bool IsLinkModel(const u8* model) {
        const u8* res = model != nullptr ? *reinterpret_cast<const u8* const*>(model) : nullptr;
        return res != nullptr && std::memcmp(res, "FMDL", 4) == 0 && ResName(res) == "link";
    }

    // Clears the visibility bits of the materials a ghost does not show.
    void HideItems(u8* model) {
        u16 count = *reinterpret_cast<u16*>(model + kModelMatCount);
        u8* mats = *reinterpret_cast<u8**>(model + kModelMats);
        u32* visible = *reinterpret_cast<u32**>(model + kModelMatVis);
        for (u16 i = 0; i < count; i++) {
            std::string_view name = ResName(*reinterpret_cast<u8**>(mats + i * kMatStride));
            bool shown = std::any_of(std::begin(kShownMaterials), std::end(kShownMaterials),
                                     [&](const char* m) { return name == m; });
            if (shown) visible[i / 32] |= 1u << (i % 32);
            else visible[i / 32] &= ~(1u << (i % 32));
        }
    }

    // Screen-space dither on the puppet only: the model file's shown albedos carry a constant alpha (render pass 1),
    // and its draws get an NVN multisample state with alpha-to-coverage. The renderer binds a material's parameter
    // block through the command buffer wrapper's bind-uniform-buffer (0x113fd70: wrapper, stage, slot, {GPU address,
    // offset}*, size); an address inside the puppet's blocks marks its draw. The context's cached raster-state setter
    // (0xff9430: context, state; cache at +0xa0, which other code reads, the wrapper at +8) would bind the material's
    // own state over it, so it binds ours again until the draw (0x113e950 / 0x113e9d0); after the draw the cached
    // state is bound again (0x113ea70: wrapper, state), as the context believes it is.
    constexpr uintptr_t kBindUniformBuffer = 0x113fd70, kSetRasterState = 0xff9430;
    constexpr uintptr_t kDrawElementsBaseVertex = 0x113e950, kDrawElementsInstanced = 0x113e9d0;
    constexpr uintptr_t kBindRasterState = 0x113ea70;
    constexpr size_t kWrapperCmdBuf = 0xb8, kContextWrapper = 8, kContextRasterCache = 0xa0;
    // GOT slots of the game's NVN function pointers (each holds the address of a global with the pointer)
    constexpr uintptr_t kGotMsSetDefaults = 0x1c951e8, kGotMsSetA2C = 0x1c95200, kGotBindMs = 0x1c947c0;
    alignas(16) u8 g_GhostMs[64];
    bool g_GhostMsReady = false;
    // The renderer records several command buffers (one wrapper each, a context each): the wrapper whose next
    // draw is the puppet's, and each wrapper's context.
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

    // The puppet's blocks: its materials' parameter blocks and its shapes' blocks, one span in the model's pool.
    bool IsPuppetBlock(u64 address) {
        u8* model = PuppetModel();
        if (model == nullptr) return false;
        u64 offset = address - *reinterpret_cast<u64*>(*reinterpret_cast<u8**>(model + kModelPool) + kPoolGpu);
        u64 lo = ~0ull, hi = 0;
        u16 count = *reinterpret_cast<u16*>(model + kModelMatCount);
        u8* mats = *reinterpret_cast<u8**>(model + kModelMats);
        for (u16 i = 0; i < count; i++) {
            u8* mat = mats + i * kMatStride;
            u64 at = *reinterpret_cast<u64*>(mat + kMatBlock);
            u32 copies = std::min(*reinterpret_cast<u32*>(mat + kMatCopies), 4u);
            u64 size = (*reinterpret_cast<u64*>(mat + kMatBlockSize) + 0xff) & ~0xffull;
            lo = std::min(lo, at);
            hi = std::max(hi, at + copies * size);
        }
        u16 shapes = *reinterpret_cast<u16*>(model + kModelShapeCount);
        u8* shape = *reinterpret_cast<u8**>(model + kModelShapes);
        for (u16 i = 0; i < shapes; i++) {
            u64 at = *reinterpret_cast<u64*>(shape + i * kShapeStride + kShapeBlock);
            lo = std::min(lo, at);
            hi = std::max(hi, at + 0x1000);
        }
        return offset >= lo && offset < hi;
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

    // After the puppet's draw on this wrapper: bind the raster state its context has cached.
    void EndGhostDraw(void* wrapper) {
        g_GhostDraws++;
        g_GhostPending = nullptr;
        u8* context = ContextOf(wrapper);
        if (context == nullptr) return;
        void* state = *reinterpret_cast<void**>(context + kContextRasterCache);
        if (state != nullptr) GameFn<void (*)(void*, void*)>(kBindRasterState)(wrapper, state);
    }

    std::string PosText(const Game::Vec3& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f", p.x, p.y, p.z);
        return buf;
    }

    std::string Info() {
        std::string s = "PUPPET actor=" + Hex(reinterpret_cast<uintptr_t>(g_Puppet.ptr));
        if (g_Puppet.ptr != nullptr) {
            u8* a = reinterpret_cast<u8*>(g_Puppet.ptr);
            s += " name=" + std::string(reinterpret_cast<const char*>(a + 0x18), strnlen(reinterpret_cast<const char*>(a + 0x18), 0x20));
            if (PuppetRoot() != nullptr) s += " pos=" + PosText(PuppetRoot()->coordsNew.pos);
            s += " model=" + std::string(PuppetModel() != nullptr ? "link" : "-");
        }
        s += " delay=" + std::to_string(g_Delay) + " steer=" + std::to_string(g_SteerHz) +
             " anim=" + (g_Steer.anim != nullptr ? g_Steer.anim : "-") + " drives=" + std::to_string(g_Drives) +
             " dithered=" + std::to_string(g_GhostDraws);
        return s;
    }
}

// The plays by name on the puppet's animation component (DBG puppet animlog).
HOOK_DEFINE_TRAMPOLINE(PlayByName) {
    static void Callback(void* comp, NameView* name, u8 mode, float blend, float frame) {
        if (mode == 0 && comp != nullptr && comp == PuppetAnimComp() && g_AnimLog.size() < 40)
            g_AnimLog.emplace_back(name->ptr, name->len);
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

HOOK_DEFINE_TRAMPOLINE(SetShaderOption) {
    static void Callback(u8* selector, int option, u32 value) {
        if (option == kSetupOption && g_ModelArmed) {
            u8* model = *reinterpret_cast<u8**>(selector + kSelectorModel);
            if (IsLinkModel(model)) {
                g_PuppetModel = model;
                g_ModelArmed = false;
            }
        }
        Orig(selector, option, value);
    }
};

namespace AP::Puppet {
    void InstallHooks() {
        SetShaderOption::InstallAtOffset(offset_manager.Offset(kSetShaderOption));
        PlayByName::InstallAtOffset(offset_manager.Offset(kPlayByName[0]));
        BindUniformBuffer::InstallAtOffset(offset_manager.Offset(kBindUniformBuffer));
        SetRasterState::InstallAtOffset(offset_manager.Offset(kSetRasterState));
        DrawElementsBaseVertex::InstallAtOffset(offset_manager.Offset(kDrawElementsBaseVertex));
        DrawElementsInstanced::InstallAtOffset(offset_manager.Offset(kDrawElementsInstanced));
    }

    // Once per input poll; samples Link and moves the puppet once per frame.
    void Tick() {
        auto* player = Player();
        Game::Framework* fmwk = Game::GetFramework();
        if (player == nullptr || fmwk == nullptr) {
            g_HistoryValid = false;
            return;
        }
        if (g_Puppet.ptr != nullptr && g_PuppetPlayer != player) Drop();  // the level was left
        u32 frame = fmwk->mFrameCount;
        if (frame == g_LastFrame) return;
        g_LastFrame = frame;
        if (player->playerCollision != nullptr) {
            auto& c = player->playerCollision->coordsNew;
            g_History[g_HistoryFrame % kHistory] = Sample{c.pos, c.rotate};
            g_HistoryFrame++;
            g_HistoryValid = true;
        }
        if (g_Puppet.ptr == nullptr) return;
        if (u8* model = PuppetModel(); model != nullptr && model != g_HiddenModel) {
            HideItems(model);
            g_HiddenModel = model;
        }
        if (!g_AnimPending.empty()) {
            PlayAnim(g_AnimPending.c_str(), 6);
            g_AnimPending.clear();
        }
        if (g_Delay >= 0 && g_HistoryValid && static_cast<u32>(g_Delay) < g_HistoryFrame) {
            const Sample& s = g_History[(g_HistoryFrame - 1 - g_Delay) % kHistory];
            if (g_SteerHz <= 0) {
                Place(s.pos, &s.rot);
            } else {
                u32 every = std::max(1, 60 / g_SteerHz);
                if (!g_Steer.valid || frame - g_Steer.lastFeed >= every) Feed(s, frame);
                SteerFrame();
            }
        } else if (g_Delay == -1) {
            Place(g_At, nullptr);
        }
    }

    bool Command(const std::vector<std::string>& a) {
        if (a[1] != "puppet") return false;
        const std::string sub = a.size() >= 3 ? a[2] : "";
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
            if (!Spawn(static_cast<u16>(strtoul(a[3].c_str(), nullptr, 16)), pos, err)) {
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
            g_Steer.valid = false;
        } else if (sub == "anim" && a.size() >= 4) {
            // DBG puppet anim <name>: plays one of the model's animations (on the game thread)
            g_AnimPending = a[3];
        } else if (sub == "animlog") {
            // DBG puppet animlog: the animations played on the puppet since the spawn or the last animlog
            std::string line = "ANIMLOG";
            for (auto& l : g_AnimLog) line += " " + l;
            g_AnimLog.clear();
            AP::Send(line);
            return true;
        } else if (sub == "drop") {
            Drop();
        }
        AP::Send(Info());
        return true;
    }
}
#endif
