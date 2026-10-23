// Input for the tests (test build, TESTING.md): frame-exact presses, optionally waiting for a condition, text advance
// that presses only while a message waits for a button, and the other ops; each frame's step.
#ifdef LASAP_TEST
#include "test/test_impl.hpp"
#include "AP/ap_bridge.hpp"
#include "Game/time.hpp"
#include "AP/ap_net.hpp"
#include <algorithm>
#include <cstdlib>
#include <deque>

namespace AP::Test {
namespace {
    using Game::NowMs;

    u32 g_Pass = 0;
    u64 g_FrameMs = 0;
    u64 g_FramePhys = 0, g_FrameInj = 0;
    u64 g_LastPhys = 0, g_LastInj = 0;

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
        if (a.fast && MachineIn("msg", "Reading") && !Bridge::PlayerFree()) g_OpHold |= a.buttons;
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

    // DBG guard <condition>: a state a test must never reach. When one starts to hold, every pending op fails with
    // "alert <condition>" and all debug clients get "ALERT <condition> <fields>". "dead" is always guarded; "hurt"
    // holds once health drops below what it was when the guard was set (or last healed to).
    struct Guard { std::string cond; bool held; int health = -1; };
    std::vector<Guard> g_Guards = {{"dead", false}};

    bool GuardHolds(Guard& g) {
        if (g.cond != "hurt") return Holds(g.cond);
        const std::string& value = FieldValue("health");
        if (value == "-") return false;
        int health = atoi(value.c_str());
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
                Broadcast("ALERT " + g.cond + AllFields());
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
}

    bool InputCommand(const std::vector<std::string>& a) {
        const std::string& cmd = a[1];
        int client = ReplyTo();
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
        } else if (cmd == "await" && a.size() >= 3) {
            // DBG await <condition> [timeout ms]: HELD <condition> f=<frame>, or FAIL await <condition>
            u64 timeout = a.size() >= 4 ? strtoull(a[3].c_str(), nullptr, 10) : 10000;
            g_Awaits.push_back({a[2], NowMs() + timeout, client});
            AP::Send("OK await");
        } else {
            return false;
        }
        return true;
    }

    void ApplyInput(nn::hid::NpadBaseState* state) {
        u32 pass = g_Passes.load();
        u64 now = NowMs();
        // A frame starts with each pass of the main loop; before the loop runs every 16 ms count as one, and while a
        // pass lasts (some loads) every 100 ms.
        if (pass != g_Pass || (pass == 0 && now - g_FrameMs >= 16) || now - g_FrameMs >= 100) {
            g_Pass = pass;
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
}
#endif
