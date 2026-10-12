#pragma once

#include "lib.hpp"
#include "fs.hpp"
#include <string>

namespace FileHandler {
    // only call once !
    inline Result MountSD() {
        return nn::fs::MountSdCardForDebug("sd");
    }
    inline Result ReadFile(const char *path, std::string &out) {
        nn::fs::FileHandle handle{};
        R_TRY(nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read));
        s64 size;
        R_TRY(nn::fs::GetFileSize(&size, handle));
        char* buffer = (char*)malloc(size);
        Result res = nn::fs::ReadFile(handle, 0, buffer, size);
        if (R_FAILED(res)) {
            free(buffer);
            return res;
        }
        nn::fs::CloseFile(handle);
        out = std::string(buffer, size);
        free(buffer);
        return 0;
    }
}