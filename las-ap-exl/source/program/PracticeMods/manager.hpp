#pragma once
#include "lib.hpp"
#include "Game/actor.hpp"
#include "Game/framework.hpp"
#include "Game/Actors/player.hpp"
#include "Game/Data/inventory.hpp"
#include "Hooks/Common/keyboard.hpp"
#include "PracticeMods/glitches.hpp"
#include "nn/hid.h"
#include <iostream>

inline bool isActive = false;
inline bool inputsFreed = false;
inline bool hasSavedPos = false;
inline Game::Vec3 savePos{};

namespace PracticeModManager {
    inline void Init() {
        KeyboardManager::Init();
        isActive = true;
        hasSavedPos = false;
    }

    inline bool CheckInputs(nn::hid::KeyboardKey keys[], int size) {
        for (int i = 0; i < size; ++i) {
            if (!KeyboardManager::IsKeyPressed(keys[i])) {
                return false;
            }
        }
        isActive = false;
        inputsFreed = false;
        return true;
    }

    inline void MainLoop(Game::Actors::Player* mPlayer) {
        KeyboardManager::MainLoop();

        if (!isActive) {
            return;
        }

        if (!inputsFreed) {
            inputsFreed = KeyboardManager::AreKeysFreed();
            return;
        }

        if (CheckInputs(Glitches::LikeLikeLaunch::keys, 2)) {
            Glitches::LikeLikeLaunch::Load();
        }
        else if (CheckInputs(Glitches::VillaSkip::keys, 2)) {
            Glitches::VillaSkip::Load();
        }
        else if (CheckInputs(Glitches::AnglerKeySkip::keys, 2)) {
            Glitches::AnglerKeySkip::Load();
        }
        else if (CheckInputs(Glitches::EarlyD6::keys, 2)) {
            Glitches::EarlyD6::Load();
        }
        else if (CheckInputs(Glitches::FlameCaveSkip::keys, 2)) {
            Glitches::FlameCaveSkip::Load();
        }
        else if (CheckInputs(Glitches::HookshotRide::keys, 2)) {
            Glitches::HookshotRide::Load();
        }
        else if (CheckInputs(Glitches::HinoxWarps::D2::keys, 3)) {
            Glitches::HinoxWarps::D2::Load();
        }
        else if (CheckInputs(Glitches::HinoxWarps::D7::keys, 3)) {
            Glitches::HinoxWarps::D7::Load();
        }
        else if (CheckInputs(Glitches::HinoxWarps::D8::keys, 3)) {
            Glitches::HinoxWarps::D8::Load();
        }

        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::M)) {
            Game::Data::Inventory::RefillHealth();
        }
        // Arrow keys to move Link whole tiles
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::UpArrow)) {
            mPlayer->playerCollision->coordsNew.pos.z -= 1.5f;
        }
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::DownArrow)) {
            mPlayer->playerCollision->coordsNew.pos.z += 1.5f;
        }
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::LeftArrow)) {
            mPlayer->playerCollision->coordsNew.pos.x -= 1.5f;
        }
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::RightArrow)) {
            mPlayer->playerCollision->coordsNew.pos.x += 1.5f;
        }

        // S and L to save and load Link's position (kinda janky)
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::S)) {
            savePos = mPlayer->playerCollision->coords.pos;
            hasSavedPos = true;
            Logging.Log("Saved position!");
        }
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::L)) {
            if (hasSavedPos) {
                mPlayer->playerCollision->coordsNew.pos = savePos;
                Logging.Log("Loaded position!");
            }
        }

        // // Warp to Home
        // else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::Home)) {
        //     LevelSystem::LoadLevel("MarinTarinHouse", Game::Vec3{7.5f, 0.0f, 8.5f});
        // }

        // position viewer
        else if (KeyboardManager::IsKeyJustPressed(nn::hid::KeyboardKey::P)) {
            Game::Vec3 pos = mPlayer->playerCollision->coords.pos;
            std::string outstr = std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z);
            Logging.Log(outstr);
        }
    }
}
