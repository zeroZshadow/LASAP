#include "game/stl.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"

namespace Game {
    namespace {
        constexpr size_t kCtrlOwners = 0x8, kCtrlWeakOwners = 0x10, kCtrlOnZeroShared = 0x10;

        long* Count(const void* ctrl, size_t at) {
            return reinterpret_cast<long*>(reinterpret_cast<uintptr_t>(ctrl) + at);
        }
    }

    void AddOwner(void* ctrl) { __atomic_fetch_add(Count(ctrl, kCtrlOwners), 1, __ATOMIC_RELAXED); }

    void AddWeakOwner(void* ctrl) { __atomic_fetch_add(Count(ctrl, kCtrlWeakOwners), 1, __ATOMIC_RELAXED); }

    void ReleaseOwner(void* ctrl) {
        if (ctrl == nullptr) return;
        if (__atomic_fetch_sub(Count(ctrl, kCtrlOwners), 1, __ATOMIC_ACQ_REL) == 0) {
            CallVirtual<void()>(ctrl, kCtrlOnZeroShared);
            shared_weak_count_release_weak(ctrl);
        }
    }

    void ReleaseWeakOwner(void* ctrl) {
        if (ctrl != nullptr) shared_weak_count_release_weak(ctrl);
    }

    bool Alive(const void* ctrl) {
        return ctrl != nullptr && __atomic_load_n(Count(ctrl, kCtrlOwners), __ATOMIC_ACQUIRE) >= 0;
    }

    void Release(SharedPtr& sp) {
        ReleaseOwner(sp.ctrl);
        sp = SharedPtr{};
    }
}
