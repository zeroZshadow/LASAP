#include "game/hooks/inventory.hpp"
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "config/config.hpp"
#include "game/data/inventory.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"

namespace {
    using Game::Data::ItemId;

    // The purpose of this is to have our own code run for specific items
    // One example would be that getting SurfHarp needs to set 4 flags to disable the ghost
    // Other items will just use the original code
    HOOK_DEFINE_TRAMPOLINE(Inventory_AddItemID) {
        static void Callback(ItemId ID, int count, int index) {
            // Archipelago placeholder: the index is the location's local id
            if (AP::IsPlaceholderItem(ID)) {
                AP::OnPlaceholderCollected(AP::LocationId{index});
                return;
            }
            AP::OnItemAdded(ID, index);

            if (!global_config.randomizer.enabled) {
                Orig(ID, count, index);
                return;
            }

            // The ghost in Tarin's house: tops bombs or arrows up to count
            if ((ID == ItemId::Bomb || ID == ItemId::Arrow) && index == AP::AMMO_REFILL_INDEX) {
                auto* items = Game::Data::Inventory::Items();
                int have = ID == ItemId::Bomb ? items->BombCount : items->ArrowCount;
                if (have < count) Orig(ID, count - have, -1);
                return;
            }

            // Automatically set item GettingFlags so that we dont need as many cases
            if (!EventFlags::CheckGettingFlag(ID)) {
                EventFlags::SetGettingFlag(ID);
            }

            u8 actualLevel = *Game::LevelIndex; // for dungeon items

            switch (ID) {
                case ItemId::Bomb:
                    if (Game::Data::Inventory::Items()->MainItems.Bomb) {
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
                case ItemId::Bow:
                    Orig(ID, count, index);
                    Orig(ItemId::Arrow, 60, -1);
                    break;
                case ItemId::MagicPowder:
                    if (Game::Data::Inventory::Items()->MainItems.MagicPowder) {
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
                case ItemId::Compass:
                case ItemId::DungeonMap:
                case ItemId::StoneBeak:
                case ItemId::SmallKey:
                case ItemId::NightmareKey:
                    // index -1 will only be used by Chamber Dungeons in which case we want to keep orig func
                    if (index == -1) {
                        Orig(ID, count, index);
                    }
                    else {
                        // index is normally ignored for dungeon items, game uses a level byte to determine where to
                        // add the items in memory, so we assign an index to the dungeon items and actually make use
                        // of it
                        // set the level byte to the index and call the orig func, then set the level byte back
                        *Game::LevelIndex = index;
                        Orig(ID, count, index);
                        *Game::LevelIndex = actualLevel;
                    }
                    break;
                case ItemId::FullMoonCello: // additional flags
                    EventFlags::SetFlag("BowWowEvent", true);
                    EventFlags::SetFlag("DoorOpen_Btl_MoriblinCave_2A", false);
                    EventFlags::SetFlag("DoorOpen_Btl_MoriblinCave_1A", false);
                    Orig(ID, count, index);
                    break;
                case ItemId::SurfHarp: // additional flags
                    EventFlags::SetFlag("GhostClear1", true);
                    EventFlags::SetFlag("Ghost2_Clear", true);
                    EventFlags::SetFlag("Ghost3_Clear", true);
                    EventFlags::SetFlag("Ghost4_Clear", true);
                    Orig(ID, count, index);
                    break;
                case ItemId::Arrow:
                    if (count == 1) {
                        count = 3;
                    }
                    Orig(ID, count, index);
                    break;
                case ItemId::MagicPowderMaxUp: // give max powder if the player found the main source
                    Orig(ID, count, index);
                    if (EventFlags::CheckFlag("GetMagicPowder")) {
                        Orig(ItemId::MagicPowder, 40, -1);
                    }
                    break;
                case ItemId::BombMaxUp: // give max bombs if the player found the main source
                    Orig(ID, count, index);
                    if (EventFlags::CheckFlag("BombsFoundFlag")) {
                        Orig(ItemId::Bomb, 60, -1);
                    }
                    break;
                case ItemId::ArrowMaxUp:
                    Orig(ID, count, index);
                    Orig(ItemId::Arrow, 60, -1);
                    break;
                default:
                    if (ID <= ItemId::BottleFairy) { // only add item if it is within the vanilla range
                        // do not add trade items besides MagnifyingLens
                        if (ID < ItemId::YoshiDoll || ID > ItemId::MermaidsScale) {
                            Orig(ID, count, index);
                        }
                    }
                    break;
            }
        }
    };
}

namespace Inventory {
    const std::unordered_map<ItemId, std::string> kItemNames = {
        {ItemId::SwordLv1, "SwordLv1"},
        {ItemId::SwordLv2, "SwordLv2"},
        {ItemId::Shield, "Shield"},
        {ItemId::MirrorShield, "MirrorShield"},
        {ItemId::Bomb, "Bomb"},
        {ItemId::Bow, "Bow"},
        {ItemId::HookShot, "HookShot"},
        {ItemId::Boomerang, "Boomerang"},
        {ItemId::MagicRod, "MagicRod"},
        {ItemId::Shovel, "Shovel"},
        {ItemId::SleepyMushroom, "SleepyMushroom"},
        {ItemId::MagicPowder, "MagicPowder"},
        {ItemId::RocsFeather, "RocsFeather"},
        {ItemId::PowerBraceletLv1, "PowerBraceletLv1"},
        {ItemId::PowerBraceletLv2, "PowerBraceletLv2"},
        {ItemId::PegasusBoots, "PegasusBoots"},
        {ItemId::Ocarina, "Ocarina"},
        {ItemId::ClothesGreen, "ClothesGreen"},
        {ItemId::ClothesRed, "ClothesRed"},
        {ItemId::ClothesBlue, "ClothesBlue"},
        {ItemId::Flippers, "Flippers"},
        {ItemId::SecretMedicine, "SecretMedicine"},
        {ItemId::Seashell, "Seashell"},
        {ItemId::GoldenLeaf, "GoldenLeaf"},
        {ItemId::TailKey, "TailKey"},
        {ItemId::SlimeKey, "SlimeKey"},
        {ItemId::AnglerKey, "AnglerKey"},
        {ItemId::FaceKey, "FaceKey"},
        {ItemId::BirdKey, "BirdKey"},
        {ItemId::YoshiDoll, "YoshiDoll"},
        {ItemId::Ribbon, "Ribbon"},
        {ItemId::DogFood, "DogFood"},
        {ItemId::Bananas, "Bananas"},
        {ItemId::Stick, "Stick"},
        {ItemId::Honeycomb, "Honeycomb"},
        {ItemId::Pineapple, "Pineapple"},
        {ItemId::Hibiscus, "Hibiscus"},
        {ItemId::Letter, "Letter"},
        {ItemId::Broom, "Broom"},
        {ItemId::FishingHook, "FishingHook"},
        {ItemId::PinkBra, "PinkBra"},
        {ItemId::MermaidsScale, "MermaidsScale"},
        {ItemId::MagnifyingLens, "MagnifyingLens"},
        {ItemId::Compass, "Compass"},
        {ItemId::DungeonMap, "DungeonMap"},
        {ItemId::StoneBeak, "StoneBeak"},
        {ItemId::SmallKey, "SmallKey"},
        {ItemId::NightmareKey, "NightmareKey"},
        {ItemId::FullMoonCello, "FullMoonCello"},
        {ItemId::ConchHorn, "ConchHorn"},
        {ItemId::SeaLilysBell, "SeaLilysBell"},
        {ItemId::SurfHarp, "SurfHarp"},
        {ItemId::WindMarimba, "WindMarimba"},
        {ItemId::CoralTriangle, "CoralTriangle"},
        {ItemId::EveningCalmOrgan, "EveningCalmOrgan"},
        {ItemId::ThunderDrum, "ThunderDrum"},
        {ItemId::HeartPiece, "HeartPiece"},
        {ItemId::HeartContainer, "HeartContainer"},
        {ItemId::Arrow, "Arrow"},
        {ItemId::Apple, "Apple"},
        {ItemId::GreenApple, "GreenApple"},
        {ItemId::Heart, "Heart"},
        {ItemId::Rupee1, "Rupee1"},
        {ItemId::Rupee5, "Rupee5"},
        {ItemId::Rupee20, "Rupee20"},
        {ItemId::Rupee50, "Rupee50"},
        {ItemId::Rupee100, "Rupee100"},
        {ItemId::Rupee300, "Rupee300"},
        {ItemId::Fairy, "Fairy"},
        {ItemId::DefenceUp, "DefenceUp"},
        {ItemId::PowerUp, "PowerUp"},
        {ItemId::Bottle, "Bottle"},
        {ItemId::PanelDungeonPiece, "PanelDungeonPiece"},
        {ItemId::PanelDungeonPieceSet, "PanelDungeonPieceSet"},
        {ItemId::ShellRader, "ShellRader"},
        {ItemId::PanelDungeonPlusChip, "PanelDungeonPlusChip"},
        {ItemId::SongWindFish, "Song_WindFish"},
        {ItemId::SongMambo, "Song_Mambo"},
        {ItemId::SongSoul, "Song_Soul"},
        {ItemId::MagicPowderMaxUp, "MagicPowder_MaxUp"},
        {ItemId::BombMaxUp, "Bomb_MaxUp"},
        {ItemId::ArrowMaxUp, "Arrow_MaxUp"},
        {ItemId::BottleFairy, "BottleFairy"}
    };

    void AddItemID(ItemId id, int count, int index) { Inventory_AddItemID::Callback(id, count, index); }

    void Install() {
        Inventory_AddItemID::InstallAtFuncPtr(Game::Inventory_AddItemID.Ptr());
    }
}
