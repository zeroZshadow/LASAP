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

    // ore::Allocator
    struct Allocator {
        struct Vtable {
            void (*dtor)(Allocator*);
            void (*deletingDtor)(Allocator*);
            void* (*alloc)(Allocator*, size_t size, size_t alignment);
            void (*free)(Allocator*, void* ptr);
        };
        const Vtable* vtable;
    };

    // The callbacks an EvflAllocator allocates through
    struct AllocateArg {
        void* (*alloc)(size_t size, size_t alignment, void* userData);
        void (*free)(void* ptr, void* userData);
        void* allocUserData;       // 0x10
        void* freeUserData;        // 0x18
    };

    // An Allocator over AllocateArg (vtable 0x1c4c850)
    struct EvflAllocator {
        Allocator base;
        AllocateArg arg;           // 0x8
    };
    static_assert(sizeof(EvflAllocator) == 0x28);

    // ore::Array
    template<typename T>
    struct Array {
        T* data;
        s32 size;                  // 0x8
        u8 pad_c[0x4];

        T* begin() const { return data; }
        T* end() const { return data + size; }
        T& operator[](size_t i) const { return data[i]; }
    };
    static_assert(sizeof(Array<u8>) == 0x10);

    // ore::DynArrayList: an Array that grows through its allocator
    template<typename T>
    struct DynArrayList {
        T* data;
        s32 size;                  // 0x8
        s32 capacity;              // 0xc
        Allocator* allocator;      // 0x10

        T* begin() const { return data; }
        T* end() const { return data + size; }
        T& operator[](size_t i) const { return data[i]; }
    };
    static_assert(sizeof(DynArrayList<u8>) == 0x18);

    // ore::IntrusiveList: the end node and the offset of the node in an element
    struct IntrusiveList {
        ListNode end;
        s32 nodeOffset;            // 0x10
        u8 pad_14[0x4];
    };
    static_assert(sizeof(IntrusiveList) == 0x18);

    struct FlowchartContext;
    struct FlowchartObj;

    // evfl::ParamAccessor: an action's or a query's parameters, a context's node's (the counter tells a reused node
    // apart) or a timeline clip's
    struct ParamAccessor {
        const ResContainer* params;  // a timeline clip's; null for a node, whose event holds them
        FlowchartContext* context; // 0x8
        s32 node;                  // 0x10: -1 for a clip
        s32 counter;               // 0x14
    };

    // evfl::TriggerType: what runs an action
    enum class TriggerType : u8 { Flowchart = 0, ClipEnter = 1, ClipLeave = 2, Oneshot = 3 };

    // evfl::ActionArg
    struct ActionArg {
        ParamAccessor params;
        void* actorUserData;       // 0x18: the ActorBinding's
        void* actionUserData;      // 0x20: the ActionEntry's
        FlowchartContext* context; // 0x28
        void* variablePack;        // 0x30: the node's
        const ResEvent* event;     // 0x38: a timeline's ResClip or ResOneshot instead
        float timelineTimeDelta;   // 0x40: 0 for a flowchart node
        TriggerType triggerType;   // 0x44
        u8 pad_45[0x3];
    };
    static_assert(offsetof(ActionArg, actionUserData) == 0x20 && sizeof(ActionArg) == 0x48);
    static_assert(offsetof(ActionArg, triggerType) == 0x44);

    // evfl::QueryArg
    struct QueryArg {
        void* actorUserData;
        void* queryUserData;       // 0x8: the QueryEntry's
        const ResEvent* event;     // 0x10
        FlowchartContext* context; // 0x18
        void* variablePack;        // 0x20
        ParamAccessor params;      // 0x28
    };
    static_assert(offsetof(QueryArg, context) == 0x18 && sizeof(QueryArg) == 0x40);

    // evfl::ActionDoneHandler: marks the action's node done and processes the context, once. The context lists the
    // pending ones.
    struct ActionDoneHandler {
        ListNode link;
        FlowchartContext* context; // 0x10: null once the context is cleared
        s32 node;                  // 0x18
        s32 counter;               // 0x1c
        FlowchartObj* obj;         // 0x20
        bool done;                 // 0x28
        bool isFlowchart;          // 0x29: false for a timeline clip (obj a TimelineObj): Flow_RunAction marks it done
        u8 pad_2a[0x6];
    };
    static_assert(offsetof(ActionDoneHandler, done) == 0x28 && sizeof(ActionDoneHandler) == 0x30);

    // The done handler goes by value in evfl's signature; not trivially copyable, it is passed by address
    using ActionHandler = void (*)(const ActionArg* arg, ActionDoneHandler* done);
    using QueryHandler = s32 (*)(const QueryArg* arg);  // the case to take

    // ActorBinding::Action / Query: the flow binds Flow_RunAction / Flow_RunQuery with the key of its step
    template<typename Handler>
    struct BindingEntry {
        Handler handler;
        void* userData;            // 0x8
        const ResString* const* name;  // 0x10: in ResActor::actions / queries
    };
    using ActionEntry = BindingEntry<ActionHandler>;
    using QueryEntry = BindingEntry<QueryHandler>;
    static_assert(sizeof(ActionEntry) == 0x18);

    // evfl::ActorBinding: what a flowchart actor is bound to
    struct ActorBinding {
        DynArrayList<ActionEntry> actions;
        DynArrayList<QueryEntry> queries;  // 0x18
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
        s32 usedActorCount;        // 0x8: the ActBinder from here
        u8 pad_c[0x4];
        Allocator* allocator;      // 0x10: the context's
        Array<ActorBinding> bindings;  // 0x18
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
        EvflAllocator allocator;   // everything the context allocates, the bindings' arrays included
        Array<FlowchartObj> objs;  // 0x28
        DynArrayList<ContextNode> nodes;  // 0x38: grows by 16
        IntrusiveList handlers;    // 0x50: the pending ActionDoneHandlers
        // 0x68: called as a sub flow starts (false) and as it returns (true); EventFlow_Make leaves it null
        void (*subFlowCallback)(FlowchartContext*, const ResFlowchart*, const ResEvent* subFlow, bool returned);
        void* metadataPack;        // 0x70
        WeakPtr<>* user;           // 0x78: the flow's weak self (Flow_FromContext)
        s32 nextNode;              // 0x80
        s32 allocatedNodes;        // 0x84: 0 once the flow has ended
        s32 obj;                   // 0x88: the object the entry is in
        s32 entry;                 // 0x8c
        bool processing;           // 0x90
        bool processAgain;         // 0x91: a node asked for another pass over the nodes
        u8 pad_92[0x6];
    };
    static_assert(offsetof(FlowchartContext, objs) == 0x28 && offsetof(FlowchartContext, nodes) == 0x38);
    static_assert(offsetof(FlowchartContext, handlers) == 0x50 && offsetof(FlowchartContext, subFlowCallback) == 0x68);
    static_assert(offsetof(FlowchartContext, metadataPack) == 0x70 && offsetof(FlowchartContext, obj) == 0x88);
    static_assert(sizeof(FlowchartContext) == 0x98);

    // ---- The game's event layer (scripts/re/EVENTS.md) ----

    struct RunningEvent;

    // The flow an event runs (EventFlow_Make, 0x1c8 bytes): the evfl context over the flowchart
    struct Flow {
        WeakPtr<> self;
        SharedPtr<> resource;      // 0x10: the flowchart resource EventFlow_Make got
        FlowchartContext context;  // 0x20
        UnorderedMap flowcharts;   // 0xb8: the entry and the sub flows it reaches (Flow_CollectFlowcharts)
        UnorderedMap actorUserData;  // 0xe0: each binding's user data by flowchart resource and ResActor (Flow_Bind)
        SharedPtr<> args;          // 0x108: the request's EventArgs (Flow_Start), dropped by the cancel callback
        SharedPtr<> entryResource;  // 0x118: Flow_FindEntry's result: the resource, the flowchart and the entry
        const ResFlowchart* flowchart;  // 0x128
        const ResEntryPoint* entry;  // 0x130
        UnorderedMap actionSteps;  // 0x138: std::functions by action key, emptied by Flow_UnbindAll
        UnorderedMap querySteps;   // 0x160: by query key
        UnorderedMap waiting;      // 0x188: the pending actions' waiters (a set of shared pointers), Flow_RunAction's
        u32 nextActionKey;         // 0x1b0: from 1
        u32 nextQueryKey;          // 0x1b4: from 1
        bool cancelled;            // 0x1b8
        u8 pad_1b9[0x7];
        RunningEvent* event;       // 0x1c0
    };
    static_assert(offsetof(Flow, context) == 0x20 && offsetof(Flow, flowcharts) == 0xb8);
    static_assert(offsetof(Flow, args) == 0x108 && offsetof(Flow, flowchart) == 0x128);
    static_assert(offsetof(Flow, actionSteps) == 0x138 && offsetof(Flow, waiting) == 0x188);
    static_assert(offsetof(Flow, nextActionKey) == 0x1b0 && offsetof(Flow, cancelled) == 0x1b8);
    static_assert(offsetof(Flow, event) == 0x1c0 && sizeof(Flow) == 0x1c8);

    // A value of an event's actor map, keyed by a flowchart actor's name: a system actor (no user data) or the
    // charas registered under the name, and the lookups that give an action's or a query's handler by name
    struct ActorMapValue {
        void* userData;            // the EventChara, null for a system actor
        UnorderedMap bySubName;    // 0x8: user data by a flowchart actor's secondary name
        Function actionLookup;     // 0x30: a ResAction's step, kept in Flow::actionSteps (returned through x8)
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
        s32 returnValue;           // 0xa0: FlowControl.SetReturnValue's value, 0 when made; read once the request ends
        u8 pad_a4[0x4];
        FixedString<0x40> jumpLevel;    // 0xa8: the pending level jump (Event_SetLevelJump)
        FixedString<0x40> jumpLocator;  // 0xf0: length 0 when none is pending
        u8 pad_138[0x8];
        u8 jumpOffset[0x10];       // 0x140: offsetX and offsetZ (a 16-aligned vector)
        Function startGate;        // 0x150: returns a waiter; the flow starts once it finishes
        u8 kind;                   // 0x180: Player_OnEventStart's eventKind from it
        bool keepTaskOnCancel;     // 0x181: also keeps the flow from starting when the start gate opens
        u8 pad_182[0xe];

        StrView Entry() const { return StrView{entry.data, entry.length}; }
    };
    static_assert(offsetof(RunningEvent, flow) == 0x10 && offsetof(RunningEvent, task) == 0x40);
    static_assert(offsetof(RunningEvent, actors) == 0x50 && offsetof(RunningEvent, charas) == 0x78);
    static_assert(offsetof(RunningEvent, jumpLevel) == 0xa8 && offsetof(RunningEvent, jumpLocator) == 0xf0);
    static_assert(offsetof(RunningEvent, jumpOffset) == 0x140 && offsetof(RunningEvent, startGate) == 0x150);
    static_assert(offsetof(RunningEvent, returnValue) == 0xa0);
    static_assert(offsetof(RunningEvent, kind) == 0x180 && sizeof(RunningEvent) == 400);

    // A request of the event manager (EventManager_Request; vtable 0x1bc2240, a second base at +0x88)
    struct EventRequest {
        const uintptr_t* vtable;
        Signal finished;           // 0x8: one-shot; the event's task ended or the request was cancelled
        RunningEvent* event;       // 0x60: a shared pointer
        void* eventCtrl;
        // 0x70: the entry point's arguments (EventArgs, a shared pointer; null for EventManager_Request)
        void* args;
        void* argsCtrl;
        bool cancelled;            // 0x80
        u8 pad_81[0x7];
        const uintptr_t* vtable2;  // 0x88

        // finished has fired: the event ended, or the request was cancelled (virtual +0x18; a one-shot signal
        // closes as it fires)
        bool Finished() const { return __atomic_load_n(&finished.nextHandle, __ATOMIC_ACQUIRE) == 0; }
    };
    static_assert(offsetof(EventRequest, finished) == 0x8 && offsetof(EventRequest, event) == 0x60);
    static_assert(offsetof(EventRequest, args) == 0x70);
    static_assert(offsetof(EventRequest, cancelled) == 0x80 && sizeof(EventRequest) == 0x90);

    // The event manager (EventManager_ptr, 0x1e0 bytes): its requests, a deque of shared_ptrs (0x10 each, 256 a
    // block), the running one first, and the signals around them. Code connects to a signal through its owner.
    struct EventManager {
        u8 unused_0[0x8];          // never accessed
        // 0x8: vtable 0x1bc1910: operator new (the alignment ignored) and delete; the evfl runtime's allocator
        Allocator allocator;
        const uintptr_t* resourceListener;  // 0x10
        SharedPtr<> resources;     // 0x18: holds event/common.evhb
        Signal queueStarted;       // 0x28: a request entered the empty queue
        Signal queueEnded;         // 0x80: the last request finished
        Signal eventStarted;       // 0xd8: before each event starts
        Signal eventEnded;         // 0x130: an event's request finished
        // 0x188: the event cameras of players 1 and 2 (EventCamera_Ctor, vtable 0x1bbbce8), disabled once the queue
        // empties; camera actions enable the first (scripts/re/EVENTS.md, Event cameras), none the second
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
        u8 pad_58[0x8];
        // 0x60: the action hooks (play an animation, move to a position, look at, ...), by index in
        // scripts/re/EVENTS.md, EventChara
        Function hooks[13];
        FixedString<0x10> idleAnim;  // 0x2d0: NPCs: "@IDLE"
        float idleBlend;           // 0x2e8
        u8 pad_2ec[0x4];
        FixedString<0x10> talkAnim;  // 0x2f0: NPCs: "@TALK"
        float talkBlend;           // 0x308
        u8 pad_30c[0x4];
        FixedString<0x40> talker;  // 0x310: the talk window's speaker name, a message label (NPCs: "Talker:{0}")
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
        float stopDistance;        // 0x504: 1 when made; the PlayerMoveTarget's as Link walks up to it
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
    static_assert(offsetof(EventChara, talker) == 0x310);
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
