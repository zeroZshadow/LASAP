// Instant Pickups: Link keeps moving through a pickup's event (DESIGN.md, Instant pickups). An event's start puts
// Link in the demoEvent state, which holds him still, when his EventChara is in the event, else stops his updates
// (re/EVENTS.md). A pickup's flow only gives the item: its Link actions are item-gets, which Instant Pickups makes
// instant. When Link moves freely as such an event starts, the hooks leave him out of it: no event state, no
// suspension.
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "config/config.hpp"
#include "game/actors/player.hpp"
#include "game/event.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include <cstring>
#include <vector>

namespace AP::Bridge {
namespace {
    using namespace Game::Event;
    using Game::StrView;

    bool Equals(StrView a, StrView b) { return a.size == b.size && memcmp(a.data, b.data, a.size) == 0; }
    bool Equals(const ResString* s, const char* text) {
        return s != nullptr && Equals(s->View(), StrView{text, strlen(text)});
    }

    // The states Link moves freely in; an event that starts in any other (talk, a chest's opening, fishing) keeps
    // him in it as the game does.
    bool MovesFreely(const Game::Actors::Player* player) {
        using enum Game::Actors::PlayerState;
        if (player->flags.Has(Game::Actors::PlayerFlag::Held)) return false;
        switch (player->State()) {
            case Locomotion: case Jump: case SwordAttack: case Fall: case Carry: case Dash: case Guard:
            case SwordChargeAttack: case SwordPoke: case Swim:
                return true;
            default:
                return false;
        }
    }

    int FindEntry(const ResFlowchart* chart, StrView name) {
        const ResDic* dic = chart->entryNames;
        if (dic == nullptr) return -1;
        for (u32 i = 0; i < dic->count && i < chart->entryCount; i++) {
            const ResString* s = dic->entries[i + 1].name;
            if (s != nullptr && Equals(s->View(), name)) return static_cast<int>(i);
        }
        return -1;
    }

    // An item-get the hook makes instant: its message entry is neither native nor the received item's
    bool InstantItemGet(const ResEvent& e) {
        const auto* params = static_cast<const ResContainer*>(e.p10);
        if (params == nullptr || params->dic == nullptr) return true;
        for (u16 i = 0; i < params->count && i < params->dic->count; i++) {
            if (!Equals(params->dic->entries[i + 1].name, "messageEntry")) continue;
            const ResContainerItem* item = params->items[i];
            if (item == nullptr || item->type != kContainerString || item->string == nullptr) return true;
            StrView entry = item->string->View();
            return !(entry.size > 0 && entry.data[0] == AP::NATIVE_ITEM_GET_MARK) &&
                   !Equals(entry, StrView{AP::RECEIVED_LABEL, strlen(AP::RECEIVED_LABEL)});
        }
        return true;
    }

    // A pickup's flow: every event its entry reaches, also through subflows of its flowchart, has Link do nothing
    // but item-gets, and at least one.
    bool PickupFlow(const RunningEvent* event) {
        if (event == nullptr || event->flow == nullptr) return false;
        const ResFlowchart* chart = event->flow->flowchart;
        if (chart == nullptr || chart->magic != kFlowchartMagic) return false;
        int entry = FindEntry(chart, event->Entry());
        if (entry < 0) return false;
        int link = -1;
        for (u16 i = 0; i < chart->actorCount; i++) {
            const ResActor& actor = chart->actors[i];
            if (Equals(actor.name, "Link") && (actor.subName == nullptr || actor.subName->size == 0)) link = i;
        }
        if (link < 0) return false;
        std::vector<bool> seen(chart->eventCount);
        std::vector<u16> todo{chart->entries[entry].mainEvent};
        int gets = 0;
        auto push = [&](u16 i) { if (i < chart->eventCount && !seen[i]) todo.push_back(i); };
        while (!todo.empty()) {
            u16 i = todo.back();
            todo.pop_back();
            if (i >= chart->eventCount || seen[i]) continue;
            seen[i] = true;
            const ResEvent& e = chart->events[i];
            switch (e.type) {
                case EventType::Action: {
                    if (e.b == link) {
                        const ResActor& actor = chart->actors[link];
                        if (e.c >= actor.actionCount) return false;
                        const ResString* action = actor.actions[e.c];
                        if (!Equals(action, "GenericItemGetSequenceByKey") &&
                            !Equals(action, "GenericItemGetSequence")) {
                            return false;
                        }
                        if (!InstantItemGet(e)) return false;
                        gets++;
                    }
                    push(e.a);
                    break;
                }
                case EventType::Switch: {
                    const auto* cases = static_cast<const ResCase*>(e.p18);
                    for (u16 k = 0; cases != nullptr && k < e.a; k++) push(cases[k].event);
                    break;
                }
                case EventType::Fork: {
                    const auto* forks = static_cast<const u16*>(e.p10);
                    for (u16 k = 0; forks != nullptr && k < e.a; k++) push(forks[k]);
                    push(e.b);
                    break;
                }
                case EventType::Join:
                    push(e.a);
                    break;
                case EventType::SubFlow: {
                    const auto* flowchart = static_cast<const ResString*>(e.p18);
                    const auto* name = static_cast<const ResString*>(e.p20);
                    if (flowchart != nullptr && flowchart->size > 0 &&
                        (chart->name == nullptr || !Equals(flowchart->View(), chart->name->View()))) {
                        return false;
                    }
                    int sub = name != nullptr ? FindEntry(chart, name->View()) : -1;
                    if (sub < 0) return false;
                    push(chart->entries[sub].mainEvent);
                    push(e.a);
                    break;
                }
                default:
                    return false;
            }
        }
        return gets > 0;
    }

    bool g_KeptOut = false;  // the running event started without Link

    HOOK_DEFINE_TRAMPOLINE(PlayerOnEventStart) {
        static void Callback(void* player) {
            g_KeptOut = false;
            const auto* link = static_cast<const Game::Actors::Player*>(player);
            if (global_config.tweaks.instant_pickups && MovesFreely(link)) {
                const RunningEvent* event = Running(static_cast<const EventManager*>(*Game::EventManager_ptr));
                if (PickupFlow(event)) {
                    Logging.Log("[AP] pickup event %.*s: Link moves on", static_cast<int>(event->entry.length),
                                event->entry.data);
                    g_KeptOut = true;
                    return;
                }
            }
            Orig(player);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(PlayerOnEventEnd) {
        static void Callback(void* player) {
            if (g_KeptOut) {
                g_KeptOut = false;
                return;
            }
            Orig(player);
        }
    };
}

    void InstallPickupEventHooks() {
        PlayerOnEventStart::InstallAtFuncPtr(Game::Player_OnEventStart.Ptr());
        PlayerOnEventEnd::InstallAtFuncPtr(Game::Player_OnEventEnd.Ptr());
    }
}
