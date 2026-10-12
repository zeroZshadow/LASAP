#pragma once
#include "lib.hpp"
#include "offsets.hpp"
#include "Config/config.hpp"

namespace patch = exl::patch;
namespace inst = exl::armv8::inst;
namespace reg = exl::armv8::reg;

void blurRemoval() {
    // Removes the blur around the edge of the screen
    // Credits to theboy181 for the patch
    patch::CodePatcher p(offset_manager.Offset(0x16cbd73));
    p.Write("NoTiltShift");
}

void stableFPS() {
    // stable 60fps
    // credits to HerculeHercule1 for the patch
    patch::CodePatcher p(offset_manager.Offset(0x1019608));
    p.WriteInst(inst::Nop());
    p.Seek(offset_manager.Offset(0x1019628));
    p.WriteInst(inst::Nop());
}

void niceItems() {
    patch::CodePatcher p(0);

    // Nice Bombs - no limit to how many can be placed at once
    if (global_config.nice_items.bombs) {
        p.Seek(offset_manager.Offset(0xd52958));
        p.WriteInst(inst::Movz(reg::W8, 1));
    }

    // Nice Hookshot - hookshot any surface
    if (global_config.nice_items.hookshot) {
        p.Seek(offset_manager.Offset(0xd7f188));
        p.WriteInst(inst::Movz(reg::W20, 3));
    }

    // Nice Magic Rod - no projectile count limit
    if (global_config.nice_items.rod) {
        p.Seek(offset_manager.Offset(0xd51698));
        p.WriteInst(inst::CmpImmediate(reg::X19, 0x10));
    }
}

void damageModifier() {
    int dmg = 80; // ohko
    if (global_config.damage.mode == "none") {
        dmg = 0;
    }

    patch::CodePatcher p(offset_manager.Offset(0xd4c754)); // normal damage
    p.WriteInst(inst::SubImmediate(reg::W22, reg::W8, dmg));

    p.Seek(offset_manager.Offset(0xdb1f74)); // fall/drown damage
    p.WriteInst(inst::SubImmediate(reg::W8, reg::W21, dmg));

    p.Seek(offset_manager.Offset(0xd7c8c8)); // trap damage
    p.WriteInst(inst::SubImmediate(reg::W20, reg::W8, dmg));

    p.Seek(offset_manager.Offset(0xd96950)); // blaino damage
    p.WriteInst(inst::SubImmediate(reg::W8, reg::W23, dmg));

    // there is also hinox throw damage, I am too lazy to find it right now
}

void randoFixes() {
    // remove checking ShieldGet flag so that the user can immediately pause
    // this is important since Tarin uses the ShieldGet flag as well but we may want custom spawn locations
    patch::CodePatcher p(offset_manager.Offset(0xeacf2c));
    p.WriteInst(inst::Movz(reg::W0, 1));

    // ignore companions when opening color dungeon
    p.Seek(offset_manager.Offset(0xc868d4));
    p.WriteInst(inst::Nop());
    p.Seek(offset_manager.Offset(0xc868d8));
    p.WriteInst(inst::CmpImmediate(reg::W8, 0));

    // Make EnemySoldierIronBall ignore GoldenLeaf[4]
    p.Seek(offset_manager.Offset(0x6a62f8));
    p.WriteInst(inst::Nop());

    // Rewrite FlowControl::CompareInt event to check if the values are equal
    // To match FlowControl::CompareString, it returns 0 if they are equal, 1 if not
    // This allows us to check the index of items through the EventFlow system
    // The main purpose of this will be for Keysanity to know which dungeon text to display
    p.Seek(offset_manager.Offset(0x8049d8));
    p.WriteInst(inst::Movz(reg::W8, 1));

    // NPCs hold the proper item model before giving it to the player
    // This is done by changing the itemID of the model to a new Items.gsheet entry with the proper model
    p.Seek(offset_manager.Offset(0x9fa0f0)); // bay-fisherman
    p.WriteInst(inst::Movz(reg::W4, 200));
    p.Seek(offset_manager.Offset(0xa40374)); // syrup
    p.WriteInst(inst::Movz(reg::W3, 201));
    p.Seek(offset_manager.Offset(0xa534a4)); // walrus
    p.WriteInst(inst::Movz(reg::W8, 202));

    // Songs, tunics, and capacity upgrades show the correct item model
    // This is done by making them go to the default itemID case
    // Default case means it will use its own npcKey in Items.gsheet
    p.Seek(offset_manager.Offset(0xd798c4));
    p.WriteInst(inst::Branch(0xd799f8 - 0xd798c4));
    p.Seek(offset_manager.Offset(0xd79814));
    p.WriteInst(inst::Branch(0xd799f8 - 0xd79814));
    p.Seek(offset_manager.Offset(0xd79804));
    p.WriteInst(inst::Branch(0xd799f8 - 0xd79804));

    // item actors ignore inventory for spawn condition
    // this works by forcing the condition to false, making it use the actor switch flag instead
    p.Seek(offset_manager.Offset(0x8c45b8));
    p.WriteInst(inst::Movz(reg::W0, 0));

    // Remove the actual call to the drop table for AreaShellDropper
    // we will then set the actor switch in its place
    p.Seek(offset_manager.Offset(0x856e4));
    p.WriteInst(inst::Nop());
}

void underwaterItems() {
    // force the "under_water" render property to true
    // this makes all item models properly show under water
    patch::CodePatcher p(offset_manager.Offset(0xff54bc));
    p.WriteInst(inst::Nop());
    p.Seek(offset_manager.Offset(0xff54c0));
    p.WriteInst(inst::Movz(reg::W27, 1));
}

void randoOptional() {
    patch::CodePatcher p(0);

    // free book - read the book of secrets without the magnifying lens
    if (global_config.randomizer.free_book) {
        p.Seek(offset_manager.Offset(0x7e3004));
        p.WriteInst(inst::Movz(reg::W0, 1));
    }

    // stealing
    p.Seek(offset_manager.Offset(0xa4a8f0));
    if (global_config.randomizer.stealing == "always") {
        p.WriteInst(inst::Branch(0xa4a910 - 0xa4a8f0));
    }
    else if (global_config.randomizer.stealing == "never") {
        p.WriteInst(inst::Nop());
    }
}

void runCodePatches() {
    EXL_ASSERT(global_config.initialized);
    if (global_config.blur_removal.enabled) {
        blurRemoval();
    }
    if (global_config.stable_fps.enabled) {
        stableFPS();
    }
    // niceItems();
    // if (global_config.damage.mode != "normal") {
    //     damageModifier();
    // }
    if (global_config.randomizer.enabled) {
        // randoFixes();
        underwaterItems();
        randoOptional();
    }
}
