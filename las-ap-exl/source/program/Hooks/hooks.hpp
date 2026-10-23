#pragma once
// The game hooks by area, each in its own file; main.cpp installs them in order.
#include "lib.hpp"

namespace EventFlags { void Install(); }    // Common/eventflags.cpp: the map tracker's flags, the goal
namespace Input { void Install(); }         // Common/input.cpp: each controller poll ticks the Archipelago bridge
namespace Inventory { void Install(); }     // Common/inventory.cpp: the module's handling of each item
namespace LevelSystem { void Install(); }   // Common/level.cpp: the current level
namespace PlayReport { void Install(); }    // Common/playreport.cpp: no telemetry
namespace Fishing { void Install(); }       // Randomizer/fishing.cpp: the pond's bottle
namespace Seashells { void Install(); }     // Randomizer/seashells.cpp: trees, pots and bombed seashells
namespace EventPatcher { void Install(); }  // Randomizer/eventpatcher.cpp: CompareInt for LASR's flows
namespace Enemies { void Install(); }       // Randomizer/enemies.cpp: Enemy Randomizer and Enemy Sizes
namespace Controls { void Install(); }      // Mods/controls.cpp: movement speeds, 360 Movement
namespace Fixes { void Install(); }         // Mods/fixes.cpp: fixes of the vanilla game
namespace Tweaks { void Install(); }        // Mods/tweaks.cpp: config.ini [tweaks]
