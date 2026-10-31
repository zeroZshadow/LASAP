#pragma once
// The game's standard library types as its functions take them and its objects hold them: string views, libc++
// shared pointers, vectors, lists, maps, deques and strings, and the game's thread pool.
#include "game/abi.hpp"
#include "lib.hpp"
#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>

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

    // libc++ __shared_weak_count: a shared pointer's control block
    struct SharedCtrl {
        const uintptr_t* vtable;   // CtrlSlot
        long owners;               // shared owners - 1; -1 once the object is gone
        long weakOwners;           // weak owners - 1
    };
    static_assert(sizeof(SharedCtrl) == 0x18);
    // Its vtable's slots
    enum CtrlSlot { kCtrlDtor, kCtrlDelete, kCtrlZeroShared, kCtrlDeleter, kCtrlZeroWeak, kCtrlSlots };

    // __shared_ptr_pointer: the control block of a shared_ptr made from a pointer (with the default deleter)
    struct SharedPointerBlock {
        SharedCtrl ctrl;
        void* ptr;                 // 0x18
    };
    static_assert(sizeof(SharedPointerBlock) == 0x20);

    void AddOwner(void* ctrl);
    void AddWeakOwner(void* ctrl);
    // Drops a shared owner: the last destroys the object and drops the owners' weak reference.
    void ReleaseOwner(void* ctrl);
    void ReleaseWeakOwner(void* ctrl);
    // The object is still there (a weak reference that would lock).
    bool Alive(const void* ctrl);

    // libc++ std::shared_ptr as the game's memory holds it and its functions take it: the object and its control
    // block (SharedCtrl), owning nothing by itself (Shared owns). T is what it points to; SharedPtr<> is one whose
    // object the module does not read. may_alias: SharedPtr<T> and SharedPtr<> are the same pair (Untyped).
    template<typename T = void>
    struct [[gnu::may_alias]] SharedPtr {
        T* ptr = nullptr;
        void* ctrl = nullptr;

        T* get() const { return ptr; }
        T* operator->() const requires(!std::is_void_v<T>) { return ptr; }
        explicit operator bool() const { return ptr != nullptr; }
        // The same pointer as the game's functions that take any object's take it (the layout is T's for any T)
        SharedPtr<>* Untyped() requires(!std::is_void_v<T>) { return reinterpret_cast<SharedPtr<>*>(this); }
    };

    // libc++ std::weak_ptr: the same pair, holding a weak reference (Weak owns one).
    template<typename T = void>
    struct [[gnu::may_alias]] WeakPtr {
        T* ptr = nullptr;
        void* ctrl = nullptr;
    };

    // A shared reference from a weak one, empty once the object is gone (SharedPtr_FromWeak)
    void LockWeak(SharedPtr<>* out, void* ptr, void* ctrl);

    // A shared reference the module owns: released when it is destroyed or assigned. Move-only, the layout of a
    // SharedPtr<T>, and non-trivial, so a game function returns one through x8 as it returns a std::shared_ptr; one
    // a module function returns is made in the caller's slot, which the game then owns.
    template<typename T = void>
    struct Shared {
        constexpr Shared() = default;
        Shared(const Shared&) = delete;
        Shared& operator=(const Shared&) = delete;
        Shared(Shared&& other) noexcept : sp(other.sp) { other.sp = {}; }
        // A derived class's, as its base (the pointer adjusted as the compiler converts it)
        template<typename D>
            requires(!std::is_same_v<D, T> && std::is_convertible_v<D*, T*>)
        Shared(Shared<D>&& other) noexcept : sp{other.get(), other.View().ctrl} { other.Give(); }
        // The old owner goes after the new one is taken: its object may hold the other
        Shared& operator=(Shared&& other) noexcept {
            if (this != &other) {
                void* old = sp.ctrl;
                sp = other.sp;
                other.sp = {};
                ReleaseOwner(old);
            }
            return *this;
        }
        ~Shared() { ReleaseOwner(sp.ctrl); }

        // Takes over the owner a game slot holds (one the game handed over) and empties the slot
        static Shared Adopt(SharedPtr<T>& slot) {
            Shared shared = Adopt(slot.ptr, slot.ctrl);
            slot = {};
            return shared;
        }
        static Shared Adopt(T* ptr, void* ctrl) {
            Shared shared;
            shared.sp = {ptr, ctrl};
            return shared;
        }
        // An owner from a weak pair (an entity's self pointer), empty once the object is gone; safe on any thread
        static Shared Lock(const WeakPtr<T>& weak) {
            SharedPtr<> out{};
            LockWeak(&out, weak.ptr, weak.ctrl);
            return Adopt(static_cast<T*>(out.ptr), out.ctrl);
        }
        // A new owner of what the pair points to (one the game holds)
        static Shared Share(const SharedPtr<T>& from) {
            if (from.ctrl != nullptr) AddOwner(from.ctrl);
            return Adopt(from.ptr, from.ctrl);
        }

        // Releases it and gives the empty slot to a game function that makes into it (its out argument)
        SharedPtr<T>* Reset() {
            ReleaseOwner(sp.ctrl);
            sp = {};
            return &sp;
        }
        // The owner, pointing to a part of the object (as std::shared_ptr's aliasing constructor): a member it holds as
        // its base class
        template<typename U>
        Shared<U> Alias(U* part) && {
            void* ctrl = sp.ctrl;
            sp = {};
            return Shared<U>::Adopt(part, ctrl);
        }
        // Hands the owner over (to a game slot) and empties this one
        SharedPtr<T> Give() {
            SharedPtr<T> given = sp;
            sp = {};
            return given;
        }
        // The pair, for game functions that read it (a std::shared_ptr they copy from); it stays this one's
        const SharedPtr<T>& View() const { return sp; }
        SharedPtr<T>* Borrowed() const { return const_cast<SharedPtr<T>*>(&sp); }
        SharedPtr<>* Untyped() const { return reinterpret_cast<SharedPtr<>*>(Borrowed()); }

        T* get() const { return sp.ptr; }
        T* operator->() const requires(!std::is_void_v<T>) { return sp.ptr; }
        explicit operator bool() const { return sp.ptr != nullptr; }

    private:
        SharedPtr<T> sp;
    };

    // A weak reference the module owns: released when it is destroyed or assigned. The layout of a WeakPtr<T>.
    template<typename T = void>
    struct Weak {
        constexpr Weak() = default;
        // To what a pair of the object's control block points to: a strong one, or a weak one as an entity's self
        Weak(T* ptr, void* ctrl) : wp{ptr, ctrl} {
            if (ctrl != nullptr) AddWeakOwner(ctrl);
        }
        Weak(const Shared<T>& shared) : Weak(shared.get(), shared.View().ctrl) {}
        Weak(const Weak& other) : Weak(other.wp.ptr, other.wp.ctrl) {}
        Weak(Weak&& other) noexcept : wp(other.wp) { other.wp = {}; }
        Weak& operator=(Weak other) {
            std::swap(wp, other.wp);
            return *this;
        }
        ~Weak() { ReleaseWeakOwner(wp.ctrl); }

        // A reference to an object, there or gone
        bool Holds() const { return wp.ctrl != nullptr; }
        bool Alive() const { return Game::Alive(wp.ctrl); }
        // The object while it is there, without an owner: on the game thread only, which the object's last owner
        // does not leave during the caller's use
        T* Peek() const { return Alive() ? wp.ptr : nullptr; }
        // An owner of the object, empty once it is gone; safe on any thread
        Shared<T> Lock() const { return Shared<T>::Lock(wp); }
        const WeakPtr<T>& View() const { return wp; }

    private:
        WeakPtr<T> wp;
    };

    // A string copied into a shared buffer of its own (SharedString_Make): the buffer and its characters (terminated).
    struct SharedString {
        SharedPtr<> holder;
        const char* data;          // 0x10: holder's
        u64 length;                // 0x18
    };
    static_assert(offsetof(SharedString, data) == 0x10 && sizeof(SharedString) == 0x20);

    // The module's types of the game's shared pointers
    template<typename T> inline constexpr bool kSharedPointer = false;
    template<typename T> inline constexpr bool kSharedPointer<SharedPtr<T>> = true;
    template<typename T> inline constexpr bool kSharedPointer<WeakPtr<T>> = true;
    template<typename T> inline constexpr bool kSharedPointer<Shared<T>> = true;
    template<typename T> inline constexpr bool kSharedPointer<Weak<T>> = true;

    // The layout types are plain pairs, which come back in x0 and x1; the owning ones come back through x8, as the
    // game returns a std::shared_ptr. Each has the layout of a std::shared_ptr / std::weak_ptr.
    static_assert(std::is_trivially_copyable_v<SharedPtr<>> && !ReturnedThroughX8<SharedPtr<>>);
    static_assert(std::is_trivially_copyable_v<WeakPtr<>> && !ReturnedThroughX8<WeakPtr<>>);
    static_assert(ReturnedThroughX8<Shared<>> && ReturnedThroughX8<Weak<>>);
    static_assert(sizeof(Shared<>) == sizeof(SharedPtr<>) && std::is_standard_layout_v<Shared<>>);
    static_assert(sizeof(Weak<>) == sizeof(WeakPtr<>) && std::is_standard_layout_v<Weak<>>);
    static_assert(sizeof(SharedPtr<>) == 0x10);

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

    // Drops the owner sp holds and empties it.
    template<typename T>
    void Release(SharedPtr<T>& sp) {
        ReleaseOwner(sp.ctrl);
        sp = {};
    }
}
