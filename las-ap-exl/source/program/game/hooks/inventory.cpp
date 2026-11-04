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

    // The items the module adds its own effects to (AP placeholders, refills, flags, dungeon items by index)
    HOOK_DEFINE_TRAMPOLINE(Inventory_AddItemID) {
        static void Callback(ItemId ID, int count, int index) {
            // Archipelago placeholder: the index is the location's local id
            if (ID == AP::PLACEHOLDER_ITEM_ID) {
                AP::OnPlaceholderCollected(AP::LocationId{index});
                return;
            }
            AP::OnItemAdded(ID, index);

            if (!global_config.randomizer.enabled) {
                Orig(ID, count, index);
                return;
            }

            // The ghost in Tarin's house: tops bombs, arrows or powder up to count
            if ((ID == ItemId::Bomb || ID == ItemId::Arrow || ID == ItemId::MagicPowder) &&
                index == AP::AMMO_REFILL_INDEX) {
                auto* items = Game::Data::Inventory::Items();
                int have = ID == ItemId::Bomb ? items->BombCount : ID == ItemId::Arrow ? items->ArrowCount
                                                                                      : items->PowderCount;
                if (have < count) Orig(ID, count - have, -1);
                return;
            }

            // Every item's GettingFlag, so no case has to set its own
            if (!EventFlags::CheckGettingFlag(ID)) {
                EventFlags::SetGettingFlag(ID);
            }

            u8 actualLevel = *Game::LevelIndex; // for dungeon items

            switch (ID) {
                case ItemId::Bomb:
                    if (Game::Data::Inventory::Items()->MainItems.Bomb) {
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
                    // index -1 (none, e.g. the Chamber Dungeon's): the current dungeon's, as vanilla
                    if (index == -1) {
                        Orig(ID, count, index);
                    }
                    else {
                        // The game ignores the index for dungeon items and adds them to LevelIndex's dungeon
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
    void AddItemID(ItemId id, int count, int index) { Inventory_AddItemID::Callback(id, count, index); }

    void Install() {
        Inventory_AddItemID::InstallAtFuncPtr(Game::Inventory_AddItemID.Ptr());
    }
}
