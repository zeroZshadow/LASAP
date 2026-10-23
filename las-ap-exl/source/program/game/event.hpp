#pragma once
// The event manager's running event and its flowchart (scripts/re/EVENTS.md). A flowchart is the event file
// (.bfevfl, the format of the evfl library the patcher uses) loaded and relocated in place: its offsets are pointers
// and its strings are a u16 length and the characters.
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Event {
    // A string in a flowchart
    struct ResString {
        u16 size;
        char data[1];

        StrView View() const { return StrView{data, size}; }
    };

    // A parameter container (an action's or a query's arguments): items in the order of its dictionary's names
    struct ResDic;
    struct ResContainerItem {
        u8 type;                   // 0x0: kArgument 0, kString 5, ... (ContainerDataType)
        u8 pad;
        u16 count;
        u32 unused;
        const ResDic* dic;         // 0x8: none for an item
        const ResString* string;   // 0x10: a kString's or a kArgument's value
    };
    struct ResContainer {
        u8 type;                   // 0x0: kContainer 1
        u8 pad;
        u16 count;                 // 0x2
        u32 unused;
        const ResDic* dic;         // 0x8: the item names
        const ResContainerItem* items[1];  // 0x10: count
    };
    constexpr u8 kContainerArgument = 0, kContainerString = 5;

    // A patricia-tree dictionary: entry 0 is the root, then one per name in the order of what it names
    struct ResDicEntry {
        u32 bit;
        u16 child[2];
        const ResString* name;     // 0x8
    };
    struct ResDic {
        u32 magic;                 // "DIC "
        u32 count;                 // 0x4: names, the root not counted
        ResDicEntry entries[1];    // 0x8: the root, then count
    };

    struct ResActor {
        const ResString* name;     // 0x0
        const ResString* subName;
        const ResString* argument;
        const ResString* const* actions;  // 0x18
        const ResString* const* queries;
        const ResContainer* params;
        u16 actionCount;           // 0x30
        u16 queryCount;
        u16 argumentEntry;
        u16 concurrentClips;
    };
    static_assert(sizeof(ResActor) == 0x38);

    constexpr u16 kNoEvent = 0xffff;
    enum class EventType : u8 { Action = 0, Switch = 1, Fork = 2, Join = 3, SubFlow = 4 };

    // An event; the fields from +0xa by type
    struct ResEvent {
        const ResString* name;     // 0x0
        EventType type;            // 0x8
        u8 pad;
        u16 a;                     // 0xa: action, join, subflow: the next event; switch: the cases; fork: the forks
        u16 b;                     // 0xc: action, switch: the actor; fork: the join event
        u16 c;                     // 0xe: action: the actor's action; switch: its query
        const void* p10;           // 0x10: action, switch, subflow: the parameters (ResContainer); fork: u16 events
        const void* p18;           // 0x18: switch: the cases (ResCase); subflow: the flowchart's name (ResString)
        const void* p20;           // 0x20: subflow: the entry's name (ResString)
    };
    static_assert(sizeof(ResEvent) == 0x28);

    struct ResCase {
        u32 value;
        u16 event;
        u16 pad;
    };

    struct ResEntryPoint {
        const u16* subFlowEvents;
        u64 unused8;
        const void* ptr10;
        u16 subFlowEventCount;     // 0x18
        u16 pad1a;
        u16 mainEvent;             // 0x1c
        u16 pad1e;
    };
    static_assert(sizeof(ResEntryPoint) == 0x20);

    struct ResFlowchart {
        u32 magic;                 // "EVFL"
        u32 stringPool;
        u32 unused8, unusedC;
        u16 actorCount;            // 0x10
        u16 actionCount;
        u16 queryCount;
        u16 eventCount;            // 0x16
        u16 entryCount;            // 0x18
        u16 pad[3];
        const ResString* name;     // 0x20
        const ResActor* actors;    // 0x28
        const ResEvent* events;    // 0x30
        const ResDic* entryNames;  // 0x38: in the order of entries
        const ResEntryPoint* entries;  // 0x40
    };
    static_assert(offsetof(ResFlowchart, entries) == 0x40);
    constexpr u32 kFlowchartMagic = 0x4c465645;  // "EVFL"

    // The flow an event runs (made by Event_Make from the flowchart and the entry)
    struct Flow {
        u8 unknown[0x128];
        const ResFlowchart* flowchart;  // 0x128
    };

    // An event (Event_Make, 400 bytes)
    struct RunningEvent {
        u8 unknown0[0x10];
        Flow* flow;                // 0x10
        u8 unknown18[0x18];
        StrView entry;             // 0x30: the entry's name
        u8 unknown40[0x140];
        u8 kind;                   // 0x180: Player_OnEventStart's eventKind from it
    };
    static_assert(offsetof(RunningEvent, entry) == 0x30 && offsetof(RunningEvent, kind) == 0x180);

    // The event manager (EventManager_ptr): its requests, a deque of shared_ptrs (0x10 each, 256 a block), the
    // running one first
    struct EventRequest {
        u8 unknown[0x60];
        RunningEvent* event;       // 0x60
    };
    struct EventManager {
        u8 unknown[0x1b0];
        EventRequest*** blocks;    // 0x1b0: blocks of {EventRequest*, control block}
        u8 unknown1b8[0x10];
        u64 start;                 // 0x1c8
        u64 count;                 // 0x1d0: 0 while no event runs
    };
    static_assert(offsetof(EventManager, count) == 0x1d0);

    // The event that runs, nullptr when none
    inline const RunningEvent* Running(const EventManager* manager) {
        if (manager == nullptr || manager->count == 0) return nullptr;
        EventRequest* const* slot = manager->blocks[manager->start >> 8] + (manager->start & 0xff) * 2;
        return *slot != nullptr ? (*slot)->event : nullptr;
    }
}
