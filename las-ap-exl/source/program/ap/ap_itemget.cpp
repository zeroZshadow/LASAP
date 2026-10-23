// Item-gets (DESIGN.md, Received items, Instant pickups): received items shown one at a time as the item event, traps
// as their effect, and with Instant Pickups found and received items as popups.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_popup.hpp"
#include "ap/ap_text.hpp"
#include "config/config.hpp"
#include "game/data/inventory.hpp"
#include "game/framework.hpp"
#include "game/hooks/inventory.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "music/music.hpp"
#include <algorithm>
#include <cstring>
#include <deque>

namespace AP::Bridge {
    u64 g_ItemGetAt = 0;

namespace {
    using Game::NowMs;
    using Game::SharedPtr;
    using Game::StrView;

    constexpr u64 kItemGetTimeoutMs = 120000;
    constexpr u64 kItemEventStartMs = 3000;      // a requested item event that has not started by then is retried
    constexpr size_t kShowQueueMax = 16;
    constexpr size_t kShowFillerMax = 3;          // with this many waiting, filler is no longer shown
    constexpr u64 kFreeSettleMs = 600;            // the player must be free this long before an item-get starts
    constexpr u64 kNextItemGetMs = 200;           // or this long when the last item event just ended
    constexpr u64 kPopupSettleMs = 200;           // and before a popup starts
    // A find waits this long before it shows: the event that gave it adds the item after the item-get (LASR's order,
    // the game's own), and a location that keeps its vanilla item is named once the item is added.
    constexpr u64 kFoundSettleMs = 50;
    constexpr int kItemEventTries = 3;

    int g_ItemEventTry = 0;
    u64 g_FreeSince = 0;
    u64 g_ItemEventEnded = 0;   // when the last item event ended, so a queued item-get follows it sooner
    bool g_WasFree = false;
    std::deque<Show> g_ShowQueue;
    Show g_Showing{};  // what the current event shows; DBG itemget sets id -1 and no trap, which is not retried
    // A received item plays as the Common.bfevfl event AP::ITEM_EVENT_ENTRY, a trap as its own event: requested,
    // then running until the player is free again.
    enum class ItemEvent { None, Requested, Running };
    ItemEvent g_ItemEvent = ItemEvent::None;
    u64 g_ItemEventSince = 0;
    int g_ItemEventId = -1;            // what the event's item-get shows instead of the placeholder
    // The item event's item-get is still to be swapped (an item, not a trap). Kept apart from the event state, which
    // turns Running as soon as the event takes the player, before its item-get starts.
    bool g_ItemEventSwap = false;
    int g_ItemEventShown = -1;         // the item the last item event's item-get showed; -1 until the swap (STATE)
    std::string g_ItemEventMessage;    // and its message entry

    // Plays an item-get of itemId with that message entry ("" for the item's own text) as the item event.
    bool StartItemEvent(int itemId, const std::string& messageEntry) {
        g_ItemEventId = itemId;
        g_ItemEventMessage = messageEntry;
        g_ItemEventShown = -1;
        Logging.Log("[AP] item event %d request", itemId);
        if (!RequestCommonEvent(AP::ITEM_EVENT_ENTRY)) return false;
        g_ItemEventSwap = true;
        g_ItemEvent = ItemEvent::Requested;
        g_ItemEventSince = NowMs();
        return true;
    }

    // Quarter hearts each trap event takes in the normal damage mode (LASR's trap flows).
    int TrapEventDamage(const std::string& key) {
        if (key == "ZapTrap") return 6;
        if (key == "SquishTrap") return 4;
        if (key == "DrownTrap" || key == "DeathballTrap" || key == "QuakeTrap") return 2;
        return -1;  // no event: the Zol Trap, which spawns enemies from a chest
    }

    // Plays a trap's effect as its event. In the normal damage mode a trap leaves at least a quarter heart.
    bool StartTrapEvent(const std::string& key) {
        int damage = TrapEventDamage(key);
        auto* items = Game::Data::Inventory::Items();
        if (damage < 0) {
            Damage(4);
            return true;
        }
        if (global_config.damage.mode == DamageMode::Normal && items->Health <= damage) items->Health = damage + 1;
        std::string entry = std::string(AP::TRAP_EVENT_PREFIX) + key;
        g_ItemEventId = -1;
        g_ItemEventSwap = false;
        Logging.Log("[AP] trap event %s request", key.c_str());
        if (!RequestCommonEvent(entry.c_str())) return false;
        g_ItemEvent = ItemEvent::Requested;
        g_ItemEventSince = NowMs();
        return true;
    }

    // Items whose own text tells where they came from in the vanilla game (the Boomerang's trade, the Mad Batter's
    // capacity upgrades); they show the AP text instead.
    bool HasSourceText(int itemId) {
        return itemId == 7 || itemId == 123 || itemId == 124 || itemId == 125;
    }

    bool IsFiller(int itemId) {
        // Rupees, ammo, hearts and other drops.
        return itemId == 4 || itemId == 11 || itemId == 60 || itemId >= 100;
    }

    // Names of the game's items that Link can find outside AP locations (Instant Pickups' toast), as the world names
    // them; any other shows its Items.gsheet key.
    struct ItemName { int id; const char* name; };
    constexpr ItemName kItemNames[] = {
        {4, "Bombs"}, {11, "Magic Powder"}, {17, "Green Tunic"}, {18, "Red Tunic"}, {19, "Blue Tunic"},
        {21, "Secret Medicine"}, {22, "Secret Seashell"}, {58, "Piece of Heart"}, {59, "Heart Container"},
        {60, "Arrows"}, {61, "Apple"}, {62, "Green Apple"}, {100, "Heart"}, {101, "1 Rupee"}, {102, "5 Rupees"},
        {104, "20 Rupees"}, {105, "50 Rupees"}, {106, "100 Rupees"}, {108, "300 Rupees"}, {110, "Fairy"},
        {113, "Bottle"}, {114, "Chamber Stone"}, {120, "Ballad of the Wind Fish"}, {121, "Manbo's Mambo"},
        {122, "Frog's Song of Soul"}, {126, "Fairy"},
    };

    std::string ItemDisplayName(int itemId) {
        for (const auto& n : kItemNames) {
            if (n.id == itemId) return n.name;
        }
        auto it = Inventory::kItemNames.find(itemId);
        return it != Inventory::kItemNames.end() ? it->second : std::to_string(itemId);
    }

    // Instant Pickups: a find shows before the received items that wait, after the finds before it.
    void QueueFound(const Show& show) {
        auto it = std::find_if(g_ShowQueue.begin(), g_ShowQueue.end(), [](const Show& s) { return !s.found; });
        g_ShowQueue.insert(it, show);
    }

    // True while a received item's event is requested or running.
    bool ItemEventActive(bool free) {
        u64 since = NowMs() - g_ItemEventSince;
        switch (g_ItemEvent) {
            case ItemEvent::None:
                return false;
            case ItemEvent::Requested:
                // It runs once it takes the player (a trap) or its item-get starts (the hook).
                if (!free) {
                    g_ItemEvent = ItemEvent::Running;
                    return true;
                }
                if (since < kItemEventStartMs) return true;
                Logging.Log("[AP] item event %d did not start", g_ItemEventId);
                if ((g_Showing.id >= 0 || !g_Showing.trap.empty()) && ++g_ItemEventTry < kItemEventTries) {
                    g_ShowQueue.push_front(g_Showing);
                }
                else g_ItemEventTry = 0;
                break;
            case ItemEvent::Running:
                if (!free && since < kItemGetTimeoutMs) return true;
                Logging.Log("[AP] item event %d ended after %lu ms", g_ItemEventId, since);
                g_ItemEventTry = 0;
                g_ItemEventEnded = NowMs();
                break;
        }
        g_ItemEvent = ItemEvent::None;
        g_ItemEventSwap = false;
        return false;
    }

    // A find's toast: as queued, else the location that keeps this vanilla item if the find collected it, else the
    // item's name.
    std::string FoundToast(const Show& show) {
        if (!show.toast.empty()) return show.toast;
        int kept = KeptAddedSince(show.id, show.at);
        if (kept >= 0) {
            std::string toast = AP::FoundToast(kept);
            if (!toast.empty()) return toast;
        }
        return "You found " + ItemDisplayName(show.id);
    }

    // Instant Pickups: the item's model above Link's head and the toast, while play goes on. The next one waits
    // until both are done. Traps still play their effect as an event. A find also plays its fanfare.
    void ShowPopup(const Show& show) {
        if (show.found) {
            AP::QueueToast(FoundToast(show));
            AP::Music::Fanfare(show.id);
        } else {
            std::string name = show.item;
            if (name.empty()) {
                auto it = Inventory::kItemNames.find(show.id);
                name = it != Inventory::kItemNames.end() ? it->second : std::to_string(show.id);
            }
            AP::QueueToast(AP::ReceivedToast(name, show.sender));
        }
        if (!AP::Popup::Show(show.id)) AP::Log("popup " + std::to_string(show.id) + " could not be made");
        g_ItemEventShown = show.id;
    }

    // Instant Pickups (DESIGN.md, Instant pickups). The item-get is the body of the event actions
    // Link.GenericItemGetSequence(ByKey) (and of the Chamber Dungeon's panels): it returns a future the event waits
    // for. Delay_Make(seconds, out) makes one; for 0 seconds it is the game's ready future, which Link's own actions
    // return when they are done at once, so the event goes on in its next update. The item-get's other work is for
    // its display (Link's animation and held model, the text, the zone's music faded out for the fanfare), apart from
    // what the module does in its place: the fanfare, the buffs' music and the autosave. It also takes what Link
    // holds over his head out of the game; without it he keeps holding it (the shop's flows take their goods from him
    // before their item-get, RemoveGoods). The inventory and the item's getting flag are the event's AddItemByKey's.
    constexpr int kGuardianAcorn = 111, kPieceOfPower = 112;
    // The autosave: when the Items.gsheet row's autoSave holds (enabled, its condition true now: ItemGet_RowAutoSave),
    // the item-get asks the scene's save manager (scene +0x90) for one (SaveManager_RequestAutoSave(manager, kind 3,
    // false)), which saves once no event runs. SceneHolder_Lock(out, the framework's game state) gives the scene.
    constexpr size_t kSceneSaveManager = 0x90;
    constexpr u8 kAutoSaveKind = 3;

    void RequestItemAutoSave(int itemId) {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || !Game::ItemGet_RowAutoSave(static_cast<u32>(itemId))) return;
        void* holder = fmwk->gameState;
        if (holder == nullptr) return;
        SharedPtr scene{};
        Game::SceneHolder_Lock(&scene, holder);
        if (scene.ptr != nullptr) {
            Game::SaveManager_RequestAutoSave(static_cast<u8*>(scene.ptr) + kSceneSaveManager, kAutoSaveKind, false);
        }
        Game::Release(scene);
    }

    // Native item-get (Link holds the item up and the item text shows). This is the body of the event action
    // Link.GenericItemGetSequenceByKey; it only displays, the inventory is changed separately.
    // It returns the running action as a libc++ shared_ptr (Player_ItemGetSequence).
    // The item event's item-get shows the placeholder with RECEIVED_LABEL as message entry; the requested item
    // and its message take their place. With Instant Pickups any other item-get is a find, unless its entry has the
    // native mark.
    HOOK_DEFINE_TRAMPOLINE(ItemGetSequence) {
        static void Callback(SharedPtr* out, void* player, int itemId, bool keepCarry, StrView* messageEntry) {
            g_ItemGetAt = NowMs();
            size_t len = strlen(AP::RECEIVED_LABEL);
            bool received = messageEntry != nullptr && messageEntry->size == len &&
                            memcmp(messageEntry->data, AP::RECEIVED_LABEL, len) == 0;
            StrView entry = messageEntry != nullptr ? *messageEntry : StrView{"", 0};
            bool native = entry.size > 0 && entry.data[0] == AP::NATIVE_ITEM_GET_MARK;
            if (native) {
                entry.data++;
                entry.size--;
            }
            if (global_config.tweaks.instant_pickups && !received && !native) {
                InstantPickup(itemId);
                Game::Delay_Make(0.0f, out);
                return;
            }
            if (native) messageEntry = &entry;
            if (g_ItemEventSwap && g_ItemEvent != ItemEvent::None && itemId == AP::PLACEHOLDER_ITEM_ID && received) {
                StrView message{g_ItemEventMessage.c_str(), g_ItemEventMessage.size()};
                g_ItemEventSwap = false;
                g_ItemEventShown = g_ItemEventId;
                if (g_ItemEvent == ItemEvent::Requested) g_ItemEventSince = NowMs();
                g_ItemEvent = ItemEvent::Running;
                Logging.Log("[AP] item event %d start", g_ItemEventId);
                Orig(out, player, g_ItemEventId, keepCarry, &message);
                return;
            }
            Orig(out, player, itemId, keepCarry, messageEntry);
        }
    };

    // A shorter item-get, for every item-get (TESTING.md, Item-get timing). The sequence (the item-get) runs as
    // steps: Link raises the item (animation item_get_st), a delay of 1 s, the item's text; then a short delay and
    // Link lowers the item (item_get_ed). Alongside the text runs the item's fanfare (its Items.gsheet sound, a BGM
    // or an SE), and the text's step ends only once the text has closed and the fanfare has stopped, which held Link
    // up to 2.5 s after a text closed with B. The fanfare still plays on; the item-get no longer waits for it, and
    // the text opens 0.25 s after the raise.
    constexpr uintptr_t kItemGetTextDelay = 0xd741f8;   // in the text's step: ldr s0, [x20, #0xa0], the delay
    constexpr u32 kTextDelayQuarter = 0x1e2a1000;       // fmov s0, #0.25

    HOOK_DEFINE_REPLACE(ItemGetFanfareDone) {
        static bool Callback(void*) { return true; }
    };
}

    void QueueShow(const Show& show) {
        if (g_ShowQueue.size() >= kShowQueueMax) return;
        if (g_ShowQueue.size() >= kShowFillerMax && IsFiller(show.id)) return;
        g_ShowQueue.push_back(show);
    }

    // A received trap waits like an item display, then plays its effect (StartTrapEvent).
    void QueueTrap(const std::string& key, const std::string& item, const std::string& sender) {
        QueueShow(Show{-1, item.empty() ? key : item, sender, key});
    }

    // An item Link found shows as a popup instead of an item-get; the buffs only start their music.
    void InstantPickup(int itemId) {
        Logging.Log("[AP] instant pickup %d", itemId);
        RequestItemAutoSave(itemId);
        if (itemId == kGuardianAcorn || itemId == kPieceOfPower) {
            AP::Music::BuffMusic();
            return;
        }
        Show show{itemId, "", "", ""};
        show.found = true;
        // the placeholder's location is known now (its flows add it first); a vanilla item is added after
        if (AP::IsPlaceholderItem(itemId)) {
            show.toast = AP::FoundToast();
            if (show.toast.empty()) show.toast = "You found an\nArchipelago item";
        }
        show.at = NowMs();
        QueueFound(show);
    }

    bool ItemEventIdle() { return g_ItemEvent == ItemEvent::None; }

    bool ItemDisplayPending() { return !g_ShowQueue.empty() || g_ItemEvent != ItemEvent::None; }

    // Shows queued items one at a time, each as an event, once the player is free and holds nothing; with Instant
    // Pickups, as popups. Finds come first and show as soon as the popup is free, also during the event that gave
    // them.
    void TickItemGet() {
        bool free = PlayerFree();
        if (free && !g_WasFree) g_FreeSince = NowMs();
        if (free) AP::ExpireFoundLocation();
        g_WasFree = free;
        if (ItemEventActive(free)) {
            g_WasFree = false;  // the settle time starts again after each item-get
            return;
        }
        if (!g_ShowQueue.empty() && g_ShowQueue.front().found) {
            if (AP::Popup::Busy() || !AP::ToastsIdle() || !PlayerReady() ||
                NowMs() - g_ShowQueue.front().at < kFoundSettleMs) {
                return;
            }
            g_Showing = g_ShowQueue.front();
            g_ShowQueue.pop_front();
            ShowPopup(g_Showing);
            return;
        }
        bool popup = global_config.tweaks.instant_pickups && !g_ShowQueue.empty() && g_ShowQueue.front().trap.empty();
        if (popup && (AP::Popup::Busy() || !AP::ToastsIdle())) return;
        // An item-get right after the last item event's end follows it sooner than after other events
        u64 settle = popup ? kPopupSettleMs : g_FreeSince - g_ItemEventEnded < 100 ? kNextItemGetMs : kFreeSettleMs;
        if (g_ShowQueue.empty() || !free || LinkDead() || NowMs() - g_FreeSince < settle ||
            (!popup && (PlayerCarrying() || g_LastLevel == kCraneGameLevel))) {
            return;
        }
        g_Showing = g_ShowQueue.front();
        g_ShowQueue.pop_front();
        if (popup) {
            ShowPopup(g_Showing);
            return;
        }
        if (!g_Showing.trap.empty()) {
            // The notice names the trap and who sent it; a retry does not repeat it.
            if (g_ItemEventTry == 0) {
                AP::QueueToast(g_Showing.item + (g_Showing.sender.empty() ? "" : "\nfrom " + g_Showing.sender));
            }
            if (!StartTrapEvent(g_Showing.trap)) AP::Log("trap event " + g_Showing.trap + " could not be requested");
            return;
        }
        int id = g_Showing.id;
        std::string message;
        if (!g_Showing.sender.empty() || HasSourceText(id)) {
            AP::SetReceivedText(g_Showing.item, g_Showing.sender);
            message = AP::RECEIVED_LABEL;
        }
        if (!StartItemEvent(id, message)) AP::Log("item event " + std::to_string(id) + " could not be requested");
    }

    void InstallItemGetHooks() {
        ItemGetSequence::InstallAtFuncPtr(Game::Player_ItemGetSequence.Ptr());
        exl::patch::CodePatcher(Game::Offset(kItemGetTextDelay)).WriteInst(exl::armv8::InstBitSet(kTextDelayQuarter));
        ItemGetFanfareDone::InstallAtFuncPtr(Game::ItemGet_FanfareDone.Ptr());
    }

#ifdef LASAP_TEST
    std::string ItemGetState() {
        return " showq=" + std::to_string(g_ShowQueue.size()) + (g_ItemEvent != ItemEvent::None ? "+1" : "") +
               " shown=" + std::to_string(g_ItemEventShown);
    }

    bool TestItemEvent(int itemId, const std::string& messageEntry) {
        g_Showing = Show{-1, "", "", ""};
        return StartItemEvent(itemId, messageEntry);
    }
#endif
}
