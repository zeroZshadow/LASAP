#pragma once
#include "lib.hpp"
#include <string>
#include <vector>

// Jukebox (music.cpp, DESIGN.md "Music"): a chosen BGM plays instead of the zone's music.
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
    // Instant Pickups: the item's fanfare, as its item-get starts it; one this started before and still plays fades
    // out first, so two never play over each other.
    void Fanfare(int itemId);
    // The Guardian Acorn's and Piece of Power's music, as their item-get starts it.
    void BuffMusic();
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
