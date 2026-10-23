#pragma once
// The game's standard library types as its functions take them and its objects hold them: string views, libc++
// shared pointers, vectors, lists, maps, deques and strings, and the game's thread pool.
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

    // The game's fixed-capacity string: the characters (terminated) and the length.
    template<size_t N>
    struct FixedString {
        char data[N];
        u64 length;

        std::string_view View() const { return {data, length < N ? length : 0}; }
    };
    static_assert(offsetof(FixedString<0x40>, length) == 0x40 && sizeof(FixedString<0x40>) == 0x48);

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

    // A string copied into a shared buffer of its own (SharedString_Make): the buffer and its characters (terminated).
    struct SharedString {
        SharedPtr holder;
        const char* data;          // 0x10: holder's
        u64 length;                // 0x18
    };
    static_assert(offsetof(SharedString, data) == 0x10 && sizeof(SharedString) == 0x20);

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

    // libc++ std::map (a red-black tree): a node {left, right, parent, bool black, key, value}; the map is the first
    // node, its end node (whose left is the root) and the size.
    struct OrderedMap {
        void* first;
        void* root;                // 0x8: the end node's left
        u64 size;                  // 0x10
    };
    static_assert(offsetof(OrderedMap, size) == 0x10 && sizeof(OrderedMap) == 0x18);

    // libc++ std::deque: the block map (a split buffer: first, begin, end, capacity end), the first element's index
    // and the size.
    struct Deque {
        void** mapFirst;
        void** mapBegin;           // 0x8
        void** mapEnd;             // 0x10
        void** mapCapacityEnd;     // 0x18
        u64 start;                 // 0x20
        u64 size;                  // 0x28
    };
    static_assert(offsetof(Deque, start) == 0x20 && sizeof(Deque) == 0x30);

    // The game's thread pool (ThreadPool_Ctor, ThreadPool_Worker; 0x78 bytes): its threads run the jobs (Function)
    // queued in its deque.
    struct ThreadPool {
        Vector<u8> threads;        // 0x18 bytes each
        Deque jobs;                // 0x18
        u8 mutex[0x20];            // 0x48
        u8 wake[0x8];              // 0x68: a condition variable
        u8 stop;                   // 0x70: the threads return once the deque is empty
        u8 pad_71[0x7];
    };
    static_assert(offsetof(ThreadPool, jobs) == 0x18 && offsetof(ThreadPool, stop) == 0x70);
    static_assert(sizeof(ThreadPool) == 0x78);

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

    // libc++ std::unordered_map: the buckets, their count, the first node, the size and the max load factor (1.0).
    struct UnorderedMap {
        void** buckets;
        u64 bucketCount;           // 0x8: a prime, grown 2, 5, 11, 23, ...
        void* first;               // 0x10
        u64 size;                  // 0x18
        float maxLoad;             // 0x20
    };
    static_assert(offsetof(UnorderedMap, size) == 0x18 && sizeof(UnorderedMap) == 0x28);

    // libc++ std::function: a small-object buffer and the callable (null when empty).
    struct alignas(16) Function {
        u8 buffer[0x20];
        void* f;                   // 0x20
    };
    static_assert(offsetof(Function, f) == 0x20 && sizeof(Function) == 0x30);

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
