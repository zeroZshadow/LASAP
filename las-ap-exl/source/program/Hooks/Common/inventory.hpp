#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Config/config.hpp"
#include "Hooks/Common/eventflags.hpp"
#include "Game/Data/inventory.hpp"
#include "AP/ap.hpp"
#include <string>
#include <unordered_map>

inline const std::unordered_map<int, std::string> itemsDict = {
    {0,     "SwordLv1"},
    {1,     "SwordLv2"},
    {2,     "Shield"},
    {3,     "MirrorShield"},
    {4,     "Bomb"},
    {5,     "Bow"},
    {6,     "HookShot"},
    {7,     "Boomerang"},
    {8,     "MagicRod"},
    {9,     "Shovel"},
    {10,    "SleepyMushroom"},
    {11,    "MagicPowder"},
    {12,    "RocsFeather"},
    {13,    "PowerBraceletLv1"},
    {14,    "PowerBraceletLv2"},
    {15,    "PegasusBoots"},
    {16,    "Ocarina"},
    {17,    "ClothesGreen"},
    {18,    "ClothesRed"},
    {19,    "ClothesBlue"},
    {20,    "Flippers"},
    {21,    "SecretMedicine"},
    {22,    "Seashell"},
    {23,    "GoldenLeaf"},
    {24,    "TailKey"},
    {25,    "SlimeKey"},
    {26,    "AnglerKey"},
    {27,    "FaceKey"},
    {28,    "BirdKey"},
    {29,    "YoshiDoll"},
    {30,    "Ribbon"},
    {31,    "DogFood"},
    {32,    "Bananas"},
    {33,    "Stick"},
    {34,    "Honeycomb"},
    {35,    "Pineapple"},
    {36,    "Hibiscus"},
    {37,    "Letter"},
    {38,    "Broom"},
    {39,    "FishingHook"},
    {40,    "PinkBra"},
    {42,    "MermaidsScale"},
    {43,    "MagnifyingLens"},
    {44,    "Compass"},
    {45,    "DungeonMap"},
    {47,    "StoneBeak"},
    {48,    "SmallKey"},
    {49,    "NightmareKey"},
    {50,    "FullMoonCello"},
    {51,    "ConchHorn"},
    {52,    "SeaLilysBell"},
    {53,    "SurfHarp"},
    {54,    "WindMarimba"},
    {55,    "CoralTriangle"},
    {56,    "EveningCalmOrgan"},
    {57,    "ThunderDrum"},
    {58,    "HeartPiece"},
    {59,    "HeartContainer"},
    {60,    "Arrow"},
    {61,    "Apple"},
    {62,    "GreenApple"},
    {100,   "Heart"},
    {101,   "Rupee1"},
    {102,   "Rupee5"},
    {104,   "Rupee20"},
    {105,   "Rupee50"},
    {106,   "Rupee100"},
    {108,   "Rupee300"},
    {110,   "Fairy"},
    {111,   "DefenceUp"},
    {112,   "PowerUp"},
    {113,   "Bottle"},
    {114,   "PanelDungeonPiece"},
    {115,   "PanelDungeonPieceSet"},
    {116,   "ShellRader"},
    {117,   "PanelDungeonPlusChip"},
    {120,   "Song_WindFish"},
    {121,   "Song_Mambo"},
    {122,   "Song_Soul"},
    {123,   "MagicPowder_MaxUp"},
    {124,   "Bomb_MaxUp"},
    {125,   "Arrow_MaxUp"},
    {126,   "BottleFairy"}
};

// The purpose of this is to have our own code run for specific items
// One example would be that getting SurfHarp needs to set 4 flags to disable the ghost
// Other items will just use the original code
HOOK_DEFINE_TRAMPOLINE(Inventory__AddItemID) {
    static void Callback(int ID, int count, int index) {
        EXL_ASSERT(global_config.initialized);

        // Archipelago placeholder: the index is the location's local id
        if (AP::IsPlaceholderItem(ID)) {
            AP::OnPlaceholderCollected(index);
            return;
        }
        // if (global_config.debug_mode.enabled) {
        //     std::string itemName = itemsDict.at(ID);
        //     std::string indexStr = "";
        //     if (ID == 22 || ID == 23 || ID == 58 || ID == 59 || ID == 114 || ID == 115 || ID == 116) {
        //         indexStr = "[" + std::to_string(index) + "]";
        //     }
        //     Logging.Log("Player has obtained '" + itemName + indexStr + "'");
        // }

        if (!global_config.randomizer.enabled) {
            Orig(ID, count, index);
            return;
        }

        // Automatically set item GettingFlags so that we dont need as many cases
        if (!EventFlags::CheckGettingFlag(ID)) {
            EventFlags::SetGettingFlag(ID);
        }

        uint8_t actualLevel = *Game::Data::Inventory::Level; // for dungeon items

        switch (ID) {
            case 4: // Bomb
                if ((*Game::Data::Inventory::Items)->MainItems.Bomb) {
                    // add 3 bombs instead of 1 if the player already has some
                    if (count == 1) {
                        count = 3;
                    }
                    Orig(ID, count, index);
                }
                else {
                    Orig(ID, 60, index); // give max bombs if not
                }
                break;
            case 5: // Bow
                Orig(ID, count, index);
                Orig(60, 60, -1);
                break;
            case 11: // MagicPowder
                if ((*Game::Data::Inventory::Items)->MainItems.MagicPowder) {
                    // add 3 powder instead of 1 if the player already has some
                    if (count == 1) {
                        count = 3;
                    }
                    Orig(ID, count, index);
                }
                else {
                    Orig(ID, 40, index);
                }
                break;
            case 44: // Compass
            case 45: // DungeonMap
            case 47: // StoneBeak
            case 48: // SmallKey
            case 49: // NightmareKey
                if (index == -1) { // index -1 will only be used by Chamber Dungeons in which case we want to keep orig func
                    Orig(ID, count, index);
                }
                else {
                    // index is normally ignored for dungeon items, game uses a level byte to determine where to add the items in memory
                    // so we assign an index to the dungeon items and actually make use of it
                    // set the level byte to the index and call the orig func, then set the level byte back
                    *Game::Data::Inventory::Level = index;
                    Orig(ID, count, index);
                    *Game::Data::Inventory::Level = actualLevel;
                }
                break;
            case 50: // FullMoonCello - additional flags
                EventFlags::SetFlag("BowWowEvent", true);
                EventFlags::SetFlag("DoorOpen_Btl_MoriblinCave_2A", false);
                EventFlags::SetFlag("DoorOpen_Btl_MoriblinCave_1A", false);
                Orig(ID, count, index);
                break;
            case 53: // SurfHarp - additional flags
                EventFlags::SetFlag("GhostClear1", true);
                EventFlags::SetFlag("Ghost2_Clear", true);
                EventFlags::SetFlag("Ghost3_Clear", true);
                EventFlags::SetFlag("Ghost4_Clear", true);
                Orig(ID, count, index);
                break;
            case 60: // arrow
                if (count == 1) {
                    count = 3;
                }
                Orig(ID, count, index);
                break;
            case 123: // MagicPowder_MaxUp - give max powder if the player found the main source
                Orig(ID, count, index);
                if (EventFlags::CheckFlag("GetMagicPowder")) {
                    Orig(11, 40, -1);
                }
                break;
            case 124: // Bomb_MaxUp - give max bombs if the player found the main source
                Orig(ID, count, index);
                if (EventFlags::CheckFlag("BombsFoundFlag")) {
                    Orig(4, 60, -1);
                }
                break;
            case 125: // Arrow_MaxUp
                Orig(ID, count, index);
                Orig(60, 60, -1);
                break;
            default:
                if (ID < 127) { // only add item if it is within the vanilla range
                    if (ID < 29 || ID > 42) { // do not add trade items besides MagifyingLens
                        Orig(ID, count, index);
                    }
                }
                break;
        }
    }
};

namespace Inventory {
    inline void AddItemID(int ID, int count, int index) {
        Inventory__AddItemID::Callback(ID, count, index);
    }

    inline void InstallHooks() {
        Inventory__AddItemID::InstallAtOffset(offset_manager.Offset(0x8d08e0));
    }
}
