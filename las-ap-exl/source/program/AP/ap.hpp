#pragma once
// Archipelago bridge: a TCP line protocol (see DESIGN.md) between the game and the AP client.
#include "lib.hpp"
#include "nn/hid.h"
#include <string>
#include <string_view>
#include <vector>

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
    // Place.msbt label of the sign at the foot of the Wind Fish's Egg's stairs, which names the instruments the egg
    // needs (patcher ap_goal.py). Showing it sets SIGN_FLAG (patcher: ap_goal.SIGN_FLAG) in the save and is reported
    // as SIGN.
    constexpr const char* SIGN_LABEL = "SBMountTamaranch";
    constexpr const char* SIGN_FLAG = "APSignRead";
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
    // The game shows the sign's text (SIGN_LABEL): kept in the save (SIGN_FLAG), reported as SIGN.
    void OnSignRead();
    // Checked in this game or, as the client reported, on the server.
    bool IsLocationChecked(int local_id);
    // Marked as skipped on the map by the player (map tracker); saved per seed, never reported.
    bool IsLocationSkipped(int local_id);
    void SetLocationSkipped(int local_id, bool skipped);
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
    // The d-pad's toast: a track's name, or the ghost names turned on or off. It takes the place of the one showing or
    // waiting, so skipping through tracks shows the last one, not a queue.
    void ShowMusicToast(const std::string& text);
    // Sets a text pane to a UTF-8 text through the game's message pipeline, as the game sets a pane to a message: the
    // holder's +0x10 is the pane, as in the game's text objects.
    void SetPaneText(void* holder, const std::string& text);
    // Shows the next toast once the popup is free; only in a level.
    void TickToasts(bool inLevel);
    // No toast showing or waiting.
    bool ToastsIdle();
    // A received item's toast: "You got <item>", then "from <sender>" on a second line; names clipped as in the
    // received text.
    std::string ReceivedToast(const std::string& item, const std::string& sender);

    // Map tracker (ap_map.cpp)
    void LoadMap();
    // Answers the map tracker's event flags; false for any other flag.
    bool MapFlag(std::string_view name, bool& value);
    // The icon group a Place.msbt label names, or -1; the hover text for a group.
    int MapLabelGroup(std::string_view label);
    std::string MapLabelText(int group);
    std::string MapState();
#ifdef LASAP_TEST
    // The overworld icon group the map's cursor was on the last frame the overworld map took buttons, or -1.
    int MapHoverGroup();
    // STATE's " mlist=<group>/<cursor>/<first row>[/closing]" while the map's check list is there, else " mlist=-";
    // DBG maplist.
    std::string MapListField();
    std::string MapListInfo(const std::vector<std::string>& args);
#endif
    // Each input poll: while the map's check list is open the game sees no buttons and no sticks (true then); buttons
    // held as it closes stay hidden from the game until released.
    bool MapListTakesPad(nn::hid::NpadBaseState* state);
    // The text of the map's X button caption (UI:MapCaptionMemory), or "" for the game's own.
    std::string MapCaptionText();
    // 0 off, 1 on, 2 maps revealed without overworld icons (DBG map, for tests and screenshots)
    void SetMapMode(int mode);
    // Every room of the current dungeon counts as visited; once per tick.
    void TickMap();
}

// Jukebox (ap_music.cpp, DESIGN.md "Music"): a chosen BGM plays instead of the zone's music.
namespace AP::Music {
    void InstallHooks();
    // MUSIC <track> | MUSIC off from the client, split on spaces
    void HandleLine(const std::vector<std::string>& a);
    // A choice made in the game (the Jukebox page): a track, or "" or "off" for the game's own music. Applies as
    // HandleLine and sends the client MUSIC <track> | MUSIC off. It only sets the change, which the next Tick applies;
    // with the pause menu open the caller calls Tick to apply it at once.
    void Choose(const std::string& track);
    // Sends the client MUSIC <the chosen track> | MUSIC off; on each new connection.
    void Report();
    // Once per tick: switches to the chosen track (or back) once a level is loaded; with repeat one off and the
    // player free, moves on once the track has played through.
    void Tick(bool free = false);
    // The chosen track, "" for none; the zone's own track, "" when not known.
    std::string Track();
    // Whether `track` is the chosen track ("" for none), and whether any music is known to play (a chosen track or
    // the zone's own), without copies, for the Jukebox page's every frame.
    bool IsChosen(const char* track);
    bool HasMusic();
    std::string ZoneTrack();
    // The track the last change started plays at full volume, also under the pause menu, which turns music down.
    void Loud();
    // Playback: repeat one (on at the start: the chosen track loops) and shuffle (turning it on draws a new order).
    // With repeat one off, a track that has played through gives way to the next (Skip(1)).
    bool RepeatOne();
    bool Shuffle();
    void SetRepeatOne(bool on);
    void SetShuffle(bool on);
    // Chooses the next track (step 1) or the previous (step -1) from the chosen one, or the zone's own: in the list's
    // order, or with shuffle in the order drawn when it turned on; shows its name in a toast and returns it.
    std::string Skip(int step);
#ifdef LASAP_TEST
    // STATE's " music=<chosen track or -> zonebgm=<the zone's track> bgm=<playing tracks as name:kind, or ->
    // bgmvol=<their volume targets in percent>"
    std::string StateField();
    // DBG music log: the last BGM requests
    std::string LogText();
    // DBG peek b|m: the BGM manager, or the first playing instance (null if none)
    const u8* PeekBase(bool instance);
#endif
}

// Multiplayer ghosts, the network half (ap_ghostnet.cpp, DESIGN.md "Ghosts"): POS lines to the client, GHOST lines
// from it.
namespace AP::GhostNet {
    // Ghosts the client shows at once; GHOST lines name a slot 0..SLOTS-1.
    constexpr int SLOTS = 30;
    // The last GHOST lines of a slot are kept: the puppet replays them on the sender's timeline.
    constexpr int HISTORY = 16;
    // Link's model's shapes: bit i draws shape i (his body and the items on it; the game shows the items he holds
    // this way). SHAPES_DEFAULT: the body, the first sword and shield.
    constexpr int SHAPE_COUNT = 20;
    constexpr u32 SHAPES_DEFAULT = 0x32bf8;
    constexpr int TUNIC_GREEN = 0, TUNIC_RED = 1, TUNIC_BLUE = 2;
    // How a player looks besides his animations: the shapes drawn, his tunic and the frames of his face's material
    // animations ("eye": 0 open, 1 closed, ...; "mouth"; "ear"). All frames 0: the face as the model draws without
    // them.
    struct Look {
        u32 shapes = SHAPES_DEFAULT;
        int tunic = TUNIC_GREEN;
        int eye = 0, mouth = 0, ear = 0;
        bool operator==(const Look&) const = default;
    };
    struct Frame {
        s64 timeMs;        // the sender's clock, unwrapped
        u64 receivedMs;    // this system's counter when the line arrived
        float x, y, z, yaw;
        int anim, animFrame;
        int upperAnim;     // the animation on his upper body, as anim
        int flags;
        Look look;
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
        int upperAnim = 0;      // the animation on his upper body (from the bone spine_a up: idle_carry while he holds
                                // something over his head), as anim
        int flags = 0;          // 1: warped (snap, do not move there), 2: hidden, 4: still since the previous frame,
                                // 8: an animation started since the previous line
        Look look;
        u64 receivedMs = 0;     // when the line arrived (system counter, ms)
        s64 timeMs = 0;         // the sender's clock when it was taken (ms, unwrapped)
        u32 timelineFrom = 0;   // the first line of the sender's clock as unwrapped now (a new one after an off)
        u32 frames = 0;         // GHOST lines for this slot so far
        Frame history[HISTORY]; // the last lines, the newest at history[(frames - 1) % HISTORY]
    };
    // Once per tick: sends Link's position while the client asks for it.
    void Tick();
    // Link's animations as POS sends them: anim id (0: none), frame in tenths, animations started so far, and the
    // anim id on his upper body.
    struct Anim { int anim = 0, frame = 0; u32 starts = 0; int upper = 0; };
    Anim LinkAnim();
    // Link's look as POS sends it.
    Look LinkLook();
    // The name of an anim id (PlayerAnimation.gsheet's fileName, the FSKA in Link's model), or nullptr.
    const char* AnimName(int anim);
    // Whether that animation loops (its row's policy).
    bool AnimLoops(int anim);
    // A new client connection: no positions until it asks, no ghosts, name tags on.
    void Reset();
    // Whether the client wants name tags over the ghosts (GHOSTNAMES).
    bool ShowNames();
    // The d-pad's up in free play: name tags on or off while ghosts are on, with a toast; the client keeps the setting
    // (GHOSTNAMES to the client).
    void ToggleNames();
    // A GHOSTS, GHOSTNAMES or GHOST line from the client.
    void HandleLine(const std::string& line);
    const Ghost& Get(int n);
    // DBG ghostnet: what was sent and what each slot holds.
    std::string State();
}

// Multiplayer ghosts, the puppets (ap_puppet.cpp): each ghost slot shown as an NPC drawn with Link's model.
namespace Game { struct Vec3; }
// The ghosts' name tags (ap_nametag.cpp): ghost slot n's slot name in a small window over a world position.
namespace AP::NameTag {
    // Shows the tag over that point (hidden while it is behind the camera or off screen).
    void Show(int n, const Game::Vec3& head, const std::string& name);
    void Hide(int n);
    // While a level loads: the tags are built again in the next.
    void DropAll();
    // DBG puppet tagstyle: the tags' scale, the text's and the window's alpha, the height above the point and the
    // window's margins around the text (layout units); built again.
    void Style(float scale, int alpha, int backAlpha, float raise, float padX, float padY);
    // STATE's tags=: per slot "-" none, "h" hidden, "g" shown while the font draws the name's glyphs, "d" drawn.
    std::string State();
#ifdef LASAP_TEST
    // DBG puppet tags: each tag's text extent as laid out
    std::string Info();
#endif
}

namespace AP::Popup {
    // Shows the item's model above Link's head for a moment (ap_popup.cpp); false when it cannot be made.
    bool Show(int itemId);
    // A model is loading or showing.
    bool Busy();
    // The item model maker's task made this holder's model: the popup's, true.
    bool IsHolder(const void* holder);
    // The popup's model is an NPC model (HeldModelFix): that scale, and that model point (model units) on the spot.
    void Fit(float scale, float cx, float cy, float cz);
    // Once per tick.
    void Tick();
}

// The Jukebox page (ap_jukebox.cpp, DESIGN.md "Jukebox page"): the pause menu's fourth page as a mode of System's slot.
namespace AP::JukeboxPane {
    void InstallHooks();
    // Once a tick: the d-pad's left and right choose the track before and after while the player is free.
    void Tick(bool free);
#ifdef LASAP_TEST
    // STATE's " pane=<0-3|-> navi=<header state> jbox=<cursor>/<top>"
    std::string StateField(bool paused);
    // The header's state (1 Open, 2 WaitKey, ...), "-" with no header: the test condition navi=<state>
    std::string NaviState();
    // DBG jukebox
    std::string Info();
#endif
}

namespace AP::Puppet {
    void InstallHooks();
    // Once per tick, after GhostNet::Tick.
    void Tick();
}
