#pragma once
#include "lib.hpp"
#include "symbols.hpp"

struct OffsetManager {
    bool initialized = false;
    bool is_update = false;

    void SetVersion() {
        // some function was extended so we read a null/padding byte
        // it will be 0 for base version, and 8 for update
        u8* version_byte = reinterpret_cast<u8*>(g_BaseAddress + 0xdfaf44);
        if (*version_byte == 8) {
            is_update = true;
        }
        initialized = true;
    }

    inline uintptr_t Offset(uintptr_t addr) {
        EXL_ASSERT(initialized);
        if (is_update) {
            // the offsets start lining up again at the next section (.rodata), padded to the next page
            if (addr > 0xdfaf44 && addr < 0x1434000) {
                addr += 0xd0;
            }
        }
        return addr;
    }
};

extern OffsetManager offset_manager;

namespace exl::reloc {
    using VersionType = util::UserVersion;

    template<VersionType Version, impl::LookupEntry... Entries>
    using UserTableType = VersionedTable<Version, Entries...>;

    using UserTableSet = TableSet<VersionType 
        /*
        // This feature allows you to specify symbols in your executable to resolve with module+offset pairs.
        // They are packed up and sorted at compile time so they can be efficiently looked up.
        // The `exl::reloc::GetLookupTable` API is provided if you want to look up entries in the table explicitly.
        // Examples of tables:
        UserTableType<VersionType::DEFAULT,
        //    Module offset is relative to.     Offset within module.       Symbol name.
            { util::ModuleIndex::Main,          0x6961,                     "example1" },
            { util::ModuleIndex::Sdk,           0x6962,                     "example2" },
            { util::ModuleIndex::Rtld,          0x6963,                     "example3" }
        >,

        // In addition, you can specify multiple tables and select the correct one at runtime. This allows you to
        // support multiple versions/variations of a game in one executable. See version.hpp to see how to implement
        // multiple supported versions.
        UserTableType<VersionType::OTHER,
        //    Module offset is relative to.     Offset within module.       Symbol name.
            { util::ModuleIndex::Main,          0x4201,                     "example1" },
            { util::ModuleIndex::Sdk,           0x4202,                     "example2" }
        >
        */
    >;
}