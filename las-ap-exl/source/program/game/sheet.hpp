#pragma once
// Datasheets (.gsheet, UI.md, Datasheets): a sheet's data and the strings and arrays its rows hold.
#include "lib.hpp"
#include <cstddef>
#include <string_view>

namespace Game {
    // A row's string: the characters (terminated) and the length.
    struct SheetString {
        const char* data;
        u32 length;                // 0x8
        u8 pad_c[0x4];

        std::string_view View() const { return data != nullptr ? std::string_view(data, length) : std::string_view(); }
    };
    static_assert(offsetof(SheetString, length) == 0x8 && sizeof(SheetString) == 0x10);

    // A row's array.
    template<typename T>
    struct SheetArray {
        const T* data;
        u32 count;                 // 0x8
        u8 pad_c[0x4];
    };
    static_assert(sizeof(SheetArray<int>) == 0x10);

    // A sheet's data: the loaded .gsheet file, its pointers relocated (patcher/RandomizerCore/Tools/gsheet.py):
    // the header, the rows and the field descriptors (0x30 each) that follow.
    template<typename Row>
    struct SheetData {
        char magic[4];
        u32 version;               // 0x4: 1
        u32 hash;                  // 0x8
        u8 boolSize;               // 0xc
        u8 pointerSize;            // 0xd
        u8 alignment;              // 0xe
        u8 pad_f;
        const char* name;          // 0x10
        u32 rootFieldCount;        // 0x18
        u8 pad_1c[0x4];
        const Row* rows;           // 0x20
        u32 count;                 // 0x28
        u32 rowSize;               // 0x2c
    };
    static_assert(offsetof(SheetData<int>, name) == 0x10 && offsetof(SheetData<int>, rows) == 0x20);
    static_assert(offsetof(SheetData<int>, count) == 0x28 && sizeof(SheetData<int>) == 0x30);

    // A sheet (the object a sheet holder's shared_ptr points at): its data, and the shared_ptr its users copy to keep
    // the data alive.
    template<typename Row>
    struct Sheet {
        u8 pad_0[0x60];
        void* keep;                // 0x60: a shared_ptr
        void* keepCtrl;            // 0x68
        const SheetData<Row>* data;  // 0x70
    };
    static_assert(offsetof(Sheet<int>, keep) == 0x60 && offsetof(Sheet<int>, data) == 0x70);
}
