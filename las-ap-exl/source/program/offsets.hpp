#pragma once
// exlaunch's symbol tables (lib/reloc). The module names the game's code by offset instead (game/sym.hpp), so they
// are empty.
#include "lib.hpp"

namespace exl::reloc {
    using VersionType = util::UserVersion;

    template<VersionType Version, impl::LookupEntry... Entries>
    using UserTableType = VersionedTable<Version, Entries...>;

    using UserTableSet = TableSet<VersionType>;
}
