#pragma once
// Code patches of the game by config.ini: each an instruction at an offset of game 1.0.0 (Game::Offset maps it).
#include "lib.hpp"
#include "Game/addr.hpp"
#include "Config/config.hpp"
#include <array>
#include <span>

namespace Patches {
    namespace inst = exl::armv8::inst;
    namespace reg = exl::armv8::reg;

    struct Patch {
        uintptr_t at;
        exl::armv8::InstBitSet inst;
    };

    // A branch at `at` to `to`.
    constexpr Patch BranchTo(uintptr_t at, uintptr_t to) {
        return {at, inst::Branch(static_cast<int>(static_cast<s64>(to) - static_cast<s64>(at)))};
    }

    inline void Apply(std::span<const Patch> patches) {
        exl::patch::CodePatcher p(0);
        for (const Patch& patch : patches) {
            p.Seek(Game::Offset(patch.at));
            p.WriteInst(patch.inst);
        }
    }

    // Removes the blur around the edge of the screen (credits to theboy181): the shader option's name no longer
    // matches.
    constexpr uintptr_t kTiltShiftName = 0x16cbd73;

    // Stable 60 fps (credits to HerculeHercule1)
    constexpr Patch kStableFps[] = {
        {0x1019608, inst::Nop()},
        {0x1019628, inst::Nop()},
    };

    // Nice Bombs: no limit to how many can be placed at once
    constexpr Patch kNiceBombs[] = {{0xd52958, inst::Movz(reg::W8, 1)}};
    // Nice Hookshot: hookshot any surface
    constexpr Patch kNiceHookshot[] = {{0xd7f188, inst::Movz(reg::W20, 3)}};
    // Nice Magic Rod: no projectile count limit
    constexpr Patch kNiceRod[] = {{0xd51698, inst::CmpImmediate(reg::X19, 0x10)}};

    // Damage taken: the health subtracted, the same whatever hit (80: one hit kills; there is also the Hinox's
    // throw, not found yet)
    constexpr std::array<Patch, 4> DamagePatches(u32 damage) {
        return {{
            {0xd4c754, inst::SubImmediate(reg::W22, reg::W8, damage)},   // normal damage
            {0xdb1f74, inst::SubImmediate(reg::W8, reg::W21, damage)},   // fall and drown damage
            {0xd7c8c8, inst::SubImmediate(reg::W20, reg::W8, damage)},   // trap damage
            {0xd96950, inst::SubImmediate(reg::W8, reg::W23, damage)},   // Blaino's damage
        }};
    }
    constexpr u32 kOhkoDamage = 80;

    constexpr Patch kRandoFixes[] = {
        // The pause menu without the ShieldGet flag (Tarin uses the flag too, and spawn locations may differ)
        {0xeacf2c, inst::Movz(reg::W0, 1)},
        // The Color Dungeon opens whatever companion follows Link
        {0xc868d4, inst::Nop()},
        {0xc868d8, inst::CmpImmediate(reg::W8, 0)},
        // EnemySoldierIronBall ignores GoldenLeaf[4]
        {0x6a62f8, inst::Nop()},
        // NPCs hold the item they give: new Items.gsheet rows with its model
        {0x9fa0f0, inst::Movz(reg::W4, 200)},   // the fisherman under the bridge
        {0xa40374, inst::Movz(reg::W3, 201)},   // Syrup
        {0xa534a4, inst::Movz(reg::W8, 202)},   // the walrus
        // Songs, tunics and capacity upgrades show their own model: the item model switch's default case, which uses
        // the item's npcKey in Items.gsheet
        BranchTo(0xd798c4, 0xd799f8),
        BranchTo(0xd79814, 0xd799f8),
        BranchTo(0xd79804, 0xd799f8),
        // AreaShellDropper does not call its drop table: the module sets its actor switch instead (seashells.hpp)
        {0x856e4, inst::Nop()},
        // Item actors appear by their actor switch flag alone (ItemActor_CanAppear's Inventory_HasItem(item, 1,
        // index) forced false: without it, LASR's dig items, ItemSlimeKey actors, hide while the Slime Key is owned,
        // and Kiki's stick and the honeycomb while that trade item is held)
        {0x8c45b8, inst::Movz(reg::W0, 0)},
        // Item models show under water: the "under_water" render property forced true
        {0xff54bc, inst::Nop()},
        {0xff54c0, inst::Movz(reg::W27, 1)},
    };

    // Free book: the Book of Secrets reads without the Magnifying Lens
    constexpr Patch kFreeBook[] = {{0x7e3004, inst::Movz(reg::W0, 1)}};
    // Stealing from the shop: always, or never
    constexpr Patch kStealAlways[] = {BranchTo(0xa4a8f0, 0xa4a910)};
    constexpr Patch kStealNever[] = {{0xa4a8f0, inst::Nop()}};

    inline void Run() {
        if (global_config.blur_removal.enabled) {
            exl::patch::CodePatcher p(Game::Offset(kTiltShiftName));
            p.Write("NoTiltShift");
        }
        if (global_config.stable_fps.enabled) Apply(kStableFps);
        if (global_config.nice_items.bombs) Apply(kNiceBombs);
        if (global_config.nice_items.hookshot) Apply(kNiceHookshot);
        if (global_config.nice_items.rod) Apply(kNiceRod);
        if (global_config.damage.mode != DamageMode::Normal) {
            Apply(DamagePatches(global_config.damage.mode == DamageMode::None ? 0 : kOhkoDamage));
        }
        if (global_config.randomizer.enabled) {
            Apply(kRandoFixes);
            if (global_config.randomizer.free_book) Apply(kFreeBook);
            if (global_config.randomizer.stealing == Stealing::Always) Apply(kStealAlways);
            if (global_config.randomizer.stealing == Stealing::Never) Apply(kStealNever);
        }
    }
}
