// The ghosts' equipment and faces, instanced (DESIGN.md, Ghosts): the shapes bound to one bone each draw once for all
// ghosts.
#include "game/framework.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"
#include "ghost/ghost_puppets_impl.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace AP::Puppets {
    Inst g_Inst[kInstGroupCount];

namespace {
    using Game::At;
    using Game::SharedPtr;
    using Game::StrView;
    using Game::GetPlayer;

    // Equipment and faces, instanced (DESIGN.md, Ghosts): the shapes bound to one bone each draw once for all ghosts.
    // Per group of them one InstancedModel component (the game's for lawn grass and floor tiles) on Link, with one
    // instance per drawn ghost whose look has the group's shapes written each frame; the puppets draw none of them.
    // Made as FieldTip makes its own (0xb846e0): the component (0x310 bytes, InstancedModel_Ctor with the model
    // resource {resource, control block, model} as a model component holds it at +0x168), a control block of its own
    // (InstancedModelCtrl_vtable) as its shared-from-this, the entity manager's closures (virtual +0x78) and its model
    // instance M (virtual +0x20, in the model list at once), the entity manager's start task
    // (EntityManager_PostStart) and the actor's child map (Actor_AttachComponent). +0x2f9 set and +0x2fa clear keep the game from adding instances. M's instances: a
    // vector of 0x30-byte records at M +0x398 (begin, end, capacity: {position, rotation quaternion, scale}), turned
    // into a storage buffer at each draw (0xfe4020, up to 512). M is drawn in its bind pose with an identity world,
    // so a shape bound to one bone shows at a ghost's bone with the ghost's bone world times the inverse of M's.
    // g3d: model object +0x38 the skeleton, its +0x20 the bones' world matrices (0x40 each: X, Y, Z axes, translation).
    constexpr size_t kInstSize = 0x310, kInstFlags = 0x40, kInstNoPush = 0x2f9, kInstVisible = 0x2fa, kInstM = 0x148,
                     kInstSelf = 0x8, kInstSelfCtrl = 0x10, kAnimSmc = 0x168, kSmcResource = 0x168, kMRecords = 0x398,
                     kModelSkeleton = 0x38, kSkeletonWorld = 0x20;
    constexpr u32 kInstMode = 0x4c41;  // cache keys (+ group) no game component uses: models of their own
    constexpr int kInstCapacity = 32;
    struct Record { float pos[4], rot[4], scale[3], pad; };
    // The shapes bound to one bone each (vertex skin count 1), as the look shows them together (a group's model draws
    // all its shapes for each instance): sword A, sword B, the Magic Rod, the shovel, the hookshot (bone 12, itemA_L),
    // shield A, the mirror shield, the ocarina (bone 24, itemA_R), the face's ear, eyes and mouth (bone 28, head).
    struct InstGroup { u32 shapes; int bone; };
    constexpr InstGroup kInstGroups[] = {{0x30000, 12}, {0xc0000, 12}, {0x1, 12}, {0x4, 12}, {0x1000, 12},
                                         {0x2000, 24}, {0xc000, 24}, {0x2, 24}, {0x380, kHeadBone}};
    constexpr u32 InstShapes() {
        u32 shapes = 0;
        for (const InstGroup& g : kInstGroups) shapes |= g.shapes;
        return shapes;
    }
    static_assert(InstShapes() == kInstancedShapes && std::size(kInstGroups) == kInstGroupCount);

    int g_InstMade = 0;
    char g_InstName[16];

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

    bool InstMake(int g, void* host, u8* smc) {
        Game::Framework* fmwk = Game::GetFramework();
        u8* comp = static_cast<u8*>(Game::operator_new(kInstSize));
        SharedPtr owner{};
        Game::SharedPtr_FromWeak(&owner, At<void*>(host, 8), At<void*>(host, 0x10));
        snprintf(g_InstName, sizeof(g_InstName), "APGhostInst%d", g_InstMade++);
        Game::ComponentName name{g_InstName, strlen(g_InstName), 1, owner.ptr, owner.ctrl};
        Game::InstancedModel_Ctor(comp, &name, smc + kSmcResource, kInstMode + g, kInstGroups[g].shapes,
                                  fmwk->models);
        Game::ReleaseOwner(owner.ctrl);
        void** ctrl = static_cast<void**>(Game::operator_new(0x20));
        ctrl[0] = const_cast<u8*>(Game::InstancedModelCtrl_vtable.Ptr());
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
        if (!(flags & 0x40000008)) {
            Game::CallVirtual<void()>(comp, 0x78);
            Game::CallVirtual<void()>(comp, 0x20);
            flags |= 8;
        }
        SharedPtr sp{comp, ctrl};
        Game::AddOwner(ctrl);
        Game::EntityManager_PostStart(*Game::EntityManager_ptr, &sp);
        Game::ReleaseOwner(ctrl);
        bool attached = Game::Actor_AttachComponent(host, comp, At<void*>(host, kActorRoot));
        u8* m = attached ? At<u8*>(comp, kInstM) : nullptr;
        if (m != nullptr) {
            // room for the records before the render thread first draws them
            u8** vec = reinterpret_cast<u8**>(m + kMRecords);
            u8* old = vec[0];
            vec[0] = static_cast<u8*>(Game::operator_new(kInstCapacity * sizeof(Record)));
            vec[1] = vec[0];
            vec[2] = vec[0] + kInstCapacity * sizeof(Record);
            if (old != nullptr) Game::operator_delete(old);
            g_Inst[g] = Inst{comp, host, 0};
        }
        Game::ReleaseOwner(ctrl);  // the module's reference: the host's child map holds its own
        return m != nullptr;
    }
}

    const float* BoneWorld(const u8* modelObject, int bone) {
        const u8* skeleton = At<const u8*>(modelObject, kModelSkeleton);
        const float* world = At<const float*>(skeleton, kSkeletonWorld);
        return world != nullptr ? world + bone * 16 : nullptr;
    }

    void InstDropAll(bool hostAlive) {
        for (int g = 0; g < kInstGroupCount; g++) InstDrop(g, hostAlive);
    }

    // Each frame: each group's component on Link (made once a ghost's model gives the resource), with one record per
    // drawn ghost whose look has the group's shapes.
    void InstTick() {
        auto* host = GetPlayer();
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
}
