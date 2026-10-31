// Item-gets (DESIGN.md, Received items, Instant pickups): received items shown one at a time as the item event, traps
// as their effect, and with Instant Pickups found and received items as popups.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_popup.hpp"
#include "ap/ap_text.hpp"
#include "config/config.hpp"
#include "game/data/inventory.hpp"
#include "game/event.hpp"
#include "game/framework.hpp"
#include "game/hooks/inventory.hpp"
#include "game/sym.hpp"
#include "game/time.hpp"
#include "music/music.hpp"
#include <algorithm>
#include <cstring>
#include <deque>
#include <utility>
#include <variant>

namespace AP::Bridge {
    u64 g_ItemGetAt = 0;

namespace {
    using Game::NowMs;
    using Game::SharedPtr;
    using Game::StrView;

    constexpr u64 kItemGetTimeoutMs = 120000;    // an item event tracked longer than this is left to run on its own
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
    // A received trap waiting to play its effect (key) as an event, named by item; location: the local id of the
    // location that held it, -1 for another slot's or none
    struct TrapShow { std::string key; std::string item; std::string sender; std::optional<LocationId> location; };
    using Show = std::variant<ItemShow, TrapShow>;
    std::deque<Show> g_ShowQueue;
    // What the current event or popup shows; none for DBG itemget's event, which is not retried
    std::optional<Show> g_Showing;

    // The queue's front when it is an item
    const ItemShow* FrontItem() {
        return g_ShowQueue.empty() ? nullptr : std::get_if<ItemShow>(&g_ShowQueue.front());
    }
    // A received item plays as the Common.bfevfl event AP::ITEM_EVENT_ENTRY, a trap as its own event: requested
    // (queued behind other events), running once its request is at the front of the event manager's queue, until
    // the request finishes (re/EVENTS.md, Requests).
    enum class ItemEvent { None, Requested, Running };
    ItemEvent g_ItemEvent = ItemEvent::None;
    Game::Shared<Game::Event::EventRequest> g_ItemRequest;  // the item event's request
    u64 g_ItemEventSince = 0;
    // What the event's item-get shows instead of the placeholder; none for a trap's event, which has no item-get
    std::optional<ItemId> g_ItemEventId;
    // The item event's item-get is still to be swapped (an item, not a trap). Kept apart from the event state, which
    // turns Running as soon as the event starts, before its item-get does.
    bool g_ItemEventSwap = false;
    std::optional<ItemId> g_ItemEventShown;  // the item the last item event's item-get showed, from the swap (STATE)
    std::string g_ItemEventMessage;    // and its message entry

    // Plays an item-get of the item with that message entry ("" for the item's own text) as the item event.
    bool StartItemEvent(ItemId item, const std::string& messageEntry) {
        g_ItemEventId = item;
        g_ItemEventMessage = messageEntry;
        g_ItemEventShown.reset();
        Logging.Log("[AP] item event %d request", std::to_underlying(item));
        g_ItemRequest = RequestCommonEvent(AP::ITEM_EVENT_ENTRY);
        if (!g_ItemRequest) return false;
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
        return -1;  // no event: the Zol Trap (ap_zol_trap.cpp)
    }

    // Plays a trap's effect as its event. In the normal damage mode a trap leaves at least a quarter heart. The Zol
    // Trap lets out Zols instead, and costs one heart only when none could appear.
    bool StartTrapEvent(const std::string& key, std::optional<LocationId> location) {
        int damage = TrapEventDamage(key);
        auto* items = Game::Data::Inventory::Items();
        if (damage < 0) {
            if (!SpawnZolTrap(location)) Damage(4);
            return true;
        }
        if (global_config.damage.mode == DamageMode::Normal && items->Health <= damage) items->Health = damage + 1;
        std::string entry = std::string(AP::TRAP_EVENT_PREFIX) + key;
        g_ItemEventId.reset();
        g_ItemEventSwap = false;
        Logging.Log("[AP] trap event %s request", key.c_str());
        g_ItemRequest = RequestCommonEvent(entry.c_str());
        if (!g_ItemRequest) return false;
        g_ItemEvent = ItemEvent::Requested;
        g_ItemEventSince = NowMs();
        return true;
    }

    // Items whose own text tells where they came from in the vanilla game (the Boomerang's trade, the Mad Batter's
    // capacity upgrades); they show the AP text instead.
    bool HasSourceText(ItemId item) {
        return item == ItemId::Boomerang || item == ItemId::MagicPowderMaxUp || item == ItemId::BombMaxUp ||
               item == ItemId::ArrowMaxUp;
    }

    bool IsFiller(ItemId item) {
        // Rupees, ammo, hearts and other drops.
        return item == ItemId::Bomb || item == ItemId::MagicPowder || item == ItemId::Arrow || item >= ItemId::Heart;
    }

    // Names of the game's items that Link can find outside AP locations (Instant Pickups' toast), as the world names
    // them; any other shows its Items.gsheet key.
    struct ItemName { ItemId id; const char* name; };
    constexpr ItemName kItemNames[] = {
        {ItemId::Bomb, "Bombs"}, {ItemId::MagicPowder, "Magic Powder"}, {ItemId::ClothesGreen, "Green Tunic"},
        {ItemId::ClothesRed, "Red Tunic"}, {ItemId::ClothesBlue, "Blue Tunic"},
        {ItemId::SecretMedicine, "Secret Medicine"}, {ItemId::Seashell, "Secret Seashell"},
        {ItemId::HeartPiece, "Piece of Heart"}, {ItemId::HeartContainer, "Heart Container"},
        {ItemId::Arrow, "Arrows"}, {ItemId::Apple, "Apple"}, {ItemId::GreenApple, "Green Apple"},
        {ItemId::Heart, "Heart"}, {ItemId::Rupee1, "1 Rupee"}, {ItemId::Rupee5, "5 Rupees"},
        {ItemId::Rupee20, "20 Rupees"}, {ItemId::Rupee50, "50 Rupees"}, {ItemId::Rupee100, "100 Rupees"},
        {ItemId::Rupee300, "300 Rupees"}, {ItemId::Fairy, "Fairy"}, {ItemId::Bottle, "Bottle"},
        {ItemId::PanelDungeonPiece, "Chamber Stone"}, {ItemId::SongWindFish, "Ballad of the Wind Fish"},
        {ItemId::SongMambo, "Manbo's Mambo"}, {ItemId::SongSoul, "Frog's Song of Soul"},
        {ItemId::BottleFairy, "Fairy"},
    };

    std::string ItemDisplayName(ItemId item) {
        for (const auto& n : kItemNames) {
            if (n.id == item) return n.name;
        }
        auto it = Inventory::kItemNames.find(item);
        return it != Inventory::kItemNames.end() ? it->second : ItemText(item);
    }

    // Instant Pickups: a find shows before the received items that wait, after the finds before it.
    void QueueFound(const ItemShow& show) {
        auto it = std::find_if(g_ShowQueue.begin(), g_ShowQueue.end(), [](const Show& s) {
            const auto* item = std::get_if<ItemShow>(&s);
            return item == nullptr || !item->found;
        });
        g_ShowQueue.insert(it, show);
    }

    // True while a received item's event is requested or running. A request waits in the queue until its event runs
    // or the game cancels it (a level jump cancels the queue), and is then retried; a running event times out.
    bool ItemEventActive() {
        using Game::Event::EventRequest;
        if (g_ItemEvent == ItemEvent::None) return false;
        const EventRequest* request = g_ItemRequest.get();
        u64 since = NowMs() - g_ItemEventSince;
        bool finished = request == nullptr || request->Finished();
        if (g_ItemEvent == ItemEvent::Requested && !finished &&
            Game::Event::FrontRequest(static_cast<const Game::Event::EventManager*>(*Game::EventManager_ptr)) ==
                request) {
            g_ItemEvent = ItemEvent::Running;
            g_ItemEventSince = NowMs();
            since = 0;
        }
        if (!finished && (g_ItemEvent == ItemEvent::Requested || since < kItemGetTimeoutMs)) return true;
        if (g_ItemEvent == ItemEvent::Requested && (request == nullptr || request->cancelled)) {
            Logging.Log("[AP] item event %s did not start", ItemText(g_ItemEventId).c_str());
            if (g_Showing && ++g_ItemEventTry < kItemEventTries) g_ShowQueue.push_front(*g_Showing);
            else g_ItemEventTry = 0;
        } else {
            Logging.Log("[AP] item event %s ended after %lu ms", ItemText(g_ItemEventId).c_str(), since);
            g_ItemEventTry = 0;
            g_ItemEventEnded = NowMs();
        }
        g_ItemRequest = {};
        g_ItemEvent = ItemEvent::None;
        g_ItemEventSwap = false;
        return false;
    }

    // A find's toast: as queued, else the location that keeps this vanilla item if the find collected it, else the
    // item's name.
    std::string FoundToast(const ItemShow& show) {
        if (!show.toast.empty()) return show.toast;
        std::optional<LocationId> kept = KeptAddedSince(show.id, show.at);
        if (kept) {
            std::string toast = AP::FoundToast(*kept);
            if (!toast.empty()) return toast;
        }
        return "You found " + ItemDisplayName(show.id);
    }

    // Instant Pickups: the item's model above Link's head and the toast, while play goes on. The next one waits
    // until both are done. Traps still play their effect as an event. A find also plays its fanfare, a received
    // instrument its song (its Items.gsheet jingle).
    void ShowPopup(const ItemShow& show) {
        ItemId item = show.id;
        if (show.found) {
            AP::QueueToast(FoundToast(show));
            AP::Music::Fanfare(item);
        } else {
            if (item >= ItemId::FullMoonCello && item <= ItemId::ThunderDrum) AP::Music::Fanfare(item);
            std::string name = show.item;
            if (name.empty()) {
                auto it = Inventory::kItemNames.find(item);
                name = it != Inventory::kItemNames.end() ? it->second : ItemText(item);
            }
            AP::QueueToast(AP::ReceivedToast(name, show.sender));
        }
        if (!AP::Popup::Show(item)) AP::Log("popup " + ItemText(item) + " could not be made");
        g_ItemEventShown = item;
    }

    // Instant Pickups (DESIGN.md, Instant pickups). The item-get is the body of the event actions
    // Link.GenericItemGetSequence(ByKey) (and of the Chamber Dungeon's panels): it returns a future the event waits
    // for. Delay_Make(seconds, out) makes one; for 0 seconds it is the game's ready future, which Link's own actions
    // return when they are done at once, so the event goes on in its next update. The item-get's other work is for
    // its display (Link's animation and held model, the text, the zone's music faded out for the fanfare), apart from
    // what the module does in its place: the fanfare, the buffs' music and the autosave. It also takes what Link
    // holds over his head out of the game; without it he keeps holding it (the shop's flows take their goods from him
    // before their item-get, RemoveGoods). The inventory and the item's getting flag are the event's AddItemByKey's.
    // The autosave: when the Items.gsheet row's autoSave holds (enabled, its condition true now: ItemGet_RowAutoSave),
    // the item-get asks the scene's save manager (LevelScene::saveManager) for one
    // (SaveManager_RequestAutoSave(manager, kind 3, false)), which saves once no event runs. SceneHolder_Lock(out, the
    // framework's game state) gives the scene.
    constexpr u8 kAutoSaveKind = 3;

    void RequestItemAutoSave(ItemId item) {
        Game::Framework* fmwk = Game::GetFramework();
        if (fmwk == nullptr || !Game::ItemGet_RowAutoSave(item)) return;
        void* holder = fmwk->gameState;
        if (holder == nullptr) return;
        Game::Shared<Game::LevelScene> scene;
        Game::SceneHolder_Lock(scene.Reset(), holder);
        if (scene) Game::SaveManager_RequestAutoSave(scene->saveManager, kAutoSaveKind, false);
    }

    // Native item-get (Link holds the item up and the item text shows). This is the body of the event action
    // Link.GenericItemGetSequenceByKey; it only displays, the inventory is changed separately.
    // It returns the running action as a libc++ shared_ptr (Player_ItemGetSequence).
    // The item event's item-get shows the placeholder with RECEIVED_LABEL as message entry; the requested item
    // and its message take their place. With Instant Pickups any other item-get is a find, unless its entry has the
    // native mark.
    HOOK_DEFINE_TRAMPOLINE(ItemGetSequence) {
        static void Callback(SharedPtr<>* out, void* player, ItemId item, bool keepCarry, StrView* messageEntry) {
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
                InstantPickup(item);
                Game::Delay_Make(0.0f, out);
                return;
            }
            if (native) messageEntry = &entry;
            if (g_ItemEventSwap && g_ItemEventId && g_ItemEvent != ItemEvent::None && item == AP::PLACEHOLDER_ITEM_ID &&
                received) {
                StrView message{g_ItemEventMessage.c_str(), g_ItemEventMessage.size()};
                g_ItemEventSwap = false;
                g_ItemEventShown = g_ItemEventId;
                if (g_ItemEvent == ItemEvent::Requested) g_ItemEventSince = NowMs();
                g_ItemEvent = ItemEvent::Running;
                Logging.Log("[AP] item event %s start", ItemText(g_ItemEventId).c_str());
                Orig(out, player, *g_ItemEventId, keepCarry, &message);
                return;
            }
            Orig(out, player, item, keepCarry, messageEntry);
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

    void QueueShow(const ItemShow& show) {
        if (g_ShowQueue.size() >= kShowQueueMax) return;
        if (g_ShowQueue.size() >= kShowFillerMax && IsFiller(show.id)) return;
        g_ShowQueue.push_back(show);
    }

    // A received trap waits like an item display, then plays its effect (StartTrapEvent).
    void QueueTrap(const std::string& key, const std::string& item, const std::string& sender,
                   std::optional<LocationId> location) {
        if (g_ShowQueue.size() >= kShowQueueMax) return;
        g_ShowQueue.push_back(TrapShow{key, item.empty() ? key : item, sender, location});
    }

    // An item Link found shows as a popup instead of an item-get; the buffs only start their music.
    void InstantPickup(ItemId item) {
        Logging.Log("[AP] instant pickup %d", std::to_underlying(item));
        RequestItemAutoSave(item);
        if (item == ItemId::DefenceUp || item == ItemId::PowerUp) {
            AP::Music::BuffMusic();
            return;
        }
        ItemShow show{item, "", ""};
        show.found = true;
        // the placeholder's location is known now (its flows add it first); a vanilla item is added after
        if (AP::IsPlaceholderItem(item)) {
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
        if (ItemEventActive()) {
            g_WasFree = false;  // the settle time starts again after each item-get
            return;
        }
        if (const ItemShow* found = FrontItem(); found != nullptr && found->found) {
            if (AP::Popup::Busy() || !AP::ToastsIdle() || !PlayerReady() || NowMs() - found->at < kFoundSettleMs) {
                return;
            }
            ItemShow show = *found;
            g_Showing = show;
            g_ShowQueue.pop_front();
            ShowPopup(show);
            return;
        }
        bool popup = global_config.tweaks.instant_pickups && FrontItem() != nullptr;
        if (popup && (AP::Popup::Busy() || !AP::ToastsIdle())) return;
        // An item-get right after the last item event's end follows it sooner than after other events
        u64 settle = popup ? kPopupSettleMs : g_FreeSince - g_ItemEventEnded < 100 ? kNextItemGetMs : kFreeSettleMs;
        if (g_ShowQueue.empty() || !free || LinkDead() || NowMs() - g_FreeSince < settle ||
            (!popup && (PlayerCarrying() || g_LastLevel == kCraneGameLevel))) {
            return;
        }
        g_Showing = g_ShowQueue.front();
        g_ShowQueue.pop_front();
        if (const auto* trap = std::get_if<TrapShow>(&*g_Showing)) {
            // The notice names the trap and who sent it; a retry does not repeat it.
            if (g_ItemEventTry == 0) {
                AP::QueueToast(trap->item + (trap->sender.empty() ? "" : "\nfrom " + trap->sender));
            }
            if (!StartTrapEvent(trap->key, trap->location)) AP::Log("trap event " + trap->key + " could not be requested");
            return;
        }
        const auto& show = *std::get_if<ItemShow>(&*g_Showing);
        if (popup) {
            ShowPopup(show);
            return;
        }
        std::string message;
        if (!show.sender.empty() || HasSourceText(show.id)) {
            AP::SetReceivedText(show.item, show.sender);
            message = AP::RECEIVED_LABEL;
        }
        if (!StartItemEvent(show.id, message)) {
            AP::Log("item event " + ItemText(show.id) + " could not be requested");
        }
    }

    void InstallItemGetHooks() {
        ItemGetSequence::InstallAtFuncPtr(Game::Player_ItemGetSequence.Ptr());
        exl::patch::CodePatcher(Game::Offset(kItemGetTextDelay)).WriteInst(exl::armv8::InstBitSet(kTextDelayQuarter));
        ItemGetFanfareDone::InstallAtFuncPtr(Game::ItemGet_FanfareDone.Ptr());
    }

#ifdef LASAP_TEST
    std::string ItemGetState() {
        return " showq=" + std::to_string(g_ShowQueue.size()) + (g_ItemEvent != ItemEvent::None ? "+1" : "") +
               " shown=" + ItemText(g_ItemEventShown);
    }

    bool TestItemEvent(ItemId item, const std::string& messageEntry) {
        g_Showing.reset();
        return StartItemEvent(item, messageEntry);
    }
#endif
}
