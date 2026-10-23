// NPC models held up in item-gets (LASR's songs and tunics): placed, turned and sized as the ocarina is.
#include "AP/ap_bridge.hpp"
#include "AP/ap_popup.hpp"
#include "Game/math.hpp"
#include "Game/model.hpp"
#include "Game/sym.hpp"
#include "Game/time.hpp"
#include <cstring>

namespace AP::Bridge {
namespace {
    using Game::NowMs;

    // NPC models as items (LASR's songs and tunics: Marin, Manbo, Mamu and the Color Dungeon's guards). The item-get
    // makes its model in a task (kItemModelTask, component "ItemModel"), a node of its own under the item's node (2
    // units above Link, tilted back 65 degrees towards the camera, its scale popping from 0 to 1, the shine). The
    // game gives the component a pose per kind of item: the ocarina and the songs kItemModelPos, rolled by about 21
    // degrees, scale 1.17; the tunics 0.15 higher, tilted forward 70 degrees, scale 0.8. Item models are small and
    // centred on their origin; an NPC model stands on its feet there. For these models the component gets the
    // ocarina's place, the fix's angle (from upright in the item's frame, facing the camera) and the scale that
    // gives the model the ocarina's size, and the model's root bone moves by -center, putting its middle on the
    // origin.
    // `radius` and `center` (model units) are the model's bind-pose bounds (scripts/model_bounds.py): its box centre
    // and the radius around it, all vertices in model space. `angle` turns the model about its centre (degrees in
    // the item's frame: x pitch, y yaw, z roll, applied roll first).
    struct HeldModelFix { const char* model; float radius; Game::Vec3 center; Game::Vec3 angle; };
    HeldModelFix g_HeldModelFixes[] = {
        {"Marin", 0.793f, {0.013f, 0.712f, -0.093f}, {20.0f, 0.0f, -20.0f}},
        {"ManboTamegoro", 1.966f, {0.0f, 0.209f, 0.080f}, {20.0f, 70.0f, -20.0f}},
        {"Mamu", 1.930f, {0.0f, 1.401f, -0.446f}, {20.0f, 0.0f, -20.0f}},
        {"SkeletalGuardRed", 0.673f, {0.0f, 0.523f, 0.035f}, {20.0f, 0.0f, -20.0f}},
        {"SkeletalGuardBlue", 0.673f, {0.0f, 0.523f, 0.035f}, {20.0f, 0.0f, -20.0f}},
    };
    // The ocarina's size in the item-get: its radius around its root (0.459) times the scale the game gives it
    constexpr float kItemModelRadius = 0.459f * 1.17f;
    constexpr Game::Vec3 kItemModelPos = {0.0f, 0.55f, 1.0f};  // the ocarina's place under the item's node
    // The item-get's model task (ItemGet_ModelTask): +0x8 the item-get's holder, +0x18 the component as a shared_ptr.
    // Node_MarkDirty does nothing while the node is marked (needsCoordUpdate), so setting that flag alone leaves the
    // model where it was.
    constexpr size_t kTaskHolder = 0x8, kHolderModel = 0x18;
    constexpr u64 kItemModelWindowMs = 3000;  // an item model task this soon after an item-get starts is its model

    bool g_InItemModelTask = false;
    const HeldModelFix* g_ItemModelFix = nullptr;  // the fix for the model the running task gets
    // The component being fixed, held weakly through its control block (libc++: +0x8 owners - 1, +0x10 weak owners
    // - 1); whether its root bone moved.
    u8* g_HeldModel = nullptr;
    u8* g_HeldCtrl = nullptr;
    const HeldModelFix* g_HeldFix = nullptr;
    bool g_HeldRootMoved = false;

    void DropHeldModel() {
        Game::ReleaseWeakOwner(g_HeldCtrl);
        g_HeldCtrl = nullptr;
        g_HeldModel = nullptr;
        g_HeldFix = nullptr;
    }

    bool HeldModelAlive() { return Game::Alive(g_HeldCtrl); }

    // The held NPC model's component node in the ocarina's place, turned by the fix's angle, at the ocarina's size;
    // the game writes its pose once the item pops up, and that write is replaced here.
    void PlaceHeldModel() {
        Game::Coords& c = reinterpret_cast<Game::RootComp*>(g_HeldModel)->coordsNew;
        const HeldModelFix& fix = *g_HeldFix;
        float scale = kItemModelRadius / fix.radius;
        Game::Quat q = Game::EulerQuat(fix.angle);
        c.pos = kItemModelPos;
        c.rotate = {q.x, q.y, q.z, q.w};
        c.scale = {scale, scale, scale};
    }

    HOOK_DEFINE_TRAMPOLINE(ModelResourceGet) {
        static void Callback(void* out, void* holder, const char* name) {
            if (g_InItemModelTask && name != nullptr) {
                g_ItemModelFix = nullptr;
                for (const auto& fix : g_HeldModelFixes) {
                    if (strcmp(name, fix.model) == 0) g_ItemModelFix = &fix;
                }
            }
            Orig(out, holder, name);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ItemModelTask) {
        static void Callback(u8* task) {
            g_InItemModelTask = true;
            g_ItemModelFix = nullptr;
            Orig(task);
            g_InItemModelTask = false;
            u8* holder = *reinterpret_cast<u8**>(task + kTaskHolder);
            if (AP::Popup::IsHolder(holder)) {
                // The popup's model: the same size, centre and angle
                const HeldModelFix* fix = g_ItemModelFix;
                if (fix != nullptr) AP::Popup::Fit(kItemModelRadius / fix->radius, fix->center, fix->angle);
                return;
            }
            if (g_ItemModelFix == nullptr || NowMs() - g_ItemGetAt > kItemModelWindowMs || holder == nullptr) return;
            u8* model = *reinterpret_cast<u8**>(holder + kHolderModel);
            u8* ctrl = *reinterpret_cast<u8**>(holder + kHolderModel + 8);
            if (model == nullptr || ctrl == nullptr) return;
            DropHeldModel();
            Game::AddWeakOwner(ctrl);
            g_HeldModel = model;
            g_HeldCtrl = ctrl;
            g_HeldFix = g_ItemModelFix;
            g_HeldRootMoved = false;
            PlaceHeldModel();
        }
    };

    HOOK_DEFINE_TRAMPOLINE(NodeMarkDirty) {
        static void Callback(Game::RootComp* node) {
            if (node != nullptr && node == reinterpret_cast<Game::RootComp*>(g_HeldModel)) {
                if (HeldModelAlive()) PlaceHeldModel();
                else DropHeldModel();
            }
            Orig(node);
        }
    };
}

    // Each frame: the held model's root bone once it can move, and its weak reference goes once the model is gone.
    void TickHeldModel() {
        if (g_HeldModel == nullptr) return;
        if (!HeldModelAlive()) DropHeldModel();
        else if (!g_HeldRootMoved) g_HeldRootMoved = Game::MoveRootBone(g_HeldModel, g_HeldFix->center);
    }

    void InstallHeldModelHooks() {
        ItemModelTask::InstallAtFuncPtr(Game::ItemGet_ModelTask.Ptr());
        ModelResourceGet::InstallAtFuncPtr(Game::ModelResource_Get.Ptr());
        NodeMarkDirty::InstallAtFuncPtr(Game::Node_MarkDirty.Ptr());
    }

#ifdef LASAP_TEST
    bool SetHeldAngle(const std::string& model, const Game::Vec3& angle) {
        for (auto& fix : g_HeldModelFixes) {
            if (model != fix.model) continue;
            fix.angle = angle;
            return true;
        }
        return false;
    }

    Game::RootComp* HeldModel() {
        return g_HeldModel != nullptr && HeldModelAlive() ? reinterpret_cast<Game::RootComp*>(g_HeldModel) : nullptr;
    }
#endif
}
