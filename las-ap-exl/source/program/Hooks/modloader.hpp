#pragma once
#include "Config/config.hpp"
// #include "Hooks/Common/audio.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Hooks/Common/input.hpp"
#include "Hooks/Common/inventory.hpp"
#include "Hooks/Common/level.hpp"
#include "Hooks/Common/playreport.hpp"
#include "Hooks/Randomizer/actorreader.hpp"
// #include "Hooks/Randomizer/companions.hpp"
#include "Hooks/Randomizer/enemies.hpp"
#include "Hooks/Randomizer/eventpatcher.hpp"
#include "Hooks/Randomizer/fishing.hpp"
// #include "Hooks/Randomizer/markers.hpp"
#include "Hooks/Randomizer/seashells.hpp"
#include "Hooks/Mods/controls.hpp"
#include "Hooks/Mods/fixes.hpp"
#include "Hooks/Mods/tweaks.hpp"
// #include "Hooks/Mods/test_mods.hpp"

namespace ModLoader {
    inline void InstallCommonHooks() {
        // AudioSystem::InstallHooks();
        EventFlags::InstallHooks();
        InputSystem::InstallHooks();
        Inventory::InstallHooks();
        LevelSystem::InstallHooks();
        PlayReport::InstallHooks();
    }

    inline void InstallRandomizerHooks() {
        EXL_ASSERT(global_config.initialized);
        if (global_config.randomizer.enabled) {
            // CompanionTweaks::InstallHooks(); // unfinished
            FishingTweaks::InstallHooks();
            // MarkerTweaks::InstallHooks(); // unfinished
            SeashellRandomizer::InstallHooks();
            EventPatcher::InstallHooks();
        }
        if (global_config.randomizer.enemies || global_config.randomizer.enemy_sizes) {
            EnemyRandomizer::InstallHooks();
            ActorReader::InstallHooks();
        }
    }

    inline void InstallMods() {
        Controls::InstallHooks();
        GeneralFixes::InstallHooks();
        Tweaks::Install();
        // TestMods::InstallHooks();
    }
}