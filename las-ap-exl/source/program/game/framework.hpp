#pragma once
// The framework (Framework_ptr), its scheduler and the camera (scripts/re/FRAMEWORK.md).
#include "game/actor.hpp"
#include "game/actors/player.hpp"
#include "game/stl.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    class GameState;

    // The camera: the renderer's view with id 1 (View_Ctor, vtable 0x1c46db0; 0x350 bytes). View rows (the rotation's
    // rows 0-2, so its columns are the camera's axes; the translation row 3) and projection rows, copied each frame
    // (View_Update) from the game camera.
    struct Camera {
        const uintptr_t* vtable;
        WeakPtr self;              // 0x8
        u32 id;                    // 0x18: 1
        u8 pad_1c[0x4];
        float view[16];            // 0x20
        float inverseView[16];     // 0x60
        float projection[16];      // 0xa0
        u8 frustum[0xf0];          // 0xe0
        u8 field_1d0;              // 0x1d0
        u8 pad_1d1[0x3];
        float nearZ;               // 0x1d4
        float farZ;                // 0x1d8
        float aspect;              // 0x1dc
        float fovY;                // 0x1e0
        u8 ownMatrices;            // 0x1e4: the view computes its own (orthographic) matrices
        u8 pad_1e5[0x3];
        s32 width, height;         // 0x1e8: the render target's
        WeakPtr source;            // 0x1f0: the game camera
        u8 field_200[0x70];        // 0x200
        SharedPtr renderObject;    // 0x270
        u8 target[0x48];           // 0x280: the render target
        u8 targetMade;             // 0x2c8
        u8 pad_2c9[0x7];
        u32 field_2d0;             // 0x2d0
        u32 setupArg;              // 0x2d4
        SharedPtr drawRoot;        // 0x2d8
        Vector<void*> drawables;   // 0x2e8
        u8 setUp;                  // 0x300
        u8 field_301;              // 0x301: cleared at the first present and by the warp map
        u8 pad_302[0x6];
        u32 field_308;             // 0x308
        u8 pad_30c[0x4];
        SharedPtr sp_310;          // 0x310
        u8 field_320[0x30];        // 0x320
    };
    static_assert(offsetof(Camera, id) == 0x18 && offsetof(Camera, view) == 0x20);
    static_assert(offsetof(Camera, inverseView) == 0x60 && offsetof(Camera, projection) == 0xa0);
    static_assert(offsetof(Camera, nearZ) == 0x1d4 && offsetof(Camera, ownMatrices) == 0x1e4);
    static_assert(offsetof(Camera, source) == 0x1f0 && offsetof(Camera, renderObject) == 0x270);
    static_assert(offsetof(Camera, drawRoot) == 0x2d8 && offsetof(Camera, drawables) == 0x2e8);
    static_assert(offsetof(Camera, setUp) == 0x300 && sizeof(Camera) == 0x350);

    // The framework's task scheduler (Scheduler_Ctor; 0x150 bytes): Task_Post and Delay_Make add to it.
    struct Scheduler {
        u8 byOrder[0x18];          // a std::map
        u8 pad_18[0x30];
        UnorderedMap tasks;        // 0x48: the live tasks, a set
        u8 mutex[0x20];            // 0x70
        u32 field_90;              // 0x90
        u8 pad_94[0x4];
        u8 object_98[0x58];        // 0x98
        u8 object_f0[0x58];        // 0xf0
        u8 field_148;              // 0x148
        u8 pad_149[0x7];
    };
    static_assert(offsetof(Scheduler, tasks) == 0x48 && offsetof(Scheduler, mutex) == 0x70);
    static_assert(sizeof(Scheduler) == 0x150);

    // A queue of phases run in order of priority (PhaseQueue_Ctor; 0xf8 bytes): the frame step, the 14 update phases,
    // the 5 render phases.
    struct PhaseQueue {
        u32 id;
        u8 pad_4[0x4];
        u8 byPriority[0x18];       // 0x8: a std::map
        UnorderedMap phases;       // 0x20
        u8 pad_48[0x30];
        u8 deferred[0x78];         // 0x78: changes made while it runs
        u8 running;                // 0xf0
        u8 pad_f1[0x7];
    };
    static_assert(offsetof(PhaseQueue, phases) == 0x20 && offsetof(PhaseQueue, running) == 0xf0);
    static_assert(sizeof(PhaseQueue) == 0xf8);

    // The framework (Framework_Ctor; 0x6c0 bytes): the main loop's state, the actor system, the game state machine,
    // Link and the renderer. Its shared_ptrs the module reads are typed `T* x; void* xCtrl`.
    struct Framework {
        void* romMount;            // owned
        SharedPtr regionResources; // 0x8
        void* heapMemory;          // 0x18
        void* heapMemoryFree;      // 0x20
        u8 mainHeap[0x10];         // 0x28
        u64 heapMemorySize;        // 0x38
        u64 lastFrameTick;         // 0x40
        u64 field_48;              // 0x48
        u32 field_50;              // 0x50: 1
        u32 field_54;              // 0x54: 14
        u32 field_58;              // 0x58: 5
        u8 pad_5c[0x4];
        Scheduler scheduler;       // 0x60
        PhaseQueue frameQueue;     // 0x1b0: the frame step, each pass of the main loop
        PhaseQueue updatePhases;   // 0x2a8
        PhaseQueue renderPhases;   // 0x3a0
        SharedPtr actorSystem;     // 0x498: the owner of factory spawns
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
        Vec4 field_4f0;            // 0x4f0
        Vec4 field_500;            // 0x500
        u8 paused;                 // 0x510: the pause menu is open or a level loads
        u8 roomPaused;             // 0x511: set by Scene_UpdateZone
        u8 pad_512[0x6];
        SharedPtr controller;      // 0x518
        void* gfxMemory;           // 0x528
        SharedPtr sp_530;          // 0x530
        SharedPtr renderer;        // 0x540: the model list InstancedModel_Ctor takes
        SharedPtr drawRoot;        // 0x550
        Camera* camera;            // 0x560: a shared_ptr
        void* cameraCtrl;          // 0x568
        SharedPtr view2;           // 0x570: the orthographic view, id 2
        SharedPtr sp_580;          // 0x580
        SharedPtr renderEnv;       // 0x590
        u64 field_5a0;             // 0x5a0
        void* field_5a8;           // 0x5a8: owned, 0x2000000 bytes
        SharedPtr sp_5b0;          // 0x5b0
        void* playReports[15];     // 0x5c0: owned
        SharedPtr systemEvents;    // 0x638
        u8 present[0x78];          // 0x648
    };
    static_assert(offsetof(Framework, scheduler) == 0x60 && offsetof(Framework, frameQueue) == 0x1b0);
    static_assert(offsetof(Framework, renderPhases) == 0x3a0 && offsetof(Framework, actorSystem) == 0x498);
    static_assert(offsetof(Framework, gameState) == 0x4a8 && offsetof(Framework, player) == 0x4b8);
    static_assert(offsetof(Framework, frameCount) == 0x4d4 && offsetof(Framework, skipRenderFrames) == 0x4e0);
    static_assert(offsetof(Framework, paused) == 0x510 && offsetof(Framework, controller) == 0x518);
    static_assert(offsetof(Framework, renderer) == 0x540 && offsetof(Framework, camera) == 0x560);
    static_assert(offsetof(Framework, playReports) == 0x5c0 && offsetof(Framework, systemEvents) == 0x638);
    static_assert(sizeof(Framework) == 0x6c0);
    // The screen as the module tells it: the bucket count of the scheduler's live tasks (Scheduler::tasks), which
    // libc++ grows through the primes 2, 5, 11, 23 as the tasks grow: the intro and title, the file menus, a level
    // load, a level.
    constexpr u64 kScreenTitle = 2, kScreenFileMenus = 5, kScreenLoading = 11, kScreenLevel = 23;
    inline u64 ScreenOf(const Framework* fmwk) { return fmwk->scheduler.tasks.bucketCount; }
    // The intro as the module tells it: a pointer 0x10 bytes past the framework's end, in the heap block after it,
    // set only while the intro plays.
    constexpr size_t kIntroProbe = 0x6d0;

    inline Framework* GetFramework() { return *Framework_ptr; }

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
