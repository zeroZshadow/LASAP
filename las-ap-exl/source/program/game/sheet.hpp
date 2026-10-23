#pragma once
// Datasheets (.gsheet, UI.md, Datasheets): a sheet's data and the strings and arrays its rows hold.
#include "game/stl.hpp"
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

    // A sheet (Datasheet_Create, Datasheet_vtable 0x1c30d40; 0x110 bytes, the object a sheet holder's shared_ptr
    // points at), a resource (Resource_Ctor): its data, the file buffer its users copy to keep the data alive, and its
    // rows by id and by key.
    template<typename Row>
    struct Sheet {
        const uintptr_t* vtable;
        WeakPtr self;              // 0x8: shared-from-this
        SharedPtr resourceOwner;   // 0x18: the resource info's
        u64 resourceInfo_28[2];    // 0x28
        SharedPtr resourceArchive; // 0x38: the resource info's
        StrView path;              // 0x48: the resource manager's key
        u8 resourceFlags;          // 0x58: bit 0 checked by the destructor
        u8 resourceLocked;         // 0x59: the destructor unlocks the resource manager
        u8 pad_5a[0x6];
        void* keep;                // 0x60: a shared_ptr, the file buffer
        void* keepCtrl;            // 0x68
        const SheetData<Row>* data;  // 0x70
        u64 dataSize;              // 0x78
        s32 keyField;              // 0x80: -1 without a key column
        u8 pad_84[0x4];
        UnorderedMap map_88;       // 0x88: keyed by u32
        UnorderedMap rowsById;     // 0xb0: id to row index (Datasheet_RowById)
        UnorderedMap rowsByKey;    // 0xd8: key string to row index (Datasheet_FindRow)
        const uintptr_t* vtable_100;  // 0x100: a virtual base's
        const uintptr_t* vtable_108;  // 0x108: a virtual base's
    };
    static_assert(offsetof(Sheet<int>, path) == 0x48 && offsetof(Sheet<int>, keep) == 0x60);
    static_assert(offsetof(Sheet<int>, data) == 0x70 && offsetof(Sheet<int>, keyField) == 0x80);
    static_assert(offsetof(Sheet<int>, rowsById) == 0xb0 && offsetof(Sheet<int>, rowsByKey) == 0xd8);
    static_assert(sizeof(Sheet<int>) == 0x110);
}
