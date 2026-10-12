// In-game text for Archipelago items. The patched messages Place:ArchipelagoItem (a found placeholder) and
// System:APReceived (an item from another player) are replaced at runtime with text naming the item and the
// player. Two game functions are hooked: the MSBT label lookup, to learn which (file, index) pairs are ours,
// and the MSBT text getter, to hand out our text for them.
#include "AP/ap.hpp"
#include "offsets.hpp"
#include "loggers.hpp"
#include <cstring>
#include <map>
#include <string>

namespace {
    // MSBT label lookup: out = {msbt, refcount, message index}.
    struct MsbtHandle { void* msbt; void* ctrl; s32 index; };
    // MSBT text getter: out = {owner, refcount, text, length in bytes without the terminator}.
    struct MsbtText { void* owner; void* ctrl; const char* text; s64 length; };
    constexpr uintptr_t kMsbtFindLabel = 0x10aaf40;
    constexpr uintptr_t kMsbtGetText = 0x10aa5a0;

    enum class Kind { None, Found, Received };
    struct Slot { void* msbt; s32 index; Kind kind; };
    Slot g_Slots[16];
    int g_SlotCount = 0;
    int g_FindCalls = 0, g_GetCalls = 0;
    std::string g_LastLabel;

    std::map<int, std::pair<std::string, std::string>> g_LocationNames;
    int g_FoundLocation = -1;
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

    std::string Clip(const std::string& s, size_t max) {
        if (s.size() <= max) return s;
        size_t n = max;
        while (n > 0 && (static_cast<u8>(s[n]) & 0xc0) == 0x80) n--;  // keep UTF-8 sequences whole
        return s.substr(0, n);
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
}

HOOK_DEFINE_TRAMPOLINE(MsbtFindLabel) {
    static void Callback(MsbtHandle* out, void* msbt, const char* label, size_t len) {
        Orig(out, msbt, label, len);
        g_FindCalls++;
        g_LastLabel.assign(label, std::min<size_t>(len, 40));
        if (LabelIs(label, len, AP::FOUND_LABEL)) Remember(msbt, out->index, Kind::Found);
        else if (LabelIs(label, len, AP::RECEIVED_LABEL)) Remember(msbt, out->index, Kind::Received);
        else return;
        Logging.Log("[AP] text label " + std::string(label, len) + " index " + std::to_string(out->index));
    }
};

HOOK_DEFINE_TRAMPOLINE(MsbtGetText) {
    static void Callback(MsbtText* out, void* msbt, s32 index) {
        Orig(out, msbt, index);
        g_GetCalls++;
        if (out->text == nullptr) return;
        Kind kind = KindOf(msbt, index);
        if (kind != Kind::None) Logging.Log("[AP] text get index " + std::to_string(index) + " found " + std::to_string(g_FoundLocation));
        switch (kind) {
            case Kind::Found: {
                auto it = g_LocationNames.find(g_FoundLocation);
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
                std::string t = "You got " + kColorItem + Clip(g_RecvItem, kItemNameMax) + kColorWhite + "\nfrom " +
                                kColorPlayer + Clip(g_RecvSender, kPlayerNameMax) + kColorWhite + "!";
                out->length = Store(g_RecvText, sizeof(g_RecvText), t + kWaitInput);
                out->text = g_RecvText;
                break;
            }
            case Kind::None:
                break;
        }
    }
};

namespace AP {
    void InstallTextHooks() {
        MsbtFindLabel::InstallAtOffset(offset_manager.Offset(kMsbtFindLabel));
        MsbtGetText::InstallAtOffset(offset_manager.Offset(kMsbtGetText));
    }

    void SetLocationName(int local_id, const std::string& item, const std::string& player) {
        g_LocationNames[local_id] = {item, player};
    }

    std::string TextState() {
        return "TEXT names=" + std::to_string(g_LocationNames.size()) + " slots=" + std::to_string(g_SlotCount) +
               " found=" + std::to_string(g_FoundLocation) + " finds=" + std::to_string(g_FindCalls) +
               " gets=" + std::to_string(g_GetCalls) + " last=" + g_LastLabel;
    }

    bool HasLocationName(int local_id) {
        return g_LocationNames.count(local_id) != 0;
    }

    void SetFoundLocation(int local_id) {
        g_FoundLocation = local_id;
    }

    void SetReceivedText(const std::string& item, const std::string& sender) {
        g_RecvItem = item;
        g_RecvSender = sender;
    }
}
