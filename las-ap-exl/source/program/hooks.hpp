#pragma once
// The game hooks by area, each in its own file; main.cpp installs them in order.
#include "lib.hpp"

namespace EventFlags { void Install(); }    // game/hooks/eventflags.cpp: the map tracker's flags, the goal
namespace Input { void Install(); }         // game/hooks/input.cpp: each controller poll ticks the Archipelago bridge
namespace Inventory { void Install(); }     // game/hooks/inventory.cpp: the module's handling of each item
namespace LevelSystem { void Install(); }   // game/hooks/level.cpp: the current level
namespace PlayReport { void Install(); }    // game/hooks/playreport.cpp: no telemetry
namespace Fishing { void Install(); }       // patches/fishing.cpp: the pond's bottle
namespace Seashells { void Install(); }     // patches/seashells.cpp: trees, pots and bombed seashells
namespace EventPatcher { void Install(); }  // patches/eventpatcher.cpp: CompareInt for LASR's flows
namespace Enemies { void Install(); }       // patches/enemies.cpp: Enemy Randomizer and Enemy Sizes
namespace Controls { void Install(); }      // mods/controls.cpp: movement speeds, 360 Movement
namespace Fixes { void Install(); }         // mods/fixes.cpp: fixes of the vanilla game
namespace Tweaks { void Install(); }        // mods/tweaks.cpp: config.ini [tweaks]
