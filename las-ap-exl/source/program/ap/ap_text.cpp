// In-game text for Archipelago items, the seed's texts in vanilla messages (the save name, the egg's sign, the shop's
// price tags, the in-game hints) and the toasts (DESIGN.md, In-game text). Hooks: the MSBT label lookup, to learn which (file, index)
// pairs are ours, and the text getter, to hand out our text for them; the Seashell Mansion's present as it pops, for
// its location; the autosave popup's state, for the toasts.
#include "ap/ap_text.hpp"
#include "ap/ap.hpp"
#include "ap/ap_list.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_shuffles.hpp"
#include "config/config.hpp"
#include "config/seed_file.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/menu.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include "map/map_tracker.hpp"
#include "test_only.hpp"
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace {
    using Game::MessageRef;
    using Game::MsbtText;
    // ShellMansionMaster.PopBox, at the call that creates the present: x3 = item key view, w4 = item index.
    constexpr uintptr_t kMansionPopBoxCreate = 0xa35994;

    // Toasts reuse the autosave popup (Game::Ui::ThrobberSave, "Now Saving"): shown in auto mode, its LoopAuto state
    // counts the timer down and slides out at zero; its text shows the message UI:SystemSaving.
    using Game::Ui::ThrobberSave;
    using Game::Ui::kThrobberNone;
    using Game::Ui::kThrobberInAuto;
    using Game::Ui::kThrobberLoopAuto;
    constexpr float kToastSeconds = 3.0f;
    constexpr const char* kSavingLabel = "SystemSaving";

    Game::Ui::ThrobberSave* g_Throbber = nullptr;  // the popup's UI object, seen by the state hook
    bool g_ToastStarting = false;   // our ShowThrobber call is running
    bool g_ToastShowing = false;    // the popup shows our text
    bool g_ToastText = false;       // the text getter hands out the toast for UI:SystemSaving
    const std::string* g_PaneText = nullptr;  // SetPaneText: the getter hands out this for UI:SystemSaving
    char g_PaneBuf[128];
    std::deque<std::string> g_Toasts;
    std::string g_MusicToast;       // the Jukebox's toast waiting, "" none
    bool g_ToastMusic = false;      // the popup shows the Jukebox's toast
    std::string g_Toast;
    char g_ToastBuf[256];

    enum class Kind {
        None, Found, Received, Saving, MapCaption, MapPlace, MapPin, MapZoom, Sign, SaveName, PriceTag, Buy, Hint,
        BookPrompt
    };
    constexpr const char* kMapCaptionLabel = "MapCaptionMemory";  // the map's X button: memories
    constexpr const char* kMapPlaceLabel = "MapCaptionPlace";     // Y: hide markers
    constexpr const char* kMapPinLabel = "MapCaptionPin";         // A: pin
    constexpr const char* kMapZoomLabel = "MapCaptionZoom";       // R: zoom
    constexpr const char* kSaveNameLabel = "NameEntry_Default";   // a new file's name entry: "Link"
    // location: the AP location a price tag or buy dialog names; source: the hint source of a hint or book question
    struct Slot { void* msbt; s32 index; Kind kind; AP::LocationId location; int source; };
    constexpr int kSlots = 96;
    Slot g_Slots[kSlots];
    int g_SlotCount = 0;

    // The shop's AP slots (the seed file's shop)
    struct ShopSlot { std::string_view tag, buy; AP::LocationId location; };  // in the seed file
    std::vector<ShopSlot> g_ShopSlots;
    char g_SignText[256];
    char g_ShopText[512];

    // The seed's in-game hints (the seed file's hints) by source: its hint's pages (lines joined by '\n'); for a book, its
    // question's label and volume number
    struct HintSource {
        std::vector<std::string> pages;
        std::string_view prompt;
        int volume = 0;
        bool Book() const { return !prompt.empty(); }
    };
    std::map<int, HintSource> g_HintSources;
    char g_HintText[1024];
    char g_PromptText[512];
    int g_FindCalls = 0, g_GetCalls = 0;

    // Place.msbt labels of the map tracker's icons: (msbt, index) -> icon group
    std::map<std::pair<void*, s32>, int> g_MapLabels;
    char g_MapText[512];
    char g_CaptionText[64];

    using AP::LocationId;

    std::map<LocationId, std::pair<std::string, std::string>> g_LocationNames;
    std::optional<LocationId> g_FoundLocation;
    bool g_FoundFresh = false;      // g_FoundLocation was set during the current event
    std::optional<LocationId> g_PresentLocation;  // the popped mansion present holding a placeholder
    std::string g_RecvItem, g_RecvSender;
    char g_FoundText[512];
    char g_RecvText[512];

    // UTF-8 MSBT tags: 0x0E, group, type, parameter size (u16 each), parameters.
    const std::string kColorItem("\x0e\x00\x00\x03\x00\x04\x00\xd9\x45\x6d\xff", 11);   // as in native item texts
    const std::string kColorPlayer("\x0e\x00\x00\x03\x00\x04\x00\x62\xcf\xff\xff", 11); // sky blue
    const std::string kColorWhite("\x0e\x00\x00\x03\x00\x04\x00\xed\xed\xed\xff", 11);
    const std::string kWaitInput(AP::WAIT_INPUT);
    const std::string kNewPage("\x0e\x00\x00\x04\x00\x00\x00", 7);  // after kWaitInput: the next text box
    const std::string kHidIcon("\x0e\x01\x00\x01\x00\x01\x00", 7);  // and one byte: the HidIcon, the button it draws
    constexpr size_t kItemNameMax = 32;
    constexpr size_t kPlayerNameMax = 16;

    // At most `max` bytes; a name cut short ends in "...".
    std::string Clip(const std::string& s, size_t max) {
        if (s.size() <= max) return s;
        size_t n = max - 3;
        while (n > 0 && (static_cast<u8>(s[n]) & 0xc0) == 0x80) n--;  // keep UTF-8 sequences whole
        while (n > 0 && s[n - 1] == ' ') n--;
        return s.substr(0, n) + "...";
    }

    // Hands out the text in a buffer of its own kind, at most its size. The game still reads a fetched text after
    // other messages were fetched (a std::string shared by all of them crashed the Jukebox page and the map's check
    // list), so each kind keeps its buffer, whose memory never moves.
    template<size_t N>
    void HandOut(MsbtText* out, char (&buf)[N], const std::string& text) {
        size_t n = std::min(text.size(), N - 1);
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
        out->text = buf;
        out->length = static_cast<s64>(n);
    }

    const Slot* SlotOf(void* msbt, s32 index) {
        for (int i = 0; i < g_SlotCount; i++) {
            if (g_Slots[i].msbt == msbt && g_Slots[i].index == index) return &g_Slots[i];
        }
        return nullptr;
    }

    Kind KindOf(void* msbt, s32 index) {
        const Slot* slot = SlotOf(msbt, index);
        return slot != nullptr ? slot->kind : Kind::None;
    }

    void Remember(void* msbt, s32 index, Kind kind, AP::LocationId location = {}, int source = -1) {
        if (index < 0 || KindOf(msbt, index) != Kind::None) return;
        if (g_SlotCount == kSlots) g_SlotCount = 0;  // message files are reloaded on language change
        g_Slots[g_SlotCount++] = Slot{msbt, index, kind, location, source};
    }

    bool LabelIs(const char* label, size_t len, const char* name) {
        return len == std::strlen(name) && std::memcmp(label, name, len) == 0;
    }

#ifdef LASAP_TEST
    // Messages the game fetched, for DBG text <label>: (msbt, index) -> label from the lookups, label -> the text
    // the getter handed out last, tags dropped and line breaks as spaces.
    std::map<std::pair<void*, s32>, std::string> g_SeenLabels;
    std::map<std::string, std::string> g_SeenTexts;

    void SeeLabel(void* msbt, s32 index, const char* label, size_t len) {
        if (index < 0) return;
        if (g_SeenLabels.size() >= 512) g_SeenLabels.clear();
        g_SeenLabels[{msbt, index}] = std::string(label, len);
    }

    void SeeText(void* msbt, s32 index, const char* text, s64 length) {
        auto it = g_SeenLabels.find({msbt, index});
        if (it == g_SeenLabels.end() || text == nullptr) return;
        std::string plain;
        for (s64 i = 0; i < length;) {
            u8 c = static_cast<u8>(text[i]);
            if (c == 0x0e && i + 7 <= length) {  // 0x0E, group, type, parameter size (u16 each), parameters
                i += 7 + static_cast<u8>(text[i + 5]) + (static_cast<u8>(text[i + 6]) << 8);
            } else if (c == 0x0f && i + 5 <= length) {  // 0x0F, group, type: a tag's end
                i += 5;
            } else {
                plain += c == '\n' ? ' ' : static_cast<char>(c);
                i++;
            }
        }
        if (g_SeenTexts.size() >= 64 && !g_SeenTexts.count(it->second)) g_SeenTexts.clear();
        g_SeenTexts[it->second] = plain;
    }
#endif

    // The item and player names of the location's item; null without them
    const std::pair<std::string, std::string>* NamesOf(std::optional<LocationId> location) {
        if (!location) return nullptr;
        auto it = g_LocationNames.find(*location);
        return it != g_LocationNames.end() ? &it->second : nullptr;
    }

    // The placeholder just found, else the mansion's present
    std::optional<LocationId> FoundOrPresent() { return g_FoundFresh ? g_FoundLocation : g_PresentLocation; }

    std::string ToastOf(std::optional<LocationId> location) {
        const auto* names = NamesOf(location);
        if (names == nullptr) return "";
        const auto& [item, player] = *names;
        std::string to = player.empty() ? "" : "\nfor " + Clip(player, kPlayerNameMax);
        return "You found " + Clip(item, kItemNameMax) + to;
    }
#ifdef LASAP_TEST
    std::string IdText(std::optional<LocationId> location) {
        return location ? AP::ToString(*location) : "-1";
    }
#endif

    // Where the tag at i ends (0x0E, group, type, parameter size (u16 each), parameters; 0x0F, group, type: a tag's
    // end), or i + 1 for a character.
    s64 Next(const char* text, s64 length, s64 i) {
        u8 c = static_cast<u8>(text[i]);
        if (c == 0x0e && i + 7 <= length) {
            return i + 7 + static_cast<u8>(text[i + 5]) + (static_cast<u8>(text[i + 6]) << 8);
        }
        if (c == 0x0f && i + 5 <= length) return i + 5;
        return i + 1;
    }

    bool TagAt(const char* text, s64 length, s64 i, const std::string& tag) {
        return i >= 0 && i + static_cast<s64>(tag.size()) <= length && std::memcmp(text + i, tag.data(), tag.size()) == 0;
    }

    // The egg's sign: the numbers the egg takes (config.ini [archipelago]), under the place name.
    std::string SignLine() {
        const auto& ap = global_config.archipelago;
        constexpr int kInstruments = 8;
        int n = ap.seashell_hunt ? ap.seashells_required : ap.instruments_required;
        std::string needed;
        if (ap.seashell_hunt) {
            needed = n == 0 ? "no secret seashells" : std::to_string(n) + " secret seashell" + (n > 1 ? "s" : "");
        } else if (n >= kInstruments) {
            needed = "all " + std::to_string(kInstruments) + " instruments";
        } else if (n == 0) {
            needed = "no instruments";
        } else {
            needed = std::to_string(n) + " instrument" + (n > 1 ? "s" : "");
        }
        return "The Wind Fish's Egg opens\nwith " + needed + ".";
    }

    // Characters (UTF-8 sequences) in s
    size_t Characters(const std::string& s) {
        size_t n = 0;
        for (char c : s) n += (static_cast<u8>(c) & 0xc0) != 0x80;
        return n;
    }

    // The first `max` characters of s
    std::string FirstCharacters(const std::string& s, size_t max) {
        size_t i = 0, n = 0;
        while (i < s.size() && (n < max || (static_cast<u8>(s[i]) & 0xc0) == 0x80)) {
            n += (static_cast<u8>(s[i]) & 0xc0) != 0x80;
            i++;
        }
        return s.substr(0, i);
    }

    // A shop slot's name: the item and its owner in kShopNameMax characters, a long item name cut short with "…"; the
    // placeholder's name while the location has none.
    std::string ShopName(LocationId location) {
        constexpr size_t kShopNameMax = 46;  // characters on the buy dialog's first line
        const auto* names = NamesOf(location);
        if (names == nullptr) return "Archipelago Item";
        std::string item = names->first;
        std::string suffix = names->second.empty() ? "" : " for " + FirstCharacters(names->second, kPlayerNameMax);
        size_t room = kShopNameMax - Characters(suffix);
        if (Characters(item) > room) {
            item = FirstCharacters(item, room - 1);
            while (!item.empty() && item.back() == ' ') item.pop_back();
            item += "…";
        }
        return item + suffix;
    }

    // A hint's message: its pages, each waiting for input before the next box; an owl statue's in the owls' sky blue.
    std::string HintText(const HintSource& hint) {
        std::string text;
        for (const std::string& page : hint.pages) {
            if (!text.empty()) text += kWaitInput + kNewPage;
            text += hint.Book() ? page : kColorPlayer + page + kColorWhite;
        }
        return text + kWaitInput;
    }

    // The source of a hint label (HINT_PREFIX and its number) that has a hint, else -1
    int HintSourceOf(const char* label, size_t len) {
        size_t prefix = AP::HINT_PREFIX.size();
        if (len <= prefix || std::memcmp(label, AP::HINT_PREFIX.data(), prefix) != 0) return -1;
        int n = 0;
        for (size_t i = prefix; i < len; i++) {
            if (label[i] < '0' || label[i] > '9') return -1;
            n = n * 10 + (label[i] - '0');
        }
        return g_HintSources.count(n) ? n : -1;
    }

    // The message with `line` as its first line
    std::string WithFirstLine(const char* text, s64 length, const std::string& line) {
        s64 end = 0;
        while (end < length && text[end] != '\n') end = Next(text, length, end);
        end = std::min(end, length);
        return line + std::string(text + end, length - end);
    }

    // A buy dialog's first line holds the item and its price ("Deluxe shovel: 200 rupees!"): the name, then the price
    // (from the item colour's tag to the white's end) and the choice, as on the price tag.
    std::string BuyText(const char* text, s64 length, const std::string& name) {
        s64 price = -1, priced = -1, choice = -1;
        for (s64 i = 0; i < length; i = Next(text, length, i)) {
            if (price < 0 && TagAt(text, length, i, kColorItem)) price = i;
            else if (price >= 0 && priced < 0 && TagAt(text, length, i, kColorWhite)) priced = Next(text, length, i);
            // the choice: group 1, type 7
            if (choice < 0 && static_cast<u8>(text[i]) == 0x0e && i + 7 <= length && text[i + 1] == 1 &&
                text[i + 2] == 0 && text[i + 3] == 7 && text[i + 4] == 0) {
                choice = i;
            }
        }
        if (priced < 0 || choice < priced) return WithFirstLine(text, length, name);
        return name + "\n" + std::string(text + price, priced - price) + std::string(text + choice, length - choice);
    }
}

HOOK_DEFINE_TRAMPOLINE(MsbtFindLabel) {
    // A shop slot's price tag or buy dialog
    static bool FindShopSlot(void* msbt, s32 index, const char* label, size_t len) {
        for (const ShopSlot& slot : g_ShopSlots) {
            if (LabelIs(label, len, slot.tag.data())) Remember(msbt, index, Kind::PriceTag, slot.location);
            else if (LabelIs(label, len, slot.buy.data())) Remember(msbt, index, Kind::Buy, slot.location);
            else continue;
            return true;
        }
        return false;
    }

    // A hint's message, or a book's question, of a source with a hint
    static bool FindHint(void* msbt, s32 index, const char* label, size_t len) {
        int source = HintSourceOf(label, len);
        if (source >= 0) {
            Remember(msbt, index, Kind::Hint, {}, source);
            return true;
        }
        for (const auto& [n, hint] : g_HintSources) {
            if (hint.prompt.empty() || !LabelIs(label, len, hint.prompt.data())) continue;
            Remember(msbt, index, Kind::BookPrompt, {}, n);
            return true;
        }
        return false;
    }

    static void Callback(MessageRef* out, void* msbt, const char* label, size_t len) {
        Orig(out, msbt, label, len);
        g_FindCalls++;
        TEST_ONLY(SeeLabel(msbt, out->index, label, len);)
        int group = AP::MapTracker::LabelGroup(std::string_view(label, len));
        if (group >= 0) {
            if (out->index >= 0) g_MapLabels[{msbt, out->index}] = group;
            return;
        }
        // Another message in the same place: message files are reloaded on language change.
        if (!g_MapLabels.empty()) g_MapLabels.erase({msbt, out->index});
        if (LabelIs(label, len, AP::FOUND_LABEL)) Remember(msbt, out->index, Kind::Found);
        else if (LabelIs(label, len, AP::RECEIVED_LABEL)) Remember(msbt, out->index, Kind::Received);
        else if (LabelIs(label, len, kSavingLabel)) Remember(msbt, out->index, Kind::Saving);
        else if (LabelIs(label, len, kMapCaptionLabel)) Remember(msbt, out->index, Kind::MapCaption);
        else if (LabelIs(label, len, kMapPlaceLabel)) Remember(msbt, out->index, Kind::MapPlace);
        else if (LabelIs(label, len, kMapPinLabel)) Remember(msbt, out->index, Kind::MapPin);
        else if (LabelIs(label, len, kMapZoomLabel)) Remember(msbt, out->index, Kind::MapZoom);
        else if (LabelIs(label, len, AP::SIGN_LABEL)) Remember(msbt, out->index, Kind::Sign);
        else if (LabelIs(label, len, kSaveNameLabel)) Remember(msbt, out->index, Kind::SaveName);
        else if (!FindShopSlot(msbt, out->index, label, len) && !FindHint(msbt, out->index, label, len)) return;
        Logging.Log("[AP] text label %.*s index %d", static_cast<int>(len), label, out->index);
    }
};

HOOK_DEFINE_TRAMPOLINE(MsbtGetText) {
    static void Callback(MsbtText* out, void* msbt, s32 index) {
        Orig(out, msbt, index);
        g_GetCalls++;
        AP::Shuffles::FixText(out, msbt, index);
        Resolve(out, msbt, index);
        TEST_ONLY(SeeText(msbt, index, out->text, out->length);)
    }

    // Hands out our text for the messages that are ours.
    static void Resolve(MsbtText* out, void* msbt, s32 index) {
        if (out->text == nullptr) return;
        auto map = g_MapLabels.find({msbt, index});
        if (map != g_MapLabels.end()) {
            std::string text = AP::MapTracker::LabelText(map->second);
            TEST_ONLY(AP::MapTracker::NoteText(text);)
            if (!text.empty()) HandOut(out, g_MapText, text);
            return;
        }
        const Slot* slot = SlotOf(msbt, index);
        Kind kind = slot != nullptr ? slot->kind : Kind::None;
        switch (kind) {
            case Kind::Found: {
                const auto* names = NamesOf(FoundOrPresent());
                if (names == nullptr) return;  // unknown: keep "You found an Archipelago item!"
                const auto& [item, player] = *names;
                std::string t = player.empty()
                    ? "You found your " + kColorItem + Clip(item, kItemNameMax) + kColorWhite + "!"
                    : "You found " + kColorItem + Clip(item, kItemNameMax) + kColorWhite + "\nfor " +
                      kColorPlayer + Clip(player, kPlayerNameMax) + kColorWhite + "!";
                HandOut(out, g_FoundText, t + kWaitInput);
                break;
            }
            case Kind::Received: {
                std::string t = "You got " + kColorItem + Clip(g_RecvItem, kItemNameMax) + kColorWhite;
                t += g_RecvSender.empty() ? std::string("!")
                                          : "\nfrom " + kColorPlayer + Clip(g_RecvSender, kPlayerNameMax) + kColorWhite + "!";
                HandOut(out, g_RecvText, t + kWaitInput);
                break;
            }
            case Kind::Saving:
                if (g_PaneText != nullptr) {
                    HandOut(out, g_PaneBuf, *g_PaneText);
                    return;
                }
                if (!g_ToastText) return;
                HandOut(out, g_ToastBuf, g_Toast);
                break;
            case Kind::MapCaption:
            case Kind::MapPlace:
            case Kind::MapPin:
            case Kind::MapZoom: {
                // the map's X, Y, A and R captions; the Jukebox's guide, made from the map's, uses them too
                int button = kind == Kind::MapCaption ? 0 : kind == Kind::MapPlace ? 1 : kind == Kind::MapPin ? 2 : 3;
                std::string text = AP::ListUi::GuideCaption(button);
                if (text.empty() && kind == Kind::MapCaption) text = AP::MapTracker::CaptionText();
                if (text.empty()) return;
                // The caption starts with a tag drawing the button (0x0E, group, type, parameter size, parameters),
                // or with the guide's own.
                s64 tags = 0;
                while (tags + 7 <= out->length && out->text[tags] == 0x0e) {
                    tags += 7 + static_cast<u8>(out->text[tags + 5]) + (static_cast<u8>(out->text[tags + 6]) << 8);
                }
                tags = std::min(tags, out->length);
                int icon = AP::ListUi::GuideIcon(button);
                std::string head = icon >= 0 ? kHidIcon + static_cast<char>(icon) : std::string(out->text, tags);
                HandOut(out, g_CaptionText, head + text);
                break;
            }
            case Kind::Sign: {
                AP::OnSignRead();
                // the place name, then the numbers, before the wait for input it ends with
                s64 end = out->length;
                s64 wait = end - static_cast<s64>(kWaitInput.size());
                if (TagAt(out->text, out->length, wait, kWaitInput)) end = wait;
                HandOut(out, g_SignText, std::string(out->text, end) + "\n" + SignLine() + kWaitInput);
                break;
            }
            case Kind::SaveName: {
                const std::string& name = global_config.archipelago.save_name;
                if (name.empty()) return;
                out->text = name.c_str();
                out->length = static_cast<s64>(name.size());
                break;
            }
            case Kind::PriceTag:
                HandOut(out, g_ShopText, WithFirstLine(out->text, out->length, ShopName(slot->location)));
                break;
            case Kind::Buy:
                HandOut(out, g_ShopText, BuyText(out->text, out->length, ShopName(slot->location)));
                break;
            case Kind::Hint: {
                // showing it sets its flag, which the save keeps; those of server hints are reported (ap_checks.cpp)
                std::string flag = std::string(AP::HINT_PREFIX) + std::to_string(slot->source);
                EventFlags::SetFlag(flag, true);
                HandOut(out, g_HintText, HintText(g_HintSources[slot->source]));
                break;
            }
            case Kind::BookPrompt: {
                // the book's title, its first line, names the volume
                const HintSource& hint = g_HintSources[slot->source];
                std::string title = "\"Koholint Lost and Found, Volume " + std::to_string(hint.volume) + "\"";
                HandOut(out, g_PromptText, WithFirstLine(out->text, out->length, title));
                break;
            }
            case Kind::None:
                break;
        }
    }
};

namespace {
    // Sets the popup's text from UI:SystemSaving: the toast while `toast`, else the game's own "Now Saving".
    void SetThrobberText(bool toast) {
        if (g_Throbber == nullptr) return;
        static const char label[] = "UI:SystemSaving";
        Game::StrView view{label, sizeof(label) - 1};
        g_ToastText = toast;
        Game::MessageText_SetLabel(&g_Throbber->text, &view);
        g_ToastText = false;
    }
}

HOOK_DEFINE_TRAMPOLINE(ThrobberSetState) {
    static void Callback(Game::StateMachine<6>* machine, s32 state) {
        Orig(machine, state);
        g_Throbber = reinterpret_cast<ThrobberSave*>(reinterpret_cast<u8*>(machine) - offsetof(ThrobberSave, machine));
        if (g_ToastStarting && state == kThrobberInAuto) {
            // our ShowThrobber call: show the toast's text for its time
            g_Throbber->timer = kToastSeconds;
            SetThrobberText(true);
            g_ToastShowing = true;
        } else if (g_ToastShowing && state == kThrobberNone) {
            SetThrobberText(false);
            g_ToastShowing = false;
        }
    }
};

HOOK_DEFINE_INLINE(MansionPopBox) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        auto* key = reinterpret_cast<const Game::StrView*>(ctx->X[3]);
        int index = static_cast<int>(ctx->X[4]);
        if (key != nullptr && key->data != nullptr && key->View() == AP::PLACEHOLDER_KEY) {
            g_PresentLocation = LocationId{index};
            Logging.Log("[AP] present holds location %d", index);
        }
    }
};

namespace AP {
    void InstallTextHooks() {
        MsbtFindLabel::InstallAtFuncPtr(Game::Msbt_FindLabel.Ptr());
        MsbtGetText::InstallAtFuncPtr(Game::Msbt_GetText.Ptr());
        MansionPopBox::InstallAtPtr(Game::Addr(kMansionPopBoxCreate));
        ThrobberSetState::InstallAtFuncPtr(Game::ThrobberSave_ChangeState.Ptr());
    }

    void SetPaneText(Game::Ui::TextHolder* holder, const std::string& text) {
        static const char label[] = "UI:SystemSaving";
        Game::StrView view{label, sizeof(label) - 1};
        g_PaneText = &text;
        Game::MessageText_SetLabel(holder, &view);
        g_PaneText = nullptr;
    }

    void QueueToast(const std::string& text) {
        if (g_Toasts.size() < 8) g_Toasts.push_back(text);
    }

    void ShowMusicToast(const std::string& text) {
        // showing, before it slides out: the new name in its place, for a whole toast's time
        if (g_ToastShowing && g_ToastMusic && g_Throbber != nullptr &&
            (g_Throbber->machine.state == kThrobberInAuto || g_Throbber->machine.state == kThrobberLoopAuto)) {
            g_Toast = text;
            SetThrobberText(true);
            g_Throbber->timer = kToastSeconds;
            return;
        }
        g_MusicToast = text;
    }

    void TickToasts(bool inLevel) {
        if (g_ToastShowing || (g_Toasts.empty() && g_MusicToast.empty()) || !inLevel) return;
        // ShowThrobber does nothing while the popup is busy (a real save); the toast then waits
        if (g_Throbber != nullptr && g_Throbber->machine.state != kThrobberNone) return;
        bool music = g_Toasts.empty();
        g_Toast = music ? g_MusicToast : g_Toasts.front();
        g_ToastStarting = true;
        Game::ThrobberSave_Show(true);
        g_ToastStarting = false;
        if (g_ToastShowing) {
            g_ToastMusic = music;
            if (music) {
                g_MusicToast.clear();
            } else {
                g_Toasts.pop_front();
            }
            Logging.Log("[AP] toast: %s", g_Toast.c_str());
        }
    }

    void SetLocationName(LocationId location, const std::string& item, const std::string& player) {
        g_LocationNames[location] = {item, player};
    }

    std::pair<std::string, std::string> SplitNames(std::string_view names) {
        size_t bar = names.find('|');
        if (bar == std::string_view::npos) return {std::string(names), ""};
        return {std::string(names.substr(0, bar)), std::string(names.substr(bar + 1))};
    }

#ifdef LASAP_TEST
    std::string TextState() {
        return "TEXT names=" + std::to_string(g_LocationNames.size()) + " slots=" + std::to_string(g_SlotCount) +
               " found=" + IdText(g_FoundLocation) + " finds=" + std::to_string(g_FindCalls) +
               " gets=" + std::to_string(g_GetCalls) + " present=" + IdText(g_PresentLocation);
    }

    std::string SeenText(const std::string& label) {
        auto it = g_SeenTexts.find(label);
        return it == g_SeenTexts.end() ? "TEXT " + label + " unseen" : "TEXT " + label + " said=" + it->second;
    }
#endif

    bool HasLocationName(LocationId location) {
        return g_LocationNames.count(location) != 0;
    }

    std::string HintedItemText(LocationId location) {
        const auto* names = NamesOf(location);
        if (names == nullptr) return "";
        const auto& [item, player] = *names;
        return player.empty() ? Clip(item, kItemNameMax)
                              : Clip(item, kItemNameMax) + " for " + Clip(player, kPlayerNameMax);
    }

    void SetFoundLocation(LocationId location) {
        g_FoundLocation = location;
        g_FoundFresh = true;
        if (location == g_PresentLocation) g_PresentLocation.reset();
    }

    void ExpireFoundLocation() {
        g_FoundFresh = false;
    }

    std::optional<LocationId> LastFoundLocation() { return g_FoundLocation; }

    std::optional<LocationId> FoundOrPresentLocation() { return FoundOrPresent(); }

    std::string ReceivedToast(const std::string& item, const std::string& sender) {
        std::string from = sender.empty() ? "" : "\nfrom " + Clip(sender, kPlayerNameMax);
        return "You got " + Clip(item, kItemNameMax) + from;
    }

    std::string FoundToast(LocationId location) {
        return ToastOf(location);
    }

    std::string FoundToast() {
        return ToastOf(FoundOrPresent());
    }

    bool ToastsIdle() {
        return !g_ToastShowing && g_Toasts.empty();
    }

    bool ToastShowing() { return g_ToastShowing; }

    void SetReceivedText(const std::string& item, const std::string& sender) {
        g_RecvItem = item;
        g_RecvSender = sender;
    }

    // The seed file's shop: "<price tag label> <buy dialog label> <local id>" lines, the shop's AP slots, System.msbt
    // labels whose first line the module makes the location's item name (patcher las_ap_patch.shop_lines)
    void LoadShopSlots() {
        SeedFile::ForEach("shop", [](Fields& f) {
            std::string_view tag = f.Str(), buy = f.Str();
            if (auto location = f.Num<AP::LocationId>()) g_ShopSlots.push_back({tag, buy, *location});
        });
        Logging.Log("[AP] shop slots: %zu", g_ShopSlots.size());
    }

    // The seed file's hints, per source n (its place in ap_hints.SOURCES): "L <n> <text>" its hint's next line, "P
    // <n>" a new page, "B <n> <label> <volume>" for a book its question (System:<label>) naming that volume (patcher
    // ap_hints.py)
    void LoadHintTexts() {
        SeedFile::ForEach("hints", [](Fields& f) {
            std::string_view kind = f.Str();
            auto n = f.Num<int>();
            if (!n || *n < 0 || (kind != "L" && kind != "P" && kind != "B")) return;
            HintSource& source = g_HintSources[*n];
            if (kind == "L") {
                if (source.pages.empty()) source.pages.emplace_back();
                if (!source.pages.back().empty()) source.pages.back() += '\n';
                source.pages.back() += f.Rest();
            } else if (kind == "P") {
                source.pages.emplace_back();
            } else {
                source.prompt = f.Str();
                source.volume = f.Num<int>().value_or(0);
            }
        });
        Logging.Log("[AP] hint texts: %zu", g_HintSources.size());
    }

    void LoadLocationNames() {
        // The seed file's names: "<local id> <item name>|<player name, empty for an own item>" lines, so found items
        // are named before the client connects. NAME lines from the client replace them.
        int count = 0;
        SeedFile::ForEach("names", [&count](Fields& f) {
            auto location = f.Num<LocationId>();
            if (!location) return;
            auto [item, player] = SplitNames(f.Rest());
            SetLocationName(*location, item, player);
            count++;
        });
        Logging.Log("[AP] location names: %d", count);
    }
}
