#pragma once
#include "../lib.hpp"
#include "offsets.hpp"
#include "Hooks/Common/actorswitch.hpp"
#include <string>

// AreaShellDropper call drop table
HOOK_DEFINE_INLINE(AreaShellDropper__FetchItemDrop) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        long arg1 = ctx->X[19];
        ActorSwitchManager::SetSwitch(arg1 + 0x250, 0, true);
    }
};

// Force value to 0 (false) so that it thinks the player does not have the shell
// Otherwise the AreaShellDropper actor will get deleted
HOOK_DEFINE_INLINE(AreaShellDropper__CheckShell) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        ctx->X[9] = 0;
    }
};

// Function running every frame that checks if the player has the shell and deletes itself if so
// Not sure why this exists when the game checks separately when a bomb explodes
// We can just replace it with an empty func
HOOK_DEFINE_REPLACE(AreaShellDropper__Delete) {
    static void Callback(long arg1) {

    }
};

// Tree call drop table
HOOK_DEFINE_TRAMPOLINE(ObjTree_CallDropTableOnBonk) {
    static void Callback(long arg1, long arg2, long arg3, long arg4, long arg5) {
        u64 hash = *(u64*)(arg1 + 0x98);
        if (hash == 7061688197741586161 || hash == 8430773688343466954 || hash == 5188616263732613924) {
            ActorSwitchManager::SetSwitch(arg1 + 0x250, 0, true);
        }
        else {
            Orig(arg1, arg2, arg3, arg4, arg5);
        }
    }
};

// Make pots set actor switch
HOOK_DEFINE_TRAMPOLINE(ObjPot__CallDropTable) {
    static u64 Callback(u64 arg1, char arg2) {
        u64 hash = *(u64*)(arg1 + 0x98);
        if (hash == 7638146751023334438) {
            ActorSwitchManager::SetSwitch(arg1 + 0x250, 0, true);
            return arg1;
        }
        else {
            return Orig(arg1, arg2);
        }
    }
};

namespace SeashellRandomizer {
    void InstallHooks() {
        AreaShellDropper__FetchItemDrop::InstallAtOffset(offset_manager.Offset(0x856fc));
        AreaShellDropper__CheckShell::InstallAtOffset(offset_manager.Offset(0x84a44));
        AreaShellDropper__Delete::InstallAtOffset(offset_manager.Offset(0x85510));
        ObjTree_CallDropTableOnBonk::InstallAtOffset(offset_manager.Offset(0xcb1f30));
        ObjPot__CallDropTable::InstallAtOffset(offset_manager.Offset(0xb2ae50));
    }
}
