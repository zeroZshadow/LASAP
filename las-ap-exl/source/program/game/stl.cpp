#include "game/stl.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"

namespace Game {
    namespace {
        SharedCtrl* Ctrl(const void* ctrl) { return static_cast<SharedCtrl*>(const_cast<void*>(ctrl)); }
    }

    void AddOwner(void* ctrl) { __atomic_fetch_add(&Ctrl(ctrl)->owners, 1, __ATOMIC_RELAXED); }

    void AddWeakOwner(void* ctrl) { __atomic_fetch_add(&Ctrl(ctrl)->weakOwners, 1, __ATOMIC_RELAXED); }

    void ReleaseOwner(void* ctrl) {
        if (ctrl == nullptr) return;
        if (__atomic_fetch_sub(&Ctrl(ctrl)->owners, 1, __ATOMIC_ACQ_REL) == 0) {
            CallVirtual<void()>(ctrl, kCtrlZeroShared * sizeof(uintptr_t));
            shared_weak_count_release_weak(ctrl);
        }
    }

    void ReleaseWeakOwner(void* ctrl) {
        if (ctrl != nullptr) shared_weak_count_release_weak(ctrl);
    }

    void LockWeak(SharedPtr<>* out, void* ptr, void* ctrl) {
        *out = {};
        if (ctrl != nullptr) SharedPtr_FromWeak(out, ptr, ctrl);
    }

    bool Alive(const void* ctrl) {
        return ctrl != nullptr && __atomic_load_n(&Ctrl(ctrl)->owners, __ATOMIC_ACQUIRE) >= 0;
    }
}
