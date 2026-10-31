// The drop actor (DESIGN.md, Drop actor): ItemAPDrop, an item actor class of the module's own at an empty id of the
// game's actor table, for every location LASR shows as a stand-in item: the item a bush, rock, pot, tree or hole drops,
// a dungeon's falling key, a golden leaf, the item buried at a dig spot, a boss's item, the ghost's reward (patcher
// ap_drop.py). Its class is the item base's (scripts/re/ITEMS.md, Item actors) with type ids of its own, made with an
// Npc.gsheet row of its own. Its LEB parameter 0 says how it appears (AP::DropMotion): its Fall event action hops it
// out of the ground, sends a tree's or hole's on the arc of the seashell the tree or hole drops, or drops a dungeon key
// or golden leaf from above. With Bouncy Items (config.ini's bouncy_items) it is a ball, also out of the chests: it
// launches in a random direction and spins, a chest's out of its lid and an owl statue's off its top, rolls to a stop,
// bounces off walls and floats.
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "config/config.hpp"
#include "config/file_handler.hpp"
#include "game/actor.hpp"
#include "game/actors/item.hpp"
#include "game/actors/tree.hpp"
#include "game/data/inventory.hpp"
#include "game/event.hpp"
#include "game/framework.hpp"
#include "game/hooks/level.hpp"
#include "game/mem.hpp"
#include "game/switch.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include "lib.hpp"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>

namespace AP::Bridge {
namespace {
    using Game::Actor;
    using Game::Actors::ItemActor;
    using Game::Actors::ItemComponent;
    using Game::Function;
    using Game::SharedPtr;
    using Game::Shared;
    using Game::SetSwitch;
    using Game::StrView;
    using Game::SwitchOn;
    using Game::EntityFlag;

    // The sizes of the item base's two vtables, which the drop's actor and component copy
    constexpr size_t kActorVtableSize = 0x170, kComponentVtableSize = 0x428;
    // Slots of both vtables: IsA (type id) and the type id
    constexpr size_t kIsA = 0x68, kTypeId = 0x70;
    // The actor's: zone enter and the NPC component maker
    constexpr size_t kZoneEnter = 0x100, kMakeComponent = 0x128;
    // The component's: its pop check and appear (once the check holds), its collision and EventChara makers, and
    // what the item base asks it: hidden while the inventory holds its item (no), takes impulses (yes), frozen until
    // it falls (yes), sleeps (no)
    constexpr size_t kCheckPop = 0x88, kAppear = 0x98, kMakeCollision = 0x210, kMakeEventChara = 0x220;
    constexpr size_t kHidesOwned = 0x3e0, kImpulses = 0x3e8, kFrozen = 0x3f0, kSleeps = 0x3f8;
    // The destructor slots the control blocks call: the actor's (in place) and the component's deleting one
    constexpr size_t kActorDtor = 0x10, kComponentDelete = 0x18;
    // The NPC component's body linear velocity setter (const Vec3*)
    constexpr size_t kSetVelocity = 0x290;
    // With Bouncy Items, the component's: the item update (0x8c6030; not during events), which the drop's wraps, and
    // in water the upward acceleration the item update adds while the body is deeper than 0.3 below the water surface
    // (the item base's 7.0, less than gravity: it sinks) and the body's linear damping (the item base's 1.8). The
    // drop's lift is a little more than gravity, so it floats, and its damping more, so it settles
    constexpr size_t kItemUpdate = 0x3c8, kWaterLift = 0x400, kWaterDamping = 0x410;
    constexpr float kLift = 12.0f, kDamping = 4.0f;
    // A signal's virtual that connects a Function*
    constexpr size_t kSignalConnect = 0x20;
    // LebRoom virtual +0x28 (index): the room's LEB record of that index
    constexpr size_t kLebRoomRecord = 0x28;
    // The item id: the placeholder's Items.gsheet row (its sounds and eventPause)
    constexpr u8 kItemId = static_cast<u8>(AP::PLACEHOLDER_ITEM_ID);
    // The items only a boss's BossItemDrop makes item actors of: the hookshot, the Angler Key, the Face Key and the
    // heart container
    bool BossItem(AP::ItemId item) {
        using AP::ItemId;
        return item == ItemId::HookShot || item == ItemId::AnglerKey || item == ItemId::FaceKey ||
               item == ItemId::HeartContainer;
    }
    // Its model, a component on a node under the actor's root, takes the item pose pair the item constructor stores
    // (read when the model is made). Each drop's pair stands its model's bind-pose bounds (DROPS_PATH) on the root,
    // centred and untilted, at the small key's scale or less, so that its radius around the centre is at most
    // kModelRadius; a drop without a line has its model's origin on the root. Both poses of the pair are that one, and
    // so are the side view's pair's, which the constructor takes from right after it in a side-view area (kPoses).
    // A ball's (Bouncy Items) puts the centre on the ball's centre instead, the sphere's radius above the root, apart
    // from the ball and its touch, and a drop without a line its origin there.
    constexpr float kBallCentre = 0.3f, kModelScale = 1.1f, kModelRadius = 0.6f;
    constexpr size_t kMaxDrops = 256, kPoses = 4;
    constexpr u32 kLebInt = 3;
    // Out of the ground, as a bush's, rock's or pot's own seashell: the game's default drop start (0x899f40) and
    // launch (0x89a190) for a single item, 0.5 above the object and straight up at 4
    constexpr float kHopRise = 0.5f, kHopUp = 4.0f;
    // On an arc, as the seashell would come: from a tree (its drop, 0xcb1f30) the tree's parameters give the launch
    // speed and the crown's height above its root, which the drop leaves 0.5 above;
    // from a hole, the AreaShellDropper's (virtuals +0x138 and +0x140) 2.25 above it at 10.8. The launch solver
    // (0x89d880) aims at the landing spot with the game's gravity, on the high arc, or at 45 degrees and half the
    // slope's angle when out of reach.
    constexpr float kCrownAbove = 0.5f, kDropperAbove = 2.25f, kDropperSpeed = 10.8f;
    constexpr float kGravity = -9.81f, kNear = 1e-4f, kQuarterPi = 0.7853982f;
    // The landing spot the tree and the dropper aim at, the point of their first LEB rail link, which the drop has as
    // its own (Game::ActorRailLink: the LEB room's virtual +0x48 (point index) gives the point's position); without
    // one, the drop's spawn
    constexpr size_t kRoomRailPoint = 0x48;
    // Bouncy Items. A ball out of the ground: the start above the spawn and the ranges of the speeds up and outward
    // (units per second), outward in any direction: it may get away from Link
    constexpr float kRise = 0.5f, kSpeedUpMin = 5.5f, kSpeedUpMax = 7.5f, kSpeedOutMin = 1.0f, kSpeedOutMax = 3.0f;
    // A ball falling as a dungeon key, from about as high as a launch out of the ground goes (the small key's fall
    // starts 10 above, which a ball bounces back from for over 15 s)
    constexpr float kFallHeight = 3.0f;
    constexpr float kTwoPi = 6.2831853f;
    // A ball's arc lands on a spot rolled around the seashell's: up to 45 degrees either side, 0.6 to 1.4 as far
    constexpr float kArcNear = 0.6f, kArcFar = 1.4f;
    // Out of a chest: from 1 above it at this speed onto a random spot in front of it, up to 45 degrees from the way to
    // its spawn (in front of the chest) and 2.25 to 6.75 out; off an owl statue the same from its top
    constexpr float kChestLid = 1.0f, kStatueTop = 2.0f, kChestSpeed = 8.0f, kChestNear = 2.25f, kChestFar = 6.75f;
    // and Link, who opened or examined it, cannot take it on its way over him: the item's touch delay (ItemActor,
    // seconds; 0.5 from when it is made) starts again and lasts the arc's flight
    // Every launch spins it about a random level axis at 6 to 14 radians a second, so that it skips aside when it lands
    constexpr float kSpinMin = 6.0f, kSpinMax = 14.0f;
    // The PhysX actor of the component's collision's body (PxRigidDynamic, PhysX 3.4): its getLinearVelocity (returns
    // a PxVec3), getAngularVelocity, setAngularVelocity (const Vec3*, bool wake), setLinearDamping and
    // setAngularDamping (float)
    constexpr size_t kGetLinear = 0x130, kGetAngular = 0x140, kSetAngular = 0x148;
    constexpr size_t kSetLinearDamping = 0x1c8, kSetAngularDamping = 0x1d8;
    // The ball's update. Its spin is free in the air and damped while it touches the ground out of water, as is its
    // speed (PhysX has no rolling resistance), so that it rolls to a stop; in the air its linear damping is its row's
    // drag again (component virtual +0x408). It touches the ground for two updates in a row in which its vertical speed
    // did not drop by kFallingShare of free fall's (a roll, rest; a bounce is one); the first update in which it does
    // again, it is in the air. Each update's time from the system tick (19.2 MHz); one after more than kMaxStep (the
    // update did not run, as during events) only takes the velocity.
    constexpr size_t kTraitsDrag = 0x408;
    constexpr float kSpinDampAir = 0.05f, kSpinDampGround = 8.0f, kDragGround = 3.0f, kFallingShare = 0.37f;
    constexpr float kTicksPerSecond = 19200000.0f, kMaxStep = 0.1f;
    constexpr u8 kTouching = 2;
    // PhysX bounces no contact slower than the scene's bounce threshold, so a ball that meets a wall at walking pace
    // stops against it. An update in which its level velocity lost more against its way than its damping takes, by
    // over kImpact a second, without landing (its vertical speed did not go up by kFallingShare of gravity's), met a
    // wall: the wall's normal is the way of that loss, and the ball gets back kWallBounce (its row's bounciness) of the
    // speed it had into the wall, less what PhysX gave back.
    constexpr float kImpact = 30.0f, kWallBounce = 0.7f;
    // Its state, after the item component's
    struct Ball {
        Game::Vec3 velocity;       // at its last update
        u64 tick;                  // of its last update, 0 before the first
        u8 touches;                // updates in a row it touched the ground
        u8 opened;                 // out of its chest, whose opened flag is still to go off
    };
    // The drop's NPC component: the item component's, then the ball's
    struct DropComponent {
        ItemComponent item;
        Ball ball;
    };
    static_assert(offsetof(DropComponent, ball) == sizeof(ItemComponent));
    // An item class's allocation (make_shared): the control block, then the actor
    struct ActorBlock {
        Game::SharedCtrl ctrl;
        u8 pad_18[0x8];
        ItemActor actor;
    };
    static_assert(offsetof(ActorBlock, actor) == 0x20 && sizeof(ActorBlock) == 0x340);
#ifdef LASAP_TEST
    // The PhysX actor's getAngularDamping
    constexpr size_t kGetAngularDamping = 0x1e0;
#endif

    // libc++ std::function's callable (__func, the game built without RTTI): its vtable's slots
    enum FuncSlot { kFuncDtor, kFuncDelete, kFuncClone, kFuncCloneInto, kFuncDestroy, kFuncDestroyFree, kFuncCall,
                    kFuncSlots };

    uintptr_t g_ActorVtable[kActorVtableSize / 8];
    uintptr_t g_ComponentVtable[kComponentVtableSize / 8];
    // The control blocks: the actor's make_shared block (ActorBlock), the component's pointer block
    uintptr_t g_ActorCtrlVtable[Game::kCtrlSlots];
    uintptr_t g_ComponentCtrlVtable[Game::kCtrlSlots];
    // The callables the drop puts in the game's std::functions: its body made and lost, its event actions, the Fall
    // action's step
    uintptr_t g_BodyMadeVtable[kFuncSlots];
    uintptr_t g_BodyLostVtable[kFuncSlots];
    uintptr_t g_ActionsVtable[kFuncSlots];
    uintptr_t g_FallStepVtable[kFuncSlots];
    // The type ids of the drop's actor and component: their addresses
    u8 g_ActorType, g_ComponentType;
    void (*g_ItemUpdate)(void*) = nullptr;  // the item base's, which the ball's wraps
    // Each drop's model poses (the pair and the side view's), by its LEB key
    struct DropModel {
        u64 key;
        Game::ItemPose poses[kPoses];
    };
    DropModel g_Models[kMaxDrops]{};
    size_t g_ModelCount = 0;
    Game::ItemPose g_DefaultPoses[kPoses]{};
    // A PxVec3 a PhysX getter returns: its copy constructor is not trivial, so it comes back through x8
    struct PxVec3 {
        float x = 0, y = 0, z = 0;
        ~PxVec3() {}
    };
#ifdef LASAP_TEST
    Game::Weak<Actor> g_Last;  // the drop actor that fell last
#endif
    // The boss drop actors made, a level's at once (Catfish's Maw has Master Stalfos's and the Slime Eel's), and the
    // one made last
    constexpr int kBossSlots = 4;
    Game::Weak<Actor> g_Bosses[kBossSlots];
    int g_BossLast = 0;
    // Where a boss's item (a heart container, the Angler Key, the Face Key, the hookshot) would have started while its
    // drop was not made yet (its room loading): the boss's LEB key (0: none), the spot, and the level load it holds
    // for (a taken drop is never made, so a later load, of another save too, must not find it)
    struct HeartSpot {
        u64 boss = 0;
        Game::Vec3 at{};
        u32 load = 0;
    } g_Heart;
    // The launch the boss gave its item last (Enemy_DropItems' default: straight up at 4)
    Game::Vec3 g_BossLaunch{0, 4, 0};

    bool Bouncy() { return global_config.tweaks.bouncy_items; }

    StrView RowKey() { return StrView{AP::DROP_ACTOR}; }

    void SetPoses(Game::ItemPose* poses, const float centre[3], float radius) {
        float s = std::fmin(kModelScale, kModelRadius / radius);
        float lift = Bouncy() ? kBallCentre : s * radius;
        poses[0] = {{-s * centre[0], lift - s * centre[1], -s * centre[2]}, {0, 0, 0}, {s, s, s}};
        std::fill(poses + 1, poses + kPoses, poses[0]);
    }

    // "<LEB key> <centre x> <y> <z> <radius>" lines (patcher ap_drop.write_drops)
    void LoadDrops() {
        float lift = Bouncy() ? kBallCentre : 0;
        g_DefaultPoses[0] = {{0, lift, 0}, {0, 0, 0}, {kModelScale, kModelScale, kModelScale}};
        std::fill(g_DefaultPoses + 1, g_DefaultPoses + kPoses, g_DefaultPoses[0]);
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(AP::DROPS_PATH, text))) return;
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::stringstream ss(line);
            u64 key;
            float centre[3], radius;
            if (g_ModelCount >= kMaxDrops || !(ss >> key >> centre[0] >> centre[1] >> centre[2] >> radius) ||
                !(radius > 0)) {
                continue;
            }
            DropModel& model = g_Models[g_ModelCount++];
            model.key = key;
            SetPoses(model.poses, centre, radius);
        }
        Logging.Log("[AP] drop models: %zu", g_ModelCount);
    }

    const Game::ItemPose* PosesOf(u64 key) {
        for (size_t i = 0; i < g_ModelCount; i++) {
            if (g_Models[i].key == key) return g_Models[i].poses;
        }
        Logging.Log("[AP] drop %016llx: no model bounds, its model's origin on the root",
                    static_cast<unsigned long long>(key));
        return g_DefaultPoses;
    }

    int Motion(const Actor& actor) {
        return actor.lebParams[0].type == kLebInt ? static_cast<s32>(actor.lebParams[0].value) : -1;
    }

    ItemComponent* Item(void* comp) { return static_cast<ItemComponent*>(comp); }

    Actor* Owner(void* comp) { return Item(comp)->npc.owner; }

    Game::Actors::WaterEvent* WaterOf(void* comp) {
        return Item(comp)->npc.waterEvent.get();
    }

    // ---- The game's std::function, holding the drop's callables ----

    // `from`'s callable copied into `to` (empty): in its buffer when it was in `from`'s, else on the heap
    void CopyFunction(const Function& from, Function* to) {
        to->f = nullptr;
        if (from.f == nullptr) return;
        if (from.f == from.buffer) {
            Game::CallVirtual<void(void*)>(from.f, kFuncCloneInto * 8, to->buffer);
            to->f = to->buffer;
        } else {
            to->f = Game::CallVirtual<void*()>(from.f, kFuncClone * 8);
        }
    }

    void DestroyFunction(Function* fn) {
        if (fn->f == nullptr) return;
        Game::CallVirtual<void()>(fn->f, (fn->f == fn->buffer ? kFuncDestroy : kFuncDestroyFree) * 8);
        fn->f = nullptr;
    }

    // A callable that holds only its component, in the function's buffer
    struct ComponentCall {
        const uintptr_t* vtable;
        void* comp;
    };

    void CallNothing(void*) {}
    void CallFree(void* self) { Game::operator_delete(self); }
    void* CallClone(const ComponentCall* self) {
        auto* copy = static_cast<ComponentCall*>(Game::operator_new(sizeof(ComponentCall)));
        *copy = *self;
        return copy;
    }
    void CallCloneInto(const ComponentCall* self, ComponentCall* to) { *to = *self; }

    template<typename Call>
    void SetCallVtable(uintptr_t (&vtable)[kFuncSlots], Call call) {
        vtable[kFuncDtor] = vtable[kFuncDestroy] = reinterpret_cast<uintptr_t>(&CallNothing);
        vtable[kFuncDelete] = vtable[kFuncDestroyFree] = reinterpret_cast<uintptr_t>(&CallFree);
        vtable[kFuncClone] = reinterpret_cast<uintptr_t>(&CallClone);
        vtable[kFuncCloneInto] = reinterpret_cast<uintptr_t>(&CallCloneInto);
        vtable[kFuncCall] = reinterpret_cast<uintptr_t>(call);
    }

    Function ComponentFunction(const uintptr_t* vtable, void* comp) {
        Function fn{};
        *reinterpret_cast<ComponentCall*>(fn.buffer) = {vtable, comp};
        fn.f = fn.buffer;
        return fn;
    }

    // ---- Falling ----

    // The LEB record the first link of the drop's record names (the tree, the hole's dropper, the chest, the boss)
    const Game::LebActorRecord* LinkedRecord(const Actor& drop) {
        const Game::LebActorRecord* record = drop.record;
        if (record == nullptr || record->linkCount == 0 || drop.lebRoom.ptr == nullptr) return nullptr;
        return Game::CallVirtual<const Game::LebActorRecord*(u32)>(drop.lebRoom.ptr, kLebRoomRecord,
                                                                   record->Links()[0].actorIndex);
    }

    // The actor the first LEB link of the drop's record names, held in `hold`
    Actor* LinkedActor(const Actor& drop, Shared<Actor>* hold) {
        const Game::LebActorRecord* target = LinkedRecord(drop);
        if (target == nullptr) return nullptr;
        Game::ActorRegistry_FindByKey(hold->Reset(), target->key);
        Actor* actor = hold->get();
        return actor != nullptr && !actor->entity.Gone() ? actor : nullptr;
    }

    // The point of the drop's first rail link, which it has from what drops it
    bool LandingPoint(const Actor& drop, Game::Vec3* point) {
        if (drop.railLinks.begin() == drop.railLinks.end()) return false;
        auto* link = static_cast<const Game::ActorRailLink*>(*drop.railLinks.begin());
        if (link->room.ptr == nullptr) return false;
        auto* pos = Game::CallVirtual<const Game::Vec3*(u32)>(link->room.ptr, kRoomRailPoint, link->link.point);
        if (pos == nullptr) return false;
        *point = *pos;
        return true;
    }

    // The launch solver's velocity (0x89d880): `speed` from `from` to land on `to`.
    Game::Vec3 LaunchVelocity(float speed, const Game::Vec3& from, const Game::Vec3& to) {
        float dx = to.x - from.x, dz = to.z - from.z, dy = to.y - from.y;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d < kNear) return Game::Vec3{0, speed, 0};
        float k = d * d * kGravity / (2 * speed * speed);
        float a = d / k;
        float disc = a * a * 0.25f - (1 - dy / k);
        float angle = disc < 0 ? std::atan2(dy, d) * 0.5f + kQuarterPi : std::atan(a * -0.5f + std::sqrt(disc));
        float out = std::cos(angle) * speed / d;
        return Game::Vec3{dx * out, std::sin(angle) * speed, dz * out};
    }

    // Where a linked drop starts, how fast and where it lands: from the bonked tree's crown at the tree's speed, from
    // above the bombed hole's dropper, from the opened chest's lid (Bouncy Items); onto the seashell's landing spot,
    // the chest's onto its spawn (ArcOf)
    struct Arc {
        Game::Vec3 start, to;
        float speed;
    };

    // A chest's or owl statue's drop: launched from it by its launch flag (switch 0, a level flag)
    bool Launched(int motion) { return motion == AP::DROP_CHEST || motion == AP::DROP_OWL; }

    // A boss's drop: out of the ground where its boss's item would start
    bool Boss(int motion) { return motion == AP::DROP_BOSS || motion == AP::DROP_BOSS_PIT; }

    // The drop's arc; false without the tree, dropper or chest
    bool ArcOf(const Actor& drop, int motion, Arc* arc) {
        Shared<Actor> hold;
        Actor* source = LinkedActor(drop, &hold);
        float above = motion == AP::DROP_CHEST ? kChestLid : motion == AP::DROP_OWL ? kStatueTop : kDropperAbove;
        arc->speed = Launched(motion) ? kChestSpeed : kDropperSpeed;
        if (source != nullptr && motion == AP::DROP_TREE) {
            const Game::Actors::TreeParams* params = reinterpret_cast<Game::Actors::Tree*>(source)->params;
            if (params != nullptr) {
                above = params->crownHeight + kCrownAbove;
                arc->speed = params->launchSpeed;
            } else {
                source = nullptr;
            }
        }
        bool found = source != nullptr && source->root != nullptr;
        if (found) {
            const Game::Vec3& at = source->root->coords.pos;
            arc->start = {at.x, at.y + above, at.z};
            const Game::Vec4& spawn = drop.spawnPos;
            arc->to = {spawn.x, spawn.y, spawn.z};
            if (!Launched(motion)) LandingPoint(drop, &arc->to);
        }
        return found;
    }

    void Launch(void* comp, Actor* actor, const Game::Vec3& start, const Game::Vec3& velocity) {
        Game::Node_SetWorldPos(actor->root, &start);
        Game::CallVirtual<void(const Game::Vec3*)>(comp, kSetVelocity, &velocity);
    }

    void* PhysxActor(void* comp) {
        Game::Actors::BodyCollision* collision = Item(comp)->npc.collision.get();
        return collision != nullptr && collision->body != nullptr ? collision->body->pxActor : nullptr;
    }

    Ball& BallOf(void* comp) { return static_cast<DropComponent*>(comp)->ball; }

    void Spin(void* comp) {
        void* px = PhysxActor(comp);
        if (px == nullptr) return;
        float axis = exl::util::GetRandomF32(0, kTwoPi), rate = exl::util::GetRandomF32(kSpinMin, kSpinMax);
        Game::Vec3 spin{std::cos(axis) * rate, 0, std::sin(axis) * rate};
        Game::CallVirtual<void(const Game::Vec3*, bool)>(px, kSetAngular, &spin, true);
    }

    // A spot `out` away from `from`, up to 45 degrees either side of the way toward `ahead`, at the height of `ahead`
    Game::Vec3 Ahead(const Game::Vec3& from, const Game::Vec3& ahead, float out) {
        float turn = std::atan2(ahead.z - from.z, ahead.x - from.x) + exl::util::GetRandomF32(-kQuarterPi, kQuarterPi);
        return {from.x + std::cos(turn) * out, ahead.y, from.z + std::sin(turn) * out};
    }

    // A ball's launch: a dungeon key or golden leaf falls from kFallHeight above its spawn; a tree's or hole's comes on
    // an arc onto a spot rolled around the seashell's, a chest's onto a spot rolled in front of the chest; any other
    // leaves the ground just above its spawn up and outward in a random direction. Each spins.
    void BallFall(void* comp, Actor* actor, int motion) {
        const Game::Vec4& spawn = actor->spawnPos;
        Arc arc;
        if (motion == AP::DROP_FALLS || motion == AP::DROP_REFALLS) {
            Game::Vec3 start{spawn.x, spawn.y + kFallHeight, spawn.z};
            Game::Node_SetWorldPos(actor->root, &start);
        } else if ((motion == AP::DROP_TREE || motion == AP::DROP_HOLE) && ArcOf(*actor, motion, &arc)) {
            float far = std::hypot(arc.to.x - arc.start.x, arc.to.z - arc.start.z);
            Game::Vec3 to = Ahead(arc.start, arc.to, far * exl::util::GetRandomF32(kArcNear, kArcFar));
            Launch(comp, actor, arc.start, LaunchVelocity(arc.speed, arc.start, to));
        } else if (Launched(motion) && ArcOf(*actor, motion, &arc)) {
            float out = exl::util::GetRandomF32(kChestNear, kChestFar);
            Game::Vec3 to = Ahead(arc.start, arc.to, out);
            Game::Vec3 velocity = LaunchVelocity(arc.speed, arc.start, to);
            Launch(comp, actor, arc.start, velocity);
            reinterpret_cast<ItemActor*>(actor)->touchDelay = out / std::hypot(velocity.x, velocity.z);
            BallOf(comp).opened = motion == AP::DROP_CHEST;
        } else {
            float angle = exl::util::GetRandomF32(0, kTwoPi), out = exl::util::GetRandomF32(kSpeedOutMin, kSpeedOutMax);
            Launch(comp, actor, {spawn.x, spawn.y + kRise, spawn.z},
                   {std::cos(angle) * out, exl::util::GetRandomF32(kSpeedUpMin, kSpeedUpMax), std::sin(angle) * out});
        }
        Spin(comp);
    }

    // The Fall: the small key's makes the body dynamic and still, 10 above the spawn, which a dungeon key's and a
    // golden leaf's keep; then the drop hops out of the ground just above its spawn, or a tree's or hole's leaves the
    // bonked tree's crown or the bombed hole on the arc that lands on the seashell's landing spot; with Bouncy Items a
    // ball's (BallFall). A boss's leaves its spawn, where the boss's item would start, as the boss launches the item;
    // a ball spins as any. A free-standing one (the ghost's reward) stays at its spawn. Gravity does the rest. Without
    // the body yet, it runs again once the body is made.
    void Fall(void* comp) {
        Game::SmallKey_StartFall(comp);
        if (!Item(comp)->bodyReady) return;
        Actor* actor = Owner(comp);
        if (actor == nullptr || actor->root == nullptr) return;
#ifdef LASAP_TEST
        g_Last = {actor, actor->entity.self.ctrl};
#endif
        int motion = Motion(*actor);
        if (Boss(motion)) {
            const Game::Vec4& spawn = actor->spawnPos;
            Launch(comp, actor, {spawn.x, spawn.y, spawn.z}, g_BossLaunch);
            if (Bouncy()) Spin(comp);
            return;
        }
        if (motion == AP::DROP_STANDS || motion == AP::DROP_PLACED) {
            const Game::Vec4& spawn = actor->spawnPos;
            Launch(comp, actor, {spawn.x, spawn.y, spawn.z}, {0, 0, 0});
            return;
        }
        if (Bouncy()) {
            BallFall(comp, actor, motion);
            return;
        }
        if (motion == AP::DROP_FALLS || motion == AP::DROP_REFALLS) return;
        Arc arc;
        if ((motion == AP::DROP_TREE || motion == AP::DROP_HOLE) && ArcOf(*actor, motion, &arc)) {
            Launch(comp, actor, arc.start, LaunchVelocity(arc.speed, arc.start, arc.to));
            return;
        }
        const Game::Vec4& spawn = actor->spawnPos;
        Launch(comp, actor, {spawn.x, spawn.y + kHopRise, spawn.z}, {0, kHopUp, 0});
    }

    // ---- The ball (Bouncy Items) ----

    float WaterLift(void*) { return kLift; }
    float WaterDamping(void*) { return kDamping; }

    // The bounce off a wall PhysX left out: `was` the velocity at the last update, `now` this one's, `step` the time
    // between them and `drag` the linear damping in it
    void WallBounce(void* comp, const Game::Vec3& was, Game::Vec3* now, float step, float drag) {
        float dx = now->x - was.x, dz = now->z - was.z;
        float lost = std::hypot(dx, dz);
        float damped = std::hypot(was.x, was.z) * (1 - std::exp(-drag * step));
        if (lost < damped + kImpact * step || dx * was.x + dz * was.z >= 0 ||
            now->y > was.y - kGravity * kFallingShare * step) {
            return;
        }
        float nx = dx / lost, nz = dz / lost;
        float back = kWallBounce * -(was.x * nx + was.z * nz) - (now->x * nx + now->z * nz);
        if (back <= 0) return;
        now->x += nx * back;
        now->z += nz * back;
        Game::CallVirtual<void(const Game::Vec3*)>(comp, kSetVelocity, now);
    }

    // The item update, then the ball's: its chest's opened flag off again, a wall's bounce, and the damping on the
    // ground or in the air. The chest's opened flag, which its opening set, goes off at the first update after the
    // launch, once no event runs: during its event the chest, still opening, would close again with it off; afterwards
    // it stays open until its room loads again, closed while the item was not taken.
    void ItemUpdate(void* comp) {
        g_ItemUpdate(comp);
        Ball& ball = BallOf(comp);
        Actor* actor = Owner(comp);
        if (ball.opened && actor != nullptr) {
            SetSwitch(*actor, AP::DropSwitch::Opened, false);
            ball.opened = 0;
        }
        void* px = PhysxActor(comp);
        if (px == nullptr) return;
        u64 tick = svcGetSystemTick();
        float step = ball.tick != 0 ? static_cast<float>(tick - ball.tick) / kTicksPerSecond : kMaxStep + 1;
        ball.tick = tick;
        PxVec3 v = Game::CallVirtual<PxVec3()>(px, kGetLinear);
        Game::Vec3 now{v.x, v.y, v.z};
        bool water = WaterOf(comp) != nullptr && WaterOf(comp)->inWater;
        bool ground = ball.touches >= kTouching;
        if (!water && step <= kMaxStep) {
            float drag = ground ? kDragGround : Game::CallVirtual<float()>(comp, kTraitsDrag);
            WallBounce(comp, ball.velocity, &now, step, drag);
            bool falling = now.y < ball.velocity.y + kGravity * kFallingShare * step;
            ball.touches = falling ? 0 : ball.touches < kTouching ? ball.touches + 1 : kTouching;
        }
        ball.velocity = now;
        if (water) return;
        ground = ball.touches >= kTouching;
        Game::CallVirtual<void(float)>(px, kSetAngularDamping, ground ? kSpinDampGround : kSpinDampAir);
        float drag = ground ? kDragGround : Game::CallVirtual<float()>(comp, kTraitsDrag);
        Game::CallVirtual<void(float)>(px, kSetLinearDamping, drag);
    }

    // ---- The component's virtuals ----

    bool ComponentIsA(const void* self, const void* type) {
        return type == &g_ComponentType || Game::ItemComponent_IsA(self, type);
    }
    const void* ComponentTypeId(const void*) { return &g_ComponentType; }

    // A buried drop pops when dug up at a dig spot of the scene's dig map, as ItemSlimeKey; any other at once
    bool CheckPop(void* comp) {
        Actor* actor = Owner(comp);
        if (actor != nullptr && Motion(*actor) == AP::DROP_BURIED && Game::Item_OnDigSpot(comp)) {
            return Game::Item_DugUp(comp);
        }
        return Game::NpcComponent_CheckPop(comp);
    }

    // Once its pop check holds: a buried drop appears, and hops out unless it was dug up before the room loaded; a
    // boss's whose item falls into a pit never; any other only while its switch 0 is on (its pop event activates it
    // when the switch turns on)
    void Appear(void* comp, bool quiet) {
        Actor* actor = Owner(comp);
        if (actor == nullptr) return;
        int motion = Motion(*actor);
        if (motion == AP::DROP_BURIED) {
            Game::NpcComponent_Appear(comp);
            if (!quiet) Fall(comp);
        } else if (motion != AP::DROP_BOSS_PIT && SwitchOn(*actor, AP::DropSwitch::Pop)) {
            Game::NpcComponent_Appear(comp);
        }
    }

    bool No(const void*) { return false; }
    bool Yes(const void*) { return true; }

    // A body signal's slot: its argument (a shared_ptr, moved in) released
    void BodyMade(ComponentCall* self, SharedPtr<>* arg) {
        Shared<>::Adopt(*arg);
        ItemComponent* item = Item(self->comp);
        item->bodyReady = true;
        if (item->fallPending) {
            item->fallPending = false;
            Fall(self->comp);
        }
    }

    void BodyLost(ComponentCall* self, SharedPtr<>* arg) {
        Shared<>::Adopt(*arg);
        Item(self->comp)->bodyReady = false;
    }

    void Connect(void* signal, const uintptr_t* vtable, void* comp) {
        if (signal == nullptr) return;
        Function slot = ComponentFunction(vtable, comp);
        Game::CallVirtual<void(Function*)>(signal, kSignalConnect, &slot);
        DestroyFunction(&slot);
    }

    // The collision (x8), whose body's signals tell the drop when its body is there to fall
    Shared<Game::Entity> MakeCollision(void* comp, const StrView* name, void* a2, void* a3) {
        Shared<Game::Entity> out = Game::NpcComponent_MakeCollision(comp, name, a2, a3);
        // none for a row without collision.shape (the maker's null cases)
        if (!out) return out;
        if (!out->flags.Has(EntityFlag::DestroyRequested) &&
            Game::CallVirtual<bool(const void*)>(out.get(), kIsA, Game::BodyCollision_TypeId.Ptr())) {
            auto* body = reinterpret_cast<Game::Actors::BodyCollision*>(out.get());
            Connect(body->bodyMade.owner, g_BodyMadeVtable, comp);
            Connect(body->bodyLost.owner, g_BodyLostVtable, comp);
        }
        return out;
    }

    // The EventChara's action handler, wrapped: the drop's Fall, then the item base's actions and the generic ones
    struct alignas(16) Actions {
        const uintptr_t* vtable;
        u8 pad_8[0x8];
        void* comp;                // 0x10
        u8 pad_18[0x8];
        Function inner;            // 0x20: the handler it wraps
    };
    static_assert(offsetof(Actions, inner) == 0x20 && sizeof(Actions) == 0x50);

    void ActionsDestroy(Actions* self) { DestroyFunction(&self->inner); }
    void ActionsDestroyFree(Actions* self) {
        DestroyFunction(&self->inner);
        Game::operator_delete(self);
    }
    void ActionsCloneInto(const Actions* self, Actions* to) {
        to->vtable = self->vtable;
        to->comp = self->comp;
        CopyFunction(self->inner, &to->inner);
    }
    void* ActionsClone(const Actions* self) {
        auto* copy = static_cast<Actions*>(Game::operator_new(sizeof(Actions)));
        ActionsCloneInto(self, copy);
        return copy;
    }

    // The Fall action's step, called once: it makes the fall and returns no waiter ({0, 0}), so the action is done at
    // once (its non-trivial destructor returns it through x8)
    struct StepResult {
        u64 a = 0, b = 0;
        ~StepResult() {}
    };
    StepResult FallStep(ComponentCall* self) {
        Fall(self->comp);
        return {};
    }

    bool Act(Actions* self, Function** step, const StrView* const* name) {
        if (self->inner.f != nullptr) {
            Function* handed = *step;
            if (Game::CallVirtual<bool(Function**, const StrView* const*)>(self->inner.f, kFuncCall * 8, &handed, name)) {
                return true;
            }
        }
        if ((*name)->View() != "Fall") return false;
        Function fall = ComponentFunction(g_FallStepVtable, self->comp);
        Game::Function_Swap(&fall, *step);
        DestroyFunction(&fall);
        return true;
    }

    Shared<Game::Event::EventChara> MakeEventChara(void* comp, const StrView* name, void* a2, void* a3) {
        Shared<Game::Event::EventChara> out = Game::ItemNpcComponent_MakeEventChara(comp, name, a2, a3);
        // none when the drop is destroyed or already has one (the maker's null cases)
        if (!out) return out;
        if (!out->entity.flags.Has(EntityFlag::DestroyRequested)) {
            auto& handler = out->actionLookup;
            auto* actions = static_cast<Actions*>(Game::operator_new(sizeof(Actions)));
            std::memset(actions, 0, sizeof(Actions));
            actions->vtable = g_ActionsVtable;
            actions->comp = comp;
            CopyFunction(handler, &actions->inner);
            DestroyFunction(&handler);
            handler.f = actions;
        }
        return out;
    }

    // ---- The actor's virtuals ----

    bool ActorIsA(const void* self, const void* type) {
        return type == &g_ActorType || Game::ItemActor_IsA(self, type);
    }
    const void* ActorTypeId(const void*) { return &g_ActorType; }

    // A dungeon key that falls again whenever Link enters its zone (the small key's ReFall) does once its switch is on
    void ZoneEnter(Actor* actor) {
        Game::ItemActor_ZoneEnter(actor);
        auto* comp = reinterpret_cast<Game::Entity*>(reinterpret_cast<ItemActor*>(actor)->component);
        if (comp != nullptr && !comp->Gone() && Motion(*actor) == AP::DROP_REFALLS &&
            SwitchOn(*actor, AP::DropSwitch::Pop)) {
            Fall(comp);
        }
    }

    // ---- Making it ----

    void CtrlNothing(void*) {}
    void CtrlFree(void* ctrl) { Game::operator_delete(ctrl); }
    void* CtrlNoDeleter(const void*, const void*) { return nullptr; }
    void ActorGone(ActorBlock* block) { Game::CallVirtual<void()>(&block->actor, kActorDtor); }
    void ComponentGone(Game::SharedPointerBlock* block) { Game::CallVirtual<void()>(block->ptr, kComponentDelete); }

    template<typename Block>
    void SetCtrlVtable(uintptr_t (&vtable)[Game::kCtrlSlots], void (*zeroShared)(Block*)) {
        vtable[Game::kCtrlDtor] = reinterpret_cast<uintptr_t>(&CtrlNothing);
        vtable[Game::kCtrlDelete] = vtable[Game::kCtrlZeroWeak] = reinterpret_cast<uintptr_t>(&CtrlFree);
        vtable[Game::kCtrlZeroShared] = reinterpret_cast<uintptr_t>(zeroShared);
        vtable[Game::kCtrlDeleter] = reinterpret_cast<uintptr_t>(&CtrlNoDeleter);
    }

    // The component in a shared_ptr of its own, which its self pointer holds weakly
    Shared<> Share(void* comp) {
        auto* ctrl = static_cast<Game::SharedPointerBlock*>(Game::operator_new(sizeof(Game::SharedPointerBlock)));
        *ctrl = {{g_ComponentCtrlVtable, 0, 1}, comp};
        static_cast<Game::Entity*>(comp)->self = {comp, ctrl};
        return Shared<>::Adopt(comp, ctrl);
    }

    // The actor's NPC component (its vtable +0x128, as an item class's maker), with the drop actor's row.
    Shared<> MakeComponent(Actor* actor, StrView* name) {
        Game::Entity& owner = actor->entity;
        if (owner.flags.Has(EntityFlag::Destroyed) || name->size - 1 > 30 ||
            Game::Actor_HasComponent(actor, name->data, name->size)) {
            return {};
        }
        Shared<> self = Shared<>::Lock(owner.self);
        Game::ComponentName args{name->data, name->size, 1, self.get(), self.View().ctrl};
        void* comp = Game::operator_new(sizeof(DropComponent));
        static_cast<DropComponent*>(comp)->ball = {};
        StrView key = RowKey();
        Game::ItemNpcComponent_Ctor(comp, &args, &key);
        self = {};
        *static_cast<uintptr_t**>(comp) = g_ComponentVtable;
        Item(comp)->bodyReady = Item(comp)->fallPending = false;
        Shared<> sp = Share(comp);
        auto* entity = static_cast<Game::Entity*>(comp);
        if (!entity->flags.Any(EntityFlag::DestroyRequested | EntityFlag::Initialized)) {
            Game::CallVirtual<void()>(comp, Game::EntityVirtual::kRegister);
            Game::CallVirtual<void()>(comp, Game::EntityVirtual::kSetup);
            if (!entity->flags.Has(EntityFlag::DestroyRequested)) entity->flags.Set(EntityFlag::Initialized);
        }
        bool wasActive = entity->Active();
        entity->flags.Set(EntityFlag::ParentActive, owner.Active());
        if (entity->Active() != wasActive) Game::CallVirtual<void()>(comp, Game::EntityVirtual::kOnActiveChanged);
        Game::EntityManager_PostStart(*Game::EntityManager_ptr, sp.Untyped());
        if (owner.flags.Has(EntityFlag::DestroyRequested) || !Game::Entity_AttachChild(actor, comp)) {
            Game::Entity_Destroy(comp);
            return {};
        }
        return sp;
    }

    // The actor in a make_shared block of an item class's layout, with its own vtable and self pointer.
    Shared<Actor> Construct(SharedPtr<>* owner, Game::ActorParams* params, const char* name, u64 length) {
        Game::ActorArgs args{};
        args.name = name;
        args.length = length;
        args.enabled = 1;
        args.owner = owner->ptr;
        args.ownerCtrl = owner->ctrl;
        if (owner->ctrl != nullptr) Game::AddOwner(owner->ctrl);  // released with the args
        Game::ActorParams_Copy(&args.params, params);
        auto* mem = static_cast<ActorBlock*>(Game::operator_new(sizeof(ActorBlock)));
        std::memset(static_cast<void*>(mem), 0, sizeof(ActorBlock));
        mem->ctrl.vtable = g_ActorCtrlVtable;  // shared and weak owners - 1: 0
        auto* actor = reinterpret_cast<Actor*>(&mem->actor);
        StrView key = RowKey();
        Game::ItemActor_Ctor(actor, &args, &key, kItemId, 1, PosesOf(params->lebKey));
        actor->entity.vtable = g_ActorVtable;
        if (actor->entity.self.ctrl == nullptr) {
            actor->entity.self = {actor, mem};
            mem->ctrl.weakOwners = 1;  // and the self pointer's weak reference
        }
        Game::ActorArgs_Dtor(&args);
        return Shared<Actor>::Adopt(actor, mem);
    }

    // The boss drop's boss: the boss whose LEB key its first link names
    bool OfBoss(const Actor& drop, u64 boss) {
        const Game::LebActorRecord* target = LinkedRecord(drop);
        return target != nullptr && target->key == boss;
    }

    // A boss's drop being made: the boss's item's spot, when the boss dropped it before (its room loading while the
    // drop was not taken), is its spawn, where it stands, switch 0 on (also when the boss dropped it the first time)
    void MadeBossDrop(Actor* actor) {
        // a slot whose drop is gone, else the one after the last made
        int slot = (g_BossLast + 1) % kBossSlots;
        for (int i = 0; i < kBossSlots; i++) {
            if (!g_Bosses[i].Alive()) {
                slot = i;
                break;
            }
        }
        g_Bosses[slot] = {actor, actor->entity.self.ctrl};
        g_BossLast = slot;
        if (g_Heart.boss == 0 || g_Heart.load != LevelSystem::LoadCount() || !OfBoss(*actor, g_Heart.boss)) return;
        actor->spawnPos = {g_Heart.at.x, g_Heart.at.y, g_Heart.at.z, actor->spawnPos.t};
        g_Heart = {};
        if (!SwitchOn(*actor, AP::DropSwitch::Got)) {
            SetSwitch(*actor, AP::DropSwitch::Pop, true);
        }
        Logging.Log("[AP] drop %016llx: at its boss's item", actor->record->key);
    }

    // A chest's or owl statue's drop made while its launch flag (switch 0, a level flag) is on: its room loaded again
    // within the level (an overworld room), and its chest is made anew, open only when the item was taken. The flag
    // goes off before the drop's layout reads it, so no ball waits at its spawn (a drop whose switch 0 is on when it is
    // made appears there), and opening a closed chest or examining the statue launches it again.
    void Unlaunch(const Actor& actor) {
        if (!Launched(Motion(actor)) || !SwitchOn(actor, AP::DropSwitch::Pop)) return;
        SetSwitch(actor, AP::DropSwitch::Pop, false);
        Logging.Log("[AP] drop %016llx: launched again from its chest or statue", actor.record->key);
    }

    // A drop that stands from the start (the blue rupees), made and not taken: its switch 0 (a level flag of the
    // drops' own) goes on before its layout reads it, so it appears where it stands.
    void Place(const Actor& actor) {
        if (Motion(actor) != AP::DROP_PLACED || SwitchOn(actor, AP::DropSwitch::Got)) return;
        SetSwitch(actor, AP::DropSwitch::Pop, true);
    }

    // The actor table's factory for the drop actor, as an item class's: named, set up, started and made the owner's
    // child; null when the owner has a child of that name already.
    Shared<Actor> Create(SharedPtr<>* owner, Game::ActorParams* params) {
        Shared<Actor> out;
        Game::SharedString name{};
        Game::ActorName_Make(&name, params);
        auto* container = static_cast<u8*>(owner->ptr);
        u64 length = name.length;
        bool taken = length - 1 > 30;
        if (!taken) {
            if (Game::Entity_FindChildInMap(container + 0x10, name.data, length) != nullptr) {
                taken = Game::Entity_FindChildInMap(container + 0x60, name.data, length) == nullptr;
            } else {
                taken = Game::Entity_FindChildInMap(container + 0x38, name.data, length) != nullptr;
            }
        }
        Shared<Actor> sp = taken ? Shared<Actor>{} : Construct(owner, params, name.data, length);
        Actor* actor = sp.get();
        if (actor != nullptr) Unlaunch(*actor);
        if (actor != nullptr && Boss(Motion(*actor))) MadeBossDrop(actor);
        if (actor != nullptr) Place(*actor);
        if (actor != nullptr && !actor->entity.flags.Has(EntityFlag::DestroyRequested)) {
            Game::Entity& entity = actor->entity;
            if (!entity.flags.Any(EntityFlag::DestroyRequested | EntityFlag::Initialized)) {
                Game::CallVirtual<void()>(actor, Game::EntityVirtual::kRegister);
                Game::CallVirtual<void()>(actor, Game::EntityVirtual::kSetup);
                if (!entity.flags.Has(EntityFlag::DestroyRequested)) entity.flags.Set(EntityFlag::Initialized);
            }
            if (!entity.flags.Has(EntityFlag::DestroyRequested)) {
                Game::Entity_Start(sp.Untyped());
                if (Game::Entity_AddChild(container, sp.Untyped())) {
                    out = std::move(sp);
                } else {
                    Game::Entity_Destroy(actor);
                }
            }
        }
        Game::Release(name.holder);
        return out;
    }

    // A boss's item (its BossItemDrop event, and again when its room loads while the item is not had:
    // Enemy_DropItems with the boss's start and launch) is never made (Enemy_DropItems gives no callback an empty item
    // actor). The boss's drop, made and not taken, moves to where it would start and its switch 0 (a global flag) goes
    // on, so it pops out of the ground there with the item's launch (its row's pop, Fall); a drop not made yet takes
    // the spot as its spawn when it is (MadeBossDrop).
    HOOK_DEFINE_TRAMPOLINE(ItemCreate) {
        static void Callback(SharedPtr<Actor>* out, Actor* source, const Game::Vec4* pos, const Game::Vec3* velocity,
                             AP::ItemId id, s32 index) {
            // one with no LEB actor to name its drop (no boss drop's link) comes as the game's
            if (!BossItem(id) || source == nullptr || source->record == nullptr) {
                Orig(out, source, pos, velocity, id, index);
                return;
            }
            *out = {};
            u64 boss = source->record->key;
            Game::Vec3 at{pos->x, pos->y, pos->z};
            Actor* drop = nullptr;
            for (const Game::Weak<Actor>& made : g_Bosses) {
                Actor* its = made.Peek();
                if (its != nullptr && !its->entity.Gone() && OfBoss(*its, boss)) drop = its;
            }
            if (drop == nullptr) {
                g_Heart = {boss, at, LevelSystem::LoadCount()};
                Logging.Log("[AP] drop: boss %016llx's item %d kept for its drop at %.2f %.2f %.2f", boss,
                            std::to_underlying(id), at.x, at.y, at.z);
                return;
            }
            if (SwitchOn(*drop, AP::DropSwitch::Got)) return;
            g_BossLaunch = *velocity;
            drop->spawnPos = {at.x, at.y, at.z, drop->spawnPos.t};
            if (drop->root != nullptr) Game::Node_SetWorldPos(drop->root, &at);
            SetSwitch(*drop, AP::DropSwitch::Pop, true);
            Logging.Log("[AP] drop %016llx: the boss's item %d at %.2f %.2f %.2f", drop->record->key,
                        std::to_underlying(id), at.x, at.y, at.z);
        }
    };

    template<typename F>
    uintptr_t Slot(F f) { return reinterpret_cast<uintptr_t>(f); }
}

    void InstallDropActor() {
        Game::ActorTableEntry& entry = Game::ActorTable.Ptr()[AP::DROP_ACTOR_ID];
        if (entry.factory != nullptr) {
            Logging.Log("[AP] drop actor: id 0x%x is taken", AP::DROP_ACTOR_ID);
            return;
        }
        std::memcpy(g_ActorVtable, Game::ItemActor_vtable.Ptr(), sizeof(g_ActorVtable));
        g_ActorVtable[kIsA / 8] = Slot(&ActorIsA);
        g_ActorVtable[kTypeId / 8] = Slot(&ActorTypeId);
        g_ActorVtable[kZoneEnter / 8] = Slot(&ZoneEnter);
        g_ActorVtable[kMakeComponent / 8] = Slot(&MakeComponent);
        std::memcpy(g_ComponentVtable, Game::ItemComponent_vtable.Ptr(), sizeof(g_ComponentVtable));
        g_ComponentVtable[kIsA / 8] = Slot(&ComponentIsA);
        g_ComponentVtable[kTypeId / 8] = Slot(&ComponentTypeId);
        g_ComponentVtable[kCheckPop / 8] = Slot(&CheckPop);
        g_ComponentVtable[kAppear / 8] = Slot(&Appear);
        g_ComponentVtable[kMakeCollision / 8] = Slot(&MakeCollision);
        g_ComponentVtable[kMakeEventChara / 8] = Slot(&MakeEventChara);
        g_ComponentVtable[kHidesOwned / 8] = g_ComponentVtable[kSleeps / 8] = Slot(&No);
        g_ComponentVtable[kImpulses / 8] = g_ComponentVtable[kFrozen / 8] = Slot(&Yes);
        if (Bouncy()) {
            g_ItemUpdate = reinterpret_cast<void (*)(void*)>(g_ComponentVtable[kItemUpdate / 8]);
            g_ComponentVtable[kItemUpdate / 8] = Slot(&ItemUpdate);
            g_ComponentVtable[kWaterLift / 8] = Slot(&WaterLift);
            g_ComponentVtable[kWaterDamping / 8] = Slot(&WaterDamping);
        }
        SetCtrlVtable(g_ActorCtrlVtable, &ActorGone);
        SetCtrlVtable(g_ComponentCtrlVtable, &ComponentGone);
        SetCallVtable(g_BodyMadeVtable, &BodyMade);
        SetCallVtable(g_BodyLostVtable, &BodyLost);
        SetCallVtable(g_FallStepVtable, &FallStep);
        g_ActionsVtable[kFuncDtor] = g_ActionsVtable[kFuncDestroy] = Slot(&ActionsDestroy);
        g_ActionsVtable[kFuncDelete] = g_ActionsVtable[kFuncDestroyFree] = Slot(&ActionsDestroyFree);
        g_ActionsVtable[kFuncClone] = Slot(&ActionsClone);
        g_ActionsVtable[kFuncCloneInto] = Slot(&ActionsCloneInto);
        g_ActionsVtable[kFuncCall] = Slot(&Act);
        LoadDrops();
        ItemCreate::InstallAtFuncPtr(Game::ItemActor_Create.Ptr());
        // Its resources (model, tailor, flowchart) come from its Npc row, as an item class's
        entry = Game::ActorTableEntry{};
        entry.factory = &Create;
        entry.loadGroup = 0xc8;
        entry.priority = 0x11;
        entry.name = AP::DROP_ACTOR;
    }

#ifdef LASAP_TEST
    std::string DropState() {
        Actor* actor = g_Last.Peek();
        if (actor == nullptr || actor->entity.Gone() || actor->root == nullptr) return " -";
        const Game::Vec3& p = actor->root->coords.pos;
        void* comp = reinterpret_cast<ItemActor*>(actor)->component;
        Game::Actors::WaterEvent* water = comp != nullptr ? WaterOf(comp) : nullptr;
        char text[160];
        int n = snprintf(text, sizeof(text), " %.2f,%.2f,%.2f active=%d water=", p.x, p.y, p.z,
                         actor->entity.Active() ? 1 : 0);
        if (water != nullptr && water->inWater) {
            n += snprintf(text + n, sizeof(text) - n, "%.2f", water->surface);
        } else {
            n += snprintf(text + n, sizeof(text) - n, "-");
        }
        void* px = comp != nullptr ? PhysxActor(comp) : nullptr;
        if (px != nullptr) {
            PxVec3 v = Game::CallVirtual<PxVec3()>(px, kGetLinear);
            PxVec3 w = Game::CallVirtual<PxVec3()>(px, kGetAngular);
            snprintf(text + n, sizeof(text) - n, " v=%.2f,%.2f,%.2f spin=%.2f ground=%d spindamp=%.2f", v.x, v.y, v.z,
                     std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z), BallOf(comp).touches >= kTouching ? 1 : 0,
                     Game::CallVirtual<float()>(px, kGetAngularDamping));
        }
        return text;
    }
#endif
}
