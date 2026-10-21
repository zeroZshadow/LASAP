// The received-item popup (DESIGN.md, Showing received items): with Received Items set to Popup, a received item
// shows its 3D model above Link's head for a moment while the toast names it; play goes on. The model is made the
// way NPCs that hold an item make theirs: the game's item model maker loads the item's model (its Items.gsheet
// npcKey's Npc row, as the item-get's model task does) and makes a skeletal model component of the owner, a node
// under the root given: here owned by Link and hanging under his model component. The module enables it, poses its
// node each frame and destroys it again.
#include "AP/ap.hpp"
#include "Game/actor.hpp"
#include "Game/framework.hpp"
#include "offsets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace {
    struct SharedPtr { void* ptr; void* ctrl; };
    struct StringView { const char* ptr; u64 len; };

    // (SharedPtr* out holder, SharedPtr* owner entity, StringView* name, SharedPtr* root node, u8 item id): loads the
    // item's model and, once loaded, makes the component (in a task: the item-get's model task); the holder's +0x18
    // is then the component (a shared_ptr). Owner, name and root are copied; the name's characters are not.
    constexpr uintptr_t kItemModelMake = 0x8d3670;
    constexpr size_t kHolderModel = 0x18;
    // Player (an actor): +0x8 its weak self pointer (object, control block), +0x2a8 Link's model component as a
    // shared_ptr. The model hangs under Link's model component: under his root component (PlayerCollision, +0x80) it
    // is not drawn.
    constexpr size_t kActorSelf = 0x8, kPlayerModel = 0x2a8;
    constexpr uintptr_t kNodeMarkDirty = 0xfb6aa0;     // (node): the node and its children take their new pose
    constexpr uintptr_t kEntityDestroy = 0xfabf70;     // (entity): its own destroy (vtable +0x58), not immediate
    constexpr uintptr_t kReleaseWeak = 0x14311f0;      // std::__shared_weak_count::__release_weak
    constexpr size_t kCtrlOwners = 8;
    // Entity +0x40 flags: bit 0 enabled, active while (flags & kActiveMask) == kActive; bit 30 destroy requested,
    // bit 31 destroyed. The game enables an entity by setting bit 0 and, when that makes it active, calling its
    // virtual +0x50. The item model maker leaves the component disabled; the item-get enables it once the item
    // pops up.
    constexpr size_t kEntityFlags = 0x40, kEntityActiveChanged = 0x50 / 8;
    constexpr u32 kEnabled = 1, kActiveMask = 0x40000017, kActive = 0x17;
    constexpr u32 kEntityGone = 0xc0000000;
    // The model's root bone (DESIGN.md, Showing received items, the NPC models): component +0x148 the model
    // instance, +0x60 -> +0x10 its model object, +0x38 the skeleton, +0x18 its bones' local transforms (bone 0 the
    // root: +0x0 flags, bit 27 translation zero; +0x50 translation).
    constexpr size_t kModelInstance = 0x148, kInstanceObjects = 0x60, kObjectsModel = 0x10, kModelSkeleton = 0x38;
    constexpr size_t kSkeletonLocal = 0x18, kLocalFlags = 0x0, kLocalTranslate = 0x50;
    constexpr u32 kLocalTranslateZero = 0x08000000;

    // One popup shows at a time, and each is destroyed before the next is made.
    constexpr const char kName[] = "APItemPopup";
    constexpr u64 kShowMs = 2800;      // how long the model shows (the toast shows 3 s)
    constexpr u64 kGrowMs = 200;       // it grows from nothing, and shrinks away again at the end
    constexpr u64 kLoadMs = 3000;      // a model that has not loaded by then is given up
    constexpr float kHeight = 2.3f;    // above Link's feet; the item-get holds items 2 units up
    constexpr float kRise = 0.4f;      // it rises this far while it grows
    constexpr float kTilt = -30.0f;    // degrees about x: tipped towards the camera, as the item-get tips its item
    // An NPC model is turned by its fix's angle from facing the camera, as in the item-get: the item-get's camera looks
    // down 65 degrees (the play camera 45) and the item's node is tipped back as far. The camera at Framework +0x560:
    // its view rows at +0x20 (the rotation's rows: +0x20, +0x30, +0x40), so its columns are the camera's axes.
    constexpr size_t kFrameworkCamera = 0x560, kCameraView = 0x20;
    constexpr float kItemGetTilt = -65.0f;  // without a camera
    constexpr float kSwayDeg = 35.0f;  // it turns from side to side, its front to the camera
    constexpr u64 kSwayMs = 1600;

    using ReleaseWeakFn = void (*)(void*);

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    void Release(SharedPtr& sp) {
        if (sp.ctrl != nullptr) {
            auto* owners = reinterpret_cast<long*>(reinterpret_cast<u8*>(sp.ctrl) + kCtrlOwners);
            if (__atomic_fetch_sub(owners, 1, __ATOMIC_ACQ_REL) == 0) {
                auto** vtable = *reinterpret_cast<void (***)(void*)>(sp.ctrl);
                vtable[2](sp.ctrl);  // __on_zero_shared
                GameFn<ReleaseWeakFn>(kReleaseWeak)(sp.ctrl);
            }
        }
        sp = SharedPtr{};
    }

    struct Quat { float x, y, z, w; };

    Quat Mul(const Quat& a, const Quat& b) {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    Quat Axis(int axis, float degrees) {
        float half = degrees * 3.14159265f / 360.0f;
        Quat q{0.0f, 0.0f, 0.0f, cosf(half)};
        (axis == 0 ? q.x : axis == 1 ? q.y : q.z) = sinf(half);
        return q;
    }

    // The camera's world rotation
    Quat CameraRotation() {
        Game::Framework* fmwk = Game::GetFramework();
        const u8* camera = fmwk != nullptr ? *reinterpret_cast<u8* const*>(reinterpret_cast<u8*>(fmwk) + kFrameworkCamera)
                                           : nullptr;
        if (camera == nullptr) return Axis(0, kItemGetTilt);
        const float* v = reinterpret_cast<const float*>(camera + kCameraView);
        auto m = [v](int row, int col) { return v[row * 4 + col]; };
        float trace = m(0, 0) + m(1, 1) + m(2, 2);
        Quat q;
        if (trace > 0.0f) {
            float s = sqrtf(trace + 1.0f) * 2.0f;
            q = {(m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s, 0.25f * s};
        } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
            float s = sqrtf(1.0f + m(0, 0) - m(1, 1) - m(2, 2)) * 2.0f;
            q = {0.25f * s, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s, (m(2, 1) - m(1, 2)) / s};
        } else if (m(1, 1) > m(2, 2)) {
            float s = sqrtf(1.0f + m(1, 1) - m(0, 0) - m(2, 2)) * 2.0f;
            q = {(m(0, 1) + m(1, 0)) / s, 0.25f * s, (m(1, 2) + m(2, 1)) / s, (m(0, 2) - m(2, 0)) / s};
        } else {
            float s = sqrtf(1.0f + m(2, 2) - m(0, 0) - m(1, 1)) * 2.0f;
            q = {(m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, 0.25f * s, (m(1, 0) - m(0, 1)) / s};
        }
        return q;
    }

    void Enable(u8* entity) {
        u32& flags = *reinterpret_cast<u32*>(entity + kEntityFlags);
        bool was = (flags & kActiveMask) == kActive;
        flags |= kEnabled;
        if (!was && (flags & kActiveMask) == kActive) {
            (*reinterpret_cast<void (***)(u8*)>(entity))[kEntityActiveChanged](entity);
        }
    }

    u8* g_Player = nullptr;        // the player the model was made for; another player (a level load) drops it
    SharedPtr g_Holder{};
    u64 g_Since = 0;               // when the model was asked for
    u64 g_ShownAt = 0;             // when it first showed, 0 before
    float g_Scale = 1.0f;
    float g_Center[3] = {};        // model units: its middle, moved onto the node's origin (NPC models)
    bool g_RootMoved = true;
    bool g_Fitted = false;         // an NPC model, turned by g_Angle
#ifdef LASAP_TEST
    bool g_NoSway = false;         // DBG popsway off: the model faces the camera, for comparing angles
#endif
    Quat g_Angle{0.0f, 0.0f, 0.0f, 1.0f};

    u8* Model() {
        return g_Holder.ptr != nullptr ? *reinterpret_cast<u8**>(reinterpret_cast<u8*>(g_Holder.ptr) + kHolderModel)
                                       : nullptr;
    }

    // Destroys the component (unless a level load took it with Link) and lets the holder go.
    void Drop(bool destroy) {
        u8* model = Model();
        if (destroy && model != nullptr && (*reinterpret_cast<u32*>(model + kEntityFlags) & kEntityGone) == 0) {
            GameFn<void (*)(void*)>(kEntityDestroy)(model);
        }
        Release(g_Holder);
        g_Player = nullptr;
        g_ShownAt = 0;
    }

    // An NPC model stands on its feet; once its skeleton exists, its root bone moves its middle onto the origin.
    void MoveRoot(u8* model) {
        auto ptr = [](const u8* base, size_t offset) { return base ? *reinterpret_cast<u8* const*>(base + offset) : nullptr; };
        u8* skeleton = ptr(ptr(ptr(ptr(model, kModelInstance), kInstanceObjects), kObjectsModel), kModelSkeleton);
        u8* root = ptr(skeleton, kSkeletonLocal);
        if (root == nullptr) return;
        *reinterpret_cast<u32*>(root + kLocalFlags) &= ~kLocalTranslateZero;
        auto* t = reinterpret_cast<float*>(root + kLocalTranslate);
        for (int i = 0; i < 3; i++) t[i] -= g_Center[i];
        g_RootMoved = true;
    }

    // The node's pose for the time shown: growing and rising, turning from side to side, shrinking away at the end.
    // Its parent, Link's model, turns with him; the pose takes out the parent's world rotation and scale, so the model
    // faces the camera whichever way he looks. An NPC model is turned from the camera by its fix's angle, as in the
    // item-get, and from side to side.
    void Place(u8* model, u64 shown) {
        auto* parent = *reinterpret_cast<Game::RootComp**>(g_Player + kPlayerModel);
        if (parent == nullptr) return;
        float grow = shown < kGrowMs ? (float)shown / kGrowMs
                     : shown + kGrowMs > kShowMs ? (float)(kShowMs - std::min(shown, kShowMs)) / kGrowMs : 1.0f;
        float rise = shown < kGrowMs ? (float)shown / kGrowMs : 1.0f;
        const Game::Vec4& r = parent->coords.rotate;
        float parentScale = parent->coords.scale.y > 0.0f ? parent->coords.scale.y : 1.0f;
        float sway = kSwayDeg * sinf(2.0f * 3.14159265f * (float)(shown % kSwayMs) / kSwayMs);
#ifdef LASAP_TEST
        if (g_NoSway) sway = 0.0f;
#endif
        Quat pose = g_Fitted ? Mul(CameraRotation(), Mul(Axis(1, sway), g_Angle))
                             : Mul(Axis(0, kTilt), Axis(1, sway));
        Quat q = Mul(Quat{-r.x, -r.y, -r.z, r.t}, pose);
        Game::Coords& c = reinterpret_cast<Game::RootComp*>(model)->coordsNew;
        float s = g_Scale * grow / parentScale;
        c.pos = {0.0f, (kHeight - kRise * (1.0f - rise)) / parentScale, 0.0f};
        c.rotate = {q.x, q.y, q.z, q.w};
        c.scale = {s, s, s};
        GameFn<void (*)(void*)>(kNodeMarkDirty)(model);
    }
}

namespace AP::Popup {
    bool Show(int itemId) {
        Drop(true);
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->mPlayer == nullptr) return false;
        u8* player = reinterpret_cast<u8*>(fmwk->mPlayer);
        SharedPtr owner = *reinterpret_cast<SharedPtr*>(player + kActorSelf);
        auto* parent = reinterpret_cast<SharedPtr*>(player + kPlayerModel);
        if (owner.ptr == nullptr || parent->ptr == nullptr) return false;
        StringView name{kName, sizeof(kName) - 1};
        g_Scale = 1.0f;
        g_RootMoved = true;
        g_Fitted = false;
        GameFn<void (*)(SharedPtr*, SharedPtr*, StringView*, SharedPtr*, u8)>(kItemModelMake)(
            &g_Holder, &owner, &name, parent, static_cast<u8>(itemId));
        if (g_Holder.ptr == nullptr) return false;
        g_Player = player;
        g_Since = NowMs();
        g_ShownAt = 0;
        return true;
    }

    bool Busy() { return g_Holder.ptr != nullptr; }

#ifdef LASAP_TEST
    void Sway(bool on) { g_NoSway = !on; }
#endif

    bool IsHolder(const void* holder) { return holder != nullptr && holder == g_Holder.ptr; }

    void Fit(float scale, const Game::Vec3& center, const Game::Vec3& angle) {
        g_Scale = scale;
        g_Center[0] = center.x;
        g_Center[1] = center.y;
        g_Center[2] = center.z;
        g_RootMoved = false;
        // as the item-get's model fix turns it: yaw, pitch, roll applied roll first
        g_Angle = Mul(Axis(1, angle.y), Mul(Axis(0, angle.x), Axis(2, angle.z)));
        g_Fitted = true;
    }

    void Tick() {
        if (g_Holder.ptr == nullptr) return;
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || reinterpret_cast<u8*>(fmwk->mPlayer) != g_Player) {
            // A level load took Link and his children
            Drop(false);
            return;
        }
        u64 now = NowMs();
        u8* model = Model();
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
        if (!g_RootMoved) MoveRoot(model);
        Place(model, shown);
        Enable(model);
    }
}
