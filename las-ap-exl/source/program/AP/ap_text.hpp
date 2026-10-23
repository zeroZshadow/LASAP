#pragma once
// In-game text (ap_text.cpp): the found and received items' texts, the map tracker's labels and the toasts.
#include "lib.hpp"
#include <string>

namespace Game::Ui { struct TextHolder; }

namespace AP {
    void InstallTextHooks();
    // What a location holds: item name and, for another player's item, that player's name.
    void SetLocationName(int local_id, const std::string& item, const std::string& player);
    bool HasLocationName(int local_id);
#ifdef LASAP_TEST
    // DBG text: the text features' counts.
    std::string TextState();
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
    // Sets a text box to a UTF-8 text through the game's message pipeline, as the game sets a text object to a
    // message.
    void SetPaneText(Game::Ui::TextHolder* holder, const std::string& text);
    // Shows the next toast once the popup is free; only in a level.
    void TickToasts(bool inLevel);
    // No toast showing or waiting.
    bool ToastsIdle();
    // A toast shows now (the popup has its text).
    bool ToastShowing();
    // A received item's toast: "You got <item>", then "from <sender>" on a second line; names clipped as in the
    // received text.
    std::string ReceivedToast(const std::string& item, const std::string& sender);
    // A found item's toast (Instant Pickups): "You found <item>", then "for <player>" for another player's item; the
    // names as the found text has them. FoundToast() for the placeholder just found (or the mansion's present), "" for
    // a location with no name.
    std::string FoundToast(int local_id);
    std::string FoundToast();
}
