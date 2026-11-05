#pragma once
// The bridge's game side in parts, shared by its files (ap_game.cpp and the files named below) and by the test build's
// DBG commands (test_commands.cpp).
#include "ap/location_id.hpp"
#include "config/fields.hpp"
#include "game/data/item_id.hpp"
#include "game/stl.hpp"
#include "lib.hpp"
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Game { struct ActorParams; struct Framework; struct LebActorRecord; struct Node; struct Vec3; namespace Event { struct EventRequest; } }

namespace AP::Bridge {
    using Game::Data::ItemId;

    // An item for logs and replies: its id, "-" for none
    inline std::string ItemText(std::optional<ItemId> item) {
        return item ? std::to_string(static_cast<int>(*item)) : "-";
    }

    // ---- The player and the connection (ap_game.cpp) ----

    // The trendy game: Link counts as free while the player steers the crane, which an item-get would cut off, so
    // displays wait until he leaves the shop.
    constexpr const char* kCraneGameLevel = "CraneGame";
    // The goal: Link reaches the Wind Fish and the owl after the Nightmare (room Ending_12O). The Wind Fish's event
    // sets ShadowClear only at its end, when the credits start.
    constexpr const char* kGoalLevel = "Ending";
    constexpr const char* kGoalFlag = "ShadowClear";
    extern std::string g_SeedHash;
    extern std::string g_LastLevel;
    extern u64 g_LevelSince;  // when the player stood in the level (it loaded, or the game over's Retry reloaded it)
    // The intro, the title or the file menus show: no save is loaded.
    bool NoSaveLoaded(Game::Framework* fmwk);
    // The player stands in a level that has settled, and no game over holds everything back.
    bool PlayerReady();
    // True when the player can move: no talk, event, cutscene or pause menu.
    bool PlayerFree();
    // An item-get takes what Link holds over his head out of the game, so item-gets wait until he puts it down.
    bool PlayerCarrying();
    // Health is zero: Link is dying (until the Secret Medicine or a game over's Retry refills it).
    bool LinkDead();
    // Starts an event of a flowchart (its resource path, "event/<name>.bfevfl"), as the game does for its game over,
    // with the room's actors of these LEB keys in it: its request. Empty, and no event, when one of them is missing or
    // the event manager refuses (no level runs).
    Game::Shared<Game::Event::EventRequest> RequestEvent(const char* path, const char* entry,
                                                         std::initializer_list<u64> actorKeys);
    // Starts an event of Common.bfevfl.
    Game::Shared<Game::Event::EventRequest> RequestCommonEvent(const char* entry);
    // A toast in the autosave popup; '|' starts a new line.
    void QueueNotice(std::string text);
    // SERVER 1|0 from the client: whether it is logged into the slot.
    void SetServerConnected(bool connected);
    // DEATH from the client (DeathLink), with its source; false while it must wait.
    bool ReceiveDeath(const std::string& source);
#ifdef LASAP_TEST
    // Which screen the game shows, for test scripts.
    std::string ScreenName(Game::Framework* fmwk);
#endif

    // ---- Received items (ap_items.cpp) ----

    // The received items' count, kept in the save's event flags.
    int GetRecvCount();
    void SetRecvCount(int n);
    // The item of an Items.gsheet key; none for an unknown key.
    std::optional<ItemId> ItemIdFromKey(std::string_view key);
    bool IsTrapKey(const std::string& key);
    // The chamber stones owned (PanelPieceBits), bit i for stone i.
    u64 ChamberStoneBits();
    void Damage(int quarterHearts);
    // Grants one item the way an in-game pickup would, including LASR's progressive items. Returns the item
    // granted, none for traps (QueueTrap) and unknown keys.
    std::optional<ItemId> GrantItem(const std::string& key, int count, int dungeon);
    // Secret Medicines waiting (AP::MEDICINE_FLAG), given one at a time once Link has none.
    int GetMedicineWaiting();
    void TickMedicine();
    // AP locations that keep their vanilla item (the seed file's kept): the game adds the item with this id and index (-1:
    // any) when the location is collected, or, for a companion, sets this flag (no item).
    struct KeptLocation { std::optional<ItemId> item; int index; LocationId location; std::string_view flag; };
    const KeptLocation* FindKept(LocationId location);
    // The server's item for a location that keeps its vanilla item; the item to show, if any.
    std::optional<ItemId> GrantKept(const KeptLocation& kept);
    // The location that keeps this vanilla item, if the game added it at or after `since` (ms).
    std::optional<LocationId> KeptAddedSince(ItemId item, u64 since);
    void LoadKept();
    // Reports a companion once the flag its joining sets is set.
    void TickKeptFlags();

    // ---- Level jumps led elsewhere (ap_jumps.cpp) ----

    void LoadJumps();
    // The overworld map's dungeon markers that follow shuffled doors (the seed file's markers), read at boot, before the game
    // loads the map's sheets, whose rows it swaps then.
    void LoadMarkers();

    // ---- The start state (ap_start.cpp) ----

    void LoadStart();
    // Gives a new save its starting items, and sets its start flags once Link has left Tarin's house.
    void TickStart();

    // ---- Item-gets and popups (ap_itemget.cpp) ----

    // A received item waiting to be shown; with a sender, it shows "You got <item> from <sender>!".
    // With Instant Pickups on Everything, an item Link found (found): it shows as a popup with that toast ("" to name
    // it when it shows), queued at that time.
    struct ItemShow {
        ItemId id; std::string item; std::string sender;
        bool found = false; std::string toast; u64 at = 0;
        std::optional<ItemId> model;  // the item whose model the popup shows, when not this one's
        u32 event = 0;                // a find: the event that gave it (EventSerial)
    };
    void QueueShow(const ItemShow& show);
    // A received trap waits like an item display, then plays its effect. location: the location that held it; none
    // for another slot's.
    void QueueTrap(const std::string& key, const std::string& item, const std::string& sender,
                   std::optional<LocationId> location);
    // An item Link found shows as a popup instead of an item-get; the buffs only start their music.
    void InstantPickup(ItemId item);
    // No item event is requested or running.
    bool ItemEventIdle();
    // An item display waits or runs.
    bool ItemDisplayPending();
    extern u64 g_ItemGetAt;  // when the last item-get started
    // Shows queued items one at a time, once the player is free and holds nothing.
    void TickItemGet();
    void InstallItemGetHooks();
    // ---- Pickup events (ap_pickup_event.cpp) ----
    // Counts the events Link's event start has seen: the running event's number.
    u32 EventSerial();
    // The running event started without Link (a pickup's event he moves on through).
    bool LinkKeptOut();
    void InstallPickupEventHooks();
#ifdef LASAP_TEST
    // STATE's " showq=<queued>[+1] shown=<item>"
    std::string ItemGetState();
    // DBG itemget: the item event of that item with that message entry, without the queue (none must run).
    bool TestItemEvent(ItemId item, const std::string& messageEntry);
#endif

    // ---- The Zol Trap (ap_zol_trap.cpp) ----

    // A placeholder of this location was found: a chest opened for it lets out the location's Zol Trap.
    void NoteChestFind(LocationId location);
    // The Zol Trap's effect: its chest lets out its enemy, or Zols appear beside Link. False when none appeared.
    bool SpawnZolTrap(std::optional<LocationId> location);
#ifdef LASAP_TEST
    // DBG zols: " from=<chest|link|-> spawned=<n> alive=<n>" and each alive one as " <id>@<x>,<y>,<z>/<zone>"
    std::string ZolTrapState();
#endif

    // ---- The drop actor (ap_drop.cpp) ----

    // Adds ItemAPDrop to the actor table; before any room loads.
    void InstallDropActor();
    // A room reads a drop's LEB record: true when the room must leave the record alone, as its roaming ball (Bouncy
    // Items) is live and stays where it is (DESIGN.md, Bouncy Items, Rooms). With none, a chest's drop closes its chest
    // unless the item was taken (Opened when taken). `leb` is the room's LEB.
    bool DropStaysOut(void* leb, const Game::LebActorRecord* record);
#ifdef LASAP_TEST
    // DBG drop: the drop actor that fell last (TESTING.md, Debug commands), " -" when it is gone
    std::string DropState();
    // DBG drop launch <LEB key, hex>: that drop actor becomes the one DBG drop shows, and its switch 0 goes on (it pops)
    bool DropLaunch(u64 key);
    // DBG drop at <x> <y> <z>: the drop DBG drop shows, moved there at rest
    bool DropAt(const Game::Vec3& at);
#endif

    // ---- The models the locations show (ap_models.cpp) ----

    void InstallModelHooks();
    // The model an actor with a location's token among its LEB parameters shows (model file, model name): the location's
    // item's, else the Archipelago model's; none without a token.
    std::optional<std::pair<std::string_view, std::string_view>> TokenModel(const Game::ActorParams& params);
    // The item whose model and pose a held-up item shows: the location's item for the placeholder just found and for
    // LASR's rows that stand in for a location's item, else the item itself.
    ItemId ShownItem(ItemId item);

    // ---- NPC models held up in item-gets (ap_held_model.cpp) ----

    void InstallHeldModelHooks();
    // Each frame: the held model's root bone once it can move.
    void TickHeldModel();
#ifdef LASAP_TEST
    // DBG heldangle: an NPC model's item-get angle until the game restarts; false for a model without a fix.
    bool SetHeldAngle(const std::string& model, const Game::Vec3& angle);
    // DBG heldpose: the NPC model held up now, or null.
    Game::Node* HeldModel();
    // DBG heldpose: the last item-get's model while it exists, any item's, or null.
    Game::Node* ItemGetModel();
#endif

    // ---- Checks and hints (ap_checks.cpp) ----

    // Saves a location collected here and reports it.
    void RecordCheck(LocationId location);
    bool CollectedHere(LocationId location);
    // CHECKS lines with every location collected here.
    void SendAllChecks();
    // CHECKED <local id>...: locations the server has as checked; INLOGIC CLEAR, then INLOGIC <local id>...: the
    // locations in logic for the items received; DOORS CLEAR, then DOORS <local id>...: the dungeon locations whose
    // dungeon's door is reachable for them; HINTED CLEAR, then HINTED <local id>...: the locations holding a server
    // hint not yet found.
    // f holds the ids, after the line's command.
    void NoteServerChecks(Fields& f);
    void NoteInLogic(Fields& f);
    void NoteDoors(Fields& f);
    void NoteHinted(Fields& f);
    // In-game hints the client makes server hints of (the seed file's hints).
    void LoadHints();
    // Sends HINT and DUNGEON for the hints read and the dungeons entered and not yet reported on this connection, or
    // for all.
    void ReportFlags(bool all);
    void TickReportedFlags();
    // A new connection: every hint read and dungeon entered is reported again once the save is loaded.
    void ResetReportedFlags();
#ifdef LASAP_TEST
    // DBG clearchecks
    void ClearChecks();
#endif

    // ---- The protocol's lines from the client (ap_protocol.cpp) ----

    // The placeholders collected since the save loaded: their own items were named by the found text.
    extern std::set<LocationId> g_FoundSinceLoad;
    // Returns false if the line must be retried on a later frame.
    bool HandleLine(const std::string& line);
    // Once per tick: lines that wait for a loaded save, then the new ones.
    void PumpLines();

    // ---- The goal's egg (ap_egg.cpp) ----

    // The egg's instrument checks answer the goal (Inventory.HasAllInstruments).
    void InstallEggHooks();
    // On a seashell hunt: plays the Wind Fish's Egg's opening once Link stands at the egg holding the seashells.
    void TickEgg();

    // ---- The warp menu (ap_warp_menu.cpp) ----

    // Minus held long enough: opens the warp menu when the player can take it.
    void TickWarpMenu();
    // Each input poll: Minus is held back from the game while it is held, and a short press passed on.
    void WarpMenuPad(u64& buttons);
    // The map's check list took the pad: a Minus held is let go.
    void WarpMenuDropMinus();
}
