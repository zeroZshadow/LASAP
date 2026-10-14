// Test build only (LASAP_TEST): what the test harness needs from the game and nothing a player does.
// - The debug port (game port + 100): DBG commands and any protocol line, replied to on the same connection
//   within milliseconds; everything the module sends the AP client is mirrored there as "AP <line>".
// - The trace (DBG trace on): a "T <ms> <frame> <what>" line on every change of the screen, the level, the
//   player's event state, the pause flag, the game's controller (its buttons and input lock), the buttons each
//   frame's input polls carried (physical and injected), the UI state machines and watched memory.
// - Frame-exact input, optionally waiting for a condition (DBG press), and text advance that presses only while a
//   message waits for a button (DBG advance).
// UI state machines (title, file select, message window, choices, name entry) are learned from their ChangeState
// calls; each keeps its state names in a table, so the trace names their states (TESTING.md, "Game state").
#ifdef LASAP_TEST
#include "AP/ap.hpp"
#include "AP/ap_debug.hpp"
#include "Game/framework.hpp"
#include "Game/Data/inventory.hpp"
#include "Hooks/Common/level.hpp"
#include "nn/socket.hpp"
#include "nn/os.hpp"
#include <netinet/in.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>

namespace {
    u64 NowMs() {
        u64 ticks;
        asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
        return ticks / 19200;
    }

    std::string Hex(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

    uintptr_t ModuleStart() { return exl::util::GetMainModuleInfo().m_Total.m_Start; }

    // ---- Debug port (net thread) ----

    constexpr int kMaxClients = 4;
    constexpr size_t kMaxLine = 4096;
    struct Client {
        s32 fd = -1;
        std::string partial;
        std::deque<std::string> out;
        u64 lastSeen = 0;
    };
    Client g_Clients[kMaxClients];
    s32 g_Listener = -1;
    int g_Port = 0;
    u64 g_ListenRetry = 0;
    nn::os::MutexType g_Lock;
    struct Inbound { int client; std::string line; };
    std::deque<Inbound> g_In;
    // The debug client whose command the game thread is handling: AP::Send replies go there.
    std::atomic<int> g_ReplyTo = -1;

    void Lock() { nn::os::LockMutex(&g_Lock); }
    void Unlock() { nn::os::UnlockMutex(&g_Lock); }

    void Push(int client, const std::string& line) {
        if (client < 0 || client >= kMaxClients) return;
        Lock();
        if (g_Clients[client].fd >= 0 && g_Clients[client].out.size() < 4096) g_Clients[client].out.push_back(line + "\n");
        Unlock();
    }

    void Broadcast(const std::string& line) {
        Lock();
        for (auto& c : g_Clients) {
            if (c.fd >= 0 && c.out.size() < 4096) c.out.push_back(line + "\n");
        }
        Unlock();
    }

    void CloseClient(Client& c) {
        nn::socket::Close(c.fd);
        c.fd = -1;
        c.partial.clear();
        c.out.clear();
    }

    bool Listen() {
        g_Listener = nn::socket::Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_Listener < 0) return false;
        int yes = 1;
        nn::socket::SetSockOpt(g_Listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = nn::socket::InetHtons(g_Port);
        if (nn::socket::Bind(g_Listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            nn::socket::Listen(g_Listener, 2) < 0) {
            nn::socket::Close(g_Listener);
            g_Listener = -1;
            return false;
        }
        Logging.Log("[AP] debug port " + std::to_string(g_Port));
        return true;
    }

    void Accept() {
        pollfd waiting{g_Listener, POLLIN, 0};
        if (nn::socket::Poll(&waiting, 1, 0) <= 0 || !(waiting.revents & POLLIN)) return;
        sockaddr addr{};
        u32 len = sizeof(addr);
        s32 fd = nn::socket::Accept(g_Listener, &addr, &len);
        if (fd < 0) return;
        // A full table drops the client that has been quiet longest (a test that was killed).
        int slot = -1;
        Lock();
        for (int i = 0; i < kMaxClients; i++) {
            if (g_Clients[i].fd < 0) { slot = i; break; }
            if (slot < 0 || g_Clients[i].lastSeen < g_Clients[slot].lastSeen) slot = i;
        }
        if (g_Clients[slot].fd >= 0) CloseClient(g_Clients[slot]);
        g_Clients[slot].fd = fd;
        g_Clients[slot].lastSeen = NowMs();
        g_Clients[slot].out.push_back(AP::HelloLine() + " debug\n");
        Unlock();
    }

    void Serve(int i) {
        Client& c = g_Clients[i];
        char buf[1024];
        ssize_t n = nn::socket::Recv(c.fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) {
            Lock();
            CloseClient(c);
            Unlock();
            return;
        }
        if (n > 0) {
            c.lastSeen = NowMs();
            c.partial.append(buf, n);
            size_t pos;
            while ((pos = c.partial.find('\n')) != std::string::npos) {
                std::string line = c.partial.substr(0, pos);
                c.partial.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                if (line == "PING") {
                    Push(i, "PONG");
                    continue;
                }
                Lock();
                g_In.push_back({i, std::move(line)});
                Unlock();
            }
            if (c.partial.size() > kMaxLine) c.partial.clear();
        }
        Lock();
        while (c.fd >= 0 && !c.out.empty()) {
            const std::string& line = c.out.front();
            size_t sent = 0;
            while (sent < line.size()) {
                ssize_t m = nn::socket::Send(c.fd, line.data() + sent, line.size() - sent, 0);
                if (m <= 0) break;
                sent += m;
            }
            if (sent < line.size()) {
                CloseClient(c);
                break;
            }
            c.out.pop_front();
        }
        Unlock();
    }

    // ---- Safe reads of game memory ----

    // Game thread only. Remembers the last few readable regions (the heap, the module's data).
    bool Readable(uintptr_t addr, size_t len) {
        struct Region { uintptr_t start, end; };
        static Region ok[4] = {};
        static int next = 0;
        if (addr == 0) return false;
        for (const auto& r : ok) {
            if (addr >= r.start && addr + len <= r.end) return true;
        }
        MemoryInfo mi{};
        u32 pi = 0;
        if (R_FAILED(svcQueryMemory(&mi, &pi, addr))) return false;
        if (mi.type == MemType_Unmapped || !(mi.perm & Perm_R) || addr + len > mi.addr + mi.size) return false;
        ok[next] = {mi.addr, mi.addr + mi.size};
        next = (next + 1) % 4;
        return true;
    }

    template<typename T>
    bool Read(uintptr_t addr, T& out) {
        if (!Readable(addr, sizeof(T))) return false;
        out = *reinterpret_cast<const T*>(addr);
        return true;
    }

    bool InRodata(uintptr_t p) {
        const auto& info = exl::util::GetMainModuleInfo();
        return p >= info.m_Rodata.m_Start && p < info.m_Rodata.m_Start + info.m_Rodata.m_Size;
    }

    bool InModule(uintptr_t p) {
        const auto& info = exl::util::GetMainModuleInfo();
        return p >= info.m_Total.m_Start && p < info.m_Data.m_Start + info.m_Data.m_Size;
    }

    // ---- UI state machines ----
    // Layout (game 1.0.0): u32 frames in the state at +0x8, current state +0xd (0xff none), previous +0xe, owner
    // +0x40, per state {enter, update, exit} member functions from +0x48 (0x30 each), then for N states a bitset at
    // +0x50 + N*0x30 and the name pointers at +0x58 + N*0x30. Each class has its own ChangeState(machine, state).
    // A hook on a ChangeState(machine, state) function (owner = -1), or on a state's update(owner) for machines whose
    // class changes state inline: the machine is then at owner + `owner`.
    struct SmHook { const char* name; uintptr_t offset; int states; ptrdiff_t owner = -1; };
    constexpr SmHook kSmHooks[] = {
        {"six", 0x87f1b0, 6},       // shared by 6-state machines, among them the title (L+R at 0xf87ec0)
        {"choice", 0xf83750, 6},    // system choice dialog (L_SystemChoice_)
        {"msg", 0xef0f40, 15},      // message window: Closed, Opening, ..., KeyWaiting, ..., Selecting, ...
        {"file", 0xebde10, 11},     // file select (UI:SystemTitleLoad)
        {"name", 0xeaf540, 7},      // name entry
        {"files", 0xe60b90, 39},    // title file menu (UI:DataTitleInitial): files, copy, delete, difficulty
        {"map", 0xe95090, 17, 0xf0},   // overworld map, from its KeyWait update: Deactive, Open, KeyWait, ...
        {"dmap", 0xe7a0f0, 16, 0xf0},  // dungeon map, from its KeyWait update
        {"lure", 0xdb6680, 18},     // fishing rod and lure: Wait, Casting, ..., HitRetrieveHold, Catch, Lost, ...
        {"fish", 0xb5e870, 21},     // each fishing pond fish: Enter, Wait, ..., MoveAngry, ..., MoveEscape, ...
    };
    constexpr int kSmHookCount = sizeof(kSmHooks) / sizeof(kSmHooks[0]);
    // Names for the owner classes found in the trace (owner vtable, module offset).
    struct KnownOwner { uintptr_t vt; const char* name; };
    constexpr KnownOwner kKnownOwners[] = {
        {0x1c30140, "title"},       // Deactive LogoIn BtnIn WaitLR GameStart End; takes L+R in WaitLR
    };

    constexpr int kMaxMachines = 32;
    struct Machine {
        std::atomic<uintptr_t> sm{0};
        int hook = -1;
        bool ready = false;     // the game thread has read its names
        uintptr_t names = 0;    // address of the name table
        uintptr_t check = 0;    // address of its first named state's entry, and that name, to notice a machine
        uintptr_t name0 = 0;    // that is gone (state 0 can be unnamed)
        uintptr_t vt = 0;       // owner vtable, module offset
        int state = -2;         // as last sampled
        u32 id = 0;
        std::string label;
    };
    Machine g_Machines[kMaxMachines];
    std::atomic<int> g_MachineHook[kMaxMachines];  // hook index + 1, set right after the slot is taken
    u32 g_NextMachineId = 1;
    // Frames the last message page spent printing (Reading), -1 before the first.
    int g_ReadFrames = -1;
    u32 g_ReadSince = 0;

    void Register(int hook, uintptr_t sm) {
        for (auto& m : g_Machines) {
            if (m.sm.load() == sm) return;
        }
        for (int i = 0; i < kMaxMachines; i++) {
            uintptr_t expected = 0;
            if (g_Machines[i].sm.compare_exchange_strong(expected, sm)) {
                g_MachineHook[i] = hook + 1;
                return;
            }
        }
    }

    template<int K>
    struct SmChange : public exl::hook::impl::TrampolineHook<SmChange<K>> {
        static void Callback(void* sm, u32 state) {
            Register(K, reinterpret_cast<uintptr_t>(sm));
            SmChange::Orig(sm, state);
        }
    };

    template<int K>
    struct SmUpdate : public exl::hook::impl::TrampolineHook<SmUpdate<K>> {
        static void Callback(void* owner) {
            Register(K, reinterpret_cast<uintptr_t>(owner) + kSmHooks[K].owner);
            SmUpdate::Orig(owner);
        }
    };

    std::string StateName(const Machine& m, int state) {
        if (state == 0xff) return "-";
        uintptr_t p = 0;
        if (state < 0 || state >= kSmHooks[m.hook].states || !Read(m.names + state * 8, p) || !InRodata(p)) {
            return std::to_string(state);
        }
        return reinterpret_cast<const char*>(p);
    }

    // Reads the machine's current state; -1 when it is gone (its memory reused).
    int MachineState(Machine& m) {
        uintptr_t sm = m.sm.load();
        uintptr_t name0 = 0;
        u8 state = 0;
        if (sm == 0 || !Read(m.check, name0) || name0 != m.name0 || !Read(sm + 0xd, state)) return -1;
        return state;
    }

    // ---- Per-frame sampling ----

    bool g_Trace = false;
    u32 g_Frame = 0;
    u64 g_FrameMs = 0;
    u32 g_FrameNo = 0;         // frames counted here (the framework counter can stall)
    u64 g_FramePhys = 0, g_FrameInj = 0;
    u64 g_LastPhys = 0, g_LastInj = 0;

    void Trace(const std::string& what) {
        if (g_Trace) Broadcast("T " + std::to_string(NowMs()) + " " + std::to_string(g_FrameNo) + " " + what);
    }

    struct Field { const char* key; std::string value; };
    Field g_Fields[] = {{"screen", ""}, {"level", ""}, {"event", ""}, {"paused", ""}, {"free", ""},
                        {"lock", ""}, {"pad", ""}, {"show", ""}, {"text", ""}, {"choice", ""}, {"client", ""},
                        {"server", ""}, {"file", ""}, {"face", ""}, {"carry", ""}, {"health", ""}};
    void SetField(int i, const std::string& value) {
        if (g_Fields[i].value == value) return;
        g_Fields[i].value = value;
        Trace(std::string(g_Fields[i].key) + "=" + value);
    }

    // The game's controller (0x1c97188 manager, +0x518): input lock at +0x234 and its buttons (getter at the pad's
    // vtable +0x10), as the title reads them for L+R.
    constexpr uintptr_t kControllerManager = 0x1c97188;
    uintptr_t Controller() {
        uintptr_t mgr = 0, holder = 0, ctl = 0;
        if (!Read(ModuleStart() + offset_manager.Offset(kControllerManager), mgr) || !Read(mgr + 0x518, holder) ||
            !Read(holder, ctl)) {
            return 0;
        }
        return Readable(ctl, 0x240) ? ctl : 0;
    }

    bool ControllerButtons(u32& buttons, bool& locked) {
        uintptr_t ctl = Controller();
        uintptr_t pad = 0, vt = 0, fn = 0;
        if (ctl == 0 || !Read(ctl + 0x10, pad) || !Read(pad, vt) || !Read(vt + 0x10, fn) || !InModule(fn)) return false;
        locked = *reinterpret_cast<const u8*>(ctl + 0x234) != 0;
        auto get = reinterpret_cast<const u32* (*)(void*)>(fn);
        const u32* p = get(reinterpret_cast<void*>(pad));
        return Read(reinterpret_cast<uintptr_t>(p), buttons);
    }

    // Memory watches: changed bytes are traced.
    constexpr int kMaxWatches = 4;
    constexpr size_t kMaxWatchLen = 0x800;
    struct Watch { std::string base; size_t off = 0, len = 0; u8 last[kMaxWatchLen]; bool valid = false; };
    Watch g_Watches[kMaxWatches];

    Machine* MachineById(u32 id) {
        for (auto& m : g_Machines) {
            if (m.ready && m.id == id && m.sm.load() != 0) return &m;
        }
        return nullptr;
    }

    uintptr_t WatchBase(const std::string& base) {
        Game::Framework* fmwk = Game::GetFramework();
        if (base == "f") return reinterpret_cast<uintptr_t>(fmwk);
        if (base == "p") return fmwk ? reinterpret_cast<uintptr_t>(fmwk->mPlayer) : 0;
        if (base == "i") return *reinterpret_cast<uintptr_t*>(g_symbols["InventoryPtr"]);
        if (base == "c") return Controller();
        if (base[0] == 'x') return strtoull(base.c_str() + 1, nullptr, 16);
        if (base[0] == 'g') return ModuleStart() + strtoull(base.c_str() + 1, nullptr, 16);
        if (base[0] == 'm' || base[0] == 'o') {
            Machine* m = nullptr;
            if (base.size() > 2 && base[1] == ':') {
                for (auto& c : g_Machines) {
                    if (c.ready && c.sm.load() != 0 && (c.label == base.substr(2) || kSmHooks[c.hook].name == base.substr(2))) m = &c;
                }
            } else {
                m = MachineById(atoi(base.c_str() + 1));
            }
            if (m == nullptr) return 0;
            uintptr_t sm = m->sm.load(), owner = 0;
            if (base[0] == 'm') return sm;
            return Read(sm + 0x40, owner) ? owner : 0;
        }
        return 0;
    }

    void SampleWatches() {
        for (int k = 0; k < kMaxWatches; k++) {
            Watch& w = g_Watches[k];
            if (w.len == 0) continue;
            uintptr_t base = WatchBase(w.base);
            if (base == 0 || !Readable(base + w.off, w.len)) {
                if (w.valid) Trace("watch" + std::to_string(k) + " gone");
                w.valid = false;
                continue;
            }
            const u8* now = reinterpret_cast<const u8*>(base + w.off);
            if (!w.valid) {
                memcpy(w.last, now, w.len);
                w.valid = true;
                std::string s = "watch" + std::to_string(k) + " " + w.base + " at " + Hex(base) + " +" + Hex(w.off) + " ";
                char hex[3];
                for (size_t i = 0; i < w.len; i++) {
                    snprintf(hex, sizeof(hex), "%02x", now[i]);
                    s += hex;
                }
                Trace(s);
                continue;
            }
            std::string s;
            int changes = 0;
            for (size_t i = 0; i < w.len; i++) {
                if (now[i] == w.last[i]) continue;
                if (changes++ < 24) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), " +%lx:%02x>%02x", w.off + i, w.last[i], now[i]);
                    s += buf;
                }
                w.last[i] = now[i];
            }
            if (changes > 0) Trace("watch" + std::to_string(k) + s + (changes > 24 ? " ..." : ""));
        }
    }

    void SampleMachines() {
        for (int i = 0; i < kMaxMachines; i++) {
            Machine& m = g_Machines[i];
            uintptr_t sm = m.sm.load();
            if (sm == 0) continue;
            if (!m.ready) {
                int hook = g_MachineHook[i] - 1;
                if (hook < 0) continue;
                uintptr_t names = sm + 0x58 + kSmHooks[hook].states * 0x30, name0 = 0, owner = 0, vt = 0;
                int first = 0;
                while (first < 3 && Read(names + first * 8, name0) && name0 == 0) first++;
                if (!Read(names + first * 8, name0) || !InRodata(name0)) {
                    g_MachineHook[i] = 0;  // not a state machine of this layout
                    m.sm = 0;
                    continue;
                }
                m.hook = hook;
                m.names = names;
                m.check = names + first * 8;
                m.name0 = name0;
                m.vt = Read(sm + 0x40, owner) && Read(owner, vt) && InModule(vt) ? vt - ModuleStart() : 0;
                m.id = g_NextMachineId++;
                m.state = -2;
                m.label = kSmHooks[hook].name;
                for (const auto& known : kKnownOwners) {
                    if (known.name != nullptr && known.vt == m.vt) m.label = known.name;
                }
                m.ready = true;
                std::string names_text;
                for (int s = 0; s < kSmHooks[hook].states; s++) names_text += " " + StateName(m, s);
                Trace("sm " + std::to_string(m.id) + " " + m.label + " owner_vt=" + Hex(m.vt) + " at " + Hex(sm) +
                      " states:" + names_text);
            }
            int state = MachineState(m);
            if (state < 0) {
                Trace("sm " + std::to_string(m.id) + " gone");
                m.ready = false;
                g_MachineHook[i] = 0;
                m.sm = 0;
                continue;
            }
            if (state != m.state) {
                if (std::strcmp(kSmHooks[m.hook].name, "msg") == 0) {
                    if (StateName(m, state) == "Reading") g_ReadSince = g_FrameNo;
                    else if (StateName(m, m.state) == "Reading") g_ReadFrames = g_FrameNo - g_ReadSince;
                }
                m.state = state;
                Trace("ui " + std::to_string(m.id) + " " + m.label + " " + StateName(m, state));
            }
        }
    }

    // A machine of that label or hook name in that state (by name); "<label>:<state>".
    bool MachineIn(const std::string& label, const std::string& state) {
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || (m.label != label && kSmHooks[m.hook].name != label)) continue;
            if (StateName(m, m.state) == state) return true;
        }
        return false;
    }

    // Any message window that is not closed.
    // A message that holds Link. One in EventWaiting takes no button and leaves him free (the shop shows one
    // beside play after an item-get).
    bool MessageOpen() {
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || std::strcmp(kSmHooks[m.hook].name, "msg") != 0) continue;
            std::string s = StateName(m, m.state);
            if (s != "Closed" && s != "EventWaiting" && s != "-") return true;
        }
        return false;
    }

    std::string MessageState() {
        std::string out;
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || std::strcmp(kSmHooks[m.hook].name, "msg") != 0) continue;
            std::string s = StateName(m, m.state);
            if (s == "-") continue;  // a window of a level that is gone
            if (out.empty() || s != "Closed") out = s;
        }
        return out.empty() ? "none" : out;
    }

    // The open message window's owner (the machine is at owner +0x1260): after KeyWaiting or PreSelecting starts, a
    // s16 frame count at +0x1cf4 runs up to the limit at +0x1cf6 (6 for a page, 10 before a choice); a button
    // counts only once it is there. The choice cursor is the u8 at +0x1cc8 (0 = the first option, Down adds one).
    constexpr size_t kMsgMachine = 0x1260, kMsgWaitCount = 0x1cf4, kMsgWaitLimit = 0x1cf6, kMsgChoice = 0x1cc8;

    Machine* OpenMessage() {
        Machine* found = nullptr;
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || std::strcmp(kSmHooks[m.hook].name, "msg") != 0) continue;
            std::string s = StateName(m, m.state);
            if (s != "Closed" && s != "-") found = &m;
        }
        return found;
    }

    uintptr_t MessageOwner(Machine* m) {
        return m != nullptr ? m->sm.load() - kMsgMachine : 0;
    }

    // "<state> <count>/<limit>" of the open message, "-" without one.
    std::string TextField() {
        Machine* m = OpenMessage();
        s16 count = 0, limit = 0;
        uintptr_t owner = MessageOwner(m);
        if (m == nullptr || !Read(owner + kMsgWaitCount, count) || !Read(owner + kMsgWaitLimit, limit)) return "-";
        return StateName(*m, m->state) + "," + std::to_string(count) + "/" + std::to_string(limit);
    }

    // The open message waits for a button and takes one now.
    bool TextReady() {
        Machine* m = OpenMessage();
        s16 count = 0, limit = 0;
        uintptr_t owner = MessageOwner(m);
        return m != nullptr && StateName(*m, m->state) == "KeyWaiting" && Read(owner + kMsgWaitCount, count) &&
               Read(owner + kMsgWaitLimit, limit) && count >= limit;
    }

    // The file menu's cursor: u8 at the menu's owner (machine at owner +0x80) +0x159b0, the slot (0 = file 1); its
    // DataSelectKeyWait update (0xe61040) moves it.
    constexpr size_t kFilesMachine = 0x80, kFilesCursor = 0x159b0;
    int FileCursor() {
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || std::strcmp(kSmHooks[m.hook].name, "files") != 0) continue;
            u8 cursor = 0;
            if (Read(m.sm.load() - kFilesMachine + kFilesCursor, cursor)) return cursor;
        }
        return -1;
    }

    // Where Link faces: his rotation (a quaternion about the vertical axis) turns the model's forward axis +z.
    // Returns the yaw in degrees (0 = +z, the bottom of the screen; 90 = +x, right), or NAN without a player.
    float PlayerYaw() {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->mPlayer == nullptr || fmwk->mPlayer->playerCollision == nullptr) return NAN;
        const Game::Vec4& q = fmwk->mPlayer->playerCollision->coordsNew.rotate;
        float fx = 2 * (q.x * q.z + q.t * q.y), fz = 1 - 2 * (q.x * q.x + q.y * q.y);
        return std::atan2(fx, fz) * 180.0f / 3.14159265f;
    }

    // The nearest of the four screen directions to where Link faces.
    std::string FaceField() {
        float yaw = PlayerYaw();
        if (std::isnan(yaw)) return "-";
        if (yaw >= -45 && yaw < 45) return "down";
        if (yaw >= 45 && yaw < 135) return "right";
        if (yaw >= -135 && yaw < -45) return "left";
        return "up";
    }

    // A choice takes buttons once its layout's animation has run: the Selecting update (0xef2300) returns while the
    // float at +0x3c of the object at owner + (s32 at +0x1cc4) * 0x18 + 0x58 is not 0.
    constexpr size_t kMsgChoiceLayout = 0x1cc4, kMsgLayouts = 0x58, kLayoutAnim = 0x3c;
    bool ChoiceReady() {
        Machine* m = OpenMessage();
        if (m == nullptr || StateName(*m, m->state) != "Selecting") return false;
        uintptr_t owner = MessageOwner(m), layout = 0;
        s32 index = 0;
        float anim = 1;
        return Read(owner + kMsgChoiceLayout, index) && index >= 0 && index < 16 &&
               Read(owner + kMsgLayouts + index * 0x18, layout) && Read(layout + kLayoutAnim, anim) && anim == 0;
    }

    int ChoiceCursor() {
        Machine* m = OpenMessage();
        u8 cursor = 0;
        if (m == nullptr || StateName(*m, m->state) != "Selecting" || !Read(MessageOwner(m) + kMsgChoice, cursor)) return -1;
        return cursor;
    }

    // Conditions, joined with '+', each negated by a leading '!':
    //   free             the player can move, no message is open and no item display waits
    //   unlocked         the game's controller takes input (lock=0)
    //   textready        the open message waits for a button and takes one now
    //   choiceready      a choice is open and takes buttons now
    //   choice=<n>       a choice is open with the cursor on option n (from 0)
    //   <field>=<value>  a traced field: screen, level, event, paused, free, lock, pad, show, text, choice,
    //                    client (the AP client is connected), server (the client is logged into the slot),
    //                    file (the file menu's cursor, 0 = file 1), face (up, down, left, right: where Link faces),
    //                    carry (Link holds something over his head), health (quarter hearts)
    //   dead             Link is in a level with no health left
    //   <machine>:<state> a UI machine (its label or hook name) in that state
    bool HoldsOne(const std::string& c) {
        if (c.empty()) return true;
        if (c[0] == '!') return !HoldsOne(c.substr(1));
        if (c == "free") return AP::TestPlayerFree() && !AP::TestItemDisplayPending() && !MessageOpen();
        if (c == "unlocked") return g_Fields[5].value == "0";
        if (c == "textready") return TextReady();
        if (c == "choiceready") return ChoiceReady();
        if (c == "dead") return g_Fields[2].value != "-" && g_Fields[15].value == "0";
        size_t eq = c.find('=');
        if (eq != std::string::npos) {
            for (const auto& f : g_Fields) {
                if (c.compare(0, eq, f.key) == 0 && std::strlen(f.key) == eq) return f.value == c.substr(eq + 1);
            }
            return false;
        }
        size_t colon = c.find(':');
        if (colon != std::string::npos) return MachineIn(c.substr(0, colon), c.substr(colon + 1));
        return false;
    }

    bool Holds(const std::string& cond) {
        size_t start = 0;
        while (true) {
            size_t end = cond.find('+', start);
            if (!HoldsOne(cond.substr(start, end == std::string::npos ? std::string::npos : end - start))) return false;
            if (end == std::string::npos) return true;
            start = end + 1;
        }
    }

    // DBG await: replies once a condition holds at the start of a frame.
    struct Await { std::string cond; u64 deadline; int client; };
    std::deque<Await> g_Awaits;

    void StepAwaits() {
        for (auto it = g_Awaits.begin(); it != g_Awaits.end();) {
            if (Holds(it->cond)) {
                Push(it->client, "HELD " + it->cond + " f=" + std::to_string(g_FrameNo));
                Trace("held " + it->cond);
            } else if (NowMs() > it->deadline) {
                Push(it->client, "FAIL await " + it->cond);
            } else {
                ++it;
                continue;
            }
            it = g_Awaits.erase(it);
        }
    }

    // ---- Input ----

    // DBG input: buttons held until a time (the old blind hold).
    u64 g_HoldButtons = 0, g_HoldUntil = 0;
    s32 g_HoldLX = 0, g_HoldLY = 0;

    // DBG press: buttons down for some frames, then up for some, once a condition holds.
    struct Press {
        u64 buttons; s32 lx, ly;
        int down, up;
        std::string cond;
        u64 deadline;
        int client;
        enum { Wait, Down, Up } phase = Wait;
        int left = 0;
        u32 startFrame = 0;
        bool internal = false;  // from text advance: no reply
    };
    std::deque<Press> g_Presses;
    bool g_PressActive = false;   // the current frame carries the front press's buttons

    void StepPresses() {
        g_PressActive = false;
        while (!g_Presses.empty()) {
            Press& p = g_Presses.front();
            if (p.phase == Press::Wait) {
                if (Holds(p.cond)) {
                    p.phase = Press::Down;
                    p.left = p.down;
                    p.startFrame = g_FrameNo;
                    Trace("press " + Hex(p.buttons) + " " + std::to_string(p.down) + "/" + std::to_string(p.up) +
                          (p.cond.empty() ? "" : " when " + p.cond));
                } else if (NowMs() > p.deadline) {
                    if (!p.internal) Push(p.client, "FAIL press " + Hex(p.buttons) + " when " + p.cond);
                    Trace("press " + Hex(p.buttons) + " timeout");
                    g_Presses.pop_front();
                    continue;
                } else {
                    return;
                }
            }
            if (p.phase == Press::Down) {
                if (p.left > 0) {
                    p.left--;
                    g_PressActive = true;
                    return;
                }
                p.phase = Press::Up;
                p.left = p.up;
            }
            if (p.left > 0) {
                p.left--;
                return;
            }
            if (!p.internal) Push(p.client, "DONE press " + Hex(p.buttons) + " f=" + std::to_string(p.startFrame));
            g_Presses.pop_front();
        }
    }

    // Buttons held by an op for the current frame (text advance holding B while text prints).
    u64 g_OpHold = 0;

    // DBG advance: presses once per page when the message takes a button (textready), until no message is open and
    // the player is free; stops at a choice. Fast: holds the button while the text prints.
    constexpr int kRetryFrames = 20;   // a press that changed nothing after this long was not taken: press again
    constexpr int kAdvanceFrames = 2;
    struct Advance {
        bool on = false;
        bool fast = false;
        u64 buttons = 2;
        u64 deadline = 0;
        int client = -1;
        bool pressed = false;    // pressed in the current wait
        u32 pressedAt = 0;
        u32 idleFrom = 0;        // free with no text since this frame, before the first press
        int presses = 0;
        int retries = 0;
    } g_Advance;

    void StepAdvance() {
        Advance& a = g_Advance;
        if (!a.on) return;
        bool busy = !g_Presses.empty();
        bool waiting = MachineIn("msg", "KeyWaiting");
        if (MachineIn("msg", "Selecting") || MachineIn("msg", "PreSelecting")) {
            Push(a.client, "CHOICE advance " + std::to_string(a.presses));
            Trace("advance choice");
            a.on = false;
            return;
        }
        if (a.fast && MachineIn("msg", "Reading") && !AP::TestPlayerFree()) g_OpHold |= a.buttons;
        if (!waiting) a.pressed = false;
        if (waiting && !busy && (a.pressed ? g_FrameNo - a.pressedAt > kRetryFrames : TextReady())) {
            if (a.pressed) {
                Trace("advance retry");
                a.retries++;
            }
            g_Presses.push_back({a.buttons, 0, 0, kAdvanceFrames, kAdvanceFrames, "", ~0ull, a.client});
            g_Presses.back().internal = true;
            a.pressed = true;
            a.pressedAt = g_FrameNo;
            a.presses++;
            return;
        }
        bool free = !busy && Holds("free");
        if (free && a.presses == 0) {
            // Nothing to advance: Link stays free and no text opens (a talk that missed its NPC).
            if (a.idleFrom == 0) a.idleFrom = g_FrameNo;
            if (g_FrameNo - a.idleFrom > kRetryFrames) {
                Push(a.client, "FAIL advance no text");
                a.on = false;
            }
            return;
        }
        a.idleFrom = 0;
        if (free) {
            Push(a.client, "DONE advance " + std::to_string(a.presses) + " retries=" + std::to_string(a.retries));
            Trace("advance done " + std::to_string(a.presses));
            a.on = false;
        } else if (NowMs() > a.deadline) {
            Push(a.client, "FAIL advance " + std::to_string(a.presses) + " msg=" + MessageState());
            a.on = false;
        }
    }

    // DBG choose <n>: at a choice, Down/Up until the cursor is on option n, then A.
    struct Choose {
        bool on = false;
        int target = 0;
        u64 deadline = 0;
        int client = -1;
        int lastCursor = -1;
        u32 movedAt = 0;
        bool confirmed = false;
        int presses = 0;
    } g_Choose;

    void StepChoose() {
        Choose& c = g_Choose;
        if (!c.on || !g_Presses.empty()) return;
        int cursor = ChoiceCursor();
        if (c.confirmed) {
            if (cursor < 0) {
                Push(c.client, "DONE choose " + std::to_string(c.target) + " presses=" + std::to_string(c.presses));
                c.on = false;
                return;
            }
            if (g_FrameNo - c.movedAt <= kRetryFrames) return;
            Trace("choose retry");
            c.confirmed = false;
            c.lastCursor = -1;
        }
        if (cursor >= 0 && ChoiceReady() && (cursor != c.lastCursor || g_FrameNo - c.movedAt > kRetryFrames)) {
            if (cursor == c.lastCursor) Trace("choose retry");
            c.lastCursor = cursor;
            c.movedAt = g_FrameNo;
            u64 button = cursor == c.target ? 1 : cursor < c.target ? 0x8000 : 0x2000;
            c.confirmed = cursor == c.target;
            g_Presses.push_back({button, 0, 0, kAdvanceFrames, kAdvanceFrames, "", ~0ull, c.client});
            g_Presses.back().internal = true;
            c.presses++;
        }
        if (c.on && NowMs() > c.deadline) {
            Push(c.client, "FAIL choose " + std::to_string(c.target) + " cursor=" + std::to_string(cursor));
            c.on = false;
        }
    }

    // DBG tapuntil <buttons> <until> [when <cond>] [timeout ms]: presses (when the condition holds) until the other
    // holds, again only after a press changed nothing for a while; the reply counts the presses.
    struct TapUntil {
        bool on = false;
        u64 buttons = 0;
        std::string until, when;
        u64 deadline = 0;
        int client = -1;
        u32 pressedAt = 0;
        int presses = 0;
    } g_TapUntil;

    void StepTapUntil() {
        TapUntil& t = g_TapUntil;
        if (!t.on) return;
        if (Holds(t.until)) {
            Push(t.client, "DONE tapuntil " + t.until + " presses=" + std::to_string(t.presses));
            Trace("tapuntil done " + std::to_string(t.presses));
            t.on = false;
        } else if (NowMs() > t.deadline) {
            Push(t.client, "FAIL tapuntil " + t.until + " presses=" + std::to_string(t.presses));
            t.on = false;
        } else if (g_Presses.empty() && Holds(t.when) && (t.presses == 0 || g_FrameNo - t.pressedAt > kRetryFrames)) {
            if (t.presses > 0) Trace("tapuntil retry");
            g_Presses.push_back({t.buttons, 0, 0, kAdvanceFrames, kAdvanceFrames, "", ~0ull, t.client});
            g_Presses.back().internal = true;
            t.pressedAt = g_FrameNo;
            t.presses++;
        }
    }

    // DBG interact [buttons hex] [timeout ms]: once Link is free, one press (A by default) that must start something:
    // an event, a message or a lift. FAIL interact nothing when Link is still free and empty-handed after the press.
    struct Interact {
        bool on = false;
        u64 buttons = 1;
        u64 deadline = 0;
        int client = -1;
        bool pressed = false;
        u32 pressedAt = 0;
    } g_Interact;

    void StepInteract() {
        Interact& t = g_Interact;
        if (!t.on) return;
        bool idle = Holds("free+carry=0");
        if (t.pressed && !idle) {
            Push(t.client, "DONE interact f=" + std::to_string(t.pressedAt));
            Trace("interact done");
            t.on = false;
        } else if (t.pressed && g_Presses.empty() && g_FrameNo - t.pressedAt > kRetryFrames) {
            Push(t.client, "FAIL interact nothing");
            Trace("interact nothing");
            t.on = false;
        } else if (NowMs() > t.deadline) {
            Push(t.client, std::string("FAIL interact ") + (t.pressed ? "nothing" : "never free"));
            t.on = false;
        } else if (!t.pressed && g_Presses.empty() && idle) {
            g_Presses.push_back({t.buttons, 0, 0, kAdvanceFrames, kAdvanceFrames, "", ~0ull, t.client});
            g_Presses.back().internal = true;
            t.pressed = true;
            t.pressedAt = g_FrameNo;
        }
    }

    // DBG hold <buttons> <lx> <ly> until <cond> [timeout ms]: holds buttons and the stick until the condition holds.
    struct Hold {
        bool on = false;
        u64 buttons = 0;
        s32 lx = 0, ly = 0;
        std::string until;
        u64 deadline = 0;
        int client = -1;
        u32 since = 0;
    } g_Hold;
    s32 g_OpLX = 0, g_OpLY = 0;

    void StepHold() {
        Hold& h = g_Hold;
        if (!h.on) return;
        if (Holds(h.until)) {
            Push(h.client, "DONE hold " + h.until + " frames=" + std::to_string(g_FrameNo - h.since));
            Trace("hold done " + h.until);
            h.on = false;
        } else if (NowMs() > h.deadline) {
            Push(h.client, "FAIL hold " + h.until);
            h.on = false;
        } else {
            g_OpHold |= h.buttons;
            g_OpLX = h.lx;
            g_OpLY = h.ly;
        }
    }

    void Sample() {
        Game::Framework* fmwk = Game::GetFramework();
        SetField(0, AP::TestScreenName());
        SetField(1, LevelSystem::GetCurrentLevel());
        bool player = fmwk != nullptr && fmwk->mPlayer != nullptr;
        SetField(2, player ? std::to_string(reinterpret_cast<u8*>(fmwk->mPlayer)[0x4ed]) : "-");
        SetField(3, fmwk ? std::to_string(reinterpret_cast<u8*>(fmwk)[0x510]) : "-");
        SetField(4, AP::TestPlayerFree() ? "1" : "0");
        u32 buttons = 0;
        bool locked = false;
        if (ControllerButtons(buttons, locked)) {
            SetField(5, locked ? "1" : "0");
            SetField(6, Hex(buttons));
        } else {
            SetField(5, "-");
            SetField(6, "-");
        }
        SetField(7, AP::TestItemDisplayPending() ? "1" : "0");
        SampleMachines();
        SetField(8, TextField());
        int cursor = ChoiceCursor();
        SetField(9, cursor < 0 ? "-" : std::to_string(cursor));
        SetField(10, AP::IsClientConnected() ? "1" : "0");
        SetField(11, AP::IsServerConnected() ? "1" : "0");
        int file = FileCursor();
        SetField(12, file < 0 ? "-" : std::to_string(file));
        SetField(13, FaceField());
        SetField(14, AP::TestPlayerCarrying() ? "1" : "0");
        auto* items = Game::Data::Inventory::Items != nullptr ? *Game::Data::Inventory::Items : nullptr;
        SetField(15, items != nullptr ? std::to_string(items->Health) : "-");
        SampleWatches();
    }

    // DBG guard <condition>: a state a test must never reach. When one starts to hold, every pending op fails with
    // "alert <condition>" and all debug clients get "ALERT <condition> <fields>". "dead" is always guarded; "hurt"
    // holds once health drops below what it was when the guard was set (or last healed to).
    struct Guard { std::string cond; bool held; int health = -1; };
    std::vector<Guard> g_Guards = {{"dead", false}};

    bool GuardHolds(Guard& g) {
        if (g.cond != "hurt") return Holds(g.cond);
        if (g_Fields[15].value == "-") return false;
        int health = atoi(g_Fields[15].value.c_str());
        if (g.health < 0 || health > g.health) g.health = health;
        return health < g.health;
    }

    void FailOps(const std::string& why) {
        for (const auto& w : g_Awaits) Push(w.client, "FAIL await " + w.cond + " " + why);
        g_Awaits.clear();
        for (const auto& p : g_Presses) {
            if (!p.internal) Push(p.client, "FAIL press " + Hex(p.buttons) + " " + why);
        }
        g_Presses.clear();
        g_HoldUntil = 0;
        if (g_Advance.on) Push(g_Advance.client, "FAIL advance " + why);
        if (g_Choose.on) Push(g_Choose.client, "FAIL choose " + why);
        if (g_TapUntil.on) Push(g_TapUntil.client, "FAIL tapuntil " + why);
        if (g_Hold.on) Push(g_Hold.client, "FAIL hold " + why);
        if (g_Interact.on) Push(g_Interact.client, "FAIL interact " + why);
        g_Advance.on = g_Choose.on = g_TapUntil.on = g_Hold.on = g_Interact.on = false;
    }

    void StepGuards() {
        for (auto& g : g_Guards) {
            bool held = GuardHolds(g);
            if (held && !g.held) {
                std::string fields;
                for (const auto& f : g_Fields) fields += std::string(" ") + f.key + "=" + f.value;
                Broadcast("ALERT " + g.cond + fields);
                Trace("alert " + g.cond);
                FailOps("alert " + g.cond);
            }
            g.held = held;
        }
    }

    // Runs at the first input poll of each frame, before that poll's buttons are set.
    void OnFrame() {
        g_FrameNo++;
        if (g_FramePhys != g_LastPhys || g_FrameInj != g_LastInj) {
            Trace("hid phys=" + Hex(g_FramePhys) + " inj=" + Hex(g_FrameInj));
            g_LastPhys = g_FramePhys;
            g_LastInj = g_FrameInj;
        }
        g_FramePhys = g_FrameInj = 0;
        g_OpHold = 0;
        g_OpLX = g_OpLY = 0;
        Sample();
        StepGuards();
        StepAwaits();
        StepAdvance();
        StepChoose();
        StepTapUntil();
        StepInteract();
        StepHold();
        StepPresses();
    }

    u32 FrameCounter() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->mFrameCount : 0;
    }

    struct Pending { int client; std::string line; };
    std::deque<Pending> g_Retry;

    // Protocol lines the AP client acts on (las/client.py), as opposed to replies to debug commands.
    bool ForClient(const std::string& line) {
        static const char* kVerbs[] = {"CHECK", "CHECKS", "HINT", "GOAL", "DIED", "DEATHDROPPED", "RECV", "LOG"};
        std::string verb = line.substr(0, line.find(' '));
        for (const char* v : kVerbs) {
            if (verb == v) return true;
        }
        return false;
    }
}

namespace AP::Test {
    void NetStart(int port) {
        g_Port = port;
        nn::os::InitializeMutex(&g_Lock, false, 0);
    }

    void NetPoll() {
        if (g_Port == 0) return;
        if (g_Listener < 0) {
            if (NowMs() < g_ListenRetry || !Listen()) {
                g_ListenRetry = NowMs() + 1000;
                return;
            }
        }
        Accept();
        for (int i = 0; i < kMaxClients; i++) {
            if (g_Clients[i].fd >= 0) Serve(i);
        }
    }

    bool Capture(const std::string& line) {
        int to = g_ReplyTo.load();
        if (to >= 0 && !ForClient(line)) {
            Push(to, line);
            return true;
        }
        // A line for the AP client: the client gets it also when a debug command caused it (a give's CHECK), so the
        // server sees what the game did; the debug client whose command it was gets it as its reply.
        Lock();
        for (int i = 0; i < kMaxClients; i++) {
            auto& c = g_Clients[i];
            if (c.fd >= 0 && c.out.size() < 4096) c.out.push_back((i == to ? "" : "AP ") + line + "\n");
        }
        Unlock();
        return false;
    }

    void Tick() {
        std::deque<Pending> lines;
        lines.swap(g_Retry);
        Lock();
        while (!g_In.empty()) {
            lines.push_back({g_In.front().client, std::move(g_In.front().line)});
            g_In.pop_front();
        }
        Unlock();
        for (auto& p : lines) {
            g_ReplyTo = p.client;
            bool done = AP::HandleTestLine(p.line);
            g_ReplyTo = -1;
            if (!done && g_Retry.size() < 64) g_Retry.push_back(std::move(p));
        }
    }

    void ApplyInput(nn::hid::NpadBaseState* state) {
        u32 counter = FrameCounter();
        u64 now = NowMs();
        // A frame starts when the framework's counter moves; while it stalls (before the framework exists, during
        // some loads) every 16 ms count as one.
        if (counter != g_Frame || (counter == 0 && now - g_FrameMs >= 16) || now - g_FrameMs >= 100) {
            g_Frame = counter;
            g_FrameMs = now;
            OnFrame();
        }
        u64& buttons = state->mButtons._storage[0];
        g_FramePhys |= buttons;
        u64 inject = 0;
        s32 lx = 0, ly = 0;
        if (now < g_HoldUntil) {
            inject |= g_HoldButtons;
            lx = g_HoldLX;
            ly = g_HoldLY;
        }
        inject |= g_OpHold;
        if (g_OpLX != 0 || g_OpLY != 0) {
            lx = g_OpLX;
            ly = g_OpLY;
        }
        if (g_PressActive && !g_Presses.empty()) {
            const Press& p = g_Presses.front();
            inject |= p.buttons;
            if (p.lx != 0 || p.ly != 0) {
                lx = p.lx;
                ly = p.ly;
            }
        }
        buttons |= inject;
        if (lx != 0 || ly != 0) {
            state->mAnalogStickL.mX = lx;
            state->mAnalogStickL.mY = ly;
        }
        g_FrameInj |= inject;
    }

    bool Command(const std::vector<std::string>& a) {
        const std::string& cmd = a[1];
        if (cmd == "ghostnet") {
            // DBG ghostnet: the positions sent and the ghosts received (ap_ghostnet.cpp)
            AP::Send("OK " + AP::GhostNet::State());
            return true;
        }
        int client = g_ReplyTo.load();
        if (cmd == "input" && a.size() >= 4) {
            // DBG input <buttons hex> <milliseconds> [lx ly]: held for that long, whatever the game does
            g_HoldButtons = strtoull(a[2].c_str(), nullptr, 16);
            g_HoldUntil = NowMs() + atoi(a[3].c_str());
            g_HoldLX = a.size() >= 6 ? atoi(a[4].c_str()) : 0;
            g_HoldLY = a.size() >= 6 ? atoi(a[5].c_str()) : 0;
            AP::Send("OK input");
        } else if (cmd == "press" && a.size() >= 3) {
            // DBG press <buttons hex> [down frames] [up frames] [lx ly] [when <condition> [timeout ms]]
            Press p{strtoull(a[2].c_str(), nullptr, 16), 0, 0, 2, 2, "", 0, client};
            size_t i = 3;
            if (i < a.size() && a[i] != "when") p.down = atoi(a[i++].c_str());
            if (i < a.size() && a[i] != "when") p.up = atoi(a[i++].c_str());
            if (i + 1 < a.size() && a[i] != "when" && a[i + 1] != "when") {
                p.lx = atoi(a[i++].c_str());
                p.ly = atoi(a[i++].c_str());
            }
            u64 timeout = 10000;
            if (i < a.size() && a[i] == "when" && i + 1 < a.size()) {
                p.cond = a[i + 1];
                if (i + 2 < a.size()) timeout = strtoull(a[i + 2].c_str(), nullptr, 10);
            }
            p.deadline = NowMs() + timeout;
            g_Presses.push_back(p);
            AP::Send("OK press");
        } else if (cmd == "advance") {
            // DBG advance [buttons hex] [timeout ms] | DBG advance off
            if (a.size() >= 3 && a[2] == "off") {
                g_Advance.on = false;
                AP::Send("OK advance off");
                return true;
            }
            g_Advance = Advance{};
            g_Advance.on = true;
            g_Advance.buttons = a.size() >= 3 ? strtoull(a[2].c_str(), nullptr, 16) : 2;
            g_Advance.deadline = NowMs() + (a.size() >= 4 ? strtoull(a[3].c_str(), nullptr, 10) : 60000);
            g_Advance.fast = a.size() >= 5 && a[4] == "fast";
            g_Advance.client = client;
            AP::Send("OK advance");
        } else if (cmd == "hold" && a.size() >= 7 && a[5] == "until") {
            // DBG hold <buttons hex> <lx> <ly> until <condition> [timeout ms]
            g_Hold = Hold{};
            g_Hold.on = true;
            g_Hold.buttons = strtoull(a[2].c_str(), nullptr, 16);
            g_Hold.lx = atoi(a[3].c_str());
            g_Hold.ly = atoi(a[4].c_str());
            g_Hold.until = a[6];
            g_Hold.deadline = NowMs() + (a.size() >= 8 ? strtoull(a[7].c_str(), nullptr, 10) : 10000);
            g_Hold.client = client;
            g_Hold.since = g_FrameNo;
            AP::Send("OK hold");
        } else if (cmd == "choose" && a.size() >= 3) {
            // DBG choose <option from 0> [timeout ms]
            g_Choose = Choose{};
            g_Choose.on = true;
            g_Choose.target = atoi(a[2].c_str());
            g_Choose.deadline = NowMs() + (a.size() >= 4 ? strtoull(a[3].c_str(), nullptr, 10) : 10000);
            g_Choose.client = client;
            AP::Send("OK choose");
        } else if (cmd == "tapuntil" && a.size() >= 4) {
            // DBG tapuntil <buttons hex> <until> [when <condition>] [timeout ms]
            g_TapUntil = TapUntil{};
            g_TapUntil.on = true;
            g_TapUntil.buttons = strtoull(a[2].c_str(), nullptr, 16);
            g_TapUntil.until = a[3];
            size_t i = 4;
            if (i + 1 < a.size() && a[i] == "when") {
                g_TapUntil.when = a[i + 1];
                i += 2;
            }
            g_TapUntil.deadline = NowMs() + (i < a.size() ? strtoull(a[i].c_str(), nullptr, 10) : 10000);
            g_TapUntil.client = client;
            AP::Send("OK tapuntil");
        } else if (cmd == "release") {
            // Drops queued presses and holds.
            g_Presses.clear();
            g_Awaits.clear();
            g_HoldUntil = 0;
            g_Advance.on = false;
            g_Choose.on = false;
            g_TapUntil.on = false;
            g_Hold.on = false;
            g_Interact.on = false;
            AP::Send("OK release");
        } else if (cmd == "interact") {
            // DBG interact [buttons hex] [timeout ms]
            g_Interact = Interact{};
            g_Interact.on = true;
            g_Interact.buttons = a.size() >= 3 ? strtoull(a[2].c_str(), nullptr, 16) : 1;
            g_Interact.deadline = NowMs() + (a.size() >= 4 ? strtoull(a[3].c_str(), nullptr, 10) : 10000);
            g_Interact.client = client;
            AP::Send("OK interact");
        } else if (cmd == "guard" && a.size() >= 3) {
            // DBG guard <condition> | DBG guard off <condition> | DBG guard reset (back to "dead" alone)
            if (a[2] == "reset") {
                g_Guards = {{"dead", false}};
            } else if (a[2] == "off" && a.size() >= 4) {
                std::erase_if(g_Guards, [&](const Guard& g) { return g.cond == a[3]; });
            } else if (std::none_of(g_Guards.begin(), g_Guards.end(), [&](const Guard& g) { return g.cond == a[2]; })) {
                g_Guards.push_back({a[2], false});
            }
            std::string list;
            for (const auto& g : g_Guards) list += " " + g.cond;
            AP::Send("OK guard" + list);
        } else if (cmd == "trace" && a.size() >= 3) {
            g_Trace = a[2] == "on";
            if (g_Trace) {
                for (auto& f : g_Fields) f.value = "?";  // the next frame traces every field
                for (auto& m : g_Machines) m.state = -2;
            }
            AP::Send(std::string("OK trace ") + (g_Trace ? "on" : "off"));
        } else if (cmd == "ui") {
            for (auto& m : g_Machines) {
                if (!m.ready || m.sm.load() == 0) continue;
                std::string names;
                for (int s = 0; s < kSmHooks[m.hook].states; s++) names += " " + StateName(m, s);
                AP::Send("UI " + std::to_string(m.id) + " " + m.label + " owner_vt=" + Hex(m.vt) + " at " +
                         Hex(m.sm.load()) + " state=" + StateName(m, m.state) + " states:" + names);
            }
            AP::Send("OK ui");
        } else if (cmd == "watch" && a.size() >= 3) {
            // DBG watch <base> <hex offset> <hex length> | DBG watch off. Bases: f framework, p player,
            // i inventory, c controller, m<id>/o<id> (or m:<label>/o:<label>) a UI machine/its owner, g<hex> a module
            // offset, x<hex> an address.
            if (a[2] == "off") {
                for (auto& w : g_Watches) w.len = 0;
                AP::Send("OK watch off");
                return true;
            }
            for (int k = 0; k < kMaxWatches; k++) {
                if (g_Watches[k].len != 0 || a.size() < 5) continue;
                g_Watches[k].base = a[2];
                g_Watches[k].off = strtoull(a[3].c_str(), nullptr, 16);
                g_Watches[k].len = std::min<size_t>(strtoull(a[4].c_str(), nullptr, 16), kMaxWatchLen);
                g_Watches[k].valid = false;
                AP::Send("OK watch" + std::to_string(k));
                return true;
            }
            AP::Send("ERR watch");
        } else if (cmd == "await" && a.size() >= 3) {
            // DBG await <condition> [timeout ms]: HELD <condition> f=<frame>, or FAIL await <condition>
            u64 timeout = a.size() >= 4 ? strtoull(a[3].c_str(), nullptr, 10) : 10000;
            g_Awaits.push_back({a[2], NowMs() + timeout, client});
            AP::Send("OK await");
        } else if (cmd == "holds" && a.size() >= 3) {
            AP::Send(std::string("HOLDS ") + (Holds(a[2]) ? "1 " : "0 ") + a[2]);
        } else if (cmd == "frame") {
            AP::Send("FRAME " + std::to_string(g_FrameNo) + " counter=" + std::to_string(g_Frame) + " ms=" +
                     std::to_string(NowMs()));
        } else {
            return false;
        }
        return true;
    }

    std::string StateFields() {
        char yaw[16];
        snprintf(yaw, sizeof(yaw), "%.0f", PlayerYaw());
        return " frame=" + std::to_string(g_FrameNo) + " msg=" + MessageState() + " choice=" + g_Fields[9].value +
               " face=" + g_Fields[13].value + " yaw=" + yaw + " file=" + g_Fields[12].value +
               " read=" + std::to_string(g_ReadFrames);
    }

    void InstallHooks() {
        SmChange<0>::InstallAtOffset(offset_manager.Offset(kSmHooks[0].offset));
        SmChange<1>::InstallAtOffset(offset_manager.Offset(kSmHooks[1].offset));
        SmChange<2>::InstallAtOffset(offset_manager.Offset(kSmHooks[2].offset));
        SmChange<3>::InstallAtOffset(offset_manager.Offset(kSmHooks[3].offset));
        SmChange<4>::InstallAtOffset(offset_manager.Offset(kSmHooks[4].offset));
        SmChange<5>::InstallAtOffset(offset_manager.Offset(kSmHooks[5].offset));
        SmUpdate<6>::InstallAtOffset(offset_manager.Offset(kSmHooks[6].offset));
        SmUpdate<7>::InstallAtOffset(offset_manager.Offset(kSmHooks[7].offset));
        SmChange<8>::InstallAtOffset(offset_manager.Offset(kSmHooks[8].offset));
        SmChange<9>::InstallAtOffset(offset_manager.Offset(kSmHooks[9].offset));
        static_assert(kSmHookCount == 10);
    }
}
#endif
