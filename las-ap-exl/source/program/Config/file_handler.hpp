#pragma once

#include "lib.hpp"
#include "fs.hpp"
#include <string>

namespace FileHandler {
    // only call once !
    inline Result MountSD() {
        return nn::fs::MountSdCardForDebug("sd");
    }

    // The whole file; `out` is left as it was when it cannot be read.
    inline Result ReadFile(const char* path, std::string& out) {
        nn::fs::FileHandle handle{};
        R_TRY(nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read));
        struct Closer {
            nn::fs::FileHandle handle;
            ~Closer() { nn::fs::CloseFile(handle); }
        } closer{handle};
        s64 size = 0;
        R_TRY(nn::fs::GetFileSize(&size, handle));
        std::string text(static_cast<size_t>(size), '\0');
        R_TRY(nn::fs::ReadFile(handle, 0, text.data(), static_cast<ulong>(size)));
        out = std::move(text);
        return 0;
    }
}
