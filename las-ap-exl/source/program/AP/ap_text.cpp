// In-game text for Archipelago items. The patched messages Place:ArchipelagoItem (a found placeholder) and
// System:APReceived (an item from another player) are replaced at runtime with text naming the item and the
// player. Two game functions are hooked: the MSBT label lookup, to learn which (file, index) pairs are ours,
// and the MSBT text getter, to hand out our text for them.
//
// The found text names the location of the last placeholder added while the player was busy. Most flows add the
// placeholder before its item-get (see the patcher). The Seashell Mansion's presents add it after, so the module
// reads the location from the present when it pops.
#include "AP/ap.hpp"
#include "AP/ap_list.hpp"
#include "Game/sym.hpp"
#include "loggers.hpp"
#include "Config/file_handler.hpp"
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>

namespace {
    using Game::MessageRef;
    using Game::MsbtText;
    // ShellMansionMaster.PopBox, at the call that creates the present: x3 = item key view, w4 = item index.
    constexpr uintptr_t kMansionPopBoxCreate = 0xa35994;

    // Toasts reuse the autosave popup (Ld_ThrobberSave, "Now Saving"). Its UI object runs a state machine at
    // +0x80 (state byte at +0x8d: 0 None, 1 In, 2 Loop, 3 Out, 4 InAuto, 5 LoopAuto); LoopAuto counts the float
    // at +0x290 down and slides out at zero (Throbber_Show, Throbber_ChangeState). The text pane holder at +0x278
    // shows the message UI:SystemSaving.
    constexpr size_t kThrobberMachine = 0x80;
    constexpr size_t kThrobberState = 0x8d;
    constexpr size_t kThrobberPane = 0x278;
    constexpr size_t kThrobberTimer = 0x290;
    constexpr int kStateNone = 0, kStateInAuto = 4, kStateLoopAuto = 5;
    constexpr float kToastSeconds = 3.0f;
    constexpr const char* kSavingLabel = "SystemSaving";

    u8* g_Throbber = nullptr;       // the popup's UI object, seen by the state hook
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

    enum class Kind { None, Found, Received, Saving, MapCaption, MapPlace, MapPin, Sign };
    constexpr const char* kMapCaptionLabel = "MapCaptionMemory";  // the map's X button: memories
    constexpr const char* kMapPlaceLabel = "MapCaptionPlace";     // Y: hide markers
    constexpr const char* kMapPinLabel = "MapCaptionPin";         // A: pin
    struct Slot { void* msbt; s32 index; Kind kind; };
    Slot g_Slots[16];
    int g_SlotCount = 0;
    int g_FindCalls = 0, g_GetCalls = 0;

    // Place.msbt labels of the map tracker's icons: (msbt, index) -> icon group
    std::map<std::pair<void*, s32>, int> g_MapLabels;
    char g_MapText[512];
    char g_CaptionText[64];

    std::map<int, std::pair<std::string, std::string>> g_LocationNames;
    int g_FoundLocation = -1;
    bool g_FoundFresh = false;      // g_FoundLocation was set during the current event
    int g_PresentLocation = -1;     // the popped mansion present holding a placeholder
    std::string g_RecvItem, g_RecvSender;
    char g_FoundText[512];
    char g_RecvText[512];

    // UTF-8 MSBT tags: 0x0E, group, type, parameter size (u16 each), parameters.
    const std::string kColorItem("\x0e\x00\x00\x03\x00\x04\x00\xd9\x45\x6d\xff", 11);   // as in native item texts
    const std::string kColorPlayer("\x0e\x00\x00\x03\x00\x04\x00\x62\xcf\xff\xff", 11); // sky blue
    const std::string kColorWhite("\x0e\x00\x00\x03\x00\x04\x00\xed\xed\xed\xff", 11);
    const std::string kWaitInput("\x0e\x01\x00\x04\x00\x00\x00", 7);
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

    size_t Store(char* buf, size_t cap, const std::string& text) {
        size_t n = std::min(text.size(), cap - 1);
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
        return n;
    }

    Kind KindOf(void* msbt, s32 index) {
        for (int i = 0; i < g_SlotCount; i++) {
            if (g_Slots[i].msbt == msbt && g_Slots[i].index == index) return g_Slots[i].kind;
        }
        return Kind::None;
    }

    void Remember(void* msbt, s32 index, Kind kind) {
        if (index < 0 || KindOf(msbt, index) != Kind::None) return;
        if (g_SlotCount == 16) g_SlotCount = 0;  // message files are reloaded on language change
        g_Slots[g_SlotCount++] = Slot{msbt, index, kind};
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
}

HOOK_DEFINE_TRAMPOLINE(MsbtFindLabel) {
    static void Callback(MessageRef* out, void* msbt, const char* label, size_t len) {
        Orig(out, msbt, label, len);
        g_FindCalls++;
#ifdef LASAP_TEST
        SeeLabel(msbt, out->index, label, len);
#endif
        int group = AP::MapLabelGroup(std::string_view(label, len));
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
        else if (LabelIs(label, len, AP::SIGN_LABEL)) Remember(msbt, out->index, Kind::Sign);
        else return;
        Logging.Log("[AP] text label " + std::string(label, len) + " index " + std::to_string(out->index));
    }
};

HOOK_DEFINE_TRAMPOLINE(MsbtGetText) {
    static void Callback(MsbtText* out, void* msbt, s32 index) {
        Orig(out, msbt, index);
        g_GetCalls++;
        Resolve(out, msbt, index);
#ifdef LASAP_TEST
        SeeText(msbt, index, out->text, out->length);
#endif
    }

    // Hands out our text for the messages that are ours.
    static void Resolve(MsbtText* out, void* msbt, s32 index) {
        if (out->text == nullptr) return;
        auto map = g_MapLabels.find({msbt, index});
        if (map != g_MapLabels.end()) {
            std::string text = AP::MapLabelText(map->second);
#ifdef LASAP_TEST
            AP::NoteMapText(text);
#endif
            if (!text.empty()) {
                out->length = Store(g_MapText, sizeof(g_MapText), text);
                out->text = g_MapText;
            }
            return;
        }
        Kind kind = KindOf(msbt, index);
        switch (kind) {
            case Kind::Found: {
                auto it = g_LocationNames.find(g_FoundFresh ? g_FoundLocation : g_PresentLocation);
                if (it == g_LocationNames.end()) return;  // unknown: keep "You found an Archipelago item!"
                const auto& [item, player] = it->second;
                std::string t = player.empty()
                    ? "You found your " + kColorItem + Clip(item, kItemNameMax) + kColorWhite + "!"
                    : "You found " + kColorItem + Clip(item, kItemNameMax) + kColorWhite + "\nfor " +
                      kColorPlayer + Clip(player, kPlayerNameMax) + kColorWhite + "!";
                out->length = Store(g_FoundText, sizeof(g_FoundText), t + kWaitInput);
                out->text = g_FoundText;
                break;
            }
            case Kind::Received: {
                std::string t = "You got " + kColorItem + Clip(g_RecvItem, kItemNameMax) + kColorWhite;
                t += g_RecvSender.empty() ? std::string("!")
                                          : "\nfrom " + kColorPlayer + Clip(g_RecvSender, kPlayerNameMax) + kColorWhite + "!";
                out->length = Store(g_RecvText, sizeof(g_RecvText), t + kWaitInput);
                out->text = g_RecvText;
                break;
            }
            case Kind::Saving:
                if (g_PaneText != nullptr) {
                    out->length = Store(g_PaneBuf, sizeof(g_PaneBuf), *g_PaneText);
                    out->text = g_PaneBuf;
                    return;
                }
                if (!g_ToastText) return;
                out->length = Store(g_ToastBuf, sizeof(g_ToastBuf), g_Toast);
                out->text = g_ToastBuf;
                break;
            case Kind::MapCaption:
            case Kind::MapPlace:
            case Kind::MapPin: {
                // the map's X, Y and A captions; the Jukebox's guide, made from the map's, uses them too
                int button = kind == Kind::MapCaption ? 0 : kind == Kind::MapPlace ? 1 : 2;
                std::string text = AP::ListUi::GuideCaption(button);
                if (text.empty() && kind == Kind::MapCaption) text = AP::MapCaptionText();
                if (text.empty()) return;
                // The caption starts with a tag drawing the button (0x0E, group, type, parameter size, parameters).
                s64 tags = 0;
                while (tags + 7 <= out->length && out->text[tags] == 0x0e) {
                    tags += 7 + static_cast<u8>(out->text[tags + 5]) + (static_cast<u8>(out->text[tags + 6]) << 8);
                }
                tags = std::min(tags, out->length);
                out->length = Store(g_CaptionText, sizeof(g_CaptionText), std::string(out->text, tags) + text);
                out->text = g_CaptionText;
                break;
            }
            case Kind::Sign:
                AP::OnSignRead();
                break;
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
        Game::MessageText_SetLabel(g_Throbber + kThrobberPane, &view);
        g_ToastText = false;
    }
}

HOOK_DEFINE_TRAMPOLINE(ThrobberSetState) {
    static void Callback(void* machine, s32 state) {
        Orig(machine, state);
        g_Throbber = reinterpret_cast<u8*>(machine) - kThrobberMachine;
        if (g_ToastStarting && state == kStateInAuto) {
            // our ShowThrobber call: show the toast's text for its time
            *reinterpret_cast<float*>(g_Throbber + kThrobberTimer) = kToastSeconds;
            SetThrobberText(true);
            g_ToastShowing = true;
        } else if (g_ToastShowing && state == kStateNone) {
            SetThrobberText(false);
            g_ToastShowing = false;
        }
    }
};

HOOK_DEFINE_INLINE(MansionPopBox) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        auto* key = reinterpret_cast<const Game::StrView*>(ctx->X[3]);
        int index = static_cast<int>(ctx->X[4]);
        size_t len = std::strlen(AP::PLACEHOLDER_KEY);
        // The placeholder's key, or a model variant of it (ArchipelagoItem_<item key>).
        if (key != nullptr && key->data != nullptr && key->size >= len &&
            std::memcmp(key->data, AP::PLACEHOLDER_KEY, len) == 0) {
            g_PresentLocation = index;
            Logging.Log("[AP] present holds location " + std::to_string(index));
        }
    }
};

namespace AP {
    void InstallTextHooks() {
        MsbtFindLabel::InstallAtFuncPtr(Game::Msbt_FindLabel.Ptr());
        MsbtGetText::InstallAtFuncPtr(Game::Msbt_GetText.Ptr());
        MansionPopBox::InstallAtPtr(Game::Addr(kMansionPopBoxCreate));
        ThrobberSetState::InstallAtFuncPtr(Game::Throbber_ChangeState.Ptr());
    }

    void SetPaneText(void* holder, const std::string& text) {
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
            (g_Throbber[kThrobberState] == kStateInAuto || g_Throbber[kThrobberState] == kStateLoopAuto)) {
            g_Toast = text;
            SetThrobberText(true);
            *reinterpret_cast<float*>(g_Throbber + kThrobberTimer) = kToastSeconds;
            return;
        }
        g_MusicToast = text;
    }

    void TickToasts(bool inLevel) {
        if (g_ToastShowing || (g_Toasts.empty() && g_MusicToast.empty()) || !inLevel) return;
        // ShowThrobber does nothing while the popup is busy (a real save); the toast then waits
        if (g_Throbber != nullptr && g_Throbber[kThrobberState] != kStateNone) return;
        bool music = g_Toasts.empty();
        g_Toast = music ? g_MusicToast : g_Toasts.front();
        g_ToastStarting = true;
        Game::Throbber_Show(true);
        g_ToastStarting = false;
        if (g_ToastShowing) {
            g_ToastMusic = music;
            if (music) {
                g_MusicToast.clear();
            } else {
                g_Toasts.pop_front();
            }
            Logging.Log("[AP] toast: " + g_Toast);
        }
    }

    void SetLocationName(int local_id, const std::string& item, const std::string& player) {
        g_LocationNames[local_id] = {item, player};
    }

    std::string TextState() {
        return "TEXT names=" + std::to_string(g_LocationNames.size()) + " slots=" + std::to_string(g_SlotCount) +
               " found=" + std::to_string(g_FoundLocation) + " finds=" + std::to_string(g_FindCalls) +
               " gets=" + std::to_string(g_GetCalls) + " present=" + std::to_string(g_PresentLocation);
    }

#ifdef LASAP_TEST
    std::string SeenText(const std::string& label) {
        auto it = g_SeenTexts.find(label);
        return it == g_SeenTexts.end() ? "TEXT " + label + " unseen" : "TEXT " + label + " said=" + it->second;
    }
#endif

    bool HasLocationName(int local_id) {
        return g_LocationNames.count(local_id) != 0;
    }

    void SetFoundLocation(int local_id) {
        g_FoundLocation = local_id;
        g_FoundFresh = true;
        if (local_id == g_PresentLocation) g_PresentLocation = -1;
    }

    void ExpireFoundLocation() {
        g_FoundFresh = false;
    }

    std::string ReceivedToast(const std::string& item, const std::string& sender) {
        std::string from = sender.empty() ? "" : "\nfrom " + Clip(sender, kPlayerNameMax);
        return "You got " + Clip(item, kItemNameMax) + from;
    }

    std::string FoundToast(int local_id) {
        auto it = g_LocationNames.find(local_id);
        if (it == g_LocationNames.end()) return "";
        const auto& [item, player] = it->second;
        return "You found " + Clip(item, kItemNameMax) + (player.empty() ? "" : "\nfor " + Clip(player, kPlayerNameMax));
    }

    std::string FoundToast() {
        return FoundToast(g_FoundFresh ? g_FoundLocation : g_PresentLocation);
    }

    bool ToastsIdle() {
        return !g_ToastShowing && g_Toasts.empty();
    }

    bool ToastShowing() { return g_ToastShowing; }

    void SetReceivedText(const std::string& item, const std::string& sender) {
        g_RecvItem = item;
        g_RecvSender = sender;
    }

    void LoadLocationNames() {
        // Written by the patcher: one "<local id> <item name>|<player name, empty for an own item>" per line, so
        // found items are named before the client connects. NAME lines from the client replace them.
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(NAMES_PATH, text))) {
            Logging.Log("[AP] no location names file");
            return;
        }
        size_t start = 0;
        int count = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(start, end - start);
            start = end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t space = line.find(' ');
            if (space == std::string::npos) continue;
            std::string names = line.substr(space + 1);
            size_t bar = names.find('|');
            SetLocationName(atoi(line.substr(0, space).c_str()), names.substr(0, bar),
                            bar == std::string::npos ? "" : names.substr(bar + 1));
            count++;
        }
        Logging.Log("[AP] location names: " + std::to_string(count));
    }
}
