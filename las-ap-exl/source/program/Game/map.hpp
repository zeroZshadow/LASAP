#pragma once
// The pause menu's maps (scripts/re/UI.md, "Overworld map", "Dungeon map"): their pages, markers, cursor and icons.
#include "lib.hpp"
#include "Game/stl.hpp"
#include "Game/ui.hpp"
#include <cstddef>

namespace Game::Map {
    // A UiFieldMapIcons.gsheet row: the event flag that shows its icon.
    struct UiFieldMapIconsRow {
        u8 pad_0[0x20];
        StrView showFlag;          // 0x20
    };
    static_assert(offsetof(UiFieldMapIconsRow, showFlag) == 0x20);

    // A map marker (Gm_MapMarker), a layout of its own: the overworld's icons, and the dots the module adds to the
    // dungeon page. Its update (virtual kUpdate) runs its machine.
    struct MapMarker {
        Ui::LayoutWrapper wrapper;
        u8 pad_78[0x18];
        Ui::StateMachine machine;           // 0x90: MapMarker_ChangeState: 1 On, which goes on to Loop, 3 Off
        u8 pad_9e[0x228 - 0x90 - sizeof(Ui::StateMachine)];
        Ui::Animator* setMark;              // 0x228: its SetMark animation, whose frame picks the look
        u8 pad_230[0x10];
        const UiFieldMapIconsRow* row;      // 0x240
    };
    static_assert(offsetof(MapMarker, machine) == 0x90 && offsetof(MapMarker, setMark) == 0x228);
    static_assert(offsetof(MapMarker, row) == 0x240);
    namespace MapMarkerVirtual { constexpr size_t kUpdate = 0x28; }
    constexpr u8 kMarkerOn = 1, kMarkerOff = 3;

    // The overworld map page (UI 0xf).
    struct OverworldMap {
        Ui::LayoutWrapper wrapper;
        u8 pad_78[0x710 - sizeof(Ui::LayoutWrapper)];
        Vector<SharedPtr> markers;          // 0x710: MapMarker
        u8 pad_728[0x8150 - 0x728];
        s32 hoveredMarker;                  // 0x8150: index into markers, -1 none
    };
    static_assert(offsetof(OverworldMap, markers) == 0x710 && offsetof(OverworldMap, hoveredMarker) == 0x8150);

    // The dungeon map page (UI 0xe, Dg_DgnMap). Its panes N_MapPosition_00 (moved after the setup), N_MapPin_00 (the
    // room icons and pins) and N_PinOffset_00; the cursor in N_MapOrigin_00's units.
    struct DungeonMap {
        Ui::LayoutWrapper wrapper;
        u8 pad_78[0xf0 - sizeof(Ui::LayoutWrapper)];
        Ui::StateMachine machine;           // 0xf0: 2 KeyWait
        u8 pad_fe[0x788 - 0xf0 - sizeof(Ui::StateMachine)];
        Ui::Pane* mapPosition;              // 0x788
        u8 pad_790[0x10];
        Ui::Pane* mapPin;                   // 0x7a0
        u8 pad_7a8[0x10];
        Ui::Pane* pinOffset;                // 0x7b8
        u8 pad_7c0[0x13c0 - 0x7c0];
        void* hoveredPin;                   // 0x13c0: null none
        u8 pad_13c8[0x2ce4 - 0x13c8];
        float cursorScale;                  // 0x2ce4: the zoom's factor on the hover radius
        u8 pad_2ce8[0x2d08 - 0x2ce8];
        float cursorSpeed;                  // 0x2d08: 4.25 while the stick is idle
        u8 pad_2d0c[0xc];
        float cursorX, cursorY;             // 0x2d18
        u8 pad_2d20[0x3];
        u8 shownFloor;                      // 0x2d23
        u8 pad_2d24[0x1c];
        u8 info;                            // 0x2d40: Y held: the dungeon's info, no hover
    };
    static_assert(offsetof(DungeonMap, machine) == 0xf0);
    static_assert(offsetof(DungeonMap, mapPosition) == 0x788 && offsetof(DungeonMap, mapPin) == 0x7a0);
    static_assert(offsetof(DungeonMap, pinOffset) == 0x7b8 && offsetof(DungeonMap, hoveredPin) == 0x13c0);
    static_assert(offsetof(DungeonMap, cursorScale) == 0x2ce4 && offsetof(DungeonMap, cursorSpeed) == 0x2d08);
    static_assert(offsetof(DungeonMap, cursorX) == 0x2d18 && offsetof(DungeonMap, shownFloor) == 0x2d23);
    static_assert(offsetof(DungeonMap, info) == 0x2d40);
    constexpr u8 kKeyWait = 2;

    // The map cursor and the name over the icon it is on (UI 0x1c, Gm_MapCursor), drawn over the pages.
    struct MapCursor {
        Ui::LayoutWrapper wrapper;
        u8 pad_78[0x2e0 - sizeof(Ui::LayoutWrapper)];
        u16 nameHidden;                     // 0x2e0: both bytes set take the name away
        u8 pad_2e2[0x1e];
        float size;                         // 0x300: the hover radius
    };
    static_assert(offsetof(MapCursor, nameHidden) == 0x2e0 && offsetof(MapCursor, size) == 0x300);
    constexpr int kMapCursorUi = 0x1c;

    // A dungeon page's room icon; the floor's set-shown (virtual +0x78) shows it only while `shown` is set.
    struct DungeonMapIcon {
        Ui::LayoutWrapper wrapper;
        u8 pad_78[0xc4 - sizeof(Ui::LayoutWrapper)];
        u8 shown;                           // 0xc4
    };
    static_assert(offsetof(DungeonMapIcon, shown) == 0xc4);

    // The actor a room icon stands for.
    struct MapActorDesc {
        u16 actorId;
        u8 pad_2[0x2];
        float x, y, z;                      // 0x4
    };
    static_assert(offsetof(MapActorDesc, x) == 0x4 && offsetof(MapActorDesc, z) == 0xc);

    // A level's area: the dungeon page's rooms.
    struct Area {
        u8 pad_0[0x68];
        char path[0x50];                    // 0x68: "level/Lv01TailCave/Lv01TailCave_03G.leb"
        u64 pathLength;                     // 0xb8
    };
    static_assert(offsetof(Area, path) == 0x68 && offsetof(Area, pathLength) == 0xb8);

    // A UiDungeonArea.gsheet row.
    struct UiDungeonAreaRow {
        u8 pad_0[0x10];
        u32 floor;                          // 0x10: mFloor
    };
    static_assert(offsetof(UiDungeonAreaRow, floor) == 0x10);

    // A dungeon's DungeonItems entry (Game::DungeonItems_ptr).
    struct DungeonItemsEntry {
        u8 smallKeys;
        u8 flags;                           // 0x1: bit 0 compass, 1 map, 2 stone beak, 3 nightmare key
        u8 pad_2[0x6];
        u64 visited;                        // 0x8: the visited rooms, bit column + 8 * row
    };
    static_assert(offsetof(DungeonItemsEntry, flags) == 0x1 && offsetof(DungeonItemsEntry, visited) == 0x8);
}
