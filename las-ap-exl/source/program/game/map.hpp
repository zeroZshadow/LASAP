#pragma once
// The pause menu's maps (scripts/re/UI.md, "Overworld map", "Dungeon map"): their pages, markers, cursor and icons.
#include "game/actor.hpp"
#include "game/menu.hpp"
#include "game/state_machine.hpp"
#include "game/stl.hpp"
#include "game/ui.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Map {
    // A UiFieldMapIcons.gsheet row (0x50 bytes): an overworld icon, shown once its flag is set.
    struct UiFieldMapIconsRow {
        s32 id;
        u8 pad_4[0x4];
        StrView nameLabel;                  // 0x8: Place:{0}
        u32 type;                           // 0x18: FieldMapIconType, the marker's SetMark frame
        u8 pad_1c[0x4];
        StrView showFlag;                   // 0x20: mFirstShowFlagName; empty for none
        float pos[3];                       // 0x30: world position
        u8 pad_3c[0x4];
        StrView arg00;                      // 0x40
    };
    static_assert(offsetof(UiFieldMapIconsRow, nameLabel) == 0x8 && offsetof(UiFieldMapIconsRow, type) == 0x18);
    static_assert(offsetof(UiFieldMapIconsRow, showFlag) == 0x20 && offsetof(UiFieldMapIconsRow, pos) == 0x30);
    static_assert(sizeof(UiFieldMapIconsRow) == 0x50);

    // A map marker (Gm_MapMarker, MapMarker_Make, vtable 0x1c2cfa8; 0x250 bytes), a part of its own: the overworld's
    // icons, and the dots the module adds to the dungeon page. Its update (virtual kUpdate) runs its machine.
    struct MapMarker {
        Ui::Part part;
        StateMachine<4> machine;            // 0x90: MapMarker_ChangeState: Deactive, On (goes on to Loop), Loop, Off
        Ui::Animation on;                   // 0x1d0
        Ui::Animation loop;                 // 0x1e8
        Ui::Animation off;                  // 0x200
        Ui::Animation setMark;              // 0x218: its frame picks the look
        SharedPtr<> sheet;                  // 0x230: UiFieldMapIcons, kept alive
        const UiFieldMapIconsRow* row;      // 0x240
        u8 hoverLost;                       // 0x248: set by the page; Loop goes to Off
        u8 pad_249[0x7];
    };
    static_assert(offsetof(MapMarker, machine) == 0x90 && offsetof(MapMarker, on) == 0x1d0);
    static_assert(offsetof(MapMarker, setMark) == 0x218 && offsetof(MapMarker, row) == 0x240);
    static_assert(offsetof(MapMarker, hoverLost) == 0x248 && sizeof(MapMarker) == 0x250);
    namespace MapMarkerVirtual { constexpr size_t kUpdate = 0x28; }
    constexpr u8 kMarkerOn = 1, kMarkerOff = 3;

    // The map cursor and the name over the icon it is on (UI 0x1c, Gm_MapCursor, MapCursor_Make; 0x310 bytes), drawn
    // over the pages.
    struct MapCursor {
        Ui::LayoutWrapper wrapper;
        u8 pad_78[0x8];
        StateMachine<5> nameMachine;        // 0x80: the name bubble: Deactive, On, Loop, Off, HideName
        Ui::Animation on;                   // 0x1f0
        Ui::Animation loop;                 // 0x208
        Ui::Animation off;                  // 0x220
        Ui::Animation onName;               // 0x238
        Ui::Animation offName;              // 0x250
        Ui::Animation in;                   // 0x268
        Ui::Animation out;                  // 0x280
        Ui::TextHolder nameText;            // 0x298: T_MarkerName_00
        Ui::PaneHandle window;              // 0x2b0: W_window_00
        Ui::PaneHandle field_2c8;           // 0x2c8: never set
        u8 nameHidden;                      // 0x2e0: no name shown; set with nameHideRequest, the name goes away
        u8 nameHideRequest;                 // 0x2e1
        u8 pad_2e2[0x6];
        Ui::PaneHandle cursorPane;          // 0x2e8: W_Cursor_00
        float width, height;                // 0x300: W_Cursor_00's; width is the hover radius
        u8 pad_308[0x8];
    };
    static_assert(offsetof(MapCursor, nameMachine) == 0x80 && offsetof(MapCursor, on) == 0x1f0);
    static_assert(offsetof(MapCursor, nameText) == 0x298 && offsetof(MapCursor, nameHidden) == 0x2e0);
    static_assert(offsetof(MapCursor, cursorPane) == 0x2e8 && offsetof(MapCursor, width) == 0x300);
    static_assert(sizeof(MapCursor) == 0x310);
    constexpr int kMapCursorUi = 0x1c;

    // A dungeon page's room icon (DungeonMapIcon_Make, vtable 0x1c2cb38; 0xd0 bytes; its base DungeonMapPart is
    // L_MapStart_00's too); the floor's set-shown (virtual +0x78) shows it only while `shown` is set.
    struct DungeonMapIcon {
        Ui::Part part;
        Ui::Animation open;                 // 0x90: On
        Ui::Animation closed;               // 0xa8: Off
        u32 areaId;                         // 0xc0: Area::id
        u8 shown;                           // 0xc4: the compass, or the shown switch and the room visited
        u8 hasMap;                          // 0xc5
        u8 hasCompass;                      // 0xc6
        u8 field_c7;                        // 0xc7
        u32 field_c8;                       // 0xc8
        u8 visited;                         // 0xcc
        u8 pad_cd[0x3];
    };
    static_assert(offsetof(DungeonMapIcon, open) == 0x90 && offsetof(DungeonMapIcon, areaId) == 0xc0);
    static_assert(offsetof(DungeonMapIcon, shown) == 0xc4 && offsetof(DungeonMapIcon, visited) == 0xcc);
    static_assert(sizeof(DungeonMapIcon) == 0xd0);

    // The actor a room icon stands for (LevelData_MapActorAt; 0x30 bytes).
    struct MapActorDesc {
        u16 actorId;
        u8 pad_2[0x2];
        float x, y, z;                      // 0x4
        u32 field_10;                       // 0x10
        u32 zoneId;                         // 0x14
        u32 areaId;                         // 0x18: Area::id
        u8 row;                             // 0x1c
        u8 column;                          // 0x1d
        u8 shownSwitchType;                 // 0x1e: ActorSwitch_Check's type
        u8 pad_1f;
        u16 shownSwitchIndex;               // 0x20
        u8 openSwitchType;                  // 0x22
        u8 pad_23;
        u16 openSwitchIndex;                // 0x24
        u8 field_26[0xa];                   // 0x26
    };
    static_assert(offsetof(MapActorDesc, x) == 0x4 && offsetof(MapActorDesc, z) == 0xc);
    static_assert(offsetof(MapActorDesc, areaId) == 0x18 && offsetof(MapActorDesc, shownSwitchIndex) == 0x20);
    static_assert(offsetof(MapActorDesc, openSwitchIndex) == 0x24 && sizeof(MapActorDesc) == 0x30);

    // A level's area (Area_Ctor; 0xd0 bytes): the dungeon page's rooms.
    struct Area {
        const uintptr_t* vtable;
        u8 pad_8[0x8];
        Vec4 offset;                        // 0x10: the Chamber Dungeon's grid offset
        Vec4 min;                           // 0x20: the area's box
        Vec4 max;                           // 0x30
        Vec4 viewMin;                       // 0x40: the box whose overlap with the camera window shows the
        Vec4 viewMax;                       // 0x50: room's actors (RoomManager_UpdateRooms)
        u32 id;                             // 0x60
        u32 zoneIndex;                      // 0x64
        char path[0x50];                    // 0x68: "level/Lv01TailCave/Lv01TailCave_03G.leb"
        u64 pathLength;                     // 0xb8
        u8 active;                          // 0xc0: the room manager keeps the room; it reloads only while set
        u8 showForced;                      // 0xc1: the actors shown regardless of the view box; Room_Reload reacts
                                            // to its change
        u8 row;                             // 0xc2
        u8 column;                          // 0xc3
        u8 viewKind;                        // 0xc4: the camera window's size, 0 to 2
        u8 pad_c5[0xb];
    };
    static_assert(offsetof(Area, offset) == 0x10 && offsetof(Area, viewMin) == 0x40 && offsetof(Area, id) == 0x60);
    static_assert(offsetof(Area, path) == 0x68 && offsetof(Area, pathLength) == 0xb8);
    static_assert(offsetof(Area, row) == 0xc2 && sizeof(Area) == 0xd0);

    // A UiDungeonArea.gsheet row (0x20 bytes).
    struct UiDungeonAreaRow {
        StrView leaName;                    // the area's name
        s32 floor;                          // 0x10: mFloor
        s32 animFloorIndex;                 // 0x14: the floor's Dgn_NN animation
        bool isBoss;                        // 0x18
        u8 pad_19[0x7];
    };
    static_assert(offsetof(UiDungeonAreaRow, floor) == 0x10 && sizeof(UiDungeonAreaRow) == 0x20);

    // A dungeon's DungeonItems entry (Game::DungeonItems_ptr; 0x20 bytes, which the Chamber Dungeon's reset clears).
    struct DungeonItemsEntry {
        u8 smallKeys;
        u8 flags;                           // 0x1: bit 0 compass, 1 map, 2 stone beak, 3 nightmare key
        u8 pad_2[0x6];
        u64 visited;                        // 0x8: the visited rooms, bit column + 8 * row
        u8 field_10[0x10];                  // 0x10
    };
    static_assert(offsetof(DungeonItemsEntry, flags) == 0x1 && offsetof(DungeonItemsEntry, visited) == 0x8);
    static_assert(sizeof(DungeonItemsEntry) == 0x20);

    // A gsheet row and the shared_ptr keeping its sheet alive.
    struct SheetRowRef {
        SharedPtr<> sheet;
        const void* row;                    // 0x10
    };
    static_assert(sizeof(SheetRowRef) == 0x18);

    // A floor choice of the dungeon map (L_MapChoice_NN, MapChoice_vtable 0x1c2c5c0; 0x168 bytes).
    struct MapChoice {
        Ui::Part part;
        Ui::Animation on;                   // 0x90
        Ui::Animation off;                  // 0xa8
        Ui::Animation loop;                 // 0xc0
        Ui::Animation decide;               // 0xd8
        Ui::Animation normal;               // 0xf0
        Ui::Animation disable;              // 0x108
        Ui::TextHolder text;                // 0x120: T_List_00
        Ui::PaneHandle playerPane;          // 0x138: N_Player_00, Link's floor
        Ui::PaneHandle bossPane;            // 0x150: N_Boss_00, the boss's floor
    };
    static_assert(offsetof(MapChoice, text) == 0x120 && offsetof(MapChoice, bossPane) == 0x150);
    static_assert(sizeof(MapChoice) == 0x168);

    // The dungeon map's timer (L_TimeCount_00, TimeCount_vtable 0x1c2cdc0; 0x150 bytes).
    struct TimeCount {
        Ui::Part part;
        Ui::Animation in;                   // 0x90
        Ui::Animation out;                  // 0xa8
        Ui::Animation setTime;              // 0xc0
        Ui::TextHolder time[3];             // 0xd8: T_Time_00..02
        Ui::TextHolder minuteSign;          // 0x120: T_Sign_00
        Ui::TextHolder secondSign;          // 0x138: T_Sign_01
    };
    static_assert(offsetof(TimeCount, time) == 0xd8 && sizeof(TimeCount) == 0x150);

    // Link's marker on a map (L_MapPlayer_00, MapPlayer_vtable 0x1c2cd48; 0xd8 bytes).
    struct MapPlayer {
        Ui::Part part;
        Ui::Animation on;                   // 0x90
        Ui::Animation indoor;               // 0xa8
        Ui::Animation sideview;             // 0xc0
    };
    static_assert(sizeof(MapPlayer) == 0xd8);

    // A pin (Gm_MapMarkUnit, MapMarkUnit_Make, vtable 0x1c2da60; 0x140 bytes): placed on a map, or a type of the pin
    // bar.
    struct MapMarkUnit {
        Ui::Part part;
        Ui::Animation on;                   // 0x90: hovered
        Ui::Animation loop;                 // 0xa8
        Ui::Animation off;                  // 0xc0
        Ui::Animation normal;               // 0xd8
        Ui::Animation disable;              // 0xf0
        Ui::Animation setMark;              // 0x108: frame code % 10 - 1
        Ui::Animation put;                  // 0x120: a placed pin's
        u8 code;                            // 0x138: type code % 10, the dungeon map's floor code / 10
        u8 pad_139[0x7];
    };
    static_assert(offsetof(MapMarkUnit, put) == 0x120 && offsetof(MapMarkUnit, code) == 0x138);
    static_assert(sizeof(MapMarkUnit) == 0x140);

    // The pin bar's cursor (L_MarkCursor_00, MarkCursor_vtable 0x1c2c530; 0x100 bytes).
    struct MarkCursor {
        Ui::Part part;
        Ui::Animation on;                   // 0x90: Loop plays once it ends
        Ui::Animation loop;                 // 0xa8
        Ui::Animation off;                  // 0xc0
        u8 field_d8;                        // 0xd8: zeroed when made, not read
        u8 pad_d9[0x7];
        Ui::PaneHandle cursorPane;          // 0xe0: W_Cursor_00
        u8 pad_f8[0x8];
    };
    static_assert(offsetof(MarkCursor, cursorPane) == 0xe0 && sizeof(MarkCursor) == 0x100);

    // The maps' pin bar (L_MapMarkList_00, MapMarkList_vtable 0x1c2d9e8; 0xe98 bytes): the pin types and the pin count.
    struct MapMarkList {
        Ui::Part part;
        Ui::Animation in;                   // 0x90
        Ui::Animation loop;                 // 0xa8
        Ui::Animation out;                  // 0xc0
        Ui::Animation normal;               // 0xd8
        Ui::Animation disable;              // 0xf0
        Ui::Animation setPositionX;         // 0x108
        Ui::Animation setPositionY;         // 0x120
        Ui::TextHolder pinCount;            // 0x138: T_PinNum_00
        Ui::TextHolder pinCapacity;         // 0x150: T_PinNum_01
        Ui::TextHolder pinSlash;            // 0x168: T_PinSlash_00
        MapMarkUnit marks[9];               // 0x180: [1..8] L_MapMark_01..08; [0] not bound
        Ui::PaneHandle markPanes[9];        // 0xcc0: L_MapMark_00..08
        MarkCursor cursor;                  // 0xd98
    };
    static_assert(offsetof(MapMarkList, marks) == 0x180 && offsetof(MapMarkList, markPanes) == 0xcc0);
    static_assert(offsetof(MapMarkList, cursor) == 0xd98 && sizeof(MapMarkList) == 0xe98);

    // An overworld field area's part (L_FldArea_<a>, FieldAreaPart_vtable 0x1c2d050; 0x158 bytes): On once traversed.
    struct FieldAreaPart {
        Ui::Part part;
        Ui::Animation on;                   // 0x90
        Ui::Animation off;                  // 0xa8
        Ui::Animation plainOnOff;           // 0xc0
        Ui::Animation active;               // 0xd8
        Ui::Animation deactive;             // 0xf0: played when the row's conditions hold
        Ui::Animation zoomIn;               // 0x108
        Ui::Animation zoomOut;              // 0x120
        SharedPtr<> maskSheet;              // 0x138: UiFieldMapMask
        const void* maskRow;                // 0x148: once set, the update does nothing
        u8 setUp;                           // 0x150: not read
        u8 traversed;                       // 0x151: the row's UI_FieldMapTraverse flag
        u8 pad_152[0x6];
    };
    static_assert(offsetof(FieldAreaPart, maskSheet) == 0x138 && offsetof(FieldAreaPart, traversed) == 0x151);
    static_assert(sizeof(FieldAreaPart) == 0x158);

    // A Memory's marker on the overworld map (L_Memory_<i>, MemoryMarker_vtable 0x1c2ddd0; 0x230 bytes).
    struct MemoryMarker {
        Ui::Part part;
        StateMachine<4> machine;            // 0x90: MemoryMarker_ChangeState: Deactive, On, Wait, Off
        Ui::Animation on;                   // 0x1d0
        Ui::Animation loop;                 // 0x1e8
        Ui::Animation off;                  // 0x200
        Ui::Animation setMark;              // 0x218
    };
    static_assert(offsetof(MemoryMarker, on) == 0x1d0 && sizeof(MemoryMarker) == 0x230);

    // The dungeon map page (UI 0xe, Dg_DgnMap, DungeonMap_Ctor; 0x2e60 bytes). Its panes N_MapPosition_00 (moved after
    // the setup), N_MapPin_00 (the room icons and pins) and N_PinOffset_00; the cursor in N_MapOrigin_00's units.
    struct DungeonMap {
        Ui::MenuPage page;
        StateMachine<16> machine;           // 0xf0: 2 KeyWait
        UnorderedMap cellOfArea;            // 0x4d0: Area::id to row * 8 + column
        SharedPtr<> memories;               // 0x4f8: the Memories UI (DungeonMap_MemoryCreated)
        Ui::Animation floors[17];           // 0x508: Dgn_00..16, by UiDungeonAreaRow::animFloorIndex
        Ui::Animation dgn14;                // 0x6a0
        Ui::Animation dgnMapOn;             // 0x6b8
        Ui::Animation dgnMapOff;            // 0x6d0
        Ui::Animation memoryIn;             // 0x6e8
        Ui::Animation memoryOut;            // 0x700
        Ui::Animation set;                  // 0x718
        Ui::Animation mapSymbol;            // 0x730: MapSymbolInOut, played while Y is held
        Ui::PaneHandle mapOrigin;           // 0x748
        Ui::PaneHandle grid;                // 0x760
        Ui::PaneHandle mapPosition;         // 0x778
        Ui::PaneHandle mapPin;              // 0x790
        Ui::PaneHandle pinOffset;           // 0x7a8
        Ui::PaneHandle pointerLimit;        // 0x7c0
        Ui::PaneHandle scrollLimit;         // 0x7d8
        Ui::PaneHandle markerLimit;         // 0x7f0
        Ui::PaneHandle mapIcon01;           // 0x808: the stairs' parent
        Ui::PaneHandle mapIcon00;           // 0x820: the warp holes' parent
        Ui::TextHolder dungeonName;         // 0x838
        Ui::TextHolder panelDungeonName;    // 0x850
        Ui::PaneHandle cells[64];           // 0x868: B_00..63
        SharedPtr<> chips[64];              // 0xe68
        Vector<SharedPtr<>> roomIcons;      // 0x1268: DungeonMapIcon
        SharedPtr<> pins[20];               // 0x1280: MapMarkUnit
        void* hoveredPin;                   // 0x13c0: a shared_ptr, null none
        void* hoveredPinCtrl;               // 0x13c8
        Ui::Part dungeonItems[3];           // 0x13d0: the map, the compass, the stone beak
        MapChoice floorChoices[4];          // 0x1580: L_MapChoice_00..03
        SharedPtr<> bossIcon;               // 0x1b20
        MapMarkList pinBar;                 // 0x1b30
        DungeonMapIcon entrance;            // 0x29c8: L_MapStart_00
        MapPlayer linkMarker;               // 0x2a98
        float linkMapPos[2];                // 0x2b70
        TimeCount timeCount;                // 0x2b78
        u8 dungeonIndex;                    // 0x2cc8: LevelIndex
        u8 pad_2cc9[0x3];
        s32 hoveredPinIndex;                // 0x2ccc: -1 none
        s32 previousPinIndex;               // 0x2cd0
        u8 lastPinType;                     // 0x2cd4
        u8 pinType;                         // 0x2cd5: Navi::pinType
        u8 previousPinType;                 // 0x2cd6
        u8 pad_2cd7;
        float zoomScale;                    // 0x2cd8
        float zoomFrom;                     // 0x2cdc: the level the zoom started from (likely)
        float zoomProgress;                 // 0x2ce0: 0 to 1; scroll and cursorScale follow it
        float cursorScale;                  // 0x2ce4: the zoom's factor on the cursor's speed and the hover radius
        float zoomStep;                     // 0x2ce8
        u8 zoomIndex;                       // 0x2cec: into zoomLevels
        u8 previousZoomIndex;               // 0x2ced
        u8 zoomBusy;                        // 0x2cee
        u8 pad_2cef;
        float scrollStart[2];               // 0x2cf0
        float scroll[2];                    // 0x2cf8
        float scrollTarget[2];              // 0x2d00
        float cursorSpeed;                  // 0x2d08: 4.25 while the stick is idle
        float zoomLevels[2];                // 0x2d0c: 1, 2
        u8 pad_2d14[0x4];
        float cursorX, cursorY;             // 0x2d18
        u8 linkSideView;                    // 0x2d20
        u8 floorCount;                      // 0x2d21
        u8 topFloor;                        // 0x2d22
        u8 shownFloor;                      // 0x2d23
        u8 previousFloor;                   // 0x2d24
        u8 pad_2d25[0x3];
        s32 floorArtIndex;                  // 0x2d28
        s32 previousFloorArtIndex;          // 0x2d2c
        u8 linkFloor;                       // 0x2d30
        u8 bossFloor;                       // 0x2d31
        u8 entranceFloor;                   // 0x2d32
        u8 pad_2d33[0xd];
        u8 info;                            // 0x2d40: Y held: the dungeon's info, no hover
        u8 cursorRestored;                  // 0x2d41: from Navi::mapCursor, else put on Link's marker
        u8 extraIconMade;                   // 0x2d42
        u8 cursorShown;                     // 0x2d43: the cursor's In played
        u8 sideViewMarkerShown;             // 0x2d44: the side-view Link marker placed
        u8 pad_2d45[0x3];
        Ui::TagProcessor tagProcessor;      // 0x2d48: panelDungeonName's
    };
    static_assert(offsetof(DungeonMap, machine) == 0xf0 && offsetof(DungeonMap, cellOfArea) == 0x4d0);
    static_assert(offsetof(DungeonMap, floors) == 0x508 && offsetof(DungeonMap, mapSymbol) == 0x730);
    static_assert(offsetof(DungeonMap, mapOrigin) == 0x748 && offsetof(DungeonMap, mapPin) == 0x790);
    static_assert(offsetof(DungeonMap, cells) == 0x868 && offsetof(DungeonMap, roomIcons) == 0x1268);
    static_assert(offsetof(DungeonMap, hoveredPin) == 0x13c0 && offsetof(DungeonMap, floorChoices) == 0x1580);
    static_assert(offsetof(DungeonMap, pinBar) == 0x1b30 && offsetof(DungeonMap, entrance) == 0x29c8);
    static_assert(offsetof(DungeonMap, timeCount) == 0x2b78 && offsetof(DungeonMap, hoveredPinIndex) == 0x2ccc);
    static_assert(offsetof(DungeonMap, pinType) == 0x2cd5 && offsetof(DungeonMap, zoomProgress) == 0x2ce0);
    static_assert(offsetof(DungeonMap, cursorScale) == 0x2ce4 && offsetof(DungeonMap, cursorSpeed) == 0x2d08);
    static_assert(offsetof(DungeonMap, cursorX) == 0x2d18 && offsetof(DungeonMap, shownFloor) == 0x2d23);
    static_assert(offsetof(DungeonMap, info) == 0x2d40 && offsetof(DungeonMap, sideViewMarkerShown) == 0x2d44);
    static_assert(offsetof(DungeonMap, tagProcessor) == 0x2d48);
    static_assert(sizeof(DungeonMap) == 0x2e60);
    constexpr u8 kKeyWait = 2;

    // The overworld map page (UI 0xf, Gm_Map, OverworldMap_Ctor; 0x81d0 bytes).
    struct OverworldMap {
        Ui::MenuPage page;
        StateMachine<17> machine;           // 0xf0: Deactive..Close, OpenMask
        UnorderedMap map_500;               // 0x500: only made and destroyed
        Ui::Animation mapNameIn;            // 0x528
        Ui::Animation field_540;            // 0x540: never set
        Ui::Animation mapSymbol;            // 0x558
        Ui::Animation fldMaskOpen;          // 0x570
        Ui::Animation memoryIn;             // 0x588
        Ui::Animation memoryOut;            // 0x5a0
        Ui::Animation story;                // 0x5b8
        Ui::Animation heart;                // 0x5d0
        Ui::Animation sell;                 // 0x5e8
        Vector<SharedPtr<>> heartMarkers;   // 0x600: MapMarker, the Memories'
        Vector<SharedPtr<>> shellMarkers;   // 0x618
        Ui::PaneHandle mapCore;             // 0x630
        Ui::PaneHandle mapPin;              // 0x648
        Ui::PaneHandle mapBox;              // 0x660
        Ui::PaneHandle prevMarker;          // 0x678
        Ui::PaneHandle pointerLimit;        // 0x690
        Ui::PaneHandle scrollLimit;         // 0x6a8
        Ui::PaneHandle heartMarkerPane;     // 0x6c0
        Ui::PaneHandle sellMarkerPane;      // 0x6d8
        Ui::PaneHandle markerLimit;         // 0x6f0
        u64 markerRowCount;                 // 0x708: UiFieldMapIcons rows
        Vector<SharedPtr<MapMarker>> markers;  // 0x710
        u64 placeNameCount;                 // 0x728: UiFieldMapLocationName rows
        Ui::TextHolder placeNames[64];      // 0x730: T_MapName_<i>
        u8 placeNamesShown;                 // 0xd30: Y held
        u8 pad_d31[0x7];
        u64 maskRowCount;                   // 0xd38: UiFieldMapMask rows
        FieldAreaPart fieldAreas[32];       // 0xd40: L_FldArea_<a>
        Ui::PaneHandle fieldAreaMasks[32];  // 0x3840: L_FldAreaMask_<a>
        SharedPtr<> pins[30];               // 0x3b40: MapMarkUnit
        SharedPtr<> hoveredPin;             // 0x3d20
        SharedPtr<> memories;               // 0x3d30: the Memories UI
        Vector<SheetRowRef> memoryList;     // 0x3d40: the UiMemory rows shown
        u8 pad_3d58[0x8];
        MemoryMarker memoryParts[24];       // 0x3d60: L_Memory_<i>
        MapPlayer linkMarker;               // 0x71e0
        MapMarkList pinBar;                 // 0x72b8
        s32 hoveredMarker;                  // 0x8150: index into markers, -1 none
        s32 previousMarker;                 // 0x8154
        s32 hoveredPinIndex;                // 0x8158
        s32 previousPinIndex;               // 0x815c
        u8 lastPinType;                     // 0x8160
        u8 pinType;                         // 0x8161: Navi::pinType
        u8 previousPinType;                 // 0x8162
        u8 pad_8163;
        float zoomScale;                    // 0x8164
        float zoomTarget;                   // 0x8168
        float zoomProgress;                 // 0x816c: 0 to 1, 1 once a zoom ends
        float cursorScale;                  // 0x8170
        float zoomStep;                     // 0x8174
        u8 zoomIndex;                       // 0x8178
        u8 previousZoomIndex;               // 0x8179
        u8 zoomBusy;                        // 0x817a
        u8 pad_817b[0x5];
        float scrollStart[2];               // 0x8180
        float scroll[2];                    // 0x8188
        float scrollTarget[2];              // 0x8190
        float cursorSpeed;                  // 0x8198: 4.25 to 9
        float zoomLevels[2];                // 0x819c: 1, 3
        u8 pad_81a4[0x4];
        float cursor[2];                    // 0x81a8
        float linkPos[2];                   // 0x81b0
        u8 linkHidden;                      // 0x81b8: Link is not in the overworld
        u8 fromIndex;                       // 0x81b9
        u8 pad_81ba[0x2];
        u32 memoryToClose;                  // 0x81bc
        u8 maskOpening;                     // 0x81c0: a field area's mask opens: Open goes on to OpenMask
        u8 cursorShown;                     // 0x81c1: the cursor's In played
        u8 field_81c2;                      // 0x81c2: only zeroed
        u8 pad_81c3[0xd];
    };
    static_assert(offsetof(OverworldMap, machine) == 0xf0 && offsetof(OverworldMap, map_500) == 0x500);
    static_assert(offsetof(OverworldMap, mapNameIn) == 0x528 && offsetof(OverworldMap, heartMarkers) == 0x600);
    static_assert(offsetof(OverworldMap, mapCore) == 0x630 && offsetof(OverworldMap, markerRowCount) == 0x708);
    static_assert(offsetof(OverworldMap, markers) == 0x710 && offsetof(OverworldMap, placeNames) == 0x730);
    static_assert(offsetof(OverworldMap, fieldAreas) == 0xd40 && offsetof(OverworldMap, fieldAreaMasks) == 0x3840);
    static_assert(offsetof(OverworldMap, pins) == 0x3b40 && offsetof(OverworldMap, memories) == 0x3d30);
    static_assert(offsetof(OverworldMap, memoryParts) == 0x3d60 && offsetof(OverworldMap, pinBar) == 0x72b8);
    static_assert(offsetof(OverworldMap, hoveredMarker) == 0x8150 && offsetof(OverworldMap, pinType) == 0x8161);
    static_assert(offsetof(OverworldMap, cursorScale) == 0x8170 && offsetof(OverworldMap, zoomIndex) == 0x8178);
    static_assert(offsetof(OverworldMap, cursorSpeed) == 0x8198 && offsetof(OverworldMap, cursor) == 0x81a8);
    static_assert(offsetof(OverworldMap, memoryToClose) == 0x81bc && offsetof(OverworldMap, cursorShown) == 0x81c1);
    static_assert(offsetof(OverworldMap, linkMarker) == 0x71e0 && sizeof(OverworldMap) == 0x81d0);
}
