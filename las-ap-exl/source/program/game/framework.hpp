#pragma once
#include "game/actors/player.hpp"
#include "game/stl.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    class GameState;

    // The camera: view rows (the rotation's rows 0-2, so its columns are the camera's axes; the translation row 3) and
    // projection rows.
    struct Camera {
        u8 pad_0[0x20];
        float view[16];            // 0x20
        u8 pad_60[0x40];
        float projection[16];      // 0xa0
    };
    static_assert(offsetof(Camera, view) == 0x20 && offsetof(Camera, projection) == 0xa0);

    struct Framework {
        u8 pad_0[0xb0];
        u8 screen;                 // 0xb0: kScreen*
        u8 pad_b1[0x498 - 0xb1];
        SharedPtr actorSystem;     // 0x498: the owner of factory spawns
        GameState* gameState;      // 0x4a8: the scene's holder (SceneHolder_Lock), SetGameState's machine
        u8 pad_4b0[0x8];
        Actors::Player* player;    // 0x4b8
        u8 pad_4c0[0x14];
        u32 frameCount;            // 0x4d4: stands still while paused
        u8 pad_4d8[0x510 - 0x4d8];
        u8 paused;                 // 0x510: the pause menu is open or a level loads
        u8 pad_511[0x540 - 0x511];
        u8 models[0x20];           // 0x540: the model list (InstancedModel_Ctor)
        Camera* camera;            // 0x560
        u8 pad_568[0x6d0 - 0x568];
        void* intro;               // 0x6d0: set only while the intro plays
    };
    static_assert(offsetof(Framework, screen) == 0xb0 && offsetof(Framework, actorSystem) == 0x498);
    static_assert(offsetof(Framework, gameState) == 0x4a8 && offsetof(Framework, player) == 0x4b8);
    static_assert(offsetof(Framework, frameCount) == 0x4d4 && offsetof(Framework, paused) == 0x510);
    static_assert(offsetof(Framework, models) == 0x540 && offsetof(Framework, camera) == 0x560);
    static_assert(offsetof(Framework, intro) == 0x6d0);
    // The screen: the intro and title, the file menus, a level load, a level.
    constexpr u8 kScreenTitle = 0x02, kScreenFileMenus = 0x05, kScreenLoading = 0x0b, kScreenLevel = 0x17;

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
