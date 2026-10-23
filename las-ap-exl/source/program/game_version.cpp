#include "version.hpp"
#include "lib.hpp"

namespace exl::util::impl {
    // The byte at 0xdfaf44: padding (0) in 1.0.0, part of the grown function (8) in the update.
    UserVersion DetermineUserVersion() {
        auto* versionByte = reinterpret_cast<const u8*>(modules::GetTargetOffset(0xdfaf44));
        return *versionByte == 8 ? UserVersion::UPDATE : UserVersion::BASE;
    }
}
