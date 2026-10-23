#pragma once
// Multiplayer ghosts, the network half (ghost_net.cpp, DESIGN.md "Ghosts"): POS lines to the client, GHOST lines
// from it.
#include "lib.hpp"
#include <string>

namespace AP::GhostNet {
    // Ghosts the client shows at once; GHOST lines name a slot 0..SLOTS-1.
    constexpr int SLOTS = 30;
    // The last GHOST lines of a slot are kept: the puppet replays them on the sender's timeline.
    constexpr int HISTORY = 16;
    // Link's model's shapes: bit i draws shape i (his body and the items on it; the game shows the items he holds
    // this way). SHAPES_DEFAULT: the body, the first sword and shield.
    constexpr int SHAPE_COUNT = 20;
    constexpr u32 SHAPES_DEFAULT = 0x32bf8;
    constexpr int TUNIC_GREEN = 0, TUNIC_RED = 1, TUNIC_BLUE = 2;
    // How a player looks besides his animations: the shapes drawn, his tunic and the frames of his face's material
    // animations ("eye": 0 open, 1 closed, ...; "mouth"; "ear"). All frames 0: the face as the model draws without
    // them.
    struct Look {
        u32 shapes = SHAPES_DEFAULT;
        int tunic = TUNIC_GREEN;
        int eye = 0, mouth = 0, ear = 0;
        bool operator==(const Look&) const = default;
    };
    struct Frame {
        s64 timeMs;        // the sender's clock, unwrapped
        u64 receivedMs;    // this system's counter when the line arrived
        float x, y, z, yaw;
        int anim, animFrame;
        int upperAnim;     // the animation on his upper body, as anim
        int flags;
        Look look;
    };
    // Another player in this area, as the last GHOST line for its slot gave it.
    struct Ghost {
        bool active = false;  // false: nobody in this slot (GHOST <n> off, GHOSTS 0, no client)
        std::string name;     // the player's slot name
        std::string level;    // the level the player is in; it can lag behind a level change of this player
        float x = 0, y = 0, z = 0;
        float yaw = 0;          // degrees, 0 = +z (the bottom of the screen), 90 = +x
        int anim = 0;           // Link's animation: its PlayerAnimation.gsheet key + 1 (0: none)
        int animFrame = 0;      // its frame, in tenths
        int upperAnim = 0;      // the animation on his upper body (from the bone spine_a up: idle_carry while he holds
                                // something over his head), as anim
        int flags = 0;          // 1: warped (snap, do not move there), 2: hidden, 4: still since the previous frame,
                                // 8: an animation started since the previous line
        Look look;
        u64 receivedMs = 0;     // when the line arrived (system counter, ms)
        s64 timeMs = 0;         // the sender's clock when it was taken (ms, unwrapped)
        u32 timelineFrom = 0;   // the first line of the sender's clock as unwrapped now (a new one after an off)
        u32 frames = 0;         // GHOST lines for this slot so far
        Frame history[HISTORY]; // the last lines, the newest at history[(frames - 1) % HISTORY]
    };
    // Once per tick: sends Link's position while the client asks for it.
    void Tick();
    // Link's animations as POS sends them: anim id (0: none), frame in tenths, animations started so far, and the
    // anim id on his upper body.
    struct Anim { int anim = 0, frame = 0; u32 starts = 0; int upper = 0; };
    Anim LinkAnim();
    // Link's look as POS sends it.
    Look LinkLook();
    // The name of an anim id (PlayerAnimation.gsheet's fileName, the FSKA in Link's model), or nullptr.
    const char* AnimName(int anim);
    // Whether that animation loops (its row's policy).
    bool AnimLoops(int anim);
    // A new client connection: no positions until it asks, no ghosts, name tags on.
    void Reset();
    // Whether the client wants name tags over the ghosts (GHOSTNAMES).
    bool ShowNames();
    // The d-pad's up in free play: name tags on or off while ghosts are on, with a toast; the client keeps the setting
    // (GHOSTNAMES to the client).
    void ToggleNames();
    // A GHOSTS, GHOSTNAMES or GHOST line from the client.
    void HandleLine(const std::string& line);
    const Ghost& Get(int n);
#ifdef LASAP_TEST
    // DBG ghostnet: what was sent and what each slot holds.
    std::string State();
#endif
}
