// The seashell hunt's egg (DESIGN.md, Goal): the Wind Fish's Egg opens once Link holds the seashells the goal takes,
// without instruments or the Ballad: the game's opening plays when he is at the egg with them, on the seashell that
// completes them there or when he gets there later.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "config/config.hpp"
#include "game/actors/player.hpp"
#include "game/framework.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"

namespace AP::Bridge {
namespace {
    // Switch 0 of Field_01G's egg (ObjWindFishsEgg), of its door and of the Ballad's area there: set, the egg is open.
    constexpr const char* kEggFlag = "Egg_Performance";
    // The egg's opening, which the orchestra plays after the Ballad: the egg cracks and breaks, and it sets kEggFlag.
    // The egg (its LEB key in Field_01G) joins the event, as the Ballad's area brings it into the orchestra.
    constexpr const char* kEggEvent = "event/WindFishsEgg.bfevfl";
    constexpr const char* kEggEntry = "oepn";
    constexpr u64 kEggKey = 0x75000F005CFE1F4C;
    // Link is at the egg: on the top of Mt. Tamaranch in Field_01G, near the egg.
    constexpr float kEggX = 98.25f, kEggZ = 5.25f, kEggRadius = 9.0f, kEggMinY = 30.0f;
    // The opening is requested and has not taken Link yet: no second request until it has, or until he leaves the egg
    // or the level, so one that never started is asked for again when he comes back.
    bool g_Requested = false;

    bool AtEgg() {
        Game::Framework* fmwk = Game::GetFramework();
        if (g_LastLevel != "Field" || fmwk == nullptr || fmwk->player == nullptr ||
            fmwk->player->playerCollision == nullptr) {
            return false;
        }
        const auto& p = fmwk->player->playerCollision->base.node.coordsNew.pos;
        float dx = p.x - kEggX, dz = p.z - kEggZ;
        return p.y >= kEggMinY && dx * dx + dz * dz <= kEggRadius * kEggRadius;
    }

    // The egg is in the level: the opening can take it (RequestEvent would load the flowchart to find it missing).
    bool EggLoaded() {
        Game::Shared<Game::Actor> egg;
        Game::ActorRegistry_FindByKey(egg.Reset(), kEggKey);
        return static_cast<bool>(egg);
    }
}

    void TickEgg() {
        if (global_config.archipelago.seashells_required <= 0) return;
        if (!PlayerReady() || !AtEgg() || !PlayerFree()) {
            // gone from the egg, or the requested opening has taken Link
            g_Requested = false;
            return;
        }
        if (g_Requested || EventFlags::CheckFlag(kEggFlag) || !AP::GoalSeashellsHeld()) return;
        // The opening waits until Link is free at the egg, with no item display starting or showing; it tries again
        // on a later tick while the egg is not loaded.
        if (PlayerCarrying() || LinkDead() || !ItemEventIdle() || !EggLoaded()) return;
        if (RequestEvent(kEggEvent, kEggEntry, {kEggKey})) {  // the request drops: the opening runs on its own
            g_Requested = true;
            Logging.Log("[AP] egg opening requested");
        }
    }
}
