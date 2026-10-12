#pragma once
#include "Game/actor.hpp"
#include "Game/Data/inventory.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Hooks/Common/level.hpp"
#include "nn/hid.h"
#include <string>

namespace PracticeModManager {
    namespace Glitches {
        namespace LikeLikeLaunch {
            nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::G, nn::hid::KeyboardKey::D2};

            // We need some flags set to avoid cutscenes in the forest
            std::string setFlags[] = {"BowWowEvent", "SecondClear", "ThirdClear", "RescueTarin"};

            // // We need to unset some flags in case the player has already completed this quest
            std::string clearFlags[] = {"BowwowClear", "BowWowJoin"};

            void Load() {
                LevelSystem::LoadLevel("Field", Game::Vec3{17.25f, 10.5f, 132.75f});
                Game::Data::Inventory::ResetItems();
                Game::Data::Inventory::SetMaxHearts(4);
                (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
                (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                (*Game::Data::Inventory::Items)->ExtraItems.TailKey = true;
                (*Game::Data::Inventory::Items)->ExtraItems.FullMoonCello = true;
                Game::Data::Inventory::RefillConsumables();
                for (std::string flag : setFlags) {
                    EventFlags::SetFlag(flag, true);
                }
                for (std::string flag : clearFlags) {
                    EventFlags::SetFlag(flag, false);
                }
            }
        }
        namespace VillaSkip {
            nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::G, nn::hid::KeyboardKey::D3};

            void Load() {
                LevelSystem::LoadLevel("Field", Game::Vec3{84.75f, 9.75f, 117.75f});
                Game::Data::Inventory::ResetItems();
                Game::Data::Inventory::SetMaxHearts(4);
                (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
                (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                (*Game::Data::Inventory::Items)->MainItems.PowerBraceletLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shovel = true;
                (*Game::Data::Inventory::Items)->MainItems.Bomb = true;
                Game::Data::Inventory::RefillConsumables();
            }
        }
        namespace AnglerKeySkip {
            nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::G, nn::hid::KeyboardKey::D4};

            void Load() {
                LevelSystem::LoadLevel("Field", Game::Vec3{137.25f, 16.5f, 38.25f});
                Game::Data::Inventory::ResetItems();
                Game::Data::Inventory::SetMaxHearts(5);
                (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
                (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                (*Game::Data::Inventory::Items)->MainItems.PowerBraceletLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.PegasusBoots = true;
                Game::Data::Inventory::RefillConsumables();
            }
        }
        namespace EarlyD6 {
            nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::G, nn::hid::KeyboardKey::D6};

            void Load() {
                LevelSystem::LoadLevel("Field", Game::Vec3{171.75f, 12.75f, 41.25f});
                Game::Data::Inventory::ResetItems();
                Game::Data::Inventory::SetMaxHearts(5);
                (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
                (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                (*Game::Data::Inventory::Items)->MainItems.PowerBraceletLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.PegasusBoots = true;
                (*Game::Data::Inventory::Items)->MainItems.Bomb = true;
                (*Game::Data::Inventory::Items)->ExtraItems.Flippers = true;
                Game::Data::Inventory::RefillConsumables();
            }
        }
        namespace FlameCaveSkip { // D8 backdoor prac can just use this
            nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::G, nn::hid::KeyboardKey::D8};

            void Load() {
                LevelSystem::LoadLevel("Field", Game::Vec3{20.25f, 18.75f, 29.25f});
                Game::Data::Inventory::ResetItems();
                Game::Data::Inventory::SetMaxHearts(5);
                (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
                (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                (*Game::Data::Inventory::Items)->MainItems.PowerBraceletLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.PegasusBoots = true;
                (*Game::Data::Inventory::Items)->MainItems.Hookshot = true;
                (*Game::Data::Inventory::Items)->ExtraItems.Flippers = true;
                (*Game::Data::Inventory::Items)->MainItems.Bow = true;
                (*Game::Data::Inventory::Items)->MainItems.Bomb = true;
                (*Game::Data::Inventory::Items)->MainItems.Ocarina = true;
                (*Game::Data::Inventory::Items)->Songs.Soul = true;
                (*Game::Data::Inventory::Items)->Companion = 4;
                Game::Data::Inventory::RefillConsumables();
            }
        }
        namespace HookshotRide {
            nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::G, nn::hid::KeyboardKey::H};

            std::string setFlags[] = {"DoorOpen_Btl3_L05_01F"};
            std::string clearFlags[] = {"HookShotPop_Btl4_L05_01B", "HookShotGet_L05_01B", "MasterStalfos4DemoClear"};

            void Load() {
                LevelSystem::LoadLevel("Lv05CatfishsMaw", Game::Vec3{31.25f, 0.0f, 6.25f});
                Game::Data::Inventory::ResetItems();
                Game::Data::Inventory::SetMaxHearts(5);
                (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                (*Game::Data::Inventory::Items)->MainItems.PowerBraceletLv1 = true;
                (*Game::Data::Inventory::Items)->MainItems.PegasusBoots = true;
                (*Game::Data::Inventory::Items)->ExtraItems.Flippers = true;
                (*Game::Data::Inventory::Items)->MainItems.Bow = true;
                (*Game::Data::Inventory::Items)->MainItems.Bomb = true;
                Game::Data::Inventory::RefillConsumables();
                for (std::string flag : setFlags) {
                    EventFlags::SetFlag(flag, true);
                }
                for (std::string flag : clearFlags) {
                    EventFlags::SetFlag(flag, false);
                }
            }
        }
        namespace HinoxWarps {
            namespace D2 {
                nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::H, nn::hid::KeyboardKey::W, nn::hid::KeyboardKey::D2};

                void Load() {
                    LevelSystem::LoadLevel("Lv02BottleGrotto", Game::Vec3{24.75f, 0.0f, 57.75f});
                    Game::Data::Inventory::ResetItems();
                    Game::Data::Inventory::SetMaxHearts(4);
                    (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                    (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                    (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
                    (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                    Game::Data::Inventory::RefillConsumables();
                    EventFlags::SetFlag("BossClear_L02_4B", false); // revive Hinox if it is dead
                }
            }
            namespace D7 {
                nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::H, nn::hid::KeyboardKey::W, nn::hid::KeyboardKey::D7};

                void Load() {
                    LevelSystem::LoadLevel("Lv07EagleTower", Game::Vec3{31.5f, 0.0f, 44.25f});
                    Game::Data::Inventory::ResetItems();
                    (*Game::Data::Inventory::Items)->ExtraItems.Medicine = true;
                    (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                    (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                    (*Game::Data::Inventory::Items)->MainItems.PegasusBoots = true;
                    (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                    (*Game::Data::Inventory::Items)->MainItems.Hookshot = true;
                    (*Game::Data::Inventory::Items)->MainItems.Bow = true;
                    (*Game::Data::Inventory::Items)->MainItems.Bomb = true;
                    Game::Data::Inventory::RefillConsumables();
                    EventFlags::SetFlag("DestroyBtl_L07_04A", false); // revive Hinox if it is dead
                    EventFlags::SetFlag("TboxPop_Vegas_L07_04B", true); // chest from 3 of a kind is already spawned
                }
            }
            namespace D8 {
                nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::H, nn::hid::KeyboardKey::W, nn::hid::KeyboardKey::D8};

                void Load() {
                    LevelSystem::LoadLevel("Lv08TurtleRock", Game::Vec3{37.25f, 0.0f, 73.25f});
                    Game::Data::Inventory::ResetItems();
                    Game::Data::Inventory::SetMaxHearts(20);
                    (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
                    (*Game::Data::Inventory::Items)->MainItems.Shield = true;
                    (*Game::Data::Inventory::Items)->MainItems.PegasusBoots = true;
                    (*Game::Data::Inventory::Items)->MainItems.RocsFeather = true;
                    (*Game::Data::Inventory::Items)->MainItems.Hookshot = true;
                    (*Game::Data::Inventory::Items)->MainItems.Bow = true;
                    (*Game::Data::Inventory::Items)->MainItems.Bomb = true;
                    Game::Data::Inventory::RefillConsumables();
                    EventFlags::SetFlag("DoorOpen_Btl_L08_06C", false); // revive Hinox if it is dead
                    EventFlags::SetFlag("DoorOpen_Btl_L08_07C", true); // open the door to the hinox room
                }
            }
        }
    }
}
