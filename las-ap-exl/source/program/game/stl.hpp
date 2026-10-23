#pragma once
// The game's standard library types as its functions take them and its objects hold them: string views, libc++
// shared pointers, vectors, lists and strings.
#include "lib.hpp"
#include <cstddef>
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

    // libc++ std::weak_ptr: the same pair, holding a weak reference.
    struct WeakPtr {
        void* ptr = nullptr;
        void* ctrl = nullptr;
    };

    // A shared_ptr a function returns: not trivial, so it comes back through x8, as the game's functions return it.
    struct SharedPtrOut {
        void* ptr = nullptr;
        void* ctrl = nullptr;
        ~SharedPtrOut() {}
    };

    // libc++ std::vector.
    template<typename T>
    struct Vector {
        T* first;
        T* last;
        T* capacityEnd;

        T* begin() const { return first; }
        T* end() const { return last; }
        size_t size() const { return static_cast<size_t>(last - first); }
        bool empty() const { return first == last; }
        T& operator[](size_t i) const { return first[i]; }
    };
    static_assert(sizeof(Vector<void*>) == 0x18);

    // libc++ std::list: a node {previous, next, payload}; the list is its end node {last, first} and the size.
    struct ListNode {
        ListNode* previous;
        ListNode* next;
    };
    struct List {
        ListNode end;
        u64 size;
    };
    static_assert(offsetof(List, size) == 0x10 && sizeof(List) == 0x18);

    // libc++ std::string: short (byte 0 bit 0 clear: the length in byte 0's other bits, the characters at +1) or long
    // ({capacity | 1, length, characters*}).
    struct String {
        u8 raw[0x18];

        std::string_view View() const {
            if ((raw[0] & 1) == 0) return {reinterpret_cast<const char*>(raw + 1), static_cast<size_t>(raw[0] >> 1)};
            return {*reinterpret_cast<const char* const*>(raw + 0x10), *reinterpret_cast<const size_t*>(raw + 8)};
        }
    };
    static_assert(sizeof(String) == 0x18);

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
