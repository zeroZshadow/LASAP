// The ghosts' puppets (DESIGN.md, Ghosts, Puppet): one per ghost slot of AP::GhostNet, and in the test build one for
// DBG puppet. This file has the actors and the frame; the replay, the look, the dither, the instanced equipment and
// the crowds have ghost_*.cpp files of their own.
#include "ghost/ghost_puppets_impl.hpp"
#include "ap/ap_net.hpp"
#include "game/framework.hpp"
#include "game/hooks/level.hpp"
#include "game/math.hpp"
#include "game/model.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "ghost/ghost_nametag.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#ifdef LASAP_TEST
#include "test/test_impl.hpp"
#endif

namespace AP::Puppets {
    Puppet g_Puppets[kPuppets];
    u32 g_LastFrame = 0;
    bool g_NameTags = true;
    bool g_DrawGhosts = true;
    bool g_LayeredGhosts = true;
    u32 g_ShapeMask = 0xfffff;
    u32 g_Remade = 0;

namespace {
    using Game::At;
    using Game::Shared;
    using Game::SharedPtr;
    using Game::StrView;
    using Game::GetPlayer;
    using Game::NowMs;
    using Game::NowMsFine;
    using Game::Ticks;

    // Spawned through the actor factory (Actor_Create; scripts/re/ACTORS.md, Creation). Ghosts are NpcGhost actors
    // with an Npc row of their own: the NPC component's constructor looks its class's row up inside the factory call
    // (ACTORS.md, NPCs), so while the module's own spawn runs the key is APGhost, if the sheet has that row (the
    // patcher adds it, ap_player_ghost.py; without the row the factory returns nothing).
    constexpr u16 kGhostActor = 0x179;
    using Game::Actors::kNpcRowSize;
    StrView g_GhostRow{"APGhost", 7};
    bool g_SpawningGhost = false;
    constexpr u64 kHiddenRemoveMs = 10000;  // a ghost hidden this long loses its actor
    // Animations by name: the NPC component's virtual play (NpcAnimation_PlayByName, or a class's
    // NpcAnimation_PlayByName2; scripts/re/ACTORS.md, NPCs), its slot found in the component's vtable at its first use.
    int g_PlaySlot = -1;  // -1: not looked up; -2: not in the component's vtable
    // The upper body, as Link has it: each time a ghost's component (the APGhost row) loads its model, the module makes
    // it Link's kind of animation player instead of the NPC's single-layer one (NpcComponent_MakeAnimator): a
    // LayeredAnimation with two layers, layer 1 masked from spine_a up and off (scripts/re/MODELS.md,
    // LayeredAnimation).
    // Layer 0 plays as the NPC's; layer 1 takes the sender's upper-body animation as Link's controller hands it over
    // (PlayerAnimation_SetSlot).
    constexpr int kUpperLayer = 1;

    // A ghost whose last line is older than this is not shown (the relay and the client send offs; this covers a
    // lost one). A still player's game sends a position every kStillMs (ghost_net.cpp).
    constexpr u64 kStaleMs = 5000;
    constexpr u32 kRespawnFrames = 60;  // after a failed spawn
    // The name tag's point: above the head bone of the ghost's model (where Link's head is drawn, so it follows a
    // fall from above, a jump or a warp's lift), or above its feet while the model has no skeleton yet.
    constexpr float kTagAboveHead = 0.68f, kTagHeight = 1.3f;  // kTagHeight: above the feet (the head + kTagAboveHead)
    constexpr u32 kNoRowFrames = 600;   // after a spawn without the APGhost row
    // A shown puppet whose actor was never active this long is made again (a backstop: zone -1 keeps the zones from
    // suspending it).
    constexpr u32 kInactiveFrames = 30;

    // The puppet's model once it is loaded: its animator's model.
    Game::ModelObj* ModelOf(const Puppet& p) { return AnimatorModel(AnimatorOf(p)); }

    // Lets the actor go, once the game has destroyed it.
    void Forget(Puppet& p) {
        ClearSpan(IndexOf(p));
#ifdef LASAP_TEST
        std::vector<std::string> log = std::move(p.animLog);
        p = Puppet{};
        p.animLog = std::move(log);
#else
        p = Puppet{};
#endif
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

    // The Npc sheet's APGhost row (the patcher adds it with the ghosts' model), or nullptr.
    const void* GhostRow() {
        void* sheet = **Game::NpcSheets_ptr;
        return sheet != nullptr ? Game::Datasheet_FindRow(sheet, &g_GhostRow, kNpcRowSize) : nullptr;
    }

    bool HasGhostRow() { return GhostRow() != nullptr; }

    bool IsLinkModel(const Game::ModelObj* model) {
        const u8* res = model != nullptr ? model->res : nullptr;
        if (res == nullptr || std::memcmp(res, "FMDL", 4) != 0) return false;
        return Game::ResNameOf(res) == "link";
    }

    // First each frame for each puppet with an actor: its model. The actor loads its model again in each level, as a
    // new model object with its own animation player.
    void RefreshModel(Puppet& p) {
        Game::ModelObj* model = ModelOf(p);
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
        Game::Actor* actor = p.actor.Peek();
        if (actor == nullptr) return;
        bool active = actor->entity.Active();
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

    // GhostNet's slot n: shown while the client is connected, the ghost is in Link's level and not hidden, and its
    // last line is recent; fed its new lines, replayed every frame. A puppet not shown stays hidden for reuse.
    void DriveGhost(int n, const std::string& level, u32 frame, u64 now, double nowFine) {
        Puppet& p = g_Puppets[n];
        const AP::GhostNet::Ghost& g = AP::GhostNet::Get(n);
        bool shown = AP::IsClientConnected() && g.active && !g.flags.Has(GhostFlag::Hidden) && g.level == level &&
                     now - g.receivedMs < kStaleMs;
        if (!shown) {
            if (p.shown) Hide(p, now);
            else if (p.actor.Holds() && now - p.hiddenSince >= kHiddenRemoveMs) Remove(p);
            return;
        }
        if (!p.shown) {
            std::string err;
            if (frame < p.spawnAfter || !Spawn(p, kGhostActor, Game::Vec3{g.x, g.y, g.z}, frame, err)) return;
            p.framesSeen = g.timelineFrom;
        }
        Game::Actor* actor = p.actor.Peek();
        if (actor == nullptr) return;
        if (!actor->entity.Active()) {
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
}

    int IndexOf(const Puppet& p) { return static_cast<int>(&p - g_Puppets); }

    bool Alive(const Puppet& p) { return p.actor.Alive(); }

    Game::Actors::NpcComponent* NpcOf(const Puppet& p) {
        return Alive(p) ? reinterpret_cast<Game::Actors::NpcActor*>(p.actor.Peek())->npc : nullptr;
    }

    void* AnimatorOf(const Puppet& p) {
        Game::Actors::NpcComponent* npc = NpcOf(p);
        return npc != nullptr ? npc->animator : nullptr;
    }

    bool IsLayered(const void* animator) {
        return At<const u8*>(animator, 0) == Game::LayeredAnimation_vtable.Ptr();
    }
    // A layered animator keeps its model instance and object with its layers.
    Game::ModelInstance* AnimatorInstance(const void* animator) {
        if (animator == nullptr) return nullptr;
        return IsLayered(animator) ? static_cast<const Game::LayeredAnimation*>(animator)->layers.instance
                                   : static_cast<const Game::SkeletalAnimation*>(animator)->layer.instance;
    }
    Game::ModelObj* AnimatorModel(const void* animator) {
        if (animator == nullptr) return nullptr;
        return IsLayered(animator) ? static_cast<const Game::LayeredAnimation*>(animator)->layers.model
                                   : static_cast<const Game::SkeletalAnimation*>(animator)->layer.model;
    }
    Game::SkeletalModelComponent* AnimatorModelComp(const void* animator) {
        if (animator == nullptr) return nullptr;
        return static_cast<const Game::SkeletalAnimation*>(animator)->modelComp;  // the layered one's base
    }

    // Removal as the game does it (0x8e3440): Entity_RemoveChild from the actor system, keyed by the actor's name.
    void Remove(Puppet& p) {
        ClearSpan(IndexOf(p));
        if (Game::Actor* actor = p.actor.Peek(); actor != nullptr && !actor->entity.Gone()) {
            void* system = Game::GetFramework()->actorSystem.ptr;
            Game::Entity_RemoveChild(system, actor->entity.name, actor->entity.nameLength, false);
        }
        Forget(p);
    }

    bool Spawn(Puppet& p, u16 id, const Game::Vec3& pos, u32 frame, std::string& err) {
        if (Alive(p)) {
            Place(p, pos, nullptr);
            p.shown = true;
#ifdef LASAP_TEST
            p.shownAt = frame;
#endif
            return true;
        }
        auto* player = GetPlayer();
        if (player == nullptr) {
            err = "no player";
            return false;
        }
        void* rot = *Game::IdentityQuat_ptr;
        void* scale = *Game::OneScale_ptr;
        if (rot == nullptr || scale == nullptr) {
            err = "no identity globals";
            return false;
        }
        if (id == kGhostActor && !HasGhostRow()) {
            err = "no APGhost row (a mod patched before ghosts)";
            p.spawnAfter = frame + kNoRowFrames;
            return false;
        }
        Game::ScopedActorParams made;
        Game::ActorParams& params = made.params;
        params.actorId = id;
        // Zone -1: NpcGhost is zone-scoped, and an actor of a zone Link has left is suspended until he enters it
        // again (scripts/re/ACTORS.md, Activity); a ghost goes where its player goes, so it belongs to no zone.
        params.zone = -1;
        params.pos = {pos.x, pos.y, pos.z, 0};
        std::memcpy(&params.rot, rot, sizeof(params.rot));
        std::memcpy(&params.scale, scale, sizeof(params.scale));

        // The owner is the framework's actor system (a shared_ptr), as the player's hookshot passes it.
        SharedPtr<>* system = &Game::GetFramework()->actorSystem;
        Shared<Game::Actor> out;
        g_SpawningGhost = id == kGhostActor;
        Game::Actor_Create(out.Reset(), system, &params);
        g_SpawningGhost = false;


        if (!out || out.View().ctrl == nullptr) {
            err = "factory returned nothing";
            p.spawnAfter = frame + kRespawnFrames;
            return false;
        }
        Forget(p);
        p.actor = out;
        out = {};  // the actor system's reference keeps it
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

    void PlayAnim(Puppet& p, const char* name, float blend, float frame) {
        Game::Actors::NpcComponent* comp = NpcOf(p);
        if (comp == nullptr) return;
        uintptr_t* vtable = Game::VTable(comp);
        if (g_PlaySlot == -1) {
            g_PlaySlot = -2;
            const uintptr_t plays[] = {reinterpret_cast<uintptr_t>(Game::NpcAnimation_PlayByName.Ptr()),
                                       reinterpret_cast<uintptr_t>(Game::NpcAnimation_PlayByName2.Ptr())};
            for (int i = 0; i < 0x40 && g_PlaySlot < 0; i++)
                for (uintptr_t f : plays)
                    if (vtable[i] == f) g_PlaySlot = i;
        }
        if (g_PlaySlot < 0) return;
        StrView view{name};
        using Play = Game::Virtual<void(StrView*, u8, float, float)>;
        Play::Call(comp, g_PlaySlot * 8, &view, 0, blend, 0);
        Play::Call(comp, g_PlaySlot * 8, &view, 2, blend, 0);
        if (frame > 0 && comp->animator != nullptr)
            Game::CallVirtual<void(float)>(comp->animator, Game::SkeletalAnimationVirtual::kSetFrame, frame);
    }

    void SetUpper(Puppet& p, int anim) {
        if (anim == p.upper) return;
        p.upper = anim;
        Game::Actors::NpcComponent* comp = NpcOf(p);
        void* animator = comp != nullptr ? comp->animator : nullptr;
        if (animator == nullptr || !IsLayered(animator)) return;
        auto* sac = static_cast<Game::LayeredAnimation*>(animator);
        if (sac->layers.count <= kUpperLayer) return;
        Shared<> played;
        const char* name = anim > 0 ? AP::GhostNet::AnimName(anim) : nullptr;
        if (name != nullptr) {
            StrView view{name};
            Game::NpcAnimation_Get(played.Reset(), comp, &view);
        }
        // none: the layer is only turned off
        if (played) {
            Game::LayeredAnimation_SetSlot(sac, kUpperLayer, played.Untyped(), g_AnimBlend, 0);
            Game::AnimationLayer* layer = sac->layers.layers[kUpperLayer];
            if (layer != nullptr) {
                namespace Layer = Game::AnimationLayerVirtual;
                Game::CallVirtual<void(bool)>(layer, Layer::kSetLoop, AP::GhostNet::AnimLoops(anim));
                Game::CallVirtual<void(float)>(layer, Layer::kSetStart, 0);
            }
        }
        Game::AnimationLayers_SetEnabled(&sac->layers, kUpperLayer, static_cast<bool>(played));
#ifdef LASAP_TEST
        if (p.animLog.size() < 40)
            p.animLog.push_back("upper:" + std::string(name != nullptr ? name : "-") + (played ? "" : "?") +
                                "@" + std::to_string(g_LastFrame));
#endif
    }

    Game::Vec4 QuatOf(float yaw) { return Game::Vec4{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}; }

    void Place(Puppet& p, const Game::Vec3& pos, const Game::Vec4* rot) {
        Game::Node* root = Alive(p) ? p.actor.Peek()->root : nullptr;
        if (root == nullptr) return;
        root->coordsNew.pos = pos;
        root->coords.pos = pos;
        if (rot != nullptr) {
            root->coordsNew.rotate = *rot;
            root->coords.rotate = *rot;
        }
        root->needsCoordUpdate = true;
    }

namespace {
    // While the module spawns a ghost: NpcGhost's component looks up the APGhost row, if the sheet has it.
    HOOK_DEFINE_TRAMPOLINE(NpcComponentCtor) {
        static void* Callback(void* self, void* args, StrView* rowKey, void* x3) {
            if (g_SpawningGhost && rowKey != nullptr && rowKey->data == Game::str_NpcGhost.Ptr()) {
                if (HasGhostRow()) rowKey = &g_GhostRow;
            }
#ifdef LASAP_TEST
            static StrView noSuchRow{"NoSuchRow", 9};
            if (AP::Test::g_ProbeNoRow > 0) {  // DBG probe norow
                AP::Test::g_ProbeNoRow--;
                rowKey = &noSuchRow;
            }
#endif
            return Orig(self, args, rowKey, x3);
        }
    };

    // A ghost's NPC component (the APGhost row) gets a layered animation player with Link's upper-body layer. The
    // player comes back through x8 (a shared_ptr), as a Shared.
    HOOK_DEFINE_TRAMPOLINE(MakeAnimator) {
        static Game::Shared<Game::SkeletalAnimation> Callback(Game::Actors::NpcComponent* comp, StrView* name,
                                                              SharedPtr<>* model) {
            const void* row = comp != nullptr ? comp->row : nullptr;
            if (g_LayeredGhosts && row != nullptr && model != nullptr && model->ptr != nullptr && row == GhostRow()) {
                Game::Shared<Game::LayeredAnimation> out;
                int layers = 2;
                Game::LayeredAnimation_Make(out.Reset(), comp->owner, name, model, &layers);
                // none when the owner has a component of that name already: the game's maker answers then
                if (!out) return Orig(comp, name, model);
                Game::AnimationLayers_SetBoneMask(&out->layers, kUpperLayer, Game::str_spine_a.Ptr(), true);
                Game::AnimationLayers_SetEnabled(&out->layers, kUpperLayer, false);
                // started and made the entity's child, as NpcAnimator_Make does with its player
                Game::Entity* entity = comp->owner != nullptr ? &comp->owner->entity : nullptr;
                void* manager = *Game::EntityManager_ptr;
                if (manager != nullptr) Game::EntityManager_PostStart(manager, out.Untyped());
                Game::Entity_AttachChild(entity, out.get());
                return std::move(out).Alias(&out->base);
            }
            return Orig(comp, name, model);
        }
    };
}

    void InstallHooks() {
        NpcComponentCtor::InstallAtFuncPtr(Game::NpcComponent_Ctor.Ptr());
        MakeAnimator::InstallAtFuncPtr(Game::NpcComponent_MakeAnimator.Ptr());
        InstallDitherHooks();
#ifdef LASAP_TEST
        TestInstallHooks();
#endif
    }

    void TickPuppets();

    // Moves the puppets once per frame.
    void Tick() {
#ifdef LASAP_TEST
        u64 t0 = Ticks();
        u32 before = g_LastFrame;
        TickPuppets();
        if (g_LastFrame != before) TestTickTime(Ticks() - t0);
#else
        TickPuppets();
#endif
    }

    void TickPuppets() {
        auto* player = GetPlayer();
        Game::Framework* fmwk = Game::GetFramework();
        if (player == nullptr || fmwk == nullptr) {
            // a level loads
            for (int n = 0; n < kPuppets; n++) {
                ClearSpan(n);
                g_Puppets[n].wasActive = false;
            }
            SettleCrowd();
            AP::NameTag::DropAll();
            InstDropAll(false);
#ifdef LASAP_TEST
            TestLevelLoad();
#endif
            return;
        }
        u32 frame = fmwk->frameCount;
        if (frame == g_LastFrame) {
            // the game paused (a menu opens): its frame count stays and the actors stand, but the instanced models'
            // records are emptied each frame drawn, so they are written again
            InstTick();
            return;
        }
        g_LastFrame = frame;
        for (auto& p : g_Puppets) {
            if (p.actor.Holds() && !Alive(p)) Forget(p);
            if (p.actor.Holds()) RefreshModel(p);
        }
#ifdef LASAP_TEST
        TestFrameStart(player);
#endif
        std::string level = LevelSystem::GetCurrentLevel();
        u64 now = NowMs();
        double nowFine = NowMsFine();
        for (int n = 0; n < kGhosts; n++) DriveGhost(n, level, frame, now, nowFine);
#ifdef LASAP_TEST
        TestGhostsDriven(frame, now, nowFine);
#endif
        Rank(player, frame, now);
        for (auto& p : g_Puppets) {
            if (Alive(p)) Update(p);
        }
        InstTick();
#ifdef LASAP_TEST
        TestFrameEnd(frame, fmwk->paused);
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
}
