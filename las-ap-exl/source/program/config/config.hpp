#pragma once
// The module's settings: sd:/config/lasr-exl/config.ini, which the patcher writes (DESIGN.md).
#include "lib.hpp"
#include <string>

enum class DamageMode { Normal, Ohko, None };
enum class Stealing { Normal, Always, Never };
// No 30 FPS Drops: on emulators only, always or never.
enum class FpsDrops { Emulator, On, Off };

struct PatchConfig {
    struct {
        bool full_360 = false;
        float walk_speed = 1.0f;
        float dash_speed = 1.0f;
        float swim_speed = 1.0f;
        float pop_speed = 1.15f;
    } movement;

    struct {
        bool bombs = false;
        bool hookshot = false;
        bool rod = false;
    } nice_items;

    struct {
        bool enabled = false;
    } blur_removal;

    struct {
        bool enabled = false;
    } stable_fps;

    struct {
        DamageMode mode = DamageMode::Normal;
    } damage;

    struct {
        bool enabled = false;
        bool free_book = false;
        Stealing stealing = Stealing::Normal;
        bool enemies = false;
        bool enemy_sizes = false;
    } randomizer;

    struct {
        bool instant_text = false;
        bool no_low_health_beep = false;
        bool no_guardian_acorn = false;
        bool no_piece_of_power = false;
        bool easy_fishing = false;
        FpsDrops no_30fps_drops = FpsDrops::Emulator;
        bool instant_pickups = false;  // items Link finds and items from the server show as a popup, not an item-get
        bool bouncy_items = false;  // the drop actor is a ball, launched at random (ap_drop.cpp; Bouncy Items)
    } tweaks;

    struct {
        std::string seed_hash;
        int port = 0;  // 0: AP::PORT
    } archipelago;

    // Reads config.ini: [section] lines, then key = value lines (case and spaces ignored); ; and # start comments. A
    // blank line ends the section.
    void parse(const std::string& text);
};

extern PatchConfig global_config;
