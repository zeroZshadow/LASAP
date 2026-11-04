#pragma once
// Item ids (scripts/re/ITEMS.md).
#include "lib.hpp"
#include <string_view>

namespace Game::Data {
    // Items.gsheet rows, as Inventory_AddItemID takes them: X(name, item id) where the row's key is the name, K(name,
    // item id, key) where it is not. The ids the game leaves free hold the placeholder's (AP::PLACEHOLDER_ITEM_ID) and
    // LASR's items. The trade sequence runs from YoshiDoll to MagnifyingLens; a dungeon item's level byte picks the
    // dungeon; DefenceUp is the Guardian Acorn, PowerUp the Piece of Power, PanelDungeonPiece a Chamber Stone.
#define LAS_ITEMS(X, K)                                                                                                \
    X(SwordLv1, 0) X(SwordLv2, 1) X(Shield, 2) X(MirrorShield, 3) X(Bomb, 4) X(Bow, 5) X(HookShot, 6)                  \
    X(Boomerang, 7) X(MagicRod, 8) X(Shovel, 9) X(SleepyMushroom, 10) X(MagicPowder, 11) X(RocsFeather, 12)            \
    X(PowerBraceletLv1, 13) X(PowerBraceletLv2, 14) X(PegasusBoots, 15) X(Ocarina, 16) X(ClothesGreen, 17)             \
    X(ClothesRed, 18) X(ClothesBlue, 19) X(Flippers, 20) X(SecretMedicine, 21) X(Seashell, 22) X(GoldenLeaf, 23)       \
    X(TailKey, 24) X(SlimeKey, 25) X(AnglerKey, 26) X(FaceKey, 27) X(BirdKey, 28)                                      \
    X(YoshiDoll, 29) X(Ribbon, 30) X(DogFood, 31) X(Bananas, 32) X(Stick, 33) X(Honeycomb, 34) X(Pineapple, 35)        \
    X(Hibiscus, 36) X(Letter, 37) X(Broom, 38) X(FishingHook, 39) X(PinkBra, 40) X(MermaidsScale, 42)                  \
    X(MagnifyingLens, 43)                                                                                              \
    X(Compass, 44) X(DungeonMap, 45) X(StoneBeak, 47) X(SmallKey, 48) X(NightmareKey, 49)                              \
    X(FullMoonCello, 50) X(ConchHorn, 51) X(SeaLilysBell, 52) X(SurfHarp, 53) X(WindMarimba, 54)                       \
    X(CoralTriangle, 55) X(EveningCalmOrgan, 56) X(ThunderDrum, 57)                                                    \
    X(HeartPiece, 58) X(HeartContainer, 59) X(Arrow, 60) X(Apple, 61) X(GreenApple, 62)                                \
    X(Heart, 100) X(Rupee1, 101) X(Rupee5, 102) X(Rupee20, 104) X(Rupee50, 105) X(Rupee100, 106) X(Rupee300, 108)      \
    X(Fairy, 110) X(DefenceUp, 111) X(PowerUp, 112) X(Bottle, 113) X(PanelDungeonPiece, 114)                           \
    X(PanelDungeonPieceSet, 115) X(ShellRader, 116) X(PanelDungeonPlusChip, 117)                                       \
    K(SongWindFish, 120, "Song_WindFish") K(SongMambo, 121, "Song_Mambo") K(SongSoul, 122, "Song_Soul")                \
    K(MagicPowderMaxUp, 123, "MagicPowder_MaxUp") K(BombMaxUp, 124, "Bomb_MaxUp") K(ArrowMaxUp, 125, "Arrow_MaxUp")    \
    X(BottleFairy, 126)

#define LAS_ITEM_ID(name, id, ...) name = id,
    enum class ItemId : int { LAS_ITEMS(LAS_ITEM_ID, LAS_ITEM_ID) };
#undef LAS_ITEM_ID

    struct ItemKey {
        ItemId id;
        std::string_view key;
    };
#define LAS_ITEM_NAMED(name, id) {ItemId::name, #name},
#define LAS_ITEM_KEYED(name, id, key) {ItemId::name, key},
    constexpr ItemKey kItemKeys[] = {LAS_ITEMS(LAS_ITEM_NAMED, LAS_ITEM_KEYED)};
#undef LAS_ITEM_NAMED
#undef LAS_ITEM_KEYED

    // The item's Items.gsheet key; "" for an id without a row.
    constexpr std::string_view KeyOf(ItemId id) {
        for (const ItemKey& item : kItemKeys) {
            if (item.id == id) return item.key;
        }
        return {};
    }
}
