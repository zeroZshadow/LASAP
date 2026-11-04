#pragma once
// In-game text (ap_text.cpp): the found and received items' texts, the map tracker's labels and the toasts.
#include "ap/location_id.hpp"
#include "lib.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Game::Ui { struct TextHolder; }

namespace AP {
    // The MSBT tag (UTF-8) at a message's end that waits for a button
    inline constexpr std::string_view WAIT_INPUT{"\x0e\x01\x00\x04\x00\x00\x00", 7};

    void InstallTextHooks();
    // What a location holds: item name and, for another player's item, that player's name.
    void SetLocationName(LocationId location, const std::string& item, const std::string& player);
    // "<item name>|<player name>", the player's empty for an own item, as the seed file's names and the client's NAME
    // and ITEM lines carry them: the two names.
    std::pair<std::string, std::string> SplitNames(std::string_view names);
    bool HasLocationName(LocationId location);
    // The item a hinted location holds, for the map tracker: "<item>", or "<item> for <player>" for another player's;
    // the names clipped as in the found text. "" for a location with no name.
    std::string HintedItemText(LocationId location);
#ifdef LASAP_TEST
    // DBG text: the text features' counts.
    std::string TextState();
    // DBG text <label>: the text the game last fetched for a message label (tags dropped, line breaks as spaces).
    std::string SeenText(const std::string& label);
#endif
    // The location of a placeholder just added; it names the found text until the player is free again.
    void SetFoundLocation(LocationId location);
    void ExpireFoundLocation();
    // The placeholder added last, also once it no longer names the found text
    std::optional<LocationId> LastFoundLocation();
    // The placeholder just added, else the mansion's present
    std::optional<LocationId> FoundOrPresentLocation();
    void SetReceivedText(const std::string& item, const std::string& sender);
    void LoadLocationNames();
    // The shop's AP slots, whose price tags and buy dialogs name the item (the seed file's shop)
    void LoadShopSlots();
    // The in-game hints' texts, shown in their sources' messages (the seed file's hints)
    void LoadHintTexts();
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
    std::string FoundToast(LocationId location);
    std::string FoundToast();
}
