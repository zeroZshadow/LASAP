#include "config/config.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

PatchConfig global_config;

namespace {
    // Lowercase, without spaces.
    std::string Normalized(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
        return s;
    }

    void ParseMovement(PatchConfig& config, const std::string& key, const std::string& value) {
        Logging.Log("%s | %s", key.c_str(), value.c_str());
        auto& movement = config.movement;
        if (key == "full_360" && value == "true") movement.full_360 = true;
        else if (key == "walk_speed") movement.walk_speed = std::stof(value);
        else if (key == "dash_speed") movement.dash_speed = std::stof(value);
        else if (key == "swim_speed") movement.swim_speed = std::stof(value);
        else if (key == "pop_speed") movement.pop_speed = std::stof(value);
    }

    void ParseNiceItems(PatchConfig& config, const std::string& key, bool on) {
        if (key == "bombs" && on) config.nice_items.bombs = true;
        else if (key == "hookshot" && on) config.nice_items.hookshot = true;
        else if (key == "rod" && on) config.nice_items.rod = true;
    }

    void ParseDamage(PatchConfig& config, const std::string& key, const std::string& value) {
        if (key != "mode") return;
        config.damage.mode = value == "ohko"   ? DamageMode::Ohko
                             : value == "none" ? DamageMode::None : DamageMode::Normal;
    }

    void ParseRandomizer(PatchConfig& config, const std::string& key, const std::string& value) {
        auto& randomizer = config.randomizer;
        bool on = value == "true";
        if (key == "enabled" && on) randomizer.enabled = true;
        else if (key == "free_book" && on) randomizer.free_book = true;
        else if (key == "stealing") {
            randomizer.stealing = value == "always" ? Stealing::Always
                                  : value == "never" ? Stealing::Never : Stealing::Normal;
        }
        else if (key == "enemies" && on) randomizer.enemies = true;
        else if (key == "enemy_sizes" && on) randomizer.enemy_sizes = true;
    }

    void ParseTweaks(PatchConfig& config, const std::string& key, const std::string& value) {
        auto& tweaks = config.tweaks;
        bool on = value == "true";
        if (key == "instant_text" && on) tweaks.instant_text = true;
        else if (key == "no_low_health_beep" && on) tweaks.no_low_health_beep = true;
        else if (key == "no_guardian_acorn" && on) tweaks.no_guardian_acorn = true;
        else if (key == "no_piece_of_power" && on) tweaks.no_piece_of_power = true;
        else if (key == "easy_fishing" && on) tweaks.easy_fishing = true;
        else if (key == "no_30fps_drops") {
            tweaks.no_30fps_drops = on ? FpsDrops::On : value == "emulator" ? FpsDrops::Emulator : FpsDrops::Off;
        }
        else if (key == "instant_pickups" && on) tweaks.instant_pickups = true;
    }
}

void PatchConfig::parse(const std::string& text) {
    std::stringstream lines(text);
    std::string line, section;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with(';') || line.starts_with('#')) continue;
        if (line.starts_with('[') && line.find(']') != std::string::npos) {
            section = line.substr(1, line.find(']') - 1);
            Logging.Log(section);
        }
        if (line.empty()) section.clear();
        size_t eq = line.find('=');
        if (section.empty() || eq == std::string::npos) continue;
        std::string key = Normalized(line.substr(0, eq));
        std::string value = Normalized(line.substr(eq + 1));
        bool on = value == "true";
        if (section == "movement") ParseMovement(*this, key, value);
        else if (section == "nice_items") ParseNiceItems(*this, key, on);
        else if (section == "blur_removal" && key == "enabled" && on) blur_removal.enabled = true;
        else if (section == "stable_fps" && key == "enabled" && on) stable_fps.enabled = true;
        else if (section == "damage") ParseDamage(*this, key, value);
        else if (section == "randomizer") ParseRandomizer(*this, key, value);
        else if (section == "tweaks") ParseTweaks(*this, key, value);
        else if (section == "archipelago") {
            if (key == "seed_hash") archipelago.seed_hash = value;
            else if (key == "port") archipelago.port = std::atoi(value.c_str());
        }
    }
}
