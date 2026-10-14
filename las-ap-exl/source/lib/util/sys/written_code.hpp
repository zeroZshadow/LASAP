#pragma once

#include <common.hpp>
#include <lib/nx/arm/cache.h>
#include "modules.hpp"

namespace exl::util {

    /* How far after written code a translation that covers it may start. */
    constexpr size_t WrittenCodeReach = 0x8000;

    /*
        Invalidates the instruction cache for code written at ro, and for the code after it in the same module's text.
        Ryujinx keeps translated functions between runs (its PPTC translates them at boot, before the module writes
        code) and drops one on an IC IVAU that overlaps the function as it registered it: from its entry, over its
        size. A translation that starts after the written code, such as one at a switch case reached through BR, and
        branches back over it covers the code without overlapping the written line, so only an IC IVAU at its entry
        drops it.
    */
    inline void InvalidateWrittenCode(uintptr_t ro, size_t size) {
        uintptr_t end = ro + size + WrittenCodeReach;
        const ModuleInfo* module = TryGetModule(ro);
        if (module != nullptr && module->m_Text.InRange(ro) && module->m_Text.GetEnd() < end)
            end = module->m_Text.GetEnd();
        if (module == nullptr || !module->m_Text.InRange(ro))
            end = ro + size;

        armICacheInvalidate(reinterpret_cast<void*>(ro), end - ro);
    }
}
