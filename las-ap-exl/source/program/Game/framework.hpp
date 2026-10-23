#pragma once
#include "lib.hpp"
#include "Game/Actors/player.hpp"
#include "Game/mem.hpp"
#include "Game/sym.hpp"

namespace Game {
    class ActorSystem;
    class GameState;

    struct Framework {
        unsigned char padding_498[0x498];
        ActorSystem* mActorSystem; // 0x498
        unsigned char padding_4a8[0x4a8 - 0x498 - 0x8];
        GameState* mGameState; // 0x4a8
        unsigned char padding_4b8[0x8];
        Actors::Player* mPlayer; // 0x4b8
        unsigned char padding_4d4[0x4d4 - 0x4b8 - 0x8];
        uint32_t mFrameCount; // 0x4d4
    };

    // Framework fields read by offset: the screen (u8: 2 intro and title, 5 file menus, 0x0b level load, 0x17
    // level), paused (u8, 1 while the pause menu is open), the camera.
    constexpr size_t kFrameworkScreen = 0xb0, kFrameworkPaused = 0x510, kFrameworkCamera = 0x560;
    // The camera: view rows at +0x20..+0x50 (the last the translation; the rotation's rows +0x20, +0x30, +0x40, so
    // its columns are the camera's axes), projection rows at +0xa0..+0xd0.
    constexpr size_t kCameraView = 0x20, kCameraProjection = 0xa0;

    inline Framework* GetFramework() { return *Framework_ptr; }

    inline Actors::Player* GetPlayer() {
        Framework* fmwk = GetFramework();
        return fmwk != nullptr ? fmwk->mPlayer : nullptr;
    }

    // The camera, or null.
    inline const u8* GetCamera() { return At<const u8*>(GetFramework(), kFrameworkCamera); }
}
