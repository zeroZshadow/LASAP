#include "ap/ap.hpp"
#include "hooks.hpp"
#include "nn/hid.h"
#include <atomic>

// The game reads controllers through different nn::hid entry points depending on the screen,
// so every variant is hooked at the SDK level. Each poll drives the Archipelago bridge.
namespace {
    std::atomic<u32> g_SeenKinds = 0;

    void OnPoll(u32 kind) {
        if (!(g_SeenKinds.fetch_or(1u << kind) & (1u << kind))) {
            Logging.Log("[AP] input poll kind %u", kind);
        }
        AP::Tick();
    }

    HOOK_DEFINE_TRAMPOLINE(GetNpadState_FullKey) {
        static void Callback(nn::hid::NpadFullKeyState* state, const u32& port) {
            Orig(state, port);
            AP::ApplyInjectedInput(reinterpret_cast<nn::hid::NpadBaseState*>(state));
            OnPoll(0);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GetNpadState_Handheld) {
        static void Callback(nn::hid::NpadHandheldState* state, const u32& port) {
            Orig(state, port);
            AP::ApplyInjectedInput(reinterpret_cast<nn::hid::NpadBaseState*>(state));
            OnPoll(1);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GetNpadState_JoyDual) {
        static void Callback(nn::hid::NpadJoyDualState* state, const u32& port) {
            Orig(state, port);
            AP::ApplyInjectedInput(reinterpret_cast<nn::hid::NpadBaseState*>(state));
            OnPoll(2);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GetNpadStates_FullKey) {
        static void Callback(nn::hid::NpadFullKeyState* states, s32 count, const u32& port) {
            Orig(states, count, port);
            if (count > 0) AP::ApplyInjectedInput(reinterpret_cast<nn::hid::NpadBaseState*>(states));
            OnPoll(3);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(GetNpadStates_Handheld) {
        static void Callback(nn::hid::NpadHandheldState* states, s32 count, const u32& port) {
            Orig(states, count, port);
            if (count > 0) AP::ApplyInjectedInput(reinterpret_cast<nn::hid::NpadBaseState*>(states));
            OnPoll(4);
        }
    };
}

namespace Input {
    void Install() {
        using namespace nn::hid;
        GetNpadState_FullKey::InstallAtFuncPtr(static_cast<void (*)(NpadFullKeyState*, const u32&)>(&GetNpadState));
        GetNpadState_Handheld::InstallAtFuncPtr(static_cast<void (*)(NpadHandheldState*, const u32&)>(&GetNpadState));
        GetNpadState_JoyDual::InstallAtFuncPtr(static_cast<void (*)(NpadJoyDualState*, const u32&)>(&GetNpadState));
        GetNpadStates_FullKey::InstallAtFuncPtr(static_cast<void (*)(NpadFullKeyState*, s32, const u32&)>(&GetNpadStates));
        GetNpadStates_Handheld::InstallAtFuncPtr(static_cast<void (*)(NpadHandheldState*, s32, const u32&)>(&GetNpadStates));
    }
}
