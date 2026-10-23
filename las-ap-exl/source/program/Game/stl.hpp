#pragma once
// The game's standard library types as its functions take them: string views and libc++ shared pointers.
#include "lib.hpp"
#include <cstring>
#include <string_view>

namespace Game {
    // {characters, length}: the game's names and paths (not terminated).
    struct StrView {
        const char* data = nullptr;
        size_t size = 0;

        constexpr StrView() = default;
        constexpr StrView(const char* d, size_t n) : data(d), size(n) {}
        StrView(const char* s) : data(s), size(s != nullptr ? std::strlen(s) : 0) {}
        constexpr StrView(std::string_view s) : data(s.data()), size(s.size()) {}
        constexpr std::string_view View() const { return {data, size}; }
    };

    // libc++ std::shared_ptr: the object and its control block (+0x8 shared owners - 1, -1 once the object is gone;
    // +0x10 weak owners - 1; vtable +0x10 __on_zero_shared).
    struct SharedPtr {
        void* ptr = nullptr;
        void* ctrl = nullptr;
    };

    // A shared_ptr a function returns: not trivial, so it comes back through x8, as the game's functions return it.
    struct SharedPtrOut {
        void* ptr = nullptr;
        void* ctrl = nullptr;
        ~SharedPtrOut() {}
    };

    void AddOwner(void* ctrl);
    void AddWeakOwner(void* ctrl);
    // Drops a shared owner: the last destroys the object and drops the owners' weak reference.
    void ReleaseOwner(void* ctrl);
    void ReleaseWeakOwner(void* ctrl);
    // The object is still there (a weak reference that would lock).
    bool Alive(const void* ctrl);
    // Drops the owner sp holds and empties it.
    void Release(SharedPtr& sp);
}
