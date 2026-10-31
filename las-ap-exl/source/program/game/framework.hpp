#pragma once
// The framework (Framework_ptr), its scheduler, the game state machine and its scenes, and the camera
// (scripts/re/FRAMEWORK.md).
#include "game/actor.hpp"
#include "game/actors/player.hpp"
#include "game/addr.hpp"
#include "game/stl.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    // GameState::vector_c0's element.
    struct GameStateEntry {
        SharedPtr<> first;         // dropped once its object's u32 +0x78 is 0
        SharedPtr<> second;        // 0x10
    };
    static_assert(sizeof(GameStateEntry) == 0x20);

    // The game state machine (GameStateFactory_Create, vtable 0x1bc4300, task "Root"; 0xe0 bytes;
    // Framework::gameState): a scheduler task whose current scene is a task of its own. SetGameState sets
    // nextSceneId and finishes the scene; GameState_NextScene then starts the next one. Its first 0x81 bytes are the
    // task base (Task_Ctor): the scenes share them.
    struct GameState {
        const uintptr_t* vtable;
        WeakPtr<> weakThis;        // 0x8
        SharedPtr<> parent;        // 0x18: the task that made it; null for the game state
        Signal onFinished;         // 0x28: one-shot, emitted by Finish
        u8 finished;               // 0x80: the scheduler drops a finished task; the main loop ends with the game state
        u8 pad_81[0x3];
        u32 sceneId;               // 0x84: kSceneTitle..kSceneEnding, 0 none
        void* scene;               // 0x88: a shared_ptr; SceneHolder_Lock takes it when it is a level scene
        void* sceneCtrl;           // 0x90
        u32 nextSceneId;           // 0x98: SetGameState's (3 a level too); another finishes the game state
        u8 pad_9c[0x4];
        SharedPtr<> fadePresets;   // 0xa0: the FadePreset.gsheet resource group
        SharedPtr<> globalFlags;   // 0xb0: the GlobalFlags.gsheet resource group
        Vector<GameStateEntry> vector_c0;  // 0xc0
        u8 resetResources;         // 0xd8: the next scene change resets the resource manager first
        u8 startupDone;            // 0xd9: the UI datasheets are loaded
        u8 titleSeen;              // 0xda: a title scene finished; the next starts on the title screen
        u8 pad_db[0x5];
    };
    static_assert(offsetof(GameState, onFinished) == 0x28 && offsetof(GameState, finished) == 0x80);
    static_assert(offsetof(GameState, sceneId) == 0x84 && offsetof(GameState, scene) == 0x88);
    static_assert(offsetof(GameState, nextSceneId) == 0x98 && offsetof(GameState, fadePresets) == 0xa0);
    static_assert(offsetof(GameState, vector_c0) == 0xc0 && offsetof(GameState, titleSeen) == 0xda);
    static_assert(sizeof(GameState) == 0xe0);
    constexpr u32 kSceneTitle = 1, kSceneFileMenus = 2, kSceneLevel = 4, kSceneEnding = 5;

    // A loaded area's room (Room_Ctor; 0x250 bytes; ACTORS.md, Rooms): the container of its actors and its zone.
    struct Room {
        u8 pad_0[0x1b8];
        SharedPtr<> root;          // 0x1b8: its actors' container, the owner the room's Actor_Create calls pass
        u8 pad_1c8[0x78];
        s32 zone;                  // 0x240
        u8 pad_244[0x6];
        u8 aborted;                // 0x24a: a level jump
        u8 pad_24b[0x5];
    };
    static_assert(offsetof(Room, root) == 0x1b8 && offsetof(Room, zone) == 0x240);
    static_assert(offsetof(Room, aborted) == 0x24a && sizeof(Room) == 0x250);

    // The level's room manager (LevelScene::roomManager): a room per loaded area.
    struct RoomManager {
        u8 pad_0[0x210];
        Vector<Room*> rooms;       // 0x210
    };
    static_assert(offsetof(RoomManager, rooms) == 0x210);

    // A level's scene (LevelScene_Ctor, task "Main"; 0x820 bytes; the task base first).
    struct LevelScene {
        u8 task[0x81];             // as GameState's first 0x81 bytes
        u8 pad_81[0xf];
        u8 saveManager[0x480];     // 0x90: SaveManager_RequestAutoSave's
        u8 object_510[0x108];      // 0x510
        SharedPtr<RoomManager> roomManager;  // 0x618
        SharedPtr<> sheets;        // 0x628: the level's datasheets' resource group
        u8 pad_638[0x8];
        u8 buffKind;               // 0x640: 1 PowerUp, 2 DefenceUp
        u8 pad_641[0x3];
        float buffTime;            // 0x644: counted down
        u8 field_648[0x18];        // 0x648
        Vec4 field_660;            // 0x660
        u32 field_670;             // 0x670
        u8 field_674;              // 0x674: cleared by Finish
        u8 pad_675[0xb];
        Vec4 field_680;            // 0x680
        u32 field_690;             // 0x690
        u8 field_694;              // 0x694: cleared by Finish
        u8 pad_695[0xb];
        Signal zoneChanged;        // 0x6a0: Scene_UpdateZone emits it with the room manager's room of the new zone
        Signal roomChanged;        // 0x6f8: (new room, old room); the actors' room change
        // 0x750: (blend 0..1 in s0, room A's data, room B's data) when the zone grid cell's rooms or blend change;
        // DungeonMap_SubscribeZone's outside dungeons and the BGM's zoneSub
        Signal roomBlend;
        Signal activeZone;         // 0x7a8: (entered zone, left zone), Scene_SetActiveZone; Actor_SubscribeZones'
        s32 zone;                  // 0x800: -1 none
        s32 room;                  // 0x804
        s32 roomA;                 // 0x808: the zone grid cell's two rooms (-1 none) and the blend between them
        s32 roomB;                 // 0x80c
        u8 blend;                  // 0x810: 0..255; 0xff none
        u8 jumpFlag;               // 0x811: SetGameState's bool, written with a level jump
        u8 pad_812[0xe];
    };
    static_assert(offsetof(LevelScene, saveManager) == 0x90 && offsetof(LevelScene, roomManager) == 0x618);
    static_assert(offsetof(LevelScene, buffKind) == 0x640 && offsetof(LevelScene, zoneChanged) == 0x6a0);
    static_assert(offsetof(LevelScene, activeZone) == 0x7a8 && offsetof(LevelScene, zone) == 0x800);
    static_assert(offsetof(LevelScene, jumpFlag) == 0x811 && sizeof(LevelScene) == 0x820);

    // A view's frustum (Frustum_Make): its box and six planes.
    struct FrustumPlane {
        Vec4 normal;
        float d;                   // 0x10: -dot(normal, a point of the plane)
        u8 pad_14[0xc];
    };
    static_assert(sizeof(FrustumPlane) == 0x20);
    struct Frustum {
        Vec4 boxMin;
        Vec4 boxMax;               // 0x10
        FrustumPlane planes[6];    // 0x20
        u32 planeCount;            // 0xe0: 6
        u32 field_e4;              // 0xe4: 0
        u8 pad_e8[0x8];
    };
    static_assert(offsetof(Frustum, planes) == 0x20 && offsetof(Frustum, planeCount) == 0xe0);
    static_assert(sizeof(Frustum) == 0xf0);

    // nn::gfx ViewportScissorState (ViewportScissorState_Initialize; 0x48 bytes).
    struct ViewportScissorState {
        void* extra;               // the second and later viewports and scissors
        u32 viewportCount;         // 0x8
        u8 pad_c[0x4];
        float viewport[4];         // 0x10: x, y, width, height
        double depthRange[2];      // 0x20
        s32 scissor[4];            // 0x30
        u8 initialized;            // 0x40
        u8 flags;                  // 0x41: bit 0 scissor enabled
        u8 pad_42[0x6];
    };
    static_assert(offsetof(ViewportScissorState, viewport) == 0x10 && offsetof(ViewportScissorState, scissor) == 0x30);
    static_assert(sizeof(ViewportScissorState) == 0x48);

    // The camera: the renderer's view with id 1 (View_Ctor, vtable 0x1c46db0; 0x350 bytes). View rows (the rotation's
    // rows 0-2, so its columns are the camera's axes; the translation row 3) and projection rows, copied each frame
    // (View_Update) from the game camera.
    struct Camera {
        const uintptr_t* vtable;
        WeakPtr<> self;            // 0x8
        u32 id;                    // 0x18: 1
        u8 pad_1c[0x4];
        float view[16];            // 0x20
        float inverseView[16];     // 0x60
        float projection[16];      // 0xa0
        Frustum frustum;           // 0xe0
        u8 frustumCulling;         // 0x1d0: models are culled against the frustum while set
        u8 pad_1d1[0x3];
        float nearZ;               // 0x1d4
        float farZ;                // 0x1d8
        float aspect;              // 0x1dc
        float fovY;                // 0x1e0
        u8 ownMatrices;            // 0x1e4: the view computes its own (orthographic) matrices
        u8 pad_1e5[0x3];
        s32 width, height;         // 0x1e8: the render target's
        WeakPtr<> source;          // 0x1f0: the game camera
        Vec4 shadowMin;            // 0x200: a followed point plus Framework::shadowMinOffset, each frame
        Vec4 shadowMax;            // 0x210
        float shadowMatrix[16];    // 0x220: from the shadow bounds and the directional light
        float shadowParams[4];     // 0x260
        SharedPtr<> renderObject;  // 0x270
        ViewportScissorState viewport;  // 0x280
        u8 viewportReady;          // 0x2c8
        u8 pad_2c9[0x7];
        u32 bufferIndex;           // 0x2d0: View_Update's
        u32 setupArg;              // 0x2d4
        SharedPtr<> drawRoot;      // 0x2d8
        Vector<void*> drawables;   // 0x2e8
        u8 setUp;                  // 0x300
        u8 active;                 // 0x301: the renderer adds drawables only while set; cleared at the first present
                                   // and while the warp map or the pause menu shows
        u8 pad_302[0x6];
        u32 field_308;             // 0x308
        u8 pad_30c[0x4];
        SharedPtr<> sp_310;        // 0x310
        u64 field_320;             // 0x320
        u64 field_328;             // 0x328
        u32 resizePending;         // 0x330: a bit per buffer whose viewport View_Update rebuilds (View_Resize)
        u8 pad_334[0x4];
        u64 pendingWidth;          // 0x338
        u64 pendingHeight;         // 0x340
        u8 pad_348[0x8];
    };
    static_assert(offsetof(Camera, id) == 0x18 && offsetof(Camera, view) == 0x20);
    static_assert(offsetof(Camera, inverseView) == 0x60 && offsetof(Camera, projection) == 0xa0);
    static_assert(offsetof(Camera, frustum) == 0xe0 && offsetof(Camera, frustumCulling) == 0x1d0);
    static_assert(offsetof(Camera, nearZ) == 0x1d4 && offsetof(Camera, ownMatrices) == 0x1e4);
    static_assert(offsetof(Camera, source) == 0x1f0 && offsetof(Camera, shadowMin) == 0x200);
    static_assert(offsetof(Camera, shadowMatrix) == 0x220 && offsetof(Camera, renderObject) == 0x270);
    static_assert(offsetof(Camera, viewport) == 0x280 && offsetof(Camera, bufferIndex) == 0x2d0);
    static_assert(offsetof(Camera, drawRoot) == 0x2d8 && offsetof(Camera, drawables) == 0x2e8);
    static_assert(offsetof(Camera, setUp) == 0x300 && offsetof(Camera, active) == 0x301);
    static_assert(offsetof(Camera, resizePending) == 0x330 && sizeof(Camera) == 0x350);

    // The framework's task scheduler (Scheduler_Ctor; 0x150 bytes): Task_Post and Delay_Make add to it;
    // Scheduler_Update (update phase 1) runs the tasks by order and drops the finished ones.
    struct Scheduler {
        OrderedMap byOrder;        // a multimap: s32 order to {tag {SharedPtr, StrView name}, task SharedPtr}
        Deque pending;             // 0x18: adds and removes made while it updates
        UnorderedMap tasks;        // 0x48: the live tasks, a set
        u8 mutex[0x20];            // 0x70
        s32 updateDepth;           // 0x90: changes are deferred while above 0
        u8 pad_94[0x4];
        Signal taskAdded;          // 0x98
        Signal taskRemoved;        // 0xf0
        u8 flushing;               // 0x148: while pending is applied
        u8 pad_149[0x7];
    };
    static_assert(offsetof(Scheduler, pending) == 0x18 && offsetof(Scheduler, tasks) == 0x48);
    static_assert(offsetof(Scheduler, mutex) == 0x70 && offsetof(Scheduler, taskAdded) == 0x98);
    static_assert(offsetof(Scheduler, flushing) == 0x148 && sizeof(Scheduler) == 0x150);

    // A queue of phases run in order of priority (PhaseQueue_Ctor; 0xf8 bytes): the frame step, the 14 update phases,
    // the 5 render phases.
    struct PhaseQueue {
        u32 id;
        u8 pad_4[0x4];
        OrderedMap byPriority;     // 0x8: s32 priority to its group of phases
        UnorderedMap phases;       // 0x20
        Deque pending;             // 0x48: changes made while it runs (add, remove, clear, set priority)
        ThreadPool worker;         // 0x78: one thread
        u8 running;                // 0xf0
        u8 pad_f1[0x7];
    };
    static_assert(offsetof(PhaseQueue, phases) == 0x20 && offsetof(PhaseQueue, pending) == 0x48);
    static_assert(offsetof(PhaseQueue, worker) == 0x78 && offsetof(PhaseQueue, running) == 0xf0);
    static_assert(sizeof(PhaseQueue) == 0xf8);

    // The present setup (0x78 bytes; Framework::present): the display sizes for handheld and docked mode.
    struct Present {
        u8 active;
        u8 maxInterval;            // 0x1
        u8 pad_2[0x2];
        s32 handheldIndex;         // 0x4: into handheldSizes
        s32 dockedIndex;           // 0x8: into dockedSizes
        u8 pad_c[0x4];
        Vector<s32> handheldSizes; // 0x10
        Vector<s32> dockedSizes;   // 0x28
        WeakPtr<> renderer;        // 0x40
        u64 field_50;              // 0x50
        Vector<u8> vector_58;      // 0x58
        u64 field_70;              // 0x70
    };
    static_assert(offsetof(Present, handheldSizes) == 0x10 && offsetof(Present, renderer) == 0x40);
    static_assert(sizeof(Present) == 0x78);

    // The framework (Framework_Ctor; 0x6c0 bytes): the main loop's state, the actor system, the game state machine,
    // Link and the renderer. Its shared_ptrs the module reads are typed `T* x; void* xCtrl`.
    struct Framework {
        void* romMount;            // owned
        SharedPtr<> regionResources; // 0x8
        void* heapMemory;          // 0x18
        void* heapMemoryFree;      // 0x20
        const uintptr_t* mainHeapVtable;  // 0x28
        void* mainHeap;            // 0x30: owned
        u64 heapMemorySize;        // 0x38
        u64 lastFrameTick;         // 0x40
        u64 field_48;              // 0x48
        u32 nextFramePhaseId;      // 0x50: 1
        u32 nextUpdatePhaseId;     // 0x54: 14; an update phase added later takes it
        u32 nextRenderPhaseId;     // 0x58: 5
        u8 pad_5c[0x4];
        Scheduler scheduler;       // 0x60
        PhaseQueue frameQueue;     // 0x1b0: the frame step, each pass of the main loop
        PhaseQueue updatePhases;   // 0x2a8
        PhaseQueue renderPhases;   // 0x3a0
        SharedPtr<> actorSystem;   // 0x498: the owner of factory spawns
        GameState* gameState;      // 0x4a8: a shared_ptr; the game state machine: SceneHolder_Lock gives its scene,
                                   // SetGameState changes its state
        void* gameStateCtrl;       // 0x4b0
        Actors::Player* player;    // 0x4b8: a shared_ptr
        void* playerCtrl;          // 0x4c0
        const uintptr_t* resourceListener;  // 0x4c8: an interface
        u32 renderCallbackId;      // 0x4d0
        u32 frameCount;            // 0x4d4: stands still while paused or roomPaused
        u32 presentCount;          // 0x4d8
        u32 lastPresentVsync;      // 0x4dc
        u8 skipRenderFrames;       // 0x4e0: frames whose render phases are skipped
        u8 lastSkip;               // 0x4e1
        u8 minSkip;                // 0x4e2
        u8 maxSkip;                // 0x4e3
        u8 presented;              // 0x4e4
        u8 presentInitDone;        // 0x4e5
        u8 restart;                // 0x4e6: after the main loop, Init runs again
        u8 zoneFlagB5;             // 0x4e7: the current zone's +0xb5
        u64 field_4e8;             // 0x4e8
        Vec4 shadowMinOffset;      // 0x4f0: Camera::shadowMin's offset from the followed point
        Vec4 shadowMaxOffset;      // 0x500
        u8 paused;                 // 0x510: the pause menu is open or a level loads
        u8 roomPaused;             // 0x511: set by Scene_UpdateZone
        u8 pad_512[0x6];
        SharedPtr<> controller;    // 0x518
        void* gfxMemory;           // 0x528
        SharedPtr<> sp_530;        // 0x530
        SharedPtr<> renderer;      // 0x540: the model list InstancedModel_Ctor takes
        SharedPtr<> drawRoot;      // 0x550
        Camera* camera;            // 0x560: a shared_ptr
        void* cameraCtrl;          // 0x568
        SharedPtr<> view2;         // 0x570: the orthographic view, id 2
        SharedPtr<> view3;         // 0x580: a view given the camera's shadow bounds while its active is set
        SharedPtr<> renderEnv;     // 0x590
        u64 field_5a0;             // 0x5a0
        void* physicsHeap;         // 0x5a8: owned, 0x2000000 bytes (likely PhysX's)
        SharedPtr<> physics;       // 0x5b0: the physics system (likely)
        void* playReports[15];     // 0x5c0: owned
        SharedPtr<> systemEvents;  // 0x638
        Present present;           // 0x648
    };
    static_assert(offsetof(Framework, mainHeap) == 0x30 && offsetof(Framework, nextUpdatePhaseId) == 0x54);
    static_assert(offsetof(Framework, scheduler) == 0x60 && offsetof(Framework, frameQueue) == 0x1b0);
    static_assert(offsetof(Framework, renderPhases) == 0x3a0 && offsetof(Framework, actorSystem) == 0x498);
    static_assert(offsetof(Framework, gameState) == 0x4a8 && offsetof(Framework, player) == 0x4b8);
    static_assert(offsetof(Framework, frameCount) == 0x4d4 && offsetof(Framework, skipRenderFrames) == 0x4e0);
    static_assert(offsetof(Framework, shadowMinOffset) == 0x4f0 && offsetof(Framework, paused) == 0x510);
    static_assert(offsetof(Framework, controller) == 0x518 && offsetof(Framework, renderer) == 0x540);
    static_assert(offsetof(Framework, camera) == 0x560 && offsetof(Framework, view3) == 0x580);
    static_assert(offsetof(Framework, physicsHeap) == 0x5a8 && offsetof(Framework, playReports) == 0x5c0);
    static_assert(offsetof(Framework, systemEvents) == 0x638 && offsetof(Framework, present) == 0x648);
    static_assert(sizeof(Framework) == 0x6c0);
    inline Framework* GetFramework() { return *Framework_ptr; }

    // The scene the game state machine runs, by its class (vtable): the intro and title, the file menus (file select,
    // name entry), a level (from its load on), another (the ending), or none between scenes.
    enum class Scene { None, Title, FileMenus, Level, Other };
    constexpr uintptr_t kTitleSceneVtable = 0x1bc49c0, kFileMenusSceneVtable = 0x1bc2740,
                        kLevelSceneVtable = 0x1bc2f90;
    inline Scene SceneOf(const Framework* fmwk) {
        const GameState* state = fmwk != nullptr ? fmwk->gameState : nullptr;
        const uintptr_t* scene = state != nullptr ? static_cast<const uintptr_t*>(state->scene) : nullptr;
        if (scene == nullptr) return Scene::None;
        if (*scene == Addr(kTitleSceneVtable)) return Scene::Title;
        if (*scene == Addr(kFileMenusSceneVtable)) return Scene::FileMenus;
        if (*scene == Addr(kLevelSceneVtable)) return Scene::Level;
        return Scene::Other;
    }

    inline Actors::Player* GetPlayer() {
        Framework* fmwk = GetFramework();
        return fmwk != nullptr ? fmwk->player : nullptr;
    }

    // The camera, or null.
    inline const Camera* GetCamera() {
        Framework* fmwk = GetFramework();
        return fmwk != nullptr ? fmwk->camera : nullptr;
    }
}
