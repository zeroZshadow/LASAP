#include "game/actor.hpp"
#include "game/addr.hpp"
#include "game/switch.hpp"
#include "hooks.hpp"
#include <algorithm>
#include <iterator>

// Seashells in the shuffle: the trees, pots and AreaShellDropper (the seashells a bomb uncovers) that drop one set
// their actor switch instead, whose flag the patcher's item uses.
namespace {
    using Game::Actor;

    // The dropper's, tree's and pot's switch the patcher's seashell item appears on
    enum class ShellSwitch { Dropped = 0 };

    // The trees and the pot that drop a seashell, by LEB hash
    constexpr u64 kShellTrees[] = {7061688197741586161ULL, 8430773688343466954ULL, 5188616263732613924ULL};
    constexpr u64 kShellPot = 7638146751023334438ULL;

    // AreaShellDropper's drop (x19 the dropper), where it calls its drop table (patched out, patches.hpp); its check
    // whether the player has the shell (x9), which would delete it; its every-frame check that deletes it once the
    // player has the shell. Trees call their drop table when bonked, pots theirs when broken.
    constexpr uintptr_t kDropperDrop = 0x856fc, kDropperCheckShell = 0x84a44, kDropperDelete = 0x85510;
    constexpr uintptr_t kTreeBonkDrop = 0xcb1f30, kPotDrop = 0xb2ae50;

    void SetSwitch(Actor* actor) { Game::SetSwitch(*actor, ShellSwitch::Dropped, true); }

    HOOK_DEFINE_INLINE(AreaShellDropper_Drop) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) { SetSwitch(reinterpret_cast<Actor*>(ctx->X[19])); }
    };

    HOOK_DEFINE_INLINE(AreaShellDropper_CheckShell) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) { ctx->X[9] = 0; }
    };

    HOOK_DEFINE_REPLACE(AreaShellDropper_Delete) {
        static void Callback(long) {}
    };

    HOOK_DEFINE_TRAMPOLINE(ObjTree_BonkDrop) {
        static void Callback(Actor* actor, long arg2, long arg3, long arg4, long arg5) {
            if (std::find(std::begin(kShellTrees), std::end(kShellTrees), actor->lebKey) != std::end(kShellTrees)) {
                SetSwitch(actor);
            } else {
                Orig(actor, arg2, arg3, arg4, arg5);
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ObjPot_Drop) {
        static Actor* Callback(Actor* actor, char arg2) {
            if (actor->lebKey == kShellPot) {
                SetSwitch(actor);
                return actor;
            }
            return Orig(actor, arg2);
        }
    };
}

namespace Seashells {
    void Install() {
        AreaShellDropper_Drop::InstallAtOffset(Game::Offset(kDropperDrop));
        AreaShellDropper_CheckShell::InstallAtOffset(Game::Offset(kDropperCheckShell));
        AreaShellDropper_Delete::InstallAtOffset(Game::Offset(kDropperDelete));
        ObjTree_BonkDrop::InstallAtOffset(Game::Offset(kTreeBonkDrop));
        ObjPot_Drop::InstallAtOffset(Game::Offset(kPotDrop));
    }
}
