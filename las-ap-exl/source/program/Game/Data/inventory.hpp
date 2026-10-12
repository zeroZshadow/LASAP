#pragma once
#include "lib.hpp"
#include "symbols.hpp"
#include <bit>
#include <cstdint>

namespace Game::Data::Inventory {
    typedef bool Field;

    // struct CompanionBits {
    //     Field BowWow : 1;
    //     Field Marin : 1;
    //     Field Ghost : 1;
    //     Field Rooster : 1;
    // };

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

    struct InventoryItems {
        char unk1[0x80];
        int Rupees; // 0x80
        short Health; // 0x84
        char unk2[0x2];
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
        uint16_t unk3; // 0xa6
        MainItemBits MainItems; // 0xa8
        uint16_t unk4;
        ExtraItemBits ExtraItems; // 0xac
        char unk5;
        uint64_t unk6; // 0xb0
        char unk7[0x8];
        uint8_t BottledFairies; // 0xc0
        char unk8; // 0xc1
        uint8_t GoldenLeaves; // 0xc2
        char unk9[0x25];
        uint32_t Figurines; // 0xe8
        char unk10[0x34];
        bool HeroMode; // 0x120
        char unk11;
        uint8_t RareFishSign; // 0x122
    };

    inline InventoryItems** Items = reinterpret_cast<InventoryItems**>(g_symbols["InventoryPtr"]);
    inline uint8_t* Level = reinterpret_cast<uint8_t*>(g_symbols["LevelIndexPtr"]);

    inline void SetMaxHearts(uint8_t maxHearts) {
        (*Items)->HeartContainers = 0;
        (*Items)->HeartPieces = 0;

        uint8_t new_hearts = maxHearts - 3;
        uint8_t added_containers = 0;
        uint8_t added_pieces = 0;

        while (new_hearts > 0 && added_containers < 9) {
            (*Items)->HeartContainers |= (*Items)->HeartContainers + 1;
            new_hearts--;
            added_containers++;
        }

        while (new_hearts > 0) {
            (*Items)->HeartPieces |= (*Items)->HeartPieces + 1;
            added_pieces++;
            if (added_pieces == 4) {
                added_pieces = 0;
                new_hearts--;
            }
        }

        (*Items)->Health = (maxHearts * 4);
    }

    inline void ResetItems() {
       SetMaxHearts(3);
       (*Items)->ExtraItems = {};
       (*Items)->MainItems = {};
       (*Items)->UpgradeItems = {};
       (*Items)->Songs = {};
       (*Items)->EquipX = 0xff;
       (*Items)->EquipY = 0xff;
       (*Items)->Companion = 0;
    }

    inline void RefillConsumables() {
        uint8_t count = 0;

        if ((*Items)->MainItems.MagicPowder) {
            count = 20;
            if ((*Items)->UpgradeItems.MagicPowder_MaxUp) {
                count = 40;
            }
            (*Items)->PowderCount = count;
        }
        if ((*Items)->MainItems.Bomb) {
            count = 30;
            if ((*Items)->UpgradeItems.Bomb_MaxUp) {
                count = 60;
            }
            (*Items)->BombCount = count;
        }
        if ((*Items)->MainItems.Bow) {
            count = 30;
            if ((*Items)->UpgradeItems.Arrow_MaxUp) {
                count = 60;
            }
            (*Items)->ArrowCount = count;
        }
    }

    inline void RefillHealth() {
        int numContainers = std::popcount((*Items)->HeartContainers);
        int numPieces = std::popcount((*Items)->HeartPieces);
        (*Items)->Health = 12 + (numContainers * 4) + ((numPieces / 4) * 4);
    }
}