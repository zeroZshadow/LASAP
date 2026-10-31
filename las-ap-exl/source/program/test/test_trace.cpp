// The game state the tests read and its trace (test build, TESTING.md "Game state").
// - The trace (DBG trace on): a "T <ms> <frame> <what>" line on every change of the screen, the level, the
//   player's event state, the pause flag, the game's controller (its buttons and input lock), the buttons each
//   frame's input polls carried (physical and injected), the UI state machines and watched memory.
// - UI state machines (title, file select, message window, choices, name entry) are learned from their ChangeState
//   calls; each keeps its state names in a table, so the trace names their states.
// - Conditions on that state, which the ops (test_input.cpp) wait for.
#ifdef LASAP_TEST
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_net.hpp"
#include "game/addr.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/level.hpp"
#include "game/math.hpp"
#include "game/menu.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "ghost/ghost_net.hpp"
#include "music/music_jukebox.hpp"
#include "test/test_impl.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace AP::Test {
    u32 g_FrameNo = 0;
    std::atomic<u32> g_Passes = 0;

namespace {
    using Game::NowMs;

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
        if (state == Game::kNoState) return "-";
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

    // The framework's main loop (0x1b920) makes a pass a frame: its frame step (0x11cc0 on framework +0x1b0), then
    // the frame counter (+0x4d4) counts up unless the game is paused (+0x510 or +0x511 set: the pause menu and other
    // menus), while the frames are still drawn. Hooked at the start of each pass.
    constexpr uintptr_t kMainLoopPass = 0x1b97c;

    struct MainLoopPass : public exl::hook::impl::InlineHook<MainLoopPass> {
        static void Callback(exl::hook::nx64::InlineCtx*) { g_Passes++; }
    };

    bool g_Trace = false;

    struct Field { const char* key; std::string value; };
    Field g_Fields[] = {{"screen", ""}, {"level", ""}, {"event", ""}, {"paused", ""}, {"free", ""},
                        {"lock", ""}, {"pad", ""}, {"show", ""}, {"text", ""}, {"choice", ""}, {"client", ""},
                        {"server", ""}, {"file", ""}, {"face", ""}, {"carry", ""}, {"health", ""},
                        {"entry", ""}, {"anim", ""}, {"zone", ""}};
    void SetField(int i, const std::string& value) {
        if (g_Fields[i].value == value) return;
        g_Fields[i].value = value;
        Trace(std::string(g_Fields[i].key) + "=" + value);
    }

    // The game's controller (Framework +0x518): input lock at +0x234 and its buttons (getter at the pad's vtable
    // +0x10), as the title reads them for L+R.
    uintptr_t Controller() {
        uintptr_t mgr = 0, holder = 0, ctl = 0;
        if (!Read(reinterpret_cast<uintptr_t>(Game::Framework_ptr.Ptr()), mgr) || !Read(mgr + 0x518, holder) ||
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
        if (base == "p") return fmwk ? reinterpret_cast<uintptr_t>(fmwk->player) : 0;
        if (base == "i") return reinterpret_cast<uintptr_t>(Game::Data::Inventory::Items());
        if (base == "c") return Controller();
        if (base[0] == 'x') return strtoull(base.c_str() + 1, nullptr, 16);
        if (base[0] == 'g') return Game::Base() + strtoull(base.c_str() + 1, nullptr, 16);
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
                m.vt = Read(sm + 0x40, owner) && Read(owner, vt) && InModule(vt) ? vt - Game::Base() : 0;
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

    // The open message window (Game::Ui::MessageWindow), its owner found from its machine and read with Read: a
    // button counts once waitCount is at waitLimit.
    using Game::Ui::MessageWindow;
    constexpr size_t kMsgMachine = offsetof(MessageWindow, machine);
    constexpr size_t kMsgWaitCount = offsetof(MessageWindow, keyWait) + offsetof(Game::Ui::WaitCounter, count);
    constexpr size_t kMsgWaitLimit = offsetof(MessageWindow, keyWait) + offsetof(Game::Ui::WaitCounter, limit);
    constexpr size_t kMsgChoice = offsetof(MessageWindow, choiceCursor);

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

    // The file menu's cursor: u8 at the menu's owner (machine at owner +0x80) +0x159b0, the slot (0 = file 1); its
    // DataSelectKeyWait update (0xe61040) moves it.
    constexpr size_t kFilesMachine = 0x80, kFilesCursor = 0x159b0;
    // The name entry's cursor: u8 at its owner (machine at owner +0x80) +0x590, 0 on the name (A opens the
    // keyboard), 1 on OK; its KeyWait update (0xeaf8a0) moves it.
    constexpr size_t kNameMachine = 0x80, kNameCursor = 0x590;
    // A byte of a hooked machine's owner, -1 while that machine is not there.
    int OwnerByte(const char* hook, size_t machine, size_t offset) {
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || std::strcmp(kSmHooks[m.hook].name, hook) != 0) continue;
            u8 value = 0;
            if (Read(m.sm.load() - machine + offset, value)) return value;
        }
        return -1;
    }
    int FileCursor() { return OwnerByte("files", kFilesMachine, kFilesCursor); }
    int NameCursor() { return OwnerByte("name", kNameMachine, kNameCursor); }

    // Where Link faces: his rotation (a quaternion about the vertical axis) turns the model's forward axis +z.
    // Returns the yaw in degrees (0 = +z, the bottom of the screen; 90 = +x, right), or NAN without a player.
    float PlayerYaw() {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || fmwk->player == nullptr || fmwk->player->playerCollision == nullptr) return NAN;
        return Game::YawDegrees(fmwk->player->playerCollision->base.node.coordsNew.rotate);
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

    // Conditions, joined with '+', each negated by a leading '!':
    //   free             the player can move, no message is open and no item display waits
    //   unlocked         the game's controller takes input (lock=0)
    //   textready        the open message waits for a button and takes one now
    //   choiceready      a choice is open and takes buttons now
    //   choice=<n>       a choice is open with the cursor on option n (from 0)
    //   <field>=<value>  a traced field: screen, level, event, paused, free, lock, pad, show, text, choice,
    //                    client (the AP client is connected), server (the client is logged into the slot),
    //                    file (the file menu's cursor, 0 = file 1), face (up, down, left, right: where Link faces),
    //                    carry (Link holds something over his head), health (quarter hearts), zone (Link's
    //                    zone, -1 none)
    //   dead             Link is in a level with no health left
    //   navi=<state>     the pause menu header's state (1 Open, 2 WaitKey, ...; - with the menu closed)
    //   <machine>:<state> a UI machine (its label or hook name) in that state
    bool HoldsOne(const std::string& c) {
        if (c.empty()) return true;
        if (c[0] == '!') return !HoldsOne(c.substr(1));
        if (c.compare(0, 5, "navi=") == 0) return AP::JukeboxPane::NaviState() == c.substr(5);
        if (c == "free") return Bridge::PlayerFree() && !Bridge::ItemDisplayPending() && !MessageOpen();
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

    std::string LinkAnimName() {
        const char* name = AP::GhostNet::AnimName(AP::GhostNet::LinkAnim().anim);
        return name != nullptr ? name : "-";
    }

    u32 FrameCounter() {
        Game::Framework* fmwk = Game::GetFramework();
        return fmwk != nullptr ? fmwk->frameCount : 0;
    }

    const std::string kNoField;
}

    std::string Hex(u64 v) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lx", v);
        return buf;
    }

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

    void Trace(const std::string& what) {
        if (g_Trace) Broadcast("T " + std::to_string(NowMs()) + " " + std::to_string(g_FrameNo) + " " + what);
    }

    const std::string& FieldValue(const char* key) {
        for (const auto& f : g_Fields) {
            if (std::strcmp(f.key, key) == 0) return f.value;
        }
        return kNoField;
    }

    std::string AllFields() {
        std::string fields;
        for (const auto& f : g_Fields) fields += std::string(" ") + f.key + "=" + f.value;
        return fields;
    }

    void Sample() {
        Game::Framework* fmwk = Game::GetFramework();
        SetField(0, Bridge::ScreenName(fmwk));
        SetField(1, LevelSystem::GetCurrentLevel());
        bool player = fmwk != nullptr && fmwk->player != nullptr;
        SetField(2, player ? std::to_string(fmwk->player->machine.state) : "-");
        SetField(3, fmwk ? std::to_string(fmwk->paused) : "-");
        SetField(4, Bridge::PlayerFree() ? "1" : "0");
        u32 buttons = 0;
        bool locked = false;
        if (ControllerButtons(buttons, locked)) {
            SetField(5, locked ? "1" : "0");
            SetField(6, Hex(buttons));
        } else {
            SetField(5, "-");
            SetField(6, "-");
        }
        SetField(7, Bridge::ItemDisplayPending() ? "1" : "0");
        SampleMachines();
        SetField(8, TextField());
        int cursor = ChoiceCursor();
        SetField(9, cursor < 0 ? "-" : std::to_string(cursor));
        SetField(10, AP::IsClientConnected() ? "1" : "0");
        SetField(11, AP::IsServerConnected() ? "1" : "0");
        int file = FileCursor();
        SetField(12, file < 0 ? "-" : std::to_string(file));
        SetField(13, FaceField());
        SetField(14, Bridge::PlayerCarrying() ? "1" : "0");
        auto* items = Game::Data::Inventory::Items();
        SetField(15, items != nullptr ? std::to_string(items->Health) : "-");
        int entry = NameCursor();
        SetField(16, entry < 0 ? "-" : std::to_string(entry));
        SetField(17, LinkAnimName());
        // Link's zone (actor +0xb0, the camera room he is in; ACTORS.md)
        SetField(18, player ? std::to_string(*reinterpret_cast<s32*>(reinterpret_cast<u8*>(fmwk->player) + 0xb0)) : "-");
        SampleWatches();
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

    // A machine of that label or hook name in that state (by name); "<label>:<state>".
    bool MachineIn(const std::string& label, const std::string& state) {
        for (auto& m : g_Machines) {
            if (!m.ready || m.sm.load() == 0 || (m.label != label && kSmHooks[m.hook].name != label)) continue;
            if (StateName(m, m.state) == state) return true;
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

    // The open message waits for a button and takes one now.
    bool TextReady() {
        Machine* m = OpenMessage();
        s16 count = 0, limit = 0;
        uintptr_t owner = MessageOwner(m);
        return m != nullptr && StateName(*m, m->state) == "KeyWaiting" && Read(owner + kMsgWaitCount, count) &&
               Read(owner + kMsgWaitLimit, limit) && count >= limit;
    }

    // A choice takes buttons once its animation has run: the Selecting update (0xef2300) returns while the animator
    // of inOutChoice[choiceCount - 2] plays (its speed is not 0).
    constexpr size_t kMsgChoiceCount = offsetof(MessageWindow, choiceCount),
                     kMsgInOutChoice = offsetof(MessageWindow, inOutChoice) + offsetof(Game::Ui::Animation, animator),
                     kAnimatorSpeed = offsetof(Game::Ui::Animator, speed);
    bool ChoiceReady() {
        Machine* m = OpenMessage();
        if (m == nullptr || StateName(*m, m->state) != "Selecting") return false;
        uintptr_t owner = MessageOwner(m), animator = 0;
        s32 count = 0;
        float speed = 1;
        return Read(owner + kMsgChoiceCount, count) && count >= 2 && count <= 4 &&
               Read(owner + kMsgInOutChoice + (count - 2) * sizeof(Game::Ui::Animation), animator) &&
               Read(animator + kAnimatorSpeed, speed) && speed == 0;
    }

    int ChoiceCursor() {
        Machine* m = OpenMessage();
        u8 cursor = 0;
        if (m == nullptr || StateName(*m, m->state) != "Selecting" || !Read(MessageOwner(m) + kMsgChoice, cursor)) return -1;
        return cursor;
    }

    std::string TraceStateFields() {
        char yaw[16];
        snprintf(yaw, sizeof(yaw), "%.0f", PlayerYaw());
        return " frame=" + std::to_string(g_FrameNo) +
               " msg=" + MessageState() + " choice=" + g_Fields[9].value +
               " face=" + g_Fields[13].value + " yaw=" + yaw + " anim=" + LinkAnimName() + " file=" + g_Fields[12].value +
               " entry=" + g_Fields[16].value +
               " read=" + std::to_string(g_ReadFrames);
    }

    bool TraceCommand(const std::vector<std::string>& a) {
        const std::string& cmd = a[1];
        if (cmd == "trace" && a.size() >= 3) {
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
        } else if (cmd == "holds" && a.size() >= 3) {
            AP::Send(std::string("HOLDS ") + (Holds(a[2]) ? "1 " : "0 ") + a[2]);
        } else if (cmd == "frame") {
            AP::Send("FRAME " + std::to_string(g_FrameNo) + " counter=" + std::to_string(FrameCounter()) +
                     " passes=" + std::to_string(g_Passes.load()) + " ms=" + std::to_string(NowMs()));
        } else if (cmd == "memq" && a.size() >= 3) {
            // DBG memq <hex address | +module offset>: the memory region there (start, size, type, permissions)
            uintptr_t addr = a[2][0] == '+' ? Game::Base() + strtoull(a[2].c_str() + 1, nullptr, 16)
                                            : strtoull(a[2].c_str(), nullptr, 16);
            MemoryInfo mi{};
            u32 pi = 0;
            if (R_FAILED(svcQueryMemory(&mi, &pi, addr))) {
                AP::Send("ERR memq");
                return true;
            }
            char buf[96];
            snprintf(buf, sizeof(buf), "MEMQ %lx start=%lx size=%lx type=%x perm=%x", addr, mi.addr, mi.size, mi.type, mi.perm);
            AP::Send(buf);
        } else if (cmd == "mem" && a.size() >= 4) {
            // DBG mem <hex address> <hex length>: raw bytes, if readable
            uintptr_t addr = strtoull(a[2].c_str(), nullptr, 16);
            size_t len = std::min<size_t>(strtoull(a[3].c_str(), nullptr, 16), 0x1000);
            if (!Readable(addr, len)) {
                AP::Send("ERR mem: not readable");
                return true;
            }
            std::string line = "MEM " + a[2] + " ";
            char hex[3];
            for (size_t i = 0; i < len; i++) {
                snprintf(hex, sizeof(hex), "%02x", reinterpret_cast<const u8*>(addr)[i]);
                line += hex;
            }
            AP::Send(line);
        } else {
            return false;
        }
        return true;
    }

    void InstallHooks() {
        SmChange<0>::InstallAtPtr(Game::Addr(kSmHooks[0].offset));
        SmChange<1>::InstallAtPtr(Game::Addr(kSmHooks[1].offset));
        SmChange<2>::InstallAtPtr(Game::Addr(kSmHooks[2].offset));
        SmChange<3>::InstallAtPtr(Game::Addr(kSmHooks[3].offset));
        SmChange<4>::InstallAtPtr(Game::Addr(kSmHooks[4].offset));
        SmChange<5>::InstallAtPtr(Game::Addr(kSmHooks[5].offset));
        SmUpdate<6>::InstallAtPtr(Game::Addr(kSmHooks[6].offset));
        SmUpdate<7>::InstallAtPtr(Game::Addr(kSmHooks[7].offset));
        SmChange<8>::InstallAtPtr(Game::Addr(kSmHooks[8].offset));
        SmChange<9>::InstallAtPtr(Game::Addr(kSmHooks[9].offset));
        static_assert(kSmHookCount == 10);
        MainLoopPass::InstallAtPtr(Game::Addr(kMainLoopPass));
        InstallSpawnTrack();
    }
}
#endif
