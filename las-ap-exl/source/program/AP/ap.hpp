#pragma once
// Archipelago bridge: a TCP line protocol (see DESIGN.md) between the game and the AP client.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>
#include <string_view>

namespace AP {
    // Items.gsheet itemID of the placeholder that every randomized location gives.
    // Its AddItemID index is the location's local id.
    constexpr int PLACEHOLDER_ITEM_ID = 71;
    constexpr const char* PLACEHOLDER_KEY = "ArchipelagoItem";
    // Model variants of the placeholder (ArchipelagoItem_<item key>) take item ids that neither the game nor LASR
    // uses (LASR's Dampe and shop dummies take 63-70, its traps 127-131 and its trade items 200-202); the item-get
    // picks its model by id. They count as the placeholder too. Must match VARIANT_ITEM_IDS in the patcher.
    constexpr bool IsPlaceholderItem(int id) {
        return (id >= PLACEHOLDER_ITEM_ID && id <= 99) || id == 103 || id == 107 || id == 109 || id == 118 ||
               id == 119 || (id >= 132 && id <= 199) || (id >= 203 && id <= 255);
    }
    // The game's TCP port; config.ini can set another ([archipelago] port), e.g. for a second modded game on one PC.
    constexpr int PORT = 43384;
    constexpr int PROTOCOL_VERSION = 2;
    // Message labels the patcher adds: the found placeholder's text (Place.msbt) and the received-item text
    // (System.msbt). Their text is replaced at runtime (ap_text.cpp).
    constexpr const char* FOUND_LABEL = "ArchipelagoItem";
    constexpr const char* RECEIVED_LABEL = "APReceived";
    // Common.bfevfl entry the patcher adds: Link's item-get of the placeholder with RECEIVED_LABEL as its message
    // entry. Received items play as this event; the module swaps in the received item and its text.
    constexpr const char* ITEM_EVENT_ENTRY = "APItemGet";
    // Common.bfevfl entries the patcher adds for received traps: AP<trap key> plays LASR's effect of that trap.
    constexpr const char* TRAP_EVENT_PREFIX = "AP";
    // DeathLink event flags (patcher): the module sets DEATHLINK_FLAG with a received death, so the game over flow
    // skips the Secret Medicine and clears it; the flow sets DIED_FLAG for any other death, which the module reports.
    constexpr const char* DEATHLINK_FLAG = "APDeathLink";
    constexpr const char* DIED_FLAG = "APDied";
    // Common.bfevfl entry the patcher adds for the warp menu: a choice between warping to Tarin's house and going on.
    constexpr const char* WARP_MENU_ENTRY = "APWarpMenu";
    // Index the ghost in Tarin's house adds bombs and arrows with (patcher ap_ghost.py): the count is what Link's
    // bombs or arrows are topped up to, not an amount to add.
    constexpr int AMMO_REFILL_INDEX = 1000;
    // Location names the patcher writes next to config.ini.
    constexpr const char* NAMES_PATH = "sd:/config/lasr-exl/ap_names.txt";
    // Per seashell, heart piece, golden leaf and heart container source: its bit and its location (ap_memory.cpp).
    constexpr const char* INDICES_PATH = "sd:/config/lasr-exl/ap_indices.txt";
    // AP locations that keep their vanilla item (the Trendy Game's prizes): "<item key> <index> <local id>" lines.
    constexpr const char* VANILLA_PATH = "sd:/config/lasr-exl/ap_vanilla.txt";
    // In-game hints that become server hints when read: "<n> <flag> <slot> <location id>" lines (patcher ap_hints.py).
    constexpr const char* HINTS_PATH = "sd:/config/lasr-exl/ap_hints.txt";
    // The map tracker's icons, written by the patcher when the option is on (ap_map.cpp).
    constexpr const char* MAP_PATH = "sd:/config/lasr-exl/ap_map.txt";
    // Event flag and Place.msbt label of an overworld icon: the prefix and the icon's group. Must match the patcher.
    constexpr const char* MAP_FLAG_PREFIX = "APMap";
    // After the prefix, the icon's look other than in logic: partly in logic, out of logic, collected.
    constexpr const char* MAP_LOOK_MARKS = "PXC";

    // Inventory bits (ap_memory.cpp): kind 'S' seashells, 'H' heart pieces, 'L' golden leaves, 'C' heart containers
    void LoadIndices();
    // The bits the seed's vanilla sources of a kind set when collected; received items take others.
    u64 VanillaBits(char kind);

    // Network thread (ap_net.cpp)
    void StartNetwork(int port);
    bool IsClientConnected();
    void Send(const std::string& line);
    // Pops one received line into out. Returns false when the queue is empty.
    bool PopLine(std::string& out);
    // Puts a line back at the front so it is retried on a later frame.
    void PushFront(const std::string& line);
    // True once after each new client connection.
    bool TakeNewConnection();

    // Game side (ap_game.cpp)
    void Init(const std::string& seed_hash, int port);
    void Tick();
    void OnPlaceholderCollected(int local_id);
    // The game adds an item (Inventory::AddItemID): reports the check of a location that keeps this vanilla item.
    void OnItemAdded(int item_id, int index);
    void OnGoal();
    // Checked in this game or, as the client reported, on the server.
    bool IsLocationChecked(int local_id);
    // In logic for the items received, as the client reported (INLOGIC); HasLogic once it has.
    bool HasLogic();
    bool IsInLogic(int local_id);
    // The client is connected and logged into the slot (SERVER lines).
    bool IsServerConnected();
    void ApplyInjectedInput(nn::hid::NpadBaseState* state);
    void Log(const std::string& text);
    std::string HelloLine();

    // In-game text (ap_text.cpp)
    void InstallTextHooks();
    // What a location holds: item name and, for another player's item, that player's name.
    void SetLocationName(int local_id, const std::string& item, const std::string& player);
    bool HasLocationName(int local_id);
    std::string TextState();
#ifdef LASAP_TEST
    // DBG text <label>: the text the game last fetched for a message label (tags dropped, line breaks as spaces).
    std::string SeenText(const std::string& label);
#endif
    // The location of a placeholder just added; it names the found text until the player is free again.
    void SetFoundLocation(int local_id);
    void ExpireFoundLocation();
    void SetReceivedText(const std::string& item, const std::string& sender);
    void LoadLocationNames();
    // A short message in the autosave popup ("Now Saving"), which does not stop the game; '\n' starts a new line.
    void QueueToast(const std::string& text);
    // Shows the next toast once the popup is free; only in a level.
    void TickToasts(bool inLevel);

    // Map tracker (ap_map.cpp)
    void LoadMap();
    // Answers the map tracker's event flags; false for any other flag.
    bool MapFlag(std::string_view name, bool& value);
    // The icon group a Place.msbt label names, or -1; the hover text for a group.
    int MapLabelGroup(std::string_view label);
    std::string MapLabelText(int group);
    std::string MapState();
    // The text of the map's X button caption (UI:MapCaptionMemory), or "" for the game's own.
    std::string MapCaptionText();
    // 0 off, 1 on, 2 maps revealed without overworld icons (DBG map, for tests and screenshots)
    void SetMapMode(int mode);
    // Every room of the current dungeon counts as visited; once per tick.
    void TickMap();
}

// Multiplayer ghosts, the network half (ap_ghostnet.cpp, DESIGN.md "Ghosts"): POS lines to the client, GHOST lines
// from it.
namespace AP::GhostNet {
    // Ghosts the client shows at once; GHOST lines name a slot 0..SLOTS-1.
    constexpr int SLOTS = 4;
    // The last GHOST lines of a slot are kept: the puppet replays them on the sender's timeline.
    constexpr int HISTORY = 16;
    struct Frame {
        s64 timeMs;        // the sender's clock, unwrapped
        u64 receivedMs;    // this system's counter when the line arrived
        float x, y, z, yaw;
        int anim, animFrame, flags;
    };
    // Another player in this area, as the last GHOST line for its slot gave it.
    struct Ghost {
        bool active = false;  // false: nobody in this slot (GHOST <n> off, GHOSTS 0, no client)
        std::string name;     // the player's slot name
        std::string level;    // the level the player is in; it can lag behind a level change of this player
        float x = 0, y = 0, z = 0;
        float yaw = 0;          // degrees, 0 = +z (the bottom of the screen), 90 = +x
        int anim = 0;           // Link's animation: its PlayerAnimation.gsheet key + 1 (0: none)
        int animFrame = 0;      // its frame, in tenths
        int flags = 0;          // 1: warped (snap, do not move there), 2: hidden, 4: still since the previous frame,
                                // 8: an animation started since the previous line
        u64 receivedMs = 0;     // when the line arrived (system counter, ms)
        s64 timeMs = 0;         // the sender's clock when it was taken (ms, unwrapped)
        u32 timelineFrom = 0;   // the first line of the sender's clock as unwrapped now (a new one after an off)
        u32 frames = 0;         // GHOST lines for this slot so far
        Frame history[HISTORY]; // the last lines, the newest at history[(frames - 1) % HISTORY]
    };
    // Once per tick: sends Link's position while the client asks for it.
    void Tick();
    // Link's animation as POS sends it: anim id (0: none), frame in tenths, animations started so far.
    struct Anim { int anim = 0, frame = 0; u32 starts = 0; };
    Anim LinkAnim();
    // The name of an anim id (PlayerAnimation.gsheet's fileName, the FSKA in Link's model), or nullptr.
    const char* AnimName(int anim);
    // A new client connection: no positions until it asks, no ghosts.
    void Reset();
    // A GHOSTS or GHOST line from the client.
    void HandleLine(const std::string& line);
    const Ghost& Get(int n);
    // DBG ghostnet: what was sent and what each slot holds.
    std::string State();
}
