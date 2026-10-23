#pragma once
// Fields and virtual functions of the game's objects, by offset.
#include "lib.hpp"

namespace Game {
    // The field at that offset of an object.
    template<typename T>
    T& Field(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<u8*>(base) + offset); }

    template<typename T>
    const T& Field(const void* base, size_t offset) {
        return *reinterpret_cast<const T*>(static_cast<const u8*>(base) + offset);
    }

    // The field's value, or T{} for a null object: reads along a chain of pointers that may end early.
    template<typename T>
    T At(const void* base, size_t offset) { return base != nullptr ? Field<T>(base, offset) : T{}; }

    inline uintptr_t* VTable(const void* object) { return *reinterpret_cast<uintptr_t* const*>(object); }

    // Calls the virtual function at that byte offset of the object's vtable with the object first:
    // CallVirtual<u8*(const char*, bool)>(root, 0x60, name, true).
    template<typename Sig>
    struct Virtual;

    template<typename R, typename... Args>
    struct Virtual<R(Args...)> {
        static R Call(const void* object, size_t offset, Args... args) {
            return reinterpret_cast<R (*)(void*, Args...)>(VTable(object)[offset / 8])(const_cast<void*>(object), args...);
        }
    };

    template<typename Sig, typename... Args>
    decltype(auto) CallVirtual(const void* object, size_t offset, Args&&... args) {
        return Virtual<Sig>::Call(object, offset, std::forward<Args>(args)...);
    }
}
