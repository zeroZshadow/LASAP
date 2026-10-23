#pragma once
// The game's code and data by address. Every address is an offset in the main module of game 1.0.0 (las.sym's
// address - 0x8504000); Addr maps it to the running game, the update's included (offsets.hpp).
#include "lib.hpp"
#include "offsets.hpp"

namespace Game {
    // The offset in the running game's main module (what exl::patch's patchers take).
    inline uintptr_t Offset(uintptr_t offset) { return offset_manager.Offset(offset); }

    inline uintptr_t Addr(uintptr_t offset) { return exl::util::modules::GetTargetOffset(Offset(offset)); }

    // Where the main module starts: an address minus Base() is its offset in the running game.
    inline uintptr_t Base() { return exl::util::modules::GetTargetStart(); }

    // A function of the game, typed: Func<void(u8*, int)> F{0x123} calls as F(p, 1), and a hook installs on it with
    // Hook::InstallAtFuncPtr(F.Ptr()), which checks that the hook's callback has the same type.
    template<typename Sig>
    struct Func;

    template<typename R, typename... Args>
    struct Func<R(Args...)> {
        using Ptr_t = R (*)(Args...);
        uintptr_t offset;

        Ptr_t Ptr() const { return reinterpret_cast<Ptr_t>(Addr(offset)); }
        R operator()(Args... args) const { return Ptr()(args...); }
    };

    // A global of the game: Get() is the object at that address.
    template<typename T>
    struct Global {
        uintptr_t offset;

        T* Ptr() const { return reinterpret_cast<T*>(Addr(offset)); }
        T& Get() const { return *Ptr(); }
        T& operator*() const { return Get(); }
    };
}
