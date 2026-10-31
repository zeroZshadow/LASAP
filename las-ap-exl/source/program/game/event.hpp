#pragma once
// The event manager, its events, their flows and flowcharts (scripts/re/EVENTS.md). A flowchart is the event file
// (.bfevfl, the format of the evfl library the patcher uses) loaded and relocated in place: its offsets are pointers
// and its strings are a u16 length and the characters.
#include "game/actor.hpp"
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

    // ---- The evfl runtime (scripts/re/EVENTS.md, Flow and the evfl runtime) ----

    // An action's or a query's binding: the handler the actor's lookup gave, its user data and the resource
    struct BindingEntry {
        void* handler;
        void* userData;            // 0x8
        const void* res;           // 0x10: ResAction* or ResQuery*
    };
    static_assert(sizeof(BindingEntry) == 0x18);

    // evfl::ActorBinding: what a flowchart actor is bound to
    struct ActorBinding {
        BindingEntry* actions;
        u32 actionCount;           // 0x8
        u32 actionCapacity;
        u8 pad_10[0x8];
        BindingEntry* queries;     // 0x18
        u32 queryCount;            // 0x20
        u32 queryCapacity;
        u8 pad_28[0x8];
        void* userData;            // 0x30: the actor map value's user data
        const ResActor* actor;     // 0x38
        bool initialized;          // 0x40
        bool used;                 // 0x41
        u8 pad_42[0x6];
    };
    static_assert(offsetof(ActorBinding, queries) == 0x18 && offsetof(ActorBinding, userData) == 0x30);
    static_assert(sizeof(ActorBinding) == 0x48);

    // evfl::FlowchartObj: a flowchart of the context and its ActBinder, one binding per actor
    struct FlowchartObj {
        const ResFlowchart* flowchart;
        u32 usedActorCount;        // 0x8: the ActBinder from here
        u8 pad_c[0x4];
        void* allocator;           // 0x10
        ActorBinding* bindings;    // 0x18
        u32 bindingCount;          // 0x20
        u8 pad_24[0x4];
        bool used;                 // 0x28
        u8 pad_29[0x7];
    };
    static_assert(offsetof(FlowchartObj, bindings) == 0x18 && sizeof(FlowchartObj) == 0x30);

    // evfl::FlowchartContextNode::State
    enum class NodeState : u8 { Invalid = 0, Free = 1, NotInvoked = 2, Invoked = 3, Done = 4, Waiting = 5 };

    // evfl::FlowchartContextNode: a point of the flow being run (more than one while forks run)
    struct ContextNode {
        FlowchartObj* obj;
        void* variablePack;        // 0x8
        s32 counter;               // 0x10
        u16 event;                 // 0x14: the ResEvent's index
        u16 nextNode;              // 0x16
        u16 index;                 // 0x18
        NodeState state;           // 0x1a
        bool ownsVariablePack;     // 0x1b
        u8 pad_1c[0x4];
    };
    static_assert(offsetof(ContextNode, state) == 0x1a && sizeof(ContextNode) == 0x20);

    // evfl::FlowchartContext (0x98 bytes, the layout of Breath of the Wild's evfl)
    struct FlowchartContext {
        u8 allocator[0x28];
        FlowchartObj* objs;        // 0x28
        u32 objCount;              // 0x30
        u8 pad_34[0x4];
        ContextNode* nodes;        // 0x38
        u32 nodeCount;             // 0x40
        u32 nodeCapacity;          // 0x44
        u8 pad_48[0x8];
        u8 handlers[0x20];         // 0x50: the ActionDoneHandlers, an intrusive list
        void* metadataPack;        // 0x70
        WeakPtr<>* user;           // 0x78: the flow's weak self (Flow_FromContext)
        s32 nextNode;              // 0x80
        s32 allocatedNodes;        // 0x84: 0 once the flow has ended
        s32 obj;                   // 0x88: the object the entry is in
        s32 entry;                 // 0x8c
        bool processing;           // 0x90
        u8 pad_91[0x7];
    };
    static_assert(offsetof(FlowchartContext, objs) == 0x28 && offsetof(FlowchartContext, nodes) == 0x38);
    static_assert(offsetof(FlowchartContext, metadataPack) == 0x70 && offsetof(FlowchartContext, obj) == 0x88);
    static_assert(sizeof(FlowchartContext) == 0x98);

    // ---- The game's event layer (scripts/re/EVENTS.md) ----

    struct RunningEvent;

    // The flow an event runs (EventFlow_Make, 0x1c8 bytes): the evfl context over the flowchart
    struct Flow {
        WeakPtr<> self;
        u8 unknown10[0x10];
        FlowchartContext context;  // 0x20
        u8 unknownB8[0x70];
        const ResFlowchart* flowchart;  // 0x128
        u8 unknown130[0x8];
        UnorderedMap callbacksA;   // 0x138: std::functions, emptied by Flow_UnbindAll
        UnorderedMap callbacksB;   // 0x160
        UnorderedMap waiting;      // 0x188: the pending actions' waiters (a set of shared pointers), Flow_RunAction's
        u8 unknown1b0[0x8];
        bool cancelled;            // 0x1b8
        u8 pad_1b9[0x7];
        RunningEvent* event;       // 0x1c0
    };
    static_assert(offsetof(Flow, context) == 0x20 && offsetof(Flow, flowchart) == 0x128);
    static_assert(offsetof(Flow, callbacksA) == 0x138 && offsetof(Flow, waiting) == 0x188);
    static_assert(offsetof(Flow, cancelled) == 0x1b8 && offsetof(Flow, event) == 0x1c0 && sizeof(Flow) == 0x1c8);

    // A value of an event's actor map, keyed by a flowchart actor's name: a system actor (no user data) or the
    // charas registered under the name, and the lookups that give an action's or a query's handler by name
    struct ActorMapValue {
        void* userData;            // the EventChara, null for a system actor
        UnorderedMap bySubName;    // 0x8: user data by a flowchart actor's secondary name
        Function actionLookup;     // 0x30: the evfl handler of a ResAction (returned through x8)
        Function queryLookup;      // 0x60: of a ResQuery
    };
    static_assert(offsetof(ActorMapValue, actionLookup) == 0x30 && sizeof(ActorMapValue) == 0x90);

    // An event (Event_Make, 400 bytes)
    struct RunningEvent {
        WeakPtr<> self;
        Flow* flow;                // 0x10: a shared pointer
        void* flowCtrl;
        SharedString entry;        // 0x20: the entry's name
        void* task;                // 0x40: the FlowchartContext:Run() task, a shared pointer into a virtual base
        void* taskCtrl;
        UnorderedMap actors;       // 0x50: ActorMapValue by name
        UnorderedMap charas;       // 0x78: the EventCharas in the event (Event_HasChara)
        u32 unknownA0;
        u8 pad_a4[0x4];
        FixedString<0x40> jumpLevel;    // 0xa8: the pending level jump (Event_SetLevelJump)
        FixedString<0x40> jumpLocator;  // 0xf0: length 0 when none is pending
        u8 unknown138[0x8];
        u8 jumpOffset[0x10];       // 0x140: offsetX and offsetZ
        Function hook150;          // 0x150: when set, EventManager_StartRequest calls it instead of Flow_Start (likely)
        u8 kind;                   // 0x180: Player_OnEventStart's eventKind from it
        bool keepTaskOnCancel;     // 0x181
        u8 pad_182[0xe];

        StrView Entry() const { return StrView{entry.data, entry.length}; }
    };
    static_assert(offsetof(RunningEvent, flow) == 0x10 && offsetof(RunningEvent, task) == 0x40);
    static_assert(offsetof(RunningEvent, actors) == 0x50 && offsetof(RunningEvent, charas) == 0x78);
    static_assert(offsetof(RunningEvent, jumpLevel) == 0xa8 && offsetof(RunningEvent, jumpLocator) == 0xf0);
    static_assert(offsetof(RunningEvent, jumpOffset) == 0x140 && offsetof(RunningEvent, hook150) == 0x150);
    static_assert(offsetof(RunningEvent, kind) == 0x180 && sizeof(RunningEvent) == 400);

    // A request of the event manager (EventManager_Request; vtable 0x1bc2240, a second base at +0x88)
    struct EventRequest {
        const uintptr_t* vtable;
        Signal finished;           // 0x8: one-shot; the event's task ended or the request was cancelled
        RunningEvent* event;       // 0x60: a shared pointer
        void* eventCtrl;
        u8 unknown70[0x10];        // 0x70: Flow_Start takes it as the metadata pack's owner (+0x10 of it)
        bool cancelled;            // 0x80
        u8 pad_81[0x7];
        const uintptr_t* vtable2;  // 0x88

        // finished has fired: the event ended, or the request was cancelled (virtual +0x18; a one-shot signal
        // closes as it fires)
        bool Finished() const { return __atomic_load_n(&finished.nextHandle, __ATOMIC_ACQUIRE) == 0; }
    };
    static_assert(offsetof(EventRequest, finished) == 0x8 && offsetof(EventRequest, event) == 0x60);
    static_assert(offsetof(EventRequest, cancelled) == 0x80 && sizeof(EventRequest) == 0x90);

    // The event manager (EventManager_ptr, 0x1e0 bytes): its requests, a deque of shared_ptrs (0x10 each, 256 a
    // block), the running one first, and the signals around them. Code connects to a signal through its owner.
    struct EventManager {
        u8 unknown0[0x8];
        const uintptr_t* vtable;   // 0x8
        const uintptr_t* resourceListener;  // 0x10
        SharedPtr<> resources;     // 0x18: holds event/common.evhb
        Signal queueStarted;       // 0x28: a request entered the empty queue
        Signal queueEnded;         // 0x80: the last request finished
        Signal eventStarted;       // 0xd8: before each event starts
        Signal eventEnded;         // 0x130: an event's request finished
        // 0x188: two event camera actors (EventCamera_Ctor, vtable 0x1bbbce8), disabled once the queue empties; the
        // item-get's event enables the first
        SharedPtr<> cameraA;
        SharedPtr<> cameraB;       // 0x198
        Deque requests;            // 0x1a8: SharedPtr<EventRequest>
        bool cancelAll;            // 0x1d8: the next request end cancels the queue
        u8 pad_1d9[0x7];
    };
    static_assert(offsetof(EventManager, queueStarted) == 0x28 && offsetof(EventManager, eventStarted) == 0xd8);
    static_assert(offsetof(EventManager, eventEnded) == 0x130 && offsetof(EventManager, requests) == 0x1a8);
    static_assert(offsetof(EventManager, cancelAll) == 0x1d8 && sizeof(EventManager) == 0x1e0);

    // The request at the front of the queue, whose event runs; nullptr when none
    inline const EventRequest* FrontRequest(const EventManager* manager) {
        if (manager == nullptr || manager->requests.size == 0) return nullptr;
        u64 i = manager->requests.start;
        auto* slot = static_cast<const SharedPtr<>*>(manager->requests.mapBegin[i >> 8]) + (i & 0xff);
        return static_cast<const EventRequest*>(slot->ptr);
    }

    // The event that runs, nullptr when none
    inline const RunningEvent* Running(const EventManager* manager) {
        const EventRequest* request = FrontRequest(manager);
        return request != nullptr ? request->event : nullptr;
    }

    // The component that makes an actor a flowchart actor (EventChara_Ctor, vtable 0x1b5c208, 0x520 bytes)
    struct EventChara {
        Entity entity;
        u8 pad_58[0x8];            // Component's own
        // 0x60: 0 play an animation (a waiter), 1 play a timeline clip's animation, 2/3 play/stop a Tailor value on
        // channel Other, 4 set a Tailor property, 5 a facial, 6 move to a position (a waiter), 7 turn toward a
        // position, 8 follow a rail, 9 set the pose, 10 gravity, 11 reset the velocity, 12 look at
        // (scripts/re/EVENTS.md, EventChara)
        Function hooks[13];
        FixedString<0x10> idleAnim;  // 0x2d0: NPCs: "@IDLE"
        float idleBlend;           // 0x2e8
        u8 pad_2ec[0x4];
        FixedString<0x10> talkAnim;  // 0x2f0: NPCs: "@TALK"
        float talkBlend;           // 0x308
        u8 unknown30c[0x4c];
        FixedString<0x20> name;    // 0x358: a flowchart actor's name binds to it
        FixedString<0x20> subName;  // 0x380: and its secondary name
        Signal signal;             // 0x3a8
        Function actionLookup;     // 0x400: bool(Function* step, const StrView* name): the step of a known action
        Function queryLookup;      // 0x430
        Function lookTarget;       // 0x460: {Vec4 position, bool valid}
        u8 lookMode;               // 0x490: the look-at hook's mode with no valid target; 1 when made
        u8 pad_491[0xf];
        Function cameraAnimation;  // 0x4a0: a camera animation by name (Play*OriginCameraAnimation)
        Function pointPosition;    // 0x4d0: a named point's world position (Link's)
        float turnRate;            // 0x500
        float unknown504;          // 0x504: 1 when made
        bool turning;              // 0x508: EventChara_LookAndTurn turns toward the look target
        bool turnHeld;             // 0x509
        u8 pad_50a[0x2];
        bool looking;              // 0x50c: EventChara_LookAndTurn calls the look-at hook
        u8 pad_50d[0x3];
        u32 startedHandle;         // 0x510: EventChara_Start's eventStarted and eventEnded connections
        u32 endedHandle;           // 0x514
        u8 pad_518[0x8];
    };
    static_assert(offsetof(EventChara, hooks) == 0x60 && offsetof(EventChara, idleAnim) == 0x2d0);
    static_assert(offsetof(EventChara, talkAnim) == 0x2f0 && offsetof(EventChara, name) == 0x358);
    static_assert(offsetof(EventChara, subName) == 0x380 && offsetof(EventChara, signal) == 0x3a8);
    static_assert(offsetof(EventChara, actionLookup) == 0x400 && offsetof(EventChara, queryLookup) == 0x430);
    static_assert(offsetof(EventChara, cameraAnimation) == 0x4a0 && offsetof(EventChara, pointPosition) == 0x4d0);
    static_assert(offsetof(EventChara, turnRate) == 0x500 && sizeof(EventChara) == 0x520);

    // event/common.evhb: the event actors' names and their parents (Character, Object, Item, Enemy, ...)
    struct EvhbEntry {
        u32 nameOffset;            // from the file's start
        u32 unused;
        u32 nameLength;            // 0x8
        s32 parent;                // 0xc: -1 none
    };
    struct EvhbHeader {
        u32 magic;                 // "evhb"
        u32 version;               // 1
        u32 count;                 // 0x8
        u32 unusedC;
        EvhbEntry entries[1];      // 0x10: count
    };
}
