// #pragma once
// #include "Game/actor.hpp"
// #include "Game/Data/inventory.hpp"
// #include "Hooks/Common/eventflags.hpp"
// #include "Hooks/Common/level.hpp"
// #include "nn/hid.h"
// #include <string>

// namespace PracticeModManager {
//     namespace Dungeons {
//         namespace Lv01TailCave {
//             nn::hid::KeyboardKey keys[] = {nn::hid::KeyboardKey::D, nn::hid::KeyboardKey::D1};

//             // We need to unset all dungeon flags
//             // To save memory, we should use flag indexes in the future
//             std::string clearFlags[] = {"Lv1BossDemoClear", "AutoSaveLv01TailCave", "TBOX_OPEN_L01_05D",
//                 "TBOX_OPEN_L01_05F", "TBOX_OPEN_L01_06B", "TBOX_OPEN_L01_06C", "TBOX_OPEN_L01_06D",
//                 "TBOX_OPEN_L01_07D", "TBOX_OPEN_L01_07E", "TBOX_OPEN_L01_08B", "KeyD_Opened_L01_05C",
//                 "KeyD_Opened_L01_06E", "KeyB_Opened_L01_05E", "BossDoorOpened_L01_05G", "StairsPop_Btl_L01_04B",
//                 "DoorOpen_Btl_L01_04G_Boss", "TBoxPop_Btl_L01_06C", "DoorOpen_Btl_L01_06G", "DoorOpen_Btl_L01_07C",
//                 "TBoxPop_Btl_L01_07E", "DoorOpen_Btl_L01_08B", "KeyPop_Btl_L01_08C", "KeyGet_L01_08C",
//                 "TBoxPop_Switch_L01_07D", "TBoxPop_Vegas_L01_05F", "DoorOpen_Block_L01_04C", "WallOpen_L01_06C"};

//             void Load() {
//                 LevelSystem::LoadLevel("Lv01TailCave", Game::Vec3{52.5f, 0.0f, 93.75f});
//                 Game::Data::Inventory::ResetItems();
//                 (*Game::Data::Inventory::Items)->MainItems.SwordLv1 = true;
//                 (*Game::Data::Inventory::Items)->MainItems.Shield = true;
//                 (*Game::Data::Inventory::Items)->MainItems.MagicPowder = true;
//                 Game::Data::Inventory::RefillConsumables();
//                 for (const auto& flag : clearFlags) {
//                     EventFlags::SetFlag(flag, false);
//                 }
//             }
//         }
//     }
// }
