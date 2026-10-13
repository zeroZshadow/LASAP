#pragma once
#include "lib.hpp"
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>

struct PatchConfig {
    bool initialized = false;

    struct {
        bool enabled;
    } debug_mode;

    struct {
        bool full_360;
        float walk_speed = 1.0f;
        float dash_speed = 1.0f;
        float swim_speed = 1.0f;
        float pop_speed = 1.15f;
    } movement;

    struct {
        bool bombs;
        bool hookshot;
        bool rod;
    } nice_items;

    struct {
        bool enabled;
    } blur_removal;

    struct {
        bool enabled;
    } stable_fps;

    struct {
        std::string mode;
    } damage;

    struct {
        bool enabled;
        bool free_book;
        std::string stealing;
        bool enemies;
        bool enemy_sizes;
    } randomizer;

    struct {
        bool instant_text;
        bool no_low_health_beep;
    } tweaks;

    struct {
        std::string seed_hash;
        int port = 0; // 0: AP::PORT
    } archipelago;

    void parse(std::string config_str) {
        std::stringstream ss(config_str);
        std::string line;
        std::string current_section;

        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // ignore comments
            if (line.starts_with(';') || line.starts_with('#')) {
                continue;
            }

            // read section
            if (line.starts_with('[') && line.find(']') != std::string::npos) {
                current_section = line.substr(1, line.find(']') - 1);
                Logging.Log(current_section);
            }

            // remove section if line is blank
            if (line.empty()) {
                current_section = "";
            }

            // ignore if there is no section
            if (current_section.empty()) {
                continue;
            }

            // get index of = within string, ignore if not found
            int pos = (int)line.find('=');
            if (pos < 0) {
                continue;
            }

            // get key:value in lowercase with whitespace removed
            std::string key = line.substr(0, pos);
            std::transform(key.begin(), key.end(), key.begin(),
                [](unsigned char c){ return std::tolower(c); });
            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());

            std::string value = line.substr(pos + 1);
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c){ return std::tolower(c); });
            value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());

            // set config properties based on section and key:value
            if (current_section == "debug_mode") {
                parseDebugMode(key, value);
            }
            else if (current_section == "movement") {
                parseMovement(key, value);
            }
            else if (current_section == "nice_items") {
                parseNiceItems(key, value);
            }
            else if (current_section == "blur_removal") {
                parseBlurRemoval(key, value);
            }
            else if (current_section == "stable_fps") {
                parseStableFPS(key, value);
            }
            else if (current_section == "damage") {
                parseDamage(key, value);
            }
            else if (current_section == "randomizer") {
                parseRandomizer(key, value);
            }
            else if (current_section == "tweaks") {
                parseTweaks(key, value);
            }
            else if (current_section == "archipelago") {
                if (key == "seed_hash") {
                    archipelago.seed_hash = value;
                }
                else if (key == "port") {
                    archipelago.port = std::atoi(value.c_str());
                }
            }
        }

        initialized = true;
    }

    void parseDebugMode(std::string key, std::string value) {
        if (key == "enabled" && value == "true") {
            debug_mode.enabled = true;
        }
    }

    void parseMovement(std::string key, std::string value) {
        Logging.Log(key + " | " + value);
        if (key == "full_360" && value == "true") {
            movement.full_360 = true;
        }
        else if (key == "walk_speed") {
            movement.walk_speed = std::stof(value);
        }
        else if (key == "dash_speed") {
            movement.dash_speed = std::stof(value);
        }
        else if (key == "swim_speed") {
            movement.swim_speed = std::stof(value);
        }
        else if (key == "pop_speed") {
            movement.pop_speed = std::stof(value);
        }
    }

    void parseNiceItems(std::string key, std::string value) {
        if (key == "bombs" && value == "true") {
            nice_items.bombs = true;
        }
        else if (key == "hookshot" && value == "true") {
            nice_items.hookshot = true;
        }
        else if (key == "rod" && value == "true") {
            nice_items.rod = true;
        }
    }

    void parseBlurRemoval(std::string key, std::string value) {
        if (key == "enabled" && value == "true") {
            blur_removal.enabled = true;
        }
    }

    void parseStableFPS(std::string key, std::string value) {
        if (key == "enabled" && value == "true") {
            stable_fps.enabled = true;
        }
    }

    void parseDamage(std::string key, std::string value) {
        if (key == "mode") {
            damage.mode = value;
        }
    }

    void parseTweaks(std::string key, std::string value) {
        if (key == "instant_text" && value == "true") {
            tweaks.instant_text = true;
        }
        else if (key == "no_low_health_beep" && value == "true") {
            tweaks.no_low_health_beep = true;
        }
    }

    void parseRandomizer(std::string key, std::string value) {
        if (key == "enabled" && value == "true") {
            randomizer.enabled = true;
        }
        else if (key == "free_book" && value == "true") {
            randomizer.free_book = true;
        }
        else if (key == "stealing") {
            randomizer.stealing = value;
        }
        else if (key == "enemies" && value == "true") {
            randomizer.enemies = true;
        }
        else if (key == "enemy_sizes" && value == "true") {
            randomizer.enemy_sizes = true;
        }
    }
};

extern PatchConfig global_config;