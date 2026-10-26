#pragma once
// The bridge's game side in parts, shared by its files (ap_game.cpp and the files named below) and by the test build's
// DBG commands (test_commands.cpp).
#include "lib.hpp"
#include <set>
#include <string>
#include <vector>

namespace Game { struct Framework; struct Node; struct Vec3; }

namespace AP::Bridge {
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
    // Starts an event of Common.bfevfl, as the game does for its game over.
    bool RequestCommonEvent(const char* entry);
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
    int ItemIdFromKey(const std::string& key);
    bool IsTrapKey(const std::string& key);
    // The chamber stones owned (PanelPieceBits), bit i for stone i.
    u64 ChamberStoneBits();
    void Damage(int quarterHearts);
    // Grants one item the way an in-game pickup would, including LASR's progressive items. Returns the item id
    // granted, or -1 for traps (QueueTrap) and unknown keys.
    int GrantItem(const std::string& key, int count, int dungeon);
    // Secret Medicines waiting (AP::MEDICINE_FLAG), given one at a time once Link has none.
    int GetMedicineWaiting();
    void TickMedicine();
    // AP locations that keep their vanilla item (VANILLA_PATH): the game adds the item with this id and index (-1:
    // any) when the location is collected, or, for a companion, sets this flag (item -1).
    struct KeptLocation { int item; int index; int local_id; std::string flag; };
    const KeptLocation* FindKept(int local_id);
    // The server's item for a location that keeps its vanilla item; the item to show, -1 for none.
    int GrantKept(const KeptLocation& kept);
    // The location that keeps this vanilla item, if the game added it at or after `since` (ms), else -1.
    int KeptAddedSince(int itemId, u64 since);
    void LoadKept();
    // Reports a companion once the flag its joining sets is set.
    void TickKeptFlags();

    // ---- Item-gets and popups (ap_itemget.cpp) ----

    // A received item waiting to be shown; with a sender, it shows "You got <item> from <sender>!".
    // A trap (trap = its key) plays its effect instead; id is then -1, and location is the location that held it.
    // With Instant Pickups, an item Link found (found): it shows as a popup with that toast ("" to name it when it
    // shows), queued at that time.
    struct Show {
        int id; std::string item; std::string sender; std::string trap;
        bool found = false; std::string toast; u64 at = 0; int location = -1;
    };
    void QueueShow(const Show& show);
    // A received trap waits like an item display, then plays its effect. location: the local id of the location
    // that held it, -1 for another slot's or none.
    void QueueTrap(const std::string& key, const std::string& item, const std::string& sender, int location);
    // An item Link found shows as a popup instead of an item-get; the buffs only start their music.
    void InstantPickup(int itemId);
    // No item event is requested or running.
    bool ItemEventIdle();
    // An item display waits or runs.
    bool ItemDisplayPending();
    extern u64 g_ItemGetAt;  // when the last item-get started
    // Shows queued items one at a time, once the player is free and holds nothing.
    void TickItemGet();
    void InstallItemGetHooks();
    void InstallPickupEventHooks();
#ifdef LASAP_TEST
    // STATE's " showq=<queued>[+1] shown=<item>"
    std::string ItemGetState();
    // DBG itemget: the item event of that item with that message entry, without the queue (none must run).
    bool TestItemEvent(int itemId, const std::string& messageEntry);
#endif

    // ---- The Zol Trap (ap_zol_trap.cpp) ----

    // A placeholder of this location was found: a chest opened for it lets out the location's Zol Trap.
    void NoteChestFind(int local_id);
    // The Zol Trap's effect: its chest lets out its enemy, or Zols appear beside Link. False when none appeared.
    bool SpawnZolTrap(int location);
#ifdef LASAP_TEST
    // DBG zols: " from=<chest|link|-> spawned=<n> alive=<n>" and each alive one as " <id>@<x>,<y>,<z>/<zone>"
    std::string ZolTrapState();
#endif

    // ---- The pose of items held up in item-gets (ap_item_pose.cpp) ----

    void InstallItemPoseHooks();

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
    void RecordCheck(int local_id);
    bool CollectedHere(int local_id);
    // CHECKS lines with every location collected here.
    void SendAllChecks();
    // CHECKED <local id>...: locations the server has as checked; INLOGIC CLEAR, then INLOGIC <local id>...: the
    // locations in logic for the items received; DOORS CLEAR, then DOORS <local id>...: the dungeon locations whose
    // dungeon's door is reachable for them.
    void NoteServerChecks(const std::vector<std::string>& a);
    void NoteInLogic(const std::vector<std::string>& a);
    void NoteDoors(const std::vector<std::string>& a);
    // In-game hints the client makes server hints of (HINTS_PATH).
    void LoadHints();
    // Sends HINT for the hints read and not yet reported on this connection, or for all read.
    void ReportHints(bool all);
    void TickHintFlags();
    // A new connection: every hint read is reported again once the save is loaded.
    void ResetHints();
#ifdef LASAP_TEST
    // DBG clearchecks
    void ClearChecks();
#endif

    // ---- The protocol's lines from the client (ap_protocol.cpp) ----

    // The placeholders collected since the save loaded: their own items were named by the found text.
    extern std::set<int> g_FoundSinceLoad;
    // Returns false if the line must be retried on a later frame.
    bool HandleLine(const std::string& line);
    // Once per tick: lines that wait for a loaded save, then the new ones.
    void PumpLines();

    // ---- The warp menu (ap_warp_menu.cpp) ----

    // Minus held long enough: opens the warp menu when the player can take it.
    void TickWarpMenu();
    // Each input poll: Minus is held back from the game while it is held, and a short press passed on.
    void WarpMenuPad(u64& buttons);
    // The map's check list took the pad: a Minus held is let go.
    void WarpMenuDropMinus();
}
