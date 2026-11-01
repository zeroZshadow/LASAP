// DBG probe (test build): runtime probes for the open questions of scripts/re.
#ifdef LASAP_TEST
#include "game/actors/item.hpp"
#include "game/actors/npc.hpp"
#include "game/actors/player.hpp"
#include "game/event.hpp"
#include "game/framework.hpp"
#include "game/model.hpp"
#include "ap/ap_net.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include "test/test_impl.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace AP::Test {
    int g_ProbeNoRow = 0;

namespace {
    using Game::Actors::Player;

    std::string Hx(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

    // ---- poison: Link's model's fields without a known access hold non-canonical values ----
    constexpr u64 kPoison = 0xdead000000000000ull;
    struct Poisoned { u64* at; u64 value; u64 was; const char* what; };
    constexpr int kMaxPoisoned = 256;
    Poisoned g_Poisoned[kMaxPoisoned];
    int g_PoisonedCount = 0;
    bool g_PoisonOn = false;
    u32 g_PoisonMask = 0x1f;  // 1 inst18, 2 obj20, 4 obj28, 8 mat78, 0x10 holder0
    u32 g_PoisonNonZero = 0;  // fields that were not 0 before
    std::string g_PoisonWas;
    Game::ModelInstance* g_PoisonedInstance = nullptr;
    const void* g_PoisonedHolders = nullptr;  // the first shader holder when poisoned: holders are made again per level
    u32 g_PoisonRounds = 0, g_PoisonChecks = 0, g_PoisonBroken = 0;
    std::string g_PoisonFirstBreak;

    Game::ModelInstance* LinkInstance() {
        Player* player = Game::GetPlayer();
        if (player == nullptr || player->modelComp == nullptr) return nullptr;
        return player->modelComp->instance;
    }

    void Poison(u64* at, u64 value, const char* what, u32 bit) {
        if (!(g_PoisonMask & bit)) return;
        if (g_PoisonedCount >= kMaxPoisoned || !Readable(reinterpret_cast<uintptr_t>(at), 8)) return;
        u64 was = *at;
        if (was != 0 && g_PoisonNonZero++ < 8) g_PoisonWas += std::string(" ") + what + "=" + Hx(was);
        *at = value;
        g_Poisoned[g_PoisonedCount++] = {at, value, was, what};
    }

    const void* FirstHolder(Game::ModelInstance* inst) {
        Game::ModelData* data = inst->data;
        return data != nullptr && !data->shapeShaders.empty() ? data->shapeShaders[0].shaders[0] : nullptr;
    }

    void PoisonLink(Game::ModelInstance* inst) {
        g_PoisonedCount = 0;
        g_PoisonedHolders = FirstHolder(inst);
        g_PoisonedInstance = inst;
        g_PoisonRounds++;
        auto* base = reinterpret_cast<u8*>(inst);
        Poison(reinterpret_cast<u64*>(base + 0x18), kPoison | 0x18, "inst18", 1);
        Game::ModelObj* obj = inst->Model();
        if (obj == nullptr) return;
        Poison(reinterpret_cast<u64*>(&obj->unused_20), kPoison | 0x20, "obj20", 2);
        Poison(reinterpret_cast<u64*>(&obj->unused_28), kPoison | 0x28, "obj28", 4);
        for (u16 i = 0; i < obj->materialCount && obj->materials != nullptr; i++) {
            Poison(reinterpret_cast<u64*>(reinterpret_cast<u8*>(&obj->materials[i]) + 0x78), kPoison | 0x780000 | i,
                   "mat78", 8);
        }
        Game::ModelData* data = inst->data;
        if (data == nullptr) return;
        for (size_t s = 0; s < data->shapeShaders.size(); s++) {
            for (int k = 0; k < 3; k++) {
                Game::ShaderHolder* h = data->shapeShaders[s].shaders[k];
                if (h != nullptr) Poison(reinterpret_cast<u64*>(h), kPoison | 0x5000000 | (s << 4) | k, "holder0", 0x10);
            }
        }
    }

    void SamplePoison() {
        if (!g_PoisonOn) return;
        Game::ModelInstance* inst = LinkInstance();
        if (inst == nullptr) return;
        if (inst != g_PoisonedInstance || ((g_PoisonMask & 0x10) && FirstHolder(inst) != g_PoisonedHolders)) {
            PoisonLink(inst);
            return;
        }
        g_PoisonChecks++;
        for (int i = 0; i < g_PoisonedCount; i++) {
            Poisoned& p = g_Poisoned[i];
            if (*p.at == p.value) continue;
            if (g_PoisonBroken++ == 0) {
                g_PoisonFirstBreak = std::string(p.what) + "@" + Hx(reinterpret_cast<uintptr_t>(p.at)) + "=" +
                                     Hx(*p.at) + " frame=" + std::to_string(g_FrameNo);
            }
            Trace("probe poison " + std::string(p.what) + " " + Hx(p.value) + ">" + Hx(*p.at));
            p.value = *p.at;  // report each change once
        }
    }

    // ---- force: Link's PlayerCollision force and frameForce at each Move and at each frame's start ----
    struct ForceLog {
        u32 moves = 0, forceMoves = 0, frameMoves = 0, forceFrames = 0, frameFrames = 0;
        u64 forceStates = 0, frameStates = 0;  // a bit per Link state seen with a non-zero value at a Move
        float lastForce[3] = {}, lastFrame[3] = {};
        int lastForceState = -1, lastFrameState = -1;
        u64 regsHit = 0;  // which argument register held the PlayerCollision
        u32 velMoves = 0;  // Link's Moves entered with velocity non-zero
        float lastVel[3] = {};
        u32 otherMoves = 0, otherForce = 0, otherVel = 0;  // Moves of other PlayerCollisions (vtable 0x1c501f0)
        float lastPos[3] = {};  // Link's position at the last non-zero force/frameForce/velocity
    };
    ForceLog g_Force;
    bool g_ForceOn = false;

    bool NonZero(const Game::Vec4& v) { return v.x != 0 || v.y != 0 || v.z != 0; }

    // ---- dirty: Link's root written by hand (coordsNew + needsCoordUpdate); calls until his next Move ----
    struct DirtyLog {
        bool armed = false;
        Game::Node* root = nullptr;
        u32 armFrame = 0, moveFrame = 0;
        u32 marks = 0, marksEmit = 0;  // Node_MarkDirty on the root; those entered with needsCoordUpdate clear
        u32 cct = 0;                   // CharCtrlComp_OnTransformChanged calls (any controller)
        float armPos[3] = {}, wantPos[3] = {}, movePos[3] = {}, afterPos[3] = {};
        int after = -1;
        u32 rounds = 0;
    } g_Dirty;

    void CopyPos(float* out, const Game::Vec3& p) { out[0] = p.x, out[1] = p.y, out[2] = p.z; }

    HOOK_DEFINE_INLINE(PlayerCollisionMove) {
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            if (g_Dirty.armed) {
                Player* p = Game::GetPlayer();
                if (p != nullptr && p->playerCollision != nullptr &&
                    ctx->X[0] == reinterpret_cast<u64>(p->playerCollision)) {
                    g_Dirty.armed = false;
                    g_Dirty.moveFrame = g_FrameNo;
                    CopyPos(g_Dirty.movePos, p->playerCollision->base.node.coordsNew.pos);
                    g_Dirty.after = 2;
                }
            }
            if (!g_ForceOn) return;
            Player* player = Game::GetPlayer();
            if (player == nullptr || player->playerCollision == nullptr) return;
            auto* pc = player->playerCollision;
            bool ours = false;
            for (int r = 0; r < 4; r++) {
                if (ctx->X[r] == reinterpret_cast<u64>(pc)) {
                    g_Force.regsHit |= 1ull << r;
                    ours = true;
                }
            }
            if (!ours) {
                auto* other = reinterpret_cast<Game::Actors::PlayerCollision*>(ctx->X[0]);
                if (other != nullptr && *reinterpret_cast<uintptr_t*>(other) == Game::Addr(0x1c501f0)) {
                    g_Force.otherMoves++;
                    if (NonZero(other->force) || NonZero(other->frameForce)) g_Force.otherForce++;
                    if (NonZero(other->velocity)) g_Force.otherVel++;
                }
                return;
            }
            g_Force.moves++;
            int state = player->machine.state;
            bool any = NonZero(pc->force) || NonZero(pc->frameForce) || NonZero(pc->velocity);
            if (NonZero(pc->velocity)) {
                g_Force.velMoves++;
                g_Force.lastVel[0] = pc->velocity.x, g_Force.lastVel[1] = pc->velocity.y, g_Force.lastVel[2] = pc->velocity.z;
            }
            if (any) {
                const Game::Vec3& p = pc->base.node.coordsNew.pos;
                g_Force.lastPos[0] = p.x, g_Force.lastPos[1] = p.y, g_Force.lastPos[2] = p.z;
            }
            if (NonZero(pc->force)) {
                g_Force.forceMoves++;
                g_Force.forceStates |= 1ull << (state & 63);
                g_Force.lastForce[0] = pc->force.x, g_Force.lastForce[1] = pc->force.y, g_Force.lastForce[2] = pc->force.z;
                g_Force.lastForceState = state;
            }
            if (NonZero(pc->frameForce)) {
                g_Force.frameMoves++;
                g_Force.frameStates |= 1ull << (state & 63);
                g_Force.lastFrame[0] = pc->frameForce.x, g_Force.lastFrame[1] = pc->frameForce.y;
                g_Force.lastFrame[2] = pc->frameForce.z;
                g_Force.lastFrameState = state;
            }
        }
    };

    void SampleForce() {
        if (!g_ForceOn) return;
        Player* player = Game::GetPlayer();
        if (player == nullptr || player->playerCollision == nullptr) return;
        if (NonZero(player->playerCollision->force)) g_Force.forceFrames++;
        if (NonZero(player->playerCollision->frameForce)) g_Force.frameFrames++;
    }

    std::string ForceState() {
        char buf[600];
        snprintf(buf, sizeof(buf),
                 " moves=%u regs=%lx force: moves=%u frames=%u states=%lx last=%.3f,%.3f,%.3f@%d"
                 " frameForce: moves=%u frames=%u states=%lx last=%.3f,%.3f,%.3f@%d"
                 " vel: moves=%u last=%.3f,%.3f,%.3f at=%.2f,%.2f,%.2f other: moves=%u force=%u vel=%u",
                 g_Force.moves, g_Force.regsHit, g_Force.forceMoves, g_Force.forceFrames, g_Force.forceStates,
                 g_Force.lastForce[0], g_Force.lastForce[1], g_Force.lastForce[2], g_Force.lastForceState,
                 g_Force.frameMoves, g_Force.frameFrames, g_Force.frameStates, g_Force.lastFrame[0],
                 g_Force.lastFrame[1], g_Force.lastFrame[2], g_Force.lastFrameState, g_Force.velMoves,
                 g_Force.lastVel[0], g_Force.lastVel[1], g_Force.lastVel[2], g_Force.lastPos[0], g_Force.lastPos[1],
                 g_Force.lastPos[2], g_Force.otherMoves, g_Force.otherForce, g_Force.otherVel);
        return buf;
    }

    // ---- shapes: PhysicsShape +0x48 marked after the constructor, +0x38 and +0x48 checked at the destructor ----
    constexpr u64 kShapeMark = 0x5ead480000000000ull;
    u64 ShapeMark(const void* shape) { return kShapeMark | ((reinterpret_cast<uintptr_t>(shape) >> 4) & 0xffffffffffull); }
    constexpr int kShapeSlots = 1 << 15;
    uintptr_t g_ShapeSet[kShapeSlots];  // open addressing; 1 = deleted
    int g_ShapeLock = 0;
    bool g_ShapeOn = false;
    struct ShapeLogEntry { uintptr_t shape; u64 v48; uintptr_t p38, c40, vt38; int geom; };
    constexpr int kShapeLog = 8;
    ShapeLogEntry g_ShapeLog[kShapeLog];
    struct ShapeCounts {
        u32 made = 0, marked = 0, full = 0, freed = 0, freedMarked = 0, freed38 = 0, freedChanged = 0, freedUnmarked38 = 0;
        u32 logged = 0;
        u32 geom[8] = {};  // freed marked shapes per geometry type
    } g_Shapes;

    void ShapeLock() { while (__atomic_exchange_n(&g_ShapeLock, 1, __ATOMIC_ACQUIRE)) {} }
    void ShapeUnlock() { __atomic_store_n(&g_ShapeLock, 0, __ATOMIC_RELEASE); }
    u32 ShapeHash(uintptr_t a) { return static_cast<u32>((a >> 4) * 0x9e3779b1u) & (kShapeSlots - 1); }

    bool ShapeInsert(uintptr_t a) {
        u32 h = ShapeHash(a);
        for (int n = 0; n < kShapeSlots; n++, h = (h + 1) & (kShapeSlots - 1)) {
            if (g_ShapeSet[h] == 0 || g_ShapeSet[h] == 1 || g_ShapeSet[h] == a) {
                g_ShapeSet[h] = a;
                return true;
            }
        }
        return false;
    }

    bool ShapeErase(uintptr_t a) {
        u32 h = ShapeHash(a);
        for (int n = 0; n < kShapeSlots; n++, h = (h + 1) & (kShapeSlots - 1)) {
            if (g_ShapeSet[h] == 0) return false;
            if (g_ShapeSet[h] == a) {
                g_ShapeSet[h] = 1;
                return true;
            }
        }
        return false;
    }

    int ShapeGeom(const u8* shape) {
        void* px = *reinterpret_cast<void* const*>(shape + 0x18);
        if (px == nullptr) return -1;
        using GetType = int (*)(void*);
        GetType fn = reinterpret_cast<GetType>((*reinterpret_cast<uintptr_t**>(px))[0x38 / 8]);
        return fn(px) & 0xff;  // PxShape::getGeometryType: 0 sphere, 1 plane, 2 capsule, 3 box, 4 convex, 5 mesh, 6 field
    }

    HOOK_DEFINE_TRAMPOLINE(ShapeCtor) {
        static void Callback(u8* shape, void* pxShape) {
            Orig(shape, pxShape);
            if (!g_ShapeOn) return;
            ShapeLock();
            g_Shapes.made++;
            if (ShapeInsert(reinterpret_cast<uintptr_t>(shape))) {
                *reinterpret_cast<u64*>(shape + 0x48) = ShapeMark(shape);
                g_Shapes.marked++;
            } else {
                g_Shapes.full++;
            }
            ShapeUnlock();
        }
    };

    void ShapeNote(const u8* shape, int geom) {
        if (g_Shapes.logged >= kShapeLog) return;
        uintptr_t p38 = *reinterpret_cast<const uintptr_t*>(shape + 0x38);
        uintptr_t vt = p38 != 0 && Readable(p38, 8) ? *reinterpret_cast<const uintptr_t*>(p38) - Game::Base() : 0;
        g_ShapeLog[g_Shapes.logged++] = {reinterpret_cast<uintptr_t>(shape), *reinterpret_cast<const u64*>(shape + 0x48),
                                         p38, *reinterpret_cast<const uintptr_t*>(shape + 0x40), vt, geom};
    }

    HOOK_DEFINE_TRAMPOLINE(ShapeDtor) {
        static void Callback(u8* shape) {
            if (g_ShapeOn) {
                ShapeLock();
                bool marked = ShapeErase(reinterpret_cast<uintptr_t>(shape));
                bool has38 = *reinterpret_cast<u64*>(shape + 0x38) != 0 || *reinterpret_cast<u64*>(shape + 0x40) != 0;
                g_Shapes.freed++;
                if (marked) {
                    g_Shapes.freedMarked++;
                    int geom = ShapeGeom(shape);
                    if (geom >= 0 && geom < 8) g_Shapes.geom[geom]++;
                    bool changed = *reinterpret_cast<u64*>(shape + 0x48) != ShapeMark(shape);
                    if (has38) g_Shapes.freed38++;
                    if (changed) g_Shapes.freedChanged++;
                    if (has38 || changed) ShapeNote(shape, geom);
                } else if (has38) {
                    g_Shapes.freedUnmarked38++;
                    ShapeNote(shape, -2);
                }
                ShapeUnlock();
            }
            Orig(shape);
        }
    };

    std::string ShapeState() {
        ShapeLock();
        u32 live = 0, live38 = 0, liveChanged = 0;
        std::string notes;
        for (int i = 0; i < kShapeSlots; i++) {
            uintptr_t a = g_ShapeSet[i];
            if (a <= 1) continue;
            live++;
            auto* s = reinterpret_cast<const u8*>(a);
            bool has38 = *reinterpret_cast<const u64*>(s + 0x38) != 0 || *reinterpret_cast<const u64*>(s + 0x40) != 0;
            bool changed = *reinterpret_cast<const u64*>(s + 0x48) != ShapeMark(s);
            if (has38) live38++;
            if (changed) liveChanged++;
            if ((has38 || changed) && notes.size() < 400) {
                notes += " live:" + Hx(a) + "/48=" + Hx(*reinterpret_cast<const u64*>(s + 0x48)) + "/38=" +
                         Hx(*reinterpret_cast<const u64*>(s + 0x38));
            }
        }
        char buf[400];
        snprintf(buf, sizeof(buf),
                 " made=%u marked=%u full=%u freed=%u freedMarked=%u freed38=%u freedChanged=%u freedUnmarked38=%u"
                 " live=%u live38=%u liveChanged=%u geom=%u,%u,%u,%u,%u,%u,%u",
                 g_Shapes.made, g_Shapes.marked, g_Shapes.full, g_Shapes.freed, g_Shapes.freedMarked, g_Shapes.freed38,
                 g_Shapes.freedChanged, g_Shapes.freedUnmarked38, live, live38, liveChanged, g_Shapes.geom[0],
                 g_Shapes.geom[1], g_Shapes.geom[2], g_Shapes.geom[3], g_Shapes.geom[4], g_Shapes.geom[5],
                 g_Shapes.geom[6]);
        std::string s = buf;
        for (u32 i = 0; i < g_Shapes.logged; i++) {
            const ShapeLogEntry& e = g_ShapeLog[i];
            s += " freed:" + Hx(e.shape) + "/48=" + Hx(e.v48) + "/38=" + Hx(e.p38) + "/40=" + Hx(e.c40) + "/vt+" +
                 Hx(e.vt38) + "/g" + std::to_string(e.geom);
        }
        ShapeUnlock();
        return s + notes;
    }

    // ---- world: the physics worlds' +0xf8..+0x1c7 filled with 0xa5, checked each frame with +0x1c8 ----
    constexpr int kWorldLo = 0xf8, kWorldHi = 0x1d8;  // poisoned up to 0x1c8; 0x1c8..0x1d7 watched
    constexpr int kWorlds = 4;
    u8 g_WorldCopy[kWorlds][kWorldHi - kWorldLo];
    u8* g_WorldAt[kWorlds];
    bool g_WorldOn = false;
    u32 g_WorldChecks = 0, g_WorldChanges = 0;
    std::string g_WorldLog;

    u8* WorldPtr(int i) {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->physics.ptr == nullptr) return nullptr;
        auto* slot = reinterpret_cast<Game::SharedPtr<>*>(static_cast<u8*>(fmwk->physics.ptr) + 0x38 + 0x10 * i);
        return static_cast<u8*>(slot->ptr);
    }

    std::string WorldLine(int i) {
        u8* w = WorldPtr(i);
        if (w == nullptr) return " w" + std::to_string(i) + "=-";
        int nonzero = 0, first = -1;
        for (int o = kWorldLo; o < 0x1c8; o++) {
            if (w[o] != 0 && w[o] != 0xa5) {
                nonzero++;
                if (first < 0) first = o;
            }
        }
        int pattern = 0;
        for (int o = kWorldLo; o < 0x1c8; o++) pattern += w[o] == 0xa5;
        return " w" + std::to_string(i) + "=" + Hx(reinterpret_cast<uintptr_t>(w)) + "/vt+" +
               Hx(*reinterpret_cast<uintptr_t*>(w) - Game::Base()) + "/id" + std::to_string(*reinterpret_cast<u32*>(w + 0x18)) +
               "/nz" + std::to_string(nonzero) + (first >= 0 ? "@" + Hx(first) : "") + "/a5=" + std::to_string(pattern) +
               "/1c8=" + Hx(*reinterpret_cast<u64*>(w + 0x1c8)) + "," + Hx(*reinterpret_cast<u64*>(w + 0x1d0));
    }

    void SampleWorld() {
        if (!g_WorldOn) return;
        g_WorldChecks++;
        for (int i = 0; i < kWorlds; i++) {
            u8* w = g_WorldAt[i];
            if (w == nullptr) continue;
            if (WorldPtr(i) != w) {
                if (g_WorldLog.size() < 600) g_WorldLog += " w" + std::to_string(i) + "-moved@" + std::to_string(g_FrameNo);
                g_WorldAt[i] = nullptr;
                continue;
            }
            for (int o = kWorldLo; o < kWorldHi; o++) {
                u8 now = w[o];
                u8& was = g_WorldCopy[i][o - kWorldLo];
                if (now == was) continue;
                g_WorldChanges++;
                char buf[64];
                snprintf(buf, sizeof(buf), " w%d+%x:%02x>%02x@%u", i, o, was, now, g_FrameNo);
                if (g_WorldLog.size() < 600) g_WorldLog += buf;
                Trace(std::string("probe world") + buf);
                was = now;
            }
        }
    }

    void PoisonWorlds() {
        for (int i = 0; i < kWorlds; i++) {
            u8* w = WorldPtr(i);
            g_WorldAt[i] = w;
            if (w == nullptr) continue;
            memset(w + kWorldLo, 0xa5, 0x1c8 - kWorldLo);
            memcpy(g_WorldCopy[i], w + kWorldLo, kWorldHi - kWorldLo);
        }
        g_WorldOn = true;
    }

    // ---- hit: the attack byte NpcComponent_OnHit switches on ----
    constexpr int kHitLog = 16;
    std::string g_HitLog[kHitLog];
    int g_HitNext = 0;

    HOOK_DEFINE_TRAMPOLINE(NpcOnHit) {
        static bool Callback(Game::Actors::NpcComponent* comp, u8* attacker) {
            bool raised = Orig(comp, attacker);
            u8* attack = attacker != nullptr ? *reinterpret_cast<u8**>(attacker + 0x40) : nullptr;
            const char* name = comp != nullptr && comp->owner != nullptr
                                   ? Game::ActorTable.Ptr()[comp->owner->actorId].name : "-";
            char buf[96];
            snprintf(buf, sizeof(buf), "%s:%d->%d", name, attack != nullptr ? attack[0xc] : -1, raised ? 1 : 0);
            g_HitLog[g_HitNext++ % kHitLog] = buf;
            return raised;
        }
    };

    // ---- pool: a MemoryPool's gpu against nvnMemoryPoolGetBufferAddress ----
    // The main module's pointer to the proc it fetched by name (GOT 0x1c94cc0, written at 0x11411b0 with the name
    // nvnMemoryPoolGetBufferAddress)
    using GetBufferAddress = u64 (*)(const void*);

    std::string PoolLine(const char* what, const Game::MemoryPool* pool) {
        if (pool == nullptr) return std::string(" ") + what + "=-";
        auto slot = *reinterpret_cast<GetBufferAddress**>(Game::Addr(0x1c94cc0));
        GetBufferAddress fn = slot != nullptr ? *slot : nullptr;
        u64 got = fn != nullptr ? fn(pool->nvnPool) : 0;
        std::string s = std::string(" ") + what + "@" + Hx(reinterpret_cast<uintptr_t>(pool)) +
                        " nvn=" + Hx(reinterpret_cast<uintptr_t>(pool->nvnPool)) + " gpu50=" + Hx(pool->gpu) +
                        " proc=" + Hx(got) + (got == pool->gpu ? " equal" : " differ") + " fn=+" +
                        Hx(reinterpret_cast<uintptr_t>(fn) - Game::Base());
        // the proc's own bytes: is it nvnStorage+0x40 it reads?
        return s;
    }

    std::string PoolState() {
        Game::ModelInstance* inst = LinkInstance();
        Game::ModelObj* obj = inst != nullptr ? inst->Model() : nullptr;
        if (obj == nullptr) return " no model";
        std::string s = PoolLine("model", obj->pool);
        if (obj->materialCount > 0) s += PoolLine("mat0", obj->materials[0].pool);
        if (obj->skeleton != nullptr) s += PoolLine("skel", obj->skeleton->pool);
        if (obj->shapeCount > 0) s += PoolLine("shape0", obj->shapes[0].pool);
        return s;
    }

    // ---- bodies: Link's and an item's physics body, for DBG watch ----
    std::string BodyOf(const char* what, const void* body) {
        if (body == nullptr) return std::string(" ") + what + "=-";
        uintptr_t vt = *reinterpret_cast<const uintptr_t*>(body);
        return std::string(" ") + what + "=" + Hx(reinterpret_cast<uintptr_t>(body)) + "/vt+" + Hx(vt - Game::Base());
    }

    std::string Fl(float v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%.4g", v);
        return buf;
    }

    float YawDeg(const Game::Vec4& q) {
        return __builtin_atan2f(2 * (q.t * q.y + q.x * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)) * 57.29578f;
    }

    // ---- cam: the first event camera's +0x360.. against the renderer camera and its game camera ----
    struct CamHold { int off = -1; float value = 0; };
    CamHold g_CamHold[2];

    u8* EventCam() {
        auto* mgr = static_cast<Game::Event::EventManager*>(*Game::EventManager_ptr);
        return mgr != nullptr ? static_cast<u8*>(mgr->cameraA.ptr) : nullptr;
    }

    std::string CamState() {
        std::string s;
        u8* cam = EventCam();
        if (cam != nullptr) {
            s += " ev=" + Hx(reinterpret_cast<uintptr_t>(cam)) + " flags=" + Hx(*reinterpret_cast<u32*>(cam + 0x40)) +
                 " ev360=";
            for (int o = 0x360; o < 0x378; o += 4) s += Fl(*reinterpret_cast<float*>(cam + o)) + ",";
        } else {
            s += " ev=-";
        }
        const Game::Camera* rc = Game::GetCamera();
        if (rc != nullptr) {
            s += " near=" + Fl(rc->nearZ) + " far=" + Fl(rc->farZ) + " aspect=" + Fl(rc->aspect) + " fovY=" + Fl(rc->fovY) +
                 " wh=" + std::to_string(rc->width) + "x" + std::to_string(rc->height) + " proj00,11,22,32=" +
                 Fl(rc->projection[0]) + "," + Fl(rc->projection[5]) + "," + Fl(rc->projection[10]) + "," +
                 Fl(rc->projection[14]);
            auto* src = static_cast<u8*>(rc->source.ptr);
            s += " srcptr=" + Hx(reinterpret_cast<uintptr_t>(src));
            if (src != nullptr && Readable(reinterpret_cast<uintptr_t>(src), 0x110)) {
                s += " src=" + Hx(reinterpret_cast<uintptr_t>(src)) + "/vt+" +
                     Hx(*reinterpret_cast<uintptr_t*>(src) - Game::Base()) + " srcF0=";
                for (int o = 0xf0; o < 0x110; o += 4) s += Fl(*reinterpret_cast<float*>(src + o)) + ",";
            }
        }
        for (const CamHold& h : g_CamHold) {
            if (h.off >= 0) s += " hold+" + Hx(h.off) + "=" + Fl(h.value);
        }
        return s;
    }

    void SampleCam() {
        u8* cam = nullptr;
        for (const CamHold& h : g_CamHold) {
            if (h.off < 0) continue;
            if (cam == nullptr) cam = EventCam();
            if (cam != nullptr) *reinterpret_cast<float*>(cam + h.off) = h.value;
        }
    }

    // ---- filt / touch: PxFilterData of Link's, his sensors' and an item's shapes; Item_SetTouchable ----
    struct FilterData { u32 w[4]; };
    using GetFilter = FilterData (*)(void*);
    constexpr uintptr_t kPhysicsBodyVt = 0x1c50e48, kDynamicBodyVt = 0x1c50f88, kCctHolderVt = 0x1c4f928;

    std::string ShapeFilters(const std::string& what, const void* bodyPtr) {
        auto* body = static_cast<const Game::Actors::PhysicsBody*>(bodyPtr);
        if (body == nullptr || !Readable(reinterpret_cast<uintptr_t>(body), 0x440)) return " " + what + "=-";
        std::string s = " " + what + "=" + Hx(reinterpret_cast<uintptr_t>(body)) + "/vt+" +
                        Hx(reinterpret_cast<uintptr_t>(body->vtable) - Game::Base()) + "/n" +
                        std::to_string(body->shapeCount);
        for (int i = 0; i < body->shapeCount && i < 8; i++) {
            auto* shape = static_cast<const u8*>(body->shapes[i].ptr);
            if (shape == nullptr || !Readable(reinterpret_cast<uintptr_t>(shape), 0x50)) continue;
            void* px = *reinterpret_cast<void* const*>(shape + 0x18);
            if (px == nullptr) continue;
            auto* vt = *reinterpret_cast<uintptr_t**>(px);
            FilterData sim = reinterpret_cast<GetFilter>(vt[0xa8 / 8])(px);
            FilterData q = reinterpret_cast<GetFilter>(vt[0xb8 / 8])(px);
            char buf[160];
            snprintf(buf, sizeof(buf), " [%d]g%d sim=%x,%x,%x,%x layer=%u q=%x,%x,%x,%x", i, ShapeGeom(shape), sim.w[0],
                     sim.w[1], sim.w[2], sim.w[3], sim.w[0] & 0x1f, q.w[0], q.w[1], q.w[2], q.w[3]);
            s += buf;
        }
        return s;
    }

    // Bodies an object holds within its first 0x400 bytes (directly, or through a controller holder's +0x38)
    std::string FindBodies(const std::string& what, const void* obj) {
        if (obj == nullptr || !Readable(reinterpret_cast<uintptr_t>(obj), 0x400)) return " " + what + "=-";
        std::string s = " " + what + "=" + Hx(reinterpret_cast<uintptr_t>(obj)) + "/vt+" +
                        Hx(*static_cast<const uintptr_t*>(obj) - Game::Base());
        int found = 0;
        for (int o = 8; o < 0x400 && found < 3; o += 8) {
            uintptr_t p = *reinterpret_cast<const uintptr_t*>(static_cast<const u8*>(obj) + o);
            if (p == 0 || (p & 7) != 0 || !Readable(p, 8)) continue;
            uintptr_t vt = *reinterpret_cast<uintptr_t*>(p) - Game::Base();
            if (vt == kPhysicsBodyVt || vt == kDynamicBodyVt) {
                s += ShapeFilters(what + "+" + Hx(o), reinterpret_cast<void*>(p));
                found++;
            } else if (vt == kCctHolderVt && Readable(p + 0x38, 8)) {
                s += ShapeFilters(what + "+" + Hx(o) + "+38", *reinterpret_cast<void**>(p + 0x38));
                found++;
            }
        }
        return s;
    }

    std::string ItemFilters(Game::Actor* actor) {
        auto* item = reinterpret_cast<Game::Actors::ItemActor*>(actor);
        Game::Actors::ItemComponent* comp = item != nullptr ? item->component : nullptr;
        if (comp == nullptr) return " item=-";
        Game::Actors::BodyCollision* col = comp->npc.collision.get();
        using Word = u32 (*)(void*);
        u32 word = reinterpret_cast<Word>((*reinterpret_cast<uintptr_t**>(comp))[0x128 / 8])(comp);
        return " item=" + Hx(reinterpret_cast<uintptr_t>(actor)) + " comp=" + Hx(reinterpret_cast<uintptr_t>(comp)) +
               " v128=" + Hx(word) + " touchDelay=" + Fl(item->touchDelay) +
               ShapeFilters("itembody", col != nullptr ? col->body : nullptr);
    }

    // ---- push: Player_PushCheck's results with Link's state ----
    struct PushEntry { u32 frame, count; u8 result, state; };
    constexpr int kPushLog = 40;
    PushEntry g_PushLog[kPushLog];
    int g_PushNext = 0;
    u32 g_PushCalls = 0, g_PushTrue = 0;

    HOOK_DEFINE_TRAMPOLINE(PushCheck) {
        static bool Callback(float speed, u8* player, void* move, void* out1, void* out2) {
            bool r = Orig(speed, player, move, out1, out2);
            g_PushCalls++;
            if (r) g_PushTrue++;
            u8 state = player != nullptr ? player[0x4ed] : 0xff;
            PushEntry* last = g_PushNext > 0 ? &g_PushLog[(g_PushNext - 1) % kPushLog] : nullptr;
            if (last != nullptr && last->result == r && last->state == state) {
                last->count++;
            } else {
                g_PushLog[g_PushNext++ % kPushLog] = {g_FrameNo, 1, static_cast<u8>(r), state};
            }
            return r;
        }
    };

    // ---- light: the View block the light-culling pass binds, and the LightIndexCounts storage buffer ----
    std::atomic<uintptr_t> g_LightView{0};
    std::atomic<u32> g_LightViewHits{0};
    std::atomic<u64> g_CountsSize{0}, g_ListSize{0};
    std::atomic<u32> g_CountsHits{0}, g_ListHits{0}, g_CountsSlot{0};

    HOOK_DEFINE_INLINE(LightViewBind) {  // at 0x1083354: x23 = [x19 + 0xc0], the View block's buffer object
        static void Callback(exl::hook::nx64::InlineFloatCtx* ctx) {
            g_LightView.store(ctx->X[23], std::memory_order_relaxed);
            g_LightViewHits.fetch_add(1, std::memory_order_relaxed);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(BindStorage) {
        static void Callback(void* cmd, u32 a1, u32 a2, void* addr, u64 size) {
            uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - Game::Base();
            if (lr == 0x108374c) {
                g_CountsSize.store(size, std::memory_order_relaxed);
                g_CountsSlot.store(a1, std::memory_order_relaxed);
                g_CountsHits.fetch_add(1, std::memory_order_relaxed);
            } else if (lr == 0x10836c8) {
                g_ListSize.store(size, std::memory_order_relaxed);
                g_ListHits.fetch_add(1, std::memory_order_relaxed);
            }
            Orig(cmd, a1, a2, addr, size);
        }
    };

    std::string LightState() {
        char buf[200];
        snprintf(buf, sizeof(buf), " viewHits=%u countsHits=%u countsSize=%lu countsSlot=%u listHits=%u listSize=%lu",
                 g_LightViewHits.load(), g_CountsHits.load(), g_CountsSize.load(), g_CountsSlot.load(), g_ListHits.load(),
                 g_ListSize.load());
        std::string s = buf;
        uintptr_t v = g_LightView.load();
        if (v != 0 && Readable(v, 0x48)) {
            uintptr_t vt = *reinterpret_cast<uintptr_t*>(v) - Game::Base();
            s += " view=" + Hx(v) + "/vt+" + Hx(vt);
            using Size = u64 (*)(void*);
            u64 size = reinterpret_cast<Size>((*reinterpret_cast<uintptr_t**>(v))[0x38 / 8])(reinterpret_cast<void*>(v));
            s += " viewSize=" + Hx(size);
            if (vt == 0x1c350a8) {
                auto* block = reinterpret_cast<Game::UniformBlock*>(v);
                s += " words=" + std::to_string(block->wordCount) + " count=" + std::to_string(block->count);
                if (block->words != nullptr && block->wordCount >= 0x74) {
                    s += " w1b0..1cc=";
                    for (int i = 0x6c; i < 0x74; i++) {
                        u32 w = block->words[i];
                        float f;
                        memcpy(&f, &w, 4);
                        s += Hx(w) + "(" + Fl(f) + "),";
                    }
                }
            }
        }
        const Game::Camera* rc = Game::GetCamera();
        if (rc != nullptr) s += " wh=" + std::to_string(rc->width) + "x" + std::to_string(rc->height);
        return s;
    }

    // ---- mt: the moveTarget state's update (0xdd4950) per frame, and PlayerChara_TurnTo calls ----
    struct MtEntry { u32 frame; u8 flags, c0, c1, state; float a0[4]; float yaw; float b8; };
    constexpr int kMtLog = 160;
    MtEntry g_MtLog[kMtLog];
    int g_MtNext = 0;
    std::string g_TurnLog;

    Game::Node* LinkRoot(Player* p) { return p != nullptr ? p->actor.root : nullptr; }
    Game::Shared<> g_MoveWaiter;  // DBG probe moveto's waiter, kept until the next (namespace scope: no local static)

    HOOK_DEFINE_TRAMPOLINE(MoveTargetUpdate) {
        static void Callback(u8* state, u8* player) {
            Orig(state, player);
            auto* p = reinterpret_cast<Player*>(player);
            Game::Node* root = LinkRoot(p);
            MtEntry e{};
            e.frame = g_FrameNo;
            e.flags = player[0x1848];
            e.c0 = state[0xc0];
            e.c1 = state[0xc1];
            e.state = player[0x4ed];
            memcpy(e.a0, state + 0xa0, 16);
            e.yaw = root != nullptr ? YawDeg(root->coordsNew.rotate) : 0;
            memcpy(&e.b8, state + 0xb8, 4);
            g_MtLog[g_MtNext++ % kMtLog] = e;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(TurnTo) {
        static void Callback(u8* chara, float* target, float* rate) {
            Player* p = Game::GetPlayer();
            u8* player = reinterpret_cast<u8*>(p);
            int before = player != nullptr ? player[0x4ed] : -1;
            u8 flagsBefore = player != nullptr ? player[0x1848] : 0;
            Game::Node* root = LinkRoot(p);
            float yaw0 = root != nullptr ? YawDeg(root->coordsNew.rotate) : 0;
            Orig(chara, target, rate);
            float yaw1 = root != nullptr ? YawDeg(root->coordsNew.rotate) : 0;
            char buf[200];
            snprintf(buf, sizeof(buf), " f%u:state=%d target=%.2f,%.2f,%.2f rate=%.3f flags=%x>%x yaw=%.1f>%.1f 1830=%.3f",
                     g_FrameNo, before, target ? target[0] : 0, target ? target[1] : 0, target ? target[2] : 0,
                     rate ? *rate : 0, flagsBefore, player ? player[0x1848] : 0, yaw0, yaw1,
                     player ? *reinterpret_cast<float*>(player + 0x1830) : 0);
            if (g_TurnLog.size() < 1500) g_TurnLog += buf;
        }
    };

    // ---- dirty hooks ----
    HOOK_DEFINE_TRAMPOLINE(CctTransformChanged) {
        static void Callback(void* closure) {
            if (g_Dirty.armed) g_Dirty.cct++;
            Orig(closure);
        }
    };

    void SampleDirty() {
        if (g_Dirty.after < 0) return;
        if (g_Dirty.after-- > 0) return;
        if (g_Dirty.root != nullptr) CopyPos(g_Dirty.afterPos, g_Dirty.root->coordsNew.pos);
    }

    std::string DirtyState() {
        char buf[400];
        snprintf(buf, sizeof(buf),
                 " rounds=%u armed=%d armFrame=%u moveFrame=%u marks=%u emitting=%u cct=%u arm=%.2f,%.2f,%.2f"
                 " want=%.2f,%.2f,%.2f atMove=%.2f,%.2f,%.2f after=%.2f,%.2f,%.2f",
                 g_Dirty.rounds, g_Dirty.armed, g_Dirty.armFrame, g_Dirty.moveFrame, g_Dirty.marks, g_Dirty.marksEmit,
                 g_Dirty.cct, g_Dirty.armPos[0], g_Dirty.armPos[1], g_Dirty.armPos[2], g_Dirty.wantPos[0],
                 g_Dirty.wantPos[1], g_Dirty.wantPos[2], g_Dirty.movePos[0], g_Dirty.movePos[1], g_Dirty.movePos[2],
                 g_Dirty.afterPos[0], g_Dirty.afterPos[1], g_Dirty.afterPos[2]);
        return buf;
    }
}

    // Node_MarkDirty (ap_held_model.cpp's hook): counted on Link's root while DBG probe dirty is armed
    void ProbeMarkDirty(Game::Node* node) {
        if (!g_Dirty.armed || node != g_Dirty.root) return;
        g_Dirty.marks++;
        if (!node->needsCoordUpdate) g_Dirty.marksEmit++;
    }

    void ProbeSample() {
        SamplePoison();
        SampleForce();
        SampleWorld();
        SampleCam();
        SampleDirty();
    }

    void InstallProbeHooks() {
        PlayerCollisionMove::InstallAtPtr(Game::Addr(0x10b8050));
        NpcOnHit::InstallAtPtr(Game::Addr(0x957890));
        ShapeCtor::InstallAtPtr(Game::Addr(0x10c45c0));
        ShapeDtor::InstallAtPtr(Game::Addr(0x10c4970));
        PushCheck::InstallAtPtr(Game::Addr(0xd907f0));
        LightViewBind::InstallAtPtr(Game::Addr(0x1083354));
        BindStorage::InstallAtPtr(Game::Addr(0x113fde0));
        MoveTargetUpdate::InstallAtPtr(Game::Addr(0xdd4950));
        TurnTo::InstallAtPtr(Game::Addr(0xd45870));
        CctTransformChanged::InstallAtPtr(Game::Addr(0x10b6b60));
    }

    // DBG probe norow [n] | poison [on [mask]|off] | force on|off|state|reset | hits | pool | bodies [<actor prefix>]
    bool ProbeCommand(const std::vector<std::string>& a, Game::Actor* (*findActor)(const char*)) {
        if (a.size() < 3 || a[1] != "probe") return false;
        const std::string& what = a[2];
        std::string arg = a.size() >= 4 ? a[3] : "";
        if (what == "norow") {
            if (!arg.empty()) g_ProbeNoRow = atoi(arg.c_str());
            AP::Send("OK probe norow " + std::to_string(g_ProbeNoRow));
        } else if (what == "poison") {
            if (arg == "on") {
                g_PoisonOn = true;
                g_PoisonedInstance = nullptr;
                if (a.size() >= 5) g_PoisonMask = strtoul(a[4].c_str(), nullptr, 16);
            }
            if (arg == "off") g_PoisonOn = false;
            AP::Send("OK probe poison on=" + std::to_string(g_PoisonOn) + " rounds=" + std::to_string(g_PoisonRounds) +
                     " fields=" + std::to_string(g_PoisonedCount) + " checks=" + std::to_string(g_PoisonChecks) +
                     " broken=" + std::to_string(g_PoisonBroken) + " first=" +
                     (g_PoisonFirstBreak.empty() ? "-" : g_PoisonFirstBreak) + " inst=" +
                     Hx(reinterpret_cast<uintptr_t>(g_PoisonedInstance)) + " mask=" + Hx(g_PoisonMask) +
                     " nonzero=" + std::to_string(g_PoisonNonZero) + g_PoisonWas);
        } else if (what == "force") {
            if (arg == "on") g_ForceOn = true;
            if (arg == "off") g_ForceOn = false;
            if (arg == "reset") g_Force = ForceLog{};
            AP::Send("OK probe force on=" + std::to_string(g_ForceOn) + ForceState());
        } else if (what == "shapes") {
            if (arg == "on") g_ShapeOn = true;
            if (arg == "off") g_ShapeOn = false;
            AP::Send("OK probe shapes on=" + std::to_string(g_ShapeOn) + ShapeState());
        } else if (what == "world") {
            if (arg == "poison") PoisonWorlds();
            if (arg == "off") g_WorldOn = false;
            std::string s = "OK probe world on=" + std::to_string(g_WorldOn) + " checks=" + std::to_string(g_WorldChecks) +
                            " changes=" + std::to_string(g_WorldChanges);
            for (int i = 0; i < kWorlds; i++) s += WorldLine(i);
            AP::Send(s + " log:" + g_WorldLog);
        } else if (what == "hits") {
            std::string s = "OK probe hits";
            for (int n = g_HitNext > kHitLog ? g_HitNext - kHitLog : 0; n < g_HitNext; n++) s += " " + g_HitLog[n % kHitLog];
            AP::Send(s);
        } else if (what == "drop" && !arg.empty()) {
            // an item drop as an enemy's (ItemActor_Create) beside Link, launched up
            Player* player = Game::GetPlayer();
            if (player == nullptr || player->playerCollision == nullptr) {
                AP::Send("ERR probe drop");
                return true;
            }
            const Game::Vec3& p = player->playerCollision->base.node.coordsNew.pos;
            Game::Vec4 pos{p.x, p.y + 1, p.z + 1.5f, 0};
            Game::Vec3 vel{0, 5, 0};
            Game::Shared<Game::Actor> out;
            Game::ItemActor_Create(out.Reset(), &player->actor, &pos, &vel, static_cast<Game::Data::ItemId>(atoi(arg.c_str())), -1);
            AP::Send("OK probe drop " + Hx(reinterpret_cast<uintptr_t>(out.get())));
        } else if (what == "cam") {
            // probe cam [w <hex off> <float>] [hold <hex off> <float>] [unhold]
            if ((arg == "w" || arg == "hold") && a.size() >= 6) {
                int off = static_cast<int>(strtoul(a[4].c_str(), nullptr, 16));
                float v = static_cast<float>(atof(a[5].c_str()));
                u8* cam = EventCam();
                if (off >= 0x360 && off < 0x378 && cam != nullptr) {
                    *reinterpret_cast<float*>(cam + off) = v;
                    if (arg == "hold") {
                        int slot = g_CamHold[0].off < 0 || g_CamHold[0].off == off ? 0 : 1;
                        g_CamHold[slot] = {off, v};
                    }
                }
            }
            if (arg == "unhold") g_CamHold[0] = g_CamHold[1] = CamHold{};
            AP::Send("OK probe cam" + CamState());
        } else if (what == "filt") {
            // probe filt [item prefix]: Link's controller body, item and event sensors, an item's
            std::string s = "OK probe filt";
            Player* player = Game::GetPlayer();
            if (player != nullptr && player->playerCollision != nullptr) {
                auto* holder = static_cast<u8*>(player->playerCollision->base.body);
                s += ShapeFilters("link", holder != nullptr ? *reinterpret_cast<void**>(holder + 0x38) : nullptr);
                s += FindBodies("itemSensor", player->itemSensor.ptr);
                s += FindBodies("eventSensor", player->eventSensor.ptr);
            }
            if (!arg.empty()) s += ItemFilters(findActor(arg.c_str()));
            AP::Send(s);
        } else if (what == "touch" && a.size() >= 5) {
            // probe touch <item prefix> <0|1>: Item_SetTouchable on its item component
            Game::Actor* actor = findActor(arg.c_str());
            auto* item = reinterpret_cast<Game::Actors::ItemActor*>(actor);
            if (item == nullptr || item->component == nullptr) {
                AP::Send("ERR probe touch");
                return true;
            }
            using SetTouchable = void (*)(void*, bool);
            reinterpret_cast<SetTouchable>(Game::Addr(0x8c7850))(item->component, atoi(a[4].c_str()) != 0);
            AP::Send("OK probe touch" + ItemFilters(actor));
        } else if (what == "push") {
            if (arg == "reset") g_PushNext = 0, g_PushCalls = 0, g_PushTrue = 0;
            std::string s = "OK probe push calls=" + std::to_string(g_PushCalls) + " true=" + std::to_string(g_PushTrue);
            for (int n = g_PushNext > kPushLog ? g_PushNext - kPushLog : 0; n < g_PushNext; n++) {
                const PushEntry& e = g_PushLog[n % kPushLog];
                s += " f" + std::to_string(e.frame) + ":" + std::to_string(e.result) + "@" + std::to_string(e.state) + "x" +
                     std::to_string(e.count);
            }
            AP::Send(s);
        } else if (what == "light") {
            AP::Send("OK probe light" + LightState());
        } else if (what == "mt") {
            // probe mt [reset | <first entry>]: up to 24 entries from there
            if (arg == "reset") {
                g_MtNext = 0;
                g_TurnLog.clear();
            }
            int first = g_MtNext > kMtLog ? g_MtNext - kMtLog : 0;
            if (!arg.empty() && arg != "reset") first = std::max(first, atoi(arg.c_str()));
            std::string s = "OK probe mt entries=" + std::to_string(g_MtNext) + " turn:" + g_TurnLog + " |";
            for (int n = first; n < g_MtNext && n < first + 24; n++) {
                const MtEntry& e = g_MtLog[n % kMtLog];
                char buf[160];
                snprintf(buf, sizeof(buf), " %d:f%u st%u fl%x c0%u c1%u a0=%.3f,%.3f,%.3f,%.3f yaw=%.1f b8=%.2f", n,
                         e.frame, e.state, e.flags, e.c0, e.c1, e.a0[0], e.a0[1], e.a0[2], e.a0[3], e.yaw, e.b8);
                s += buf;
            }
            AP::Send(s);
        } else if (what == "moveto" && a.size() >= 6) {
            // probe moveto <dx> <dz> <flags hex> [time] [speed]: Player_MoveTo (0xd555b0) with a relative target
            Player* player = Game::GetPlayer();
            if (player == nullptr) {
                AP::Send("ERR probe moveto");
                return true;
            }
            alignas(16) u8 req[0x40] = {};
            auto* target = reinterpret_cast<float*>(req);
            target[0] = static_cast<float>(atof(a[3].c_str()));
            target[2] = static_cast<float>(atof(a[4].c_str()));
            req[0x10] = 1;  // relative
            req[0x11] = 0;  // mode
            float speed = a.size() >= 8 ? static_cast<float>(atof(a[7].c_str())) : 2.25f;
            float timeout = 7.0f, stop = 0.0f;
            float time = a.size() >= 7 ? static_cast<float>(atof(a[6].c_str())) : 0.0f;
            memcpy(req + 0x14, &speed, 4);
            memcpy(req + 0x18, &timeout, 4);
            memcpy(req + 0x1c, &stop, 4);
            memcpy(req + 0x20, &time, 4);
            req[0x38] = static_cast<u8>(strtoul(a[5].c_str(), nullptr, 16));
            using MoveTo = void (*)(Game::SharedPtr<>*, Player*, const u8*);
            Game::Shared<> waiter;
            reinterpret_cast<MoveTo>(Game::Addr(0xd555b0))(waiter.Reset(), player, req);
            g_MoveWaiter = std::move(waiter);
            Game::Node* root = LinkRoot(player);
            AP::Send("OK probe moveto yaw=" + Fl(root != nullptr ? YawDeg(root->coordsNew.rotate) : 0));
        } else if (what == "dirty") {
            // probe dirty [<dx> <dz>]: Link's root coordsNew moved by hand with needsCoordUpdate set
            Player* player = Game::GetPlayer();
            Game::Node* root = LinkRoot(player);
            if (a.size() >= 5 && root != nullptr) {
                u32 rounds = g_Dirty.rounds + 1;
                g_Dirty = DirtyLog{};
                g_Dirty.rounds = rounds;
                g_Dirty.root = root;
                g_Dirty.armFrame = g_FrameNo;
                CopyPos(g_Dirty.armPos, root->coordsNew.pos);
                root->coordsNew.pos.x += static_cast<float>(atof(a[3].c_str()));
                root->coordsNew.pos.z += static_cast<float>(atof(a[4].c_str()));
                root->needsCoordUpdate = true;
                CopyPos(g_Dirty.wantPos, root->coordsNew.pos);
                g_Dirty.armed = true;
            }
            std::string s = "OK probe dirty" + DirtyState();
            if (player != nullptr && player->playerCollision != nullptr) {
                s += std::string(" rootIsCct=") + (root == &player->playerCollision->base.node ? "1" : "0");
            }
            AP::Send(s);
        } else if (what == "pool") {
            AP::Send("OK probe pool" + PoolState());
        } else if (what == "bodies") {
            std::string s = "OK probe bodies";
            Player* player = Game::GetPlayer();
            if (player != nullptr && player->playerCollision != nullptr) {
                auto* holder = static_cast<u8*>(player->playerCollision->base.body);
                s += BodyOf("cct158", holder);
                if (holder != nullptr) s += BodyOf("cct158+38", *reinterpret_cast<void**>(holder + 0x38));
            }
            if (!arg.empty()) {
                Game::Actor* actor = findActor(arg.c_str());
                auto* item = reinterpret_cast<Game::Actors::ItemActor*>(actor);
                Game::Actors::ItemComponent* comp = item != nullptr ? item->component : nullptr;
                Game::Actors::BodyCollision* col = comp != nullptr ? comp->npc.collision.get() : nullptr;
                s += std::string(" ") + arg + "=" + Hx(reinterpret_cast<uintptr_t>(actor));
                s += BodyOf("body", col != nullptr ? col->body : nullptr);
            }
            AP::Send(s);
        } else {
            AP::Send("ERR probe");
        }
        return true;
    }
}
#endif
