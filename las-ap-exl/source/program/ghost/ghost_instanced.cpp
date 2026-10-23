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
    // Made as FieldTip makes its own (0xb846e0): the component (InstancedModel_Ctor with the model resource as a
    // skeletal model component holds it), a control block of its own (InstancedModelCtrl_vtable) as its
    // shared-from-this, the entity manager's closures (kRegister) and its model instance M (kSetup, in the model list
    // at once), the entity manager's start task (EntityManager_PostStart) and the actor's child map
    // (Actor_AttachComponent). Hidden (visible clear), it gets no records from the game. M's instances: its
    // records, turned into a storage buffer at each draw (0xfe4020, up to 512). M is drawn in its bind pose with an
    // identity world, so a shape bound to one bone shows at a ghost's bone with the ghost's bone world times the
    // inverse of M's.
    constexpr u32 kInstMode = 0x4c41;  // cache keys (+ group) no game component uses: models of their own
    constexpr int kInstCapacity = 32;
    using Record = Game::InstanceRecord;
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

    Game::ModelInstance* InstModel(const Inst& inst) { return inst.comp != nullptr ? inst.comp->instance : nullptr; }

    // Stops drawing a group's instances (host alive: the component stays on it, empty) and forgets the component.
    void InstDrop(int g, bool hostAlive) {
        Inst& inst = g_Inst[g];
        if (inst.comp == nullptr) return;
        if (Game::ModelInstance* m = hostAlive ? InstModel(inst) : nullptr) m->records.last = m->records.first;
        ClearSpan(kInstSpan + g);
        inst = Inst{};
    }

    bool InstMake(int g, Game::Actors::Player* host, Game::SkeletalModelComponent* smc) {
        Game::Framework* fmwk = Game::GetFramework();
        auto* comp = static_cast<Game::InstancedModelComponent*>(Game::operator_new(sizeof(Game::InstancedModelComponent)));
        Game::Entity* entity = &comp->node.entity;
        SharedPtr owner{};
        Game::SharedPtr_FromWeak(&owner, host->actor.entity.self.ptr, host->actor.entity.self.ctrl);
        snprintf(g_InstName, sizeof(g_InstName), "APGhostInst%d", g_InstMade++);
        Game::ComponentName name{g_InstName, strlen(g_InstName), 1, owner.ptr, owner.ctrl};
        Game::InstancedModel_Ctor(comp, &name, smc->modelResource, kInstMode + g, kInstGroups[g].shapes, &fmwk->renderer);
        Game::ReleaseOwner(owner.ctrl);
        void** ctrl = static_cast<void**>(Game::operator_new(0x20));
        ctrl[0] = const_cast<u8*>(Game::InstancedModelCtrl_vtable.Ptr());
        ctrl[1] = nullptr;                      // one owner: the module
        ctrl[2] = reinterpret_cast<void*>(1);   // and the component's own weak reference
        ctrl[3] = comp;
        if (entity->self.ctrl == nullptr) entity->self = {comp, ctrl};
        comp->visible = 0;  // the game pushes no records; the module writes them
        comp->added = 1;    // as after a push, until the game's per-frame reset
        if (!(entity->flags & (Game::EntityFlags::kDestroyRequested | Game::EntityFlags::kInitialized))) {
            Game::CallVirtual<void()>(comp, Game::EntityVirtual::kRegister);
            Game::CallVirtual<void()>(comp, Game::EntityVirtual::kSetup);
            entity->flags |= Game::EntityFlags::kInitialized;
        }
        SharedPtr sp{comp, ctrl};
        Game::AddOwner(ctrl);
        Game::EntityManager_PostStart(*Game::EntityManager_ptr, &sp);
        Game::ReleaseOwner(ctrl);
        bool attached = Game::Actor_AttachComponent(&host->actor, comp, host->actor.root);
        Game::ModelInstance* m = attached ? comp->instance : nullptr;
        if (m != nullptr) {
            // room for the records before the render thread first draws them
            Game::Vector<Record>& records = m->records;
            Record* old = records.first;
            records.first = static_cast<Record*>(Game::operator_new(kInstCapacity * sizeof(Record)));
            records.last = records.first;
            records.capacityEnd = records.first + kInstCapacity;
            if (old != nullptr) Game::operator_delete(old);
            g_Inst[g] = Inst{comp, host, 0};
        }
        Game::ReleaseOwner(ctrl);  // the module's reference: the host's child map holds its own
        return m != nullptr;
    }
}

    const float* BoneWorld(const Game::ModelObj* model, int bone) {
        const Game::Skeleton* skeleton = model != nullptr ? model->skeleton : nullptr;
        return skeleton != nullptr && skeleton->world != nullptr ? skeleton->world[bone] : nullptr;
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
        Game::SkeletalModelComponent* smc = nullptr;
        for (int i = 0; i < kGhosts && smc == nullptr; i++)
            if (g_Puppets[i].model != nullptr && Alive(g_Puppets[i]))
                smc = AnimatorModelComp(AnimatorOf(g_Puppets[i]));
        for (int g = 0; g < kInstGroupCount; g++) {
            Inst& inst = g_Inst[g];
            if (inst.comp == nullptr && (smc == nullptr || !InstMake(g, host, smc))) continue;
            Game::ModelInstance* m = InstModel(inst);
            const Game::ModelObj* mObject = m != nullptr ? m->Model() : nullptr;
            const float* mBone = mObject != nullptr ? BoneWorld(mObject, kInstGroups[g].bone) : nullptr;
            // M's first calculation comes with its first draw: until then its bone is not set
            if (mBone == nullptr || std::fabs(mBone[0]) + std::fabs(mBone[1]) + std::fabs(mBone[2]) < 1e-6f) continue;
            PublishSpan(kInstSpan + g, mObject);
            Game::Vector<Record>& records = m->records;
            if (records.capacityEnd - records.first < kInstCapacity) continue;
            Record* out = records.first;
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
            records.last = records.first + n;
            inst.drawn = n;
        }
    }
}
