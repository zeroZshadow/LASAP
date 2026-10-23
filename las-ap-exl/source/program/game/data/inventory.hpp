#pragma once
#include "game/actor.hpp"
#include "game/sym.hpp"
#include "lib.hpp"
#include <cstddef>
#include <cstdint>

namespace Game::Data {
    // A chapter of the save's record (SaveHeader::chapters; Inventory_RecordChapter).
    struct ChapterRecord {
        u8 instrument;             // 0 to 7
        u8 pad_1;
        u16 bits;                  // 0x2: a bitset: 0 Shovel, 1 Bow, 2 heart piece 24, 3 + i Chamber Dungeon set i
        s32 rupees;                // 0x4
        u8 pad_8[0x18];
    };
    static_assert(offsetof(ChapterRecord, rupees) == 0x4 && sizeof(ChapterRecord) == 0x20);

    // The save's header (SaveHeader_ptr, GlobalSave +0x53a0; 0x140 bytes).
    struct SaveHeader {
        u32 knockCount;            // enemies defeated; bit 0 TestEnemyKnockCountOddEven
        u16 deathCount;            // 0x4: AddDeadCounter, up to 0xffff
        u8 pad_6[0x2];
        char16_t name[9];          // 0x8: the player's name, 8 letters and a 0
        u8 pad_1a[0x6];
        u64 totalTimePlayed;       // 0x20: microseconds, outside events
        u8 chapterCount;           // 0x28
        u8 pad_29[0x17];
        ChapterRecord chapters[8]; // 0x40
    };
    static_assert(offsetof(SaveHeader, deathCount) == 0x4 && offsetof(SaveHeader, name) == 0x8);
    static_assert(offsetof(SaveHeader, totalTimePlayed) == 0x20 && offsetof(SaveHeader, chapters) == 0x40);
    static_assert(sizeof(SaveHeader) == 0x140);
}

namespace Game::Data::Inventory {
    typedef bool Field;

    struct SongBits {
        Field Ballad : 1;
        Field Mambo : 1;
        Field Soul : 1;
    };

    struct MainItemBits {
        Field SwordLv1 : 1;
        Field Shield : 1;
        Field Bomb : 1;
        Field Bow : 1;
        Field Hookshot : 1;
        Field Boomerang : 1;
        Field MagicRod : 1;
        Field Shovel : 1;

        Field MagicPowder : 1;
        Field RocsFeather : 1;
        Field PowerBraceletLv1 : 1;
        Field PegasusBoots : 1;
        Field Ocarina : 1;
        Field Bottle0 : 1;
        Field Bottle1 : 1;
        Field Bottle2 : 1;
    };

    struct ExtraItemBits {
        Field Flippers : 1;
        Field Medicine : 1;
        Field TailKey : 1;
        Field SlimeKey : 1;
        Field AnglerKey : 1;
        Field FaceKey : 1;
        Field BirdKey : 1;
        Field FullMoonCello : 1;
        Field ConchHorn : 1;
        Field SeaLilysBell : 1;
        Field SurfHarp : 1;
        Field WindMarimba : 1;
        Field CoralTriangle : 1;
        Field EveningCalmOrgan : 1;
        Field ThunderDrum : 1;
        Field SleepyMushroom : 1;
        Field ShellRader : 1;
    };

    struct UpgradeItemBits {
        Field SwordLv2 : 1;
        Field MirrorShield : 1;
        Field PowerBraceletLv2 : 1;
        Field MagicPowder_MaxUp : 1;
        Field Bomb_MaxUp : 1;
        Field Arrow_MaxUp : 1;
        Field ClothesRed : 1;
        Field ClothesBlue : 1;
    };

    // The save's current data (SaveCurrent, GlobalSave +0x54e0; 0x160 bytes; ITEMS.md).
    struct InventoryItems {
        char LevelName[0x40];
        char Locator[0x40]; // 0x40: "$NONE": the level starts at RestartPos
        int Rupees; // 0x80
        short Health; // 0x84
        uint16_t GridPuzzle; // 0x86: seven 2-bit directions, picked at a new game
        uint64_t Seashells; // 0x88
        uint64_t HeartPieces; // 0x90
        uint16_t HeartContainers; // 0x98
        uint8_t TradeItem; // 0x9a
        SongBits Songs; // 0x9b
        UpgradeItemBits UpgradeItems; // 0x9c
        uint8_t Companion; // 0x9d
        uint8_t BombCount; // 0x9e
        uint8_t ArrowCount; // 0x9f
        uint8_t PowderCount; // 0xa0
        uint8_t EquippedSong; // 0xa1
        uint8_t EquipY; // 0xa2
        uint8_t EquipX; // 0xa3
        uint8_t KillCount; // 0xa4
        uint8_t KillCountNoDamage; // 0xa5
        uint16_t TilePuzzle; // 0xa6: five 3-bit tiles, picked at a new game
        MainItemBits MainItems; // 0xa8
        uint8_t pad_aa[2]; // 0xaa: MainItems' word's unused high half
        ExtraItemBits ExtraItems; // 0xac
        uint8_t pad_af; // 0xaf: ExtraItems' word's unused top byte
        uint64_t SleepyMushroomTime; // 0xb0: SaveHeader::totalTimePlayed when picked
        uint64_t WarpPoints; // 0xb8: a bit per WarpPoint row
        uint8_t BottledFairies; // 0xc0
        uint8_t FishingLure; // 0xc1: 0 to 2
        uint8_t GoldenLeaves; // 0xc2
        uint8_t KeptItemKind; // 0xc3: 1 to 5, an item held back
        Game::Vec3 FieldPosition; // 0xc4: Link's, on leaving the overworld
        Game::Vec3 IndoorPosition; // 0xd0: on leaving another level
        Game::Vec3 RestartPos; // 0xdc
        uint32_t Figurines; // 0xe8
        uint32_t ZoneId; // 0xec: the zone entered last
        uint64_t RapidsBestTime; // 0xf0: milliseconds, 0 none
        float FishSize[8]; // 0xf8: the largest per kind
        uint8_t FishCaught[8]; // 0x118: per kind
        bool HeroMode; // 0x120
        uint8_t RestartDirection; // 0x121: bits 0-2
        uint8_t RareFishSign; // 0x122
        uint8_t HintIndex; // 0x123
        uint8_t Obtained[16]; // 0x124: a bit per item id
        uint8_t pad_134[0x4];
        uint64_t LevelEnterTime; // 0x138: SaveHeader::totalTimePlayed when the level was entered (SetCurrentLevel)
        uint64_t PreviousLevelTime; // 0x140: microseconds spent in the level before this one
        uint8_t pad_148[0x18];
    };
    static_assert(sizeof(SongBits) == 1 && sizeof(UpgradeItemBits) == 1);
    static_assert(sizeof(MainItemBits) == 2 && sizeof(ExtraItemBits) == 3);
    static_assert(offsetof(InventoryItems, Locator) == 0x40 && offsetof(InventoryItems, Rupees) == 0x80);
    static_assert(offsetof(InventoryItems, Health) == 0x84 && offsetof(InventoryItems, GridPuzzle) == 0x86);
    static_assert(offsetof(InventoryItems, Seashells) == 0x88 && offsetof(InventoryItems, HeartPieces) == 0x90);
    static_assert(offsetof(InventoryItems, HeartContainers) == 0x98 && offsetof(InventoryItems, TradeItem) == 0x9a);
    static_assert(offsetof(InventoryItems, Songs) == 0x9b && offsetof(InventoryItems, UpgradeItems) == 0x9c);
    static_assert(offsetof(InventoryItems, BombCount) == 0x9e && offsetof(InventoryItems, EquipY) == 0xa2);
    static_assert(offsetof(InventoryItems, EquipX) == 0xa3 && offsetof(InventoryItems, KillCount) == 0xa4);
    static_assert(offsetof(InventoryItems, TilePuzzle) == 0xa6 && offsetof(InventoryItems, MainItems) == 0xa8);
    static_assert(offsetof(InventoryItems, ExtraItems) == 0xac && offsetof(InventoryItems, SleepyMushroomTime) == 0xb0);
    static_assert(offsetof(InventoryItems, WarpPoints) == 0xb8 && offsetof(InventoryItems, BottledFairies) == 0xc0);
    static_assert(offsetof(InventoryItems, GoldenLeaves) == 0xc2 && offsetof(InventoryItems, FieldPosition) == 0xc4);
    static_assert(offsetof(InventoryItems, IndoorPosition) == 0xd0 && offsetof(InventoryItems, RestartPos) == 0xdc);
    static_assert(offsetof(InventoryItems, Figurines) == 0xe8 && offsetof(InventoryItems, ZoneId) == 0xec);
    static_assert(offsetof(InventoryItems, RapidsBestTime) == 0xf0 && offsetof(InventoryItems, FishSize) == 0xf8);
    static_assert(offsetof(InventoryItems, FishCaught) == 0x118 && offsetof(InventoryItems, HeroMode) == 0x120);
    static_assert(offsetof(InventoryItems, RareFishSign) == 0x122 && offsetof(InventoryItems, Obtained) == 0x124);
    static_assert(offsetof(InventoryItems, LevelEnterTime) == 0x138);
    static_assert(offsetof(InventoryItems, PreviousLevelTime) == 0x140 && sizeof(InventoryItems) == 0x160);

    inline InventoryItems* Items() { return *Inventory_ptr; }
}
