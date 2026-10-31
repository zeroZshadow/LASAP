#pragma once
// Item ids (scripts/re/ITEMS.md).
#include "lib.hpp"

namespace Game::Data {
    // Items.gsheet rows, as Inventory_AddItemID takes them, named by their keys (Inventory::kItemNames). The ids the
    // game leaves free hold the placeholder's and LASR's items (AP::IsPlaceholderItem).
    enum class ItemId : int {
        SwordLv1 = 0, SwordLv2 = 1, Shield = 2, MirrorShield = 3, Bomb = 4, Bow = 5, HookShot = 6, Boomerang = 7,
        MagicRod = 8, Shovel = 9, SleepyMushroom = 10, MagicPowder = 11, RocsFeather = 12, PowerBraceletLv1 = 13,
        PowerBraceletLv2 = 14, PegasusBoots = 15, Ocarina = 16, ClothesGreen = 17, ClothesRed = 18, ClothesBlue = 19,
        Flippers = 20, SecretMedicine = 21, Seashell = 22, GoldenLeaf = 23, TailKey = 24, SlimeKey = 25,
        AnglerKey = 26, FaceKey = 27, BirdKey = 28,
        // The trade sequence
        YoshiDoll = 29, Ribbon = 30, DogFood = 31, Bananas = 32, Stick = 33, Honeycomb = 34, Pineapple = 35,
        Hibiscus = 36, Letter = 37, Broom = 38, FishingHook = 39, PinkBra = 40, MermaidsScale = 42,
        MagnifyingLens = 43,
        // Dungeon items: the level byte picks the dungeon
        Compass = 44, DungeonMap = 45, StoneBeak = 47, SmallKey = 48, NightmareKey = 49,
        FullMoonCello = 50, ConchHorn = 51, SeaLilysBell = 52, SurfHarp = 53, WindMarimba = 54, CoralTriangle = 55,
        EveningCalmOrgan = 56, ThunderDrum = 57,
        HeartPiece = 58, HeartContainer = 59, Arrow = 60, Apple = 61, GreenApple = 62,
        // Drops and upgrades. DefenceUp is the Guardian Acorn, PowerUp the Piece of Power, PanelDungeonPiece a
        // Chamber Stone.
        Heart = 100, Rupee1 = 101, Rupee5 = 102, Rupee20 = 104, Rupee50 = 105, Rupee100 = 106, Rupee300 = 108,
        Fairy = 110, DefenceUp = 111, PowerUp = 112, Bottle = 113, PanelDungeonPiece = 114,
        PanelDungeonPieceSet = 115, ShellRader = 116, PanelDungeonPlusChip = 117, SongWindFish = 120,
        SongMambo = 121, SongSoul = 122, MagicPowderMaxUp = 123, BombMaxUp = 124, ArrowMaxUp = 125,
        BottleFairy = 126,
    };
}
