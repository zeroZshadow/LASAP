// The item popup (DESIGN.md, Showing received items): with Instant Pickups, a received or found item
// shows its 3D model above Link's head for a moment while the toast names it; play goes on. The model is made the
// way NPCs that hold an item make theirs: the game's item model maker loads the item's model (its Items.gsheet
// npcKey's Npc row, as the item-get's model task does) and makes a skeletal model component of the owner, a node
// under the root given: here owned by Link and hanging under his model component. The module enables it, poses its
// node each frame and destroys it again.
#include "ap/ap_popup.hpp"
#include "ap/ap.hpp"
#include "game/actor.hpp"
#include "game/framework.hpp"
#include "game/math.hpp"
#include "game/model.hpp"
#include "game/time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    using Game::SharedPtr;
    using Game::Quat;
    using Game::Mul;
    using Game::AxisQuat;
    using Game::NowMs;

    // ItemModel_Make loads the item's model and, once loaded, makes the component (in a task: the item-get's model
    // task); the holder's +0x18 is then the component (a shared_ptr). Owner, name and root are copied; the name's
    // characters are not.
    constexpr size_t kHolderModel = 0x18;
    // The model is owned by Link (his actor's self pointer) and hangs under his model component: under his root
    // component (PlayerCollision) it is not drawn. The game enables an entity by setting kEnabled and, when that makes
    // it active, calling its kOnActiveChanged. The item model maker leaves the component disabled; the item-get
    // enables it once the item pops up.
    // One popup shows at a time, and each is destroyed before the next is made.
    constexpr const char kName[] = "APItemPopup";
    constexpr u64 kShowMs = 2800;      // how long the model shows (the toast shows 3 s)
    constexpr u64 kGrowMs = 200;       // it grows from nothing, and shrinks away again at the end
    constexpr u64 kLoadMs = 3000;      // a model that has not loaded by then is given up
    constexpr float kHeight = 2.3f;    // above Link's feet; the item-get holds items 2 units up
    constexpr float kRise = 0.4f;      // it rises this far while it grows
    // While Link holds something over his head, the model shows this much higher, above it; it moves between the two
    // heights at kCarryRaisePerMs
    constexpr float kCarryRaise = 1.5f;
    constexpr float kCarryRaisePerMs = kCarryRaise / 150.0f;
    constexpr float kTilt = -30.0f;    // degrees about x: tipped towards the camera, as the item-get tips its item
    // An NPC model is turned by its fix's angle from facing the camera, as in the item-get: the item-get's camera looks
    // down 65 degrees (the play camera 45) and the item's node is tipped back as far.
    constexpr float kItemGetTilt = -65.0f;  // without a camera
    constexpr float kSwayDeg = 35.0f;  // it turns from side to side, its front to the camera
    constexpr u64 kSwayMs = 1600;

    // The camera's world rotation
    Quat CameraRotation() {
        const Game::Camera* camera = Game::GetCamera();
        if (camera == nullptr) return AxisQuat(0, kItemGetTilt);
        const float* v = camera->view;
        return Game::QuatFromMatrix([v](int row, int col) { return v[row * 4 + col]; });
    }

    void Enable(Game::Entity& entity) {
        bool was = entity.Active();
        entity.flags |= Game::EntityFlags::kEnabled;
        if (!was && entity.Active()) Game::CallVirtual<void()>(&entity, Game::EntityVirtual::kOnActiveChanged);
    }

    Game::Actors::Player* g_Player = nullptr;  // the player the model was made for; another player (a level load) drops it
    SharedPtr g_Holder{};
    u64 g_Since = 0;               // when the model was asked for
    u64 g_ShownAt = 0;             // when it first showed, 0 before
    float g_Scale = 1.0f;
    Game::Vec3 g_Center{};         // model units: its middle, moved onto the node's origin (NPC models)
    bool g_RootMoved = true;
    bool g_Fitted = false;         // an NPC model, turned by g_Angle
#ifdef LASAP_TEST
    bool g_NoSway = false;         // DBG popsway off: the model faces the camera, for comparing angles
    // DBG popframes: per Place, the time, the framework's frame counter, the parent's and the model's world rotation
    struct PoseFrame { u64 ms; u32 frame; Game::Vec4 parent, model; };
    std::vector<PoseFrame> g_PoseFrames;
    size_t g_PoseFramesMax = 0;
#endif
    Quat g_Angle{0.0f, 0.0f, 0.0f, 1.0f};

    Game::Node* Model() {
        return g_Holder.ptr != nullptr ? Game::Field<Game::Node*>(g_Holder.ptr, kHolderModel) : nullptr;
    }

    // Destroys the component (unless a level load took it with Link) and lets the holder go.
    void Drop(bool destroy) {
        Game::Node* model = Model();
        AP::Popup::DrawOnTop(nullptr);
        if (destroy && model != nullptr && !model->entity.Gone()) {
            Game::Entity_Destroy(model);
        }
        Game::Release(g_Holder);
        g_Player = nullptr;
        g_ShownAt = 0;
    }

    // The pose in the world: rotation, height above Link's model and scale (Place)
    Quat g_Pose{0.0f, 0.0f, 0.0f, 1.0f};
    float g_PoseHeight = 0.0f, g_PoseScale = 0.0f;
    float g_CarryRaise = 0.0f;     // up to kCarryRaise while Link holds something
    u64 g_PlacedAt = 0;

    // The node's local pose from the world pose. Its parent, Link's model, turns with him; the local pose takes out
    // the parent's world rotation and scale, so the model keeps its world pose whichever way he looks.
    void ApplyPose(Game::Node* node, const Game::Node* parent) {
        const Game::Vec4& r = parent->coords.rotate;
        float parentScale = parent->coords.scale.y > 0.0f ? parent->coords.scale.y : 1.0f;
        Quat q = Mul(Quat{-r.x, -r.y, -r.z, r.t}, g_Pose);
        float s = g_PoseScale / parentScale;
        node->coordsNew.pos = {0.0f, g_PoseHeight / parentScale, 0.0f};
        node->coordsNew.rotate = {q.x, q.y, q.z, q.w};
        node->coordsNew.scale = {s, s, s};
    }

    // Link turns in one frame, after the module's tick has placed the model. So the world update of the model's node
    // brings the parent up to date first and places the model again from it; placed from the parent's last world
    // rotation, the model turned with Link for a frame each time he turned.
    HOOK_DEFINE_TRAMPOLINE(NodeUpdateWorld) {
        static void Callback(Game::Node* node) {
            if (node != nullptr && node == Model() && node->parent != nullptr) {
                Game::Node* parent = node->parent;
                if (parent->needsCoordUpdate) Orig(parent);
                ApplyPose(node, parent);
            }
            Orig(node);
        }
    };

    // The pose for the time shown: growing and rising, turning from side to side, shrinking away at the end; facing
    // the camera whichever way Link looks. An NPC model is turned from the camera by its fix's angle, as in the
    // item-get, and from side to side.
    void Place(Game::Node* model, u64 shown) {
        if (g_Player->modelComp == nullptr) return;
        Game::Node* parent = &g_Player->modelComp->node;
        float grow = shown < kGrowMs ? (float)shown / kGrowMs
                     : shown + kGrowMs > kShowMs ? (float)(kShowMs - std::min(shown, kShowMs)) / kGrowMs : 1.0f;
        float rise = shown < kGrowMs ? (float)shown / kGrowMs : 1.0f;
#ifdef LASAP_TEST
        const Game::Vec4& r = parent->coords.rotate;
#endif
        float sway = kSwayDeg * sinf(2.0f * Game::kPi * (float)(shown % kSwayMs) / kSwayMs);
#ifdef LASAP_TEST
        if (g_NoSway) sway = 0.0f;
        if (g_PoseFrames.size() < g_PoseFramesMax) {
            u32 frame = Game::GetFramework()->frameCount;
            g_PoseFrames.push_back({NowMs(), frame, r, model->coords.rotate});
        }
#endif
        g_Pose = g_Fitted ? Mul(CameraRotation(), Mul(AxisQuat(1, sway), g_Angle))
                          : Mul(AxisQuat(0, kTilt), AxisQuat(1, sway));
        bool carrying = g_Player->carried != nullptr;
        u64 now = NowMs();
        float step = kCarryRaisePerMs * (float)(g_PlacedAt != 0 ? now - g_PlacedAt : 0);
        g_PlacedAt = now;
        g_CarryRaise = carrying ? std::min(kCarryRaise, g_CarryRaise + step) : std::max(0.0f, g_CarryRaise - step);
        g_PoseHeight = kHeight - kRise * (1.0f - rise) + g_CarryRaise;
        g_PoseScale = g_Scale * grow;
        ApplyPose(model, parent);
        Game::Node_MarkDirty(model);
    }
}

namespace AP::Popup {
    void InstallHooks() {
        NodeUpdateWorld::InstallAtFuncPtr(Game::Node_UpdateWorld.Ptr());
        InstallDrawHooks();
    }

    bool Show(int itemId) {
        Drop(true);
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->player == nullptr) return false;
        Game::Actors::Player* player = fmwk->player;
        SharedPtr owner{player->actor.entity.self.ptr, player->actor.entity.self.ctrl};
        SharedPtr parent{player->modelComp, player->modelCompCtrl};
        if (owner.ptr == nullptr || parent.ptr == nullptr) return false;
        Game::StrView name{kName, sizeof(kName) - 1};
        // The item-get's size (its pose by item id, the variants' that of their item); an NPC model's fix sets its
        // own (Fit)
        const Game::ItemPose* pose = Game::ItemPose_ItemGet(static_cast<u8>(itemId), false);
        g_Scale = pose != nullptr ? pose->scale[0] : 1.0f;
        g_RootMoved = true;
        g_Fitted = false;
        g_PlacedAt = 0;
        // a model that shows while Link holds something starts above it
        g_CarryRaise = player->carried != nullptr ? kCarryRaise : 0.0f;
        Game::ItemModel_Make(&g_Holder, &owner, &name, &parent, static_cast<u8>(itemId));
        if (g_Holder.ptr == nullptr) return false;
        g_Player = player;
        g_Since = NowMs();
        g_ShownAt = 0;
        return true;
    }

    bool Busy() { return g_Holder.ptr != nullptr; }

#ifdef LASAP_TEST
    void Sway(bool on) { g_NoSway = !on; }

    float Scale() { return g_Holder.ptr != nullptr ? g_Scale : 0.0f; }

    void RecordFrames(size_t count) {
        g_PoseFrames.clear();
        g_PoseFramesMax = count;
    }

    std::vector<std::string> RecordedFrames() {
        std::vector<std::string> lines;
        for (const auto& f : g_PoseFrames) {
            char buf[160];
            snprintf(buf, sizeof(buf), "POPFRAME %lu %u %.4f,%.4f,%.4f,%.4f %.4f,%.4f,%.4f,%.4f", f.ms, f.frame,
                     f.parent.x, f.parent.y, f.parent.z, f.parent.t, f.model.x, f.model.y, f.model.z, f.model.t);
            lines.push_back(buf);
        }
        return lines;
    }
#endif

    bool IsHolder(const void* holder) { return holder != nullptr && holder == g_Holder.ptr; }

    void Fit(float scale, const Game::Vec3& center, const Game::Vec3& angle) {
        g_Scale = scale;
        g_Center = center;
        g_RootMoved = false;
        g_Angle = Game::EulerQuat(angle);  // as the item-get's model fix turns it
        g_Fitted = true;
    }

    void Tick() {
        if (g_Holder.ptr == nullptr) return;
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->player != g_Player) {
            // A level load took Link and his children
            Drop(false);
            return;
        }
        u64 now = NowMs();
        Game::Node* model = Model();
        if (model == nullptr) {
            if (now - g_Since > kLoadMs) Drop(false);
            return;
        }
        if (g_ShownAt == 0) g_ShownAt = now;
        u64 shown = now - g_ShownAt;
        if (shown >= kShowMs) {
            Drop(true);
            return;
        }
        if (!g_RootMoved) {
            g_RootMoved = Game::MoveRootBone(reinterpret_cast<Game::SkeletalModelComponent*>(model), g_Center);
        }
        Place(model, shown);
        Enable(model->entity);
        DrawOnTop(reinterpret_cast<Game::SkeletalModelComponent*>(model)->instance);
    }
}
