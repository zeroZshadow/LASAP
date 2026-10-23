#pragma once
// The game's state machine (scripts/re/UI.md, State machines): Link's, the UIs', the actors'.
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game {
    // A state machine of N states (base vtable StateMachine_vtable: +0x10 the state count, +0x18 a state's name).
    // ChangeState runs the old state's exit (with the new state), then the new one's enter (with the old state).
    // Each state is three Itanium member-function pointers {fn, adj} (enter, update, exit) on the owner, or, with its
    // bit in objectStates, a state object at its first word (vtable +0x10 enter, +0x20 exit).
    template<int N>
    struct StateMachine {
        const uintptr_t* vtable;
        u32 framesInState;         // 0x8: cleared by a change, counted by the update
        u8 field_c;                // 0xc: cleared by a change
        u8 state;                  // 0xd: 0xff none
        u8 previous;               // 0xe
        u8 pad_f;
        Function onChange;         // 0x10: told each change (state, name)
        void* owner;               // 0x40: `this` for the member pointers
        u8 states[N][0x30];        // 0x48
        const uintptr_t* bitsVtable;  // a bit set object: its vtable, then the bits
        u8 objectStates[8];        // a bit per state (u8 up to 8 states, u16 up to 16, u64 up to 64)
        const char* names[N];
    };
    static_assert(offsetof(StateMachine<6>, state) == 0xd && offsetof(StateMachine<6>, onChange) == 0x10);
    static_assert(offsetof(StateMachine<6>, owner) == 0x40 && offsetof(StateMachine<6>, states) == 0x48);
    static_assert(offsetof(StateMachine<6>, objectStates) == 0x170 && offsetof(StateMachine<6>, names) == 0x178);
    static_assert(sizeof(StateMachine<4>) == 0x140 && sizeof(StateMachine<6>) == 0x1b0);
    static_assert(sizeof(StateMachine<15>) == 0x3a0 && sizeof(StateMachine<64>) == 0xe60);
}
