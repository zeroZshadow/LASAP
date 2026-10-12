/*
    The purpose of this is to turn the companion byte into a bitfield of companions
    As such there are tons of small hooks that need to be made at every reference to this byte
*/

#pragma once
#include "../lib.hpp"
#include "Config/config.hpp"
#include "Game/Data/inventory.hpp"

/*
    Event hooks
    Currently no need to hook CheckAlone since checking 0 means no bits are set
    Putting this here to remind myself that we might want to use those extra 4 bits for something
    In which case we will need to hook CheckAlone to check the first 4 bits
*/
HOOK_DEFINE_REPLACE(CheckWithBowWow) {
    static bool Callback() {
        return (*Game::Data::Inventory::Items)->Companions.BowWow;
    }
};

HOOK_DEFINE_REPLACE(CheckWithMarin) {
    static bool Callback() {
        return (*Game::Data::Inventory::Items)->Companions.Marin;
    }
};

HOOK_DEFINE_REPLACE(CheckWithGhost) {
    static bool Callback() {
        return (*Game::Data::Inventory::Items)->Companions.Ghost;
    }
};

HOOK_DEFINE_REPLACE(CheckWithRooster) {
    static bool Callback() {
        return (*Game::Data::Inventory::Items)->Companions.Rooster;
    }
};

/*
    For each companion bit that is set, we will run the companion creation function
    We assign what the companion value normally would be to an int and use that in the creation code
    The player actor retains a pointer to the companion actor that will now point only to the last one created
    So far they all still act as normal in tested, but this should be noted in case we run into problems
    We may need to create our own pointers for each
*/

inline uint8_t companionCreationValue = 0;

// Calls the companion creation function which arg1 becomes a pointer to
HOOK_DEFINE_TRAMPOLINE(Player__UpdateCompanion) {
    static void Callback(long arg1, long arg2, long arg3, long arg4) {
        if (CheckWithBowWow::Callback()) {
            companionCreationValue = 1;
            Orig(arg1, arg2, arg3, arg4);
        }
        if (CheckWithMarin::Callback()) {
            companionCreationValue = 2;
            Orig(arg1, arg2, arg3, arg4);
        }
        if (CheckWithGhost::Callback()) {
            companionCreationValue = 3;
            Orig(arg1, arg2, arg3, arg4);
        }
        if (CheckWithRooster::Callback()) {
            companionCreationValue = 4;
            Orig(arg1, arg2, arg3, arg4);
        }
    }
};

// Creates the companion actor, using our own companionCreationValue instead of the companion byte
HOOK_DEFINE_TRAMPOLINE(Player__CreateCompanionActor) {
    static void Callback(long arg1, uint8_t companionValue, long arg3, char arg4, long arg5, long arg6) {
        companionValue = companionCreationValue;
        Orig(arg1, companionValue, arg3, arg4, arg5, arg6);
    }
};

namespace CompanionTweaks {
    void InstallHooks() {
        CheckWithBowWow::InstallAtOffset(0xd7db80);
        CheckWithMarin::InstallAtOffset(0xd7daf0);
        CheckWithGhost::InstallAtOffset(0xd7da60);
        CheckWithRooster::InstallAtOffset(0xd7d9d0);
        Player__UpdateCompanion::InstallAtOffset(0xd53c20);
        Player__CreateCompanionActor::InstallAtOffset(0x922670);
    }
}
