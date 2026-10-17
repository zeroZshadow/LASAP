// Map tracker (DESIGN.md, Map tracker): the pause map shows the slot's unchecked locations, and the whole overworld
// and every dungeon room are revealed.
//
// The patcher adds four UiFieldMapIcons rows per overworld spot holding checks (a group), one per look. The map shows
// a row's icon while the event flag named in the row is set; those flags are answered here. The flag is
// MAP_FLAG_PREFIX<group> for the in-logic look and MAP_FLAG_PREFIX<mark><group> for the others (MAP_LOOK_MARKS); the
// group's look is set and the others are not. In logic: every unchecked location of the group is in logic, or what is
// in logic is not known yet (the client sends it); partly in logic: some are; out of logic: none is; collected: all
// are checked. Hovering an icon shows Place:<its flag>, whose text names the unchecked locations, or the collected
// ones (ap_text.cpp). The map's X button, which opens the memories, instead hides or shows the out-of-logic icons
// ("Toggle out of logic"): those icons are always made, and hidden after the map adds them while they are toggled
// off; the toggle shows or hides the page's markers in place. The overworld's areas show once their UI_FieldMapTraverse_<area> flag is set
// (UI_FieldMapOpen_<area> once the reveal has played); both read as set.
//
// In dungeons the map draws the rooms Link visited and, with the compass, the chests (open or closed). Both come
// from the dungeon's DungeonItems entry: its visited-room mask is filled while Link is in the dungeon (the rooms then
// count as visited in the save), and an inline hook makes the compass and map owned for the map screen only, so the
// items keep their use. The icon update shows a chest open once its location is checked, also on the server. Dungeon
// checks that are not chests get UiDungeonIcon rows (chest icons) from the patcher; the icon update shows them open
// once all of the room's checks are done.
#include "AP/ap.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include "loggers.hpp"
#include "Config/file_handler.hpp"
#include "Hooks/Common/level.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // Dungeon map screen (game 1.0.0)
    constexpr uintptr_t kDungeonMapFlags = 0xe76204;    // w8 = DungeonItems flags byte (bit0 compass, bit1 map)
    // DungeonItems: per dungeon a pointer to its entry (flags byte at +1, visited-room mask u64 at +8, bit col + 8·row)
    constexpr uintptr_t kDungeonItems = 0x1cc11b0;
    constexpr uintptr_t kDungeonIndex = 0x1cc13c8;     // u8: the current dungeon, 0xff outside dungeons
    constexpr size_t kDungeonVisited = 8;
    constexpr uintptr_t kDungeonIconUpdate = 0xf38040;  // void(icon*, const actor*): visibility and open state
    // The actor an icon stands for: position, and switches (kind u8, index u16) for "shown" and "open". Icons from
    // UiDungeonIcon rows have the "shown" switch of kind 4 (always set).
    constexpr size_t kActorPosX = 0x4, kActorPosZ = 0xc, kActorShownKind = 0x1e;
    constexpr u8 kSwitchAlways = 4;
    // The icon's two state panes (open, closed); their virtuals set visibility and play the state.
    constexpr size_t kIconOpenPane = 0x14 * 8, kIconClosedPane = 0x17 * 8;
    constexpr size_t kPaneSetVisible = 0x20, kPanePlay = 0xc0;
    using PaneSetVisibleFn = void (*)(void*, bool);
    using PanePlayFn = void (*)(void*, float);

    // Field map (game 1.0.0)
    // The X button, which opens the memories: in the overworld map's KeyWait state w21 = X pressed (x19 page), and
    // the dungeon map's check bool(page*).
    constexpr uintptr_t kFieldMemoryButton = 0xe95278;
    constexpr uintptr_t kMemoryButton = 0xe87840;
    // In the map's icon setup, after a shown icon's marker joined the page's markers: x19 page, x26 marker.
    constexpr uintptr_t kMarkerAdded = 0xe917c8;
    constexpr size_t kPageMarkers = 0x710;          // std::vector<std::shared_ptr<marker>>: begin, end
    constexpr size_t kMarkerRow = 0x240;            // UiFieldMapIcons row; its show flag name view at +0x20
    constexpr size_t kRowFlagName = 0x20;
    constexpr size_t kMarkerLayout = 0x48;          // virtual: the marker's layout, root pane at +0x18
    constexpr size_t kLayoutPane = 0x18;
    constexpr size_t kPaneFlags = 0x58;             // bit 0 visible
    constexpr uintptr_t kPlaySound = 0xa57e0;       // void(const StrView* name, void*, float volume, float pitch)
    struct StrView { const char* data; s64 length; };
    using LayoutFn = u8* (*)(u8*);
    using PlaySoundFn = void (*)(const StrView*, void*, float, float);

    constexpr const char* kTraversePrefix = "UI_FieldMapTraverse_";
    constexpr const char* kOpenPrefix = "UI_FieldMapOpen_";

    struct DungeonGroup {
        float x, z;
        std::vector<int> ids;
    };
    struct Chest {
        std::string level;
        float x, z;
        int id;
    };

    bool g_Loaded = false;
    bool g_Enabled = false;  // loaded and not switched off for a test (DBG map 0)
    bool g_IconsHidden = false;  // revealed maps without overworld icons, for screenshots (DBG map 2)
    bool g_OutHidden = false;    // out-of-logic icons toggled off with the X button
    u8* g_MarkerPage = nullptr;  // the map page whose markers hold ours (the icon setup ran for it)
    std::vector<std::vector<int>> g_Overworld;  // group -> local ids
    std::vector<DungeonGroup> g_Dungeon;
    std::vector<Chest> g_Chests;
    std::vector<std::string> g_GroupNames;      // group -> its name
    int g_FlagsCalls = 0, g_IconCalls = 0, g_IconMatches = 0, g_ChestMatches = 0;  // for DBG map

    bool StartsWith(std::string_view s, const char* prefix) {
        return s.substr(0, std::strlen(prefix)) == prefix;
    }

    // InLogic, then the looks of MAP_LOOK_MARKS in their order.
    enum Look { InLogic, Partial, OutOfLogic, Collected, kLooks };

    bool Reachable(int id) {
        return !AP::HasLogic() || AP::IsInLogic(id);
    }

    Look GroupLook(const std::vector<int>& ids) {
        size_t left = 0, reachable = 0;
        for (int id : ids) {
            if (AP::IsLocationChecked(id)) continue;
            left++;
            if (Reachable(id)) reachable++;
        }
        if (left == 0) return Collected;
        return reachable == left ? InLogic : reachable > 0 ? Partial : OutOfLogic;
    }

    // The group and look a flag or label names (MAP_FLAG_PREFIX[mark]<group>), or -1.
    int ParseIcon(std::string_view name, Look& look) {
        if (!StartsWith(name, AP::MAP_FLAG_PREFIX)) return -1;
        name.remove_prefix(std::strlen(AP::MAP_FLAG_PREFIX));
        look = InLogic;
        const char* mark = name.empty() ? nullptr : std::strchr(AP::MAP_LOOK_MARKS, name[0]);
        if (mark != nullptr && *mark != '\0') {
            look = static_cast<Look>(InLogic + 1 + (mark - AP::MAP_LOOK_MARKS));
            name.remove_prefix(1);
        }
        if (name.empty() || name[0] < '0' || name[0] > '9') return -1;
        return std::atoi(std::string(name).c_str());
    }

    // The group and look of a map marker's row, or -1.
    int MarkerIcon(u8* marker, Look& look) {
        const u8* row = *reinterpret_cast<u8**>(marker + kMarkerRow);
        if (row == nullptr) return -1;
        auto* name = reinterpret_cast<const std::pair<const char*, size_t>*>(row + kRowFlagName);
        if (name->first == nullptr) return -1;
        return ParseIcon(std::string_view(name->first, name->second), look);
    }

    void SetMarkerVisible(u8* marker, bool visible) {
        void** vtable = *reinterpret_cast<void***>(marker);
        u8* layout = reinterpret_cast<LayoutFn>(vtable[kMarkerLayout / 8])(marker);
        u8* pane = *reinterpret_cast<u8**>(layout + kLayoutPane);
        pane[kPaneFlags] = (pane[kPaneFlags] & ~1) | (visible ? 1 : 0);
    }

    // Shows or hides the page's out-of-logic markers as toggled.
    void ApplyOutHidden(u8* page) {
        auto** marker = *reinterpret_cast<u8***>(page + kPageMarkers);
        auto** end = *reinterpret_cast<u8***>(page + kPageMarkers + 8);
        for (; marker < end; marker += 2) {  // shared_ptr: object, control block
            Look look;
            if (*marker != nullptr && MarkerIcon(*marker, look) >= 0 && look == OutOfLogic) {
                SetMarkerVisible(*marker, !g_OutHidden);
            }
        }
    }

    void PlaySound(const char* name) {
        StrView view{name, static_cast<s64>(std::strlen(name))};
        reinterpret_cast<PlaySoundFn>(g_BaseAddress + offset_manager.Offset(kPlaySound))(&view, nullptr, 1.0f, 1.0f);
    }

    void ToggleOutOfLogic(u8* page) {
        g_OutHidden = !g_OutHidden;
        if (page == g_MarkerPage) ApplyOutHidden(page);
        PlaySound(g_OutHidden ? "SE_SY_MAP_PIN_OFF" : "SE_SY_MAP_PIN_ON");
    }

    bool AnyUnchecked(const std::vector<int>& ids) {
        for (int id : ids) {
            if (!AP::IsLocationChecked(id)) return true;
        }
        return false;
    }

    void SetPane(u8* icon, size_t offset, bool visible) {
        void* pane = *reinterpret_cast<void**>(icon + offset);
        void** vtable = *reinterpret_cast<void***>(pane);
        reinterpret_cast<PaneSetVisibleFn>(vtable[kPaneSetVisible / 8])(pane, visible);
        if (visible) reinterpret_cast<PanePlayFn>(vtable[kPanePlay / 8])(pane, 1.0f);
    }
}

HOOK_DEFINE_INLINE(DungeonMapFlags) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        g_FlagsCalls++;
        if (g_Enabled) ctx->W[8] |= 3;  // compass and map
    }
};

HOOK_DEFINE_INLINE(MarkerAdded) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        g_MarkerPage = reinterpret_cast<u8*>(ctx->X[19]);
        u8* marker = reinterpret_cast<u8*>(ctx->X[26]);
        Look look;
        if (g_OutHidden && MarkerIcon(marker, look) >= 0 && look == OutOfLogic) SetMarkerVisible(marker, false);
    }
};

HOOK_DEFINE_INLINE(FieldMemoryButton) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) {
        if (ctx->W[21] == 0 || !g_Enabled) return;
        ToggleOutOfLogic(reinterpret_cast<u8*>(ctx->X[19]));
        ctx->W[21] = 0;  // no memories
    }
};

HOOK_DEFINE_TRAMPOLINE(MemoryButton) {
    static bool Callback(u8* page) {
        bool pressed = Orig(page);
        if (!pressed || !g_Enabled) return pressed;
        ToggleOutOfLogic(page);
        return false;  // no memories
    }
};

HOOK_DEFINE_TRAMPOLINE(DungeonIconUpdate) {
    static void Callback(u8* icon, const u8* actor) {
        Orig(icon, actor);
        g_IconCalls++;
        if (!g_Enabled) return;
        float x = *reinterpret_cast<const float*>(actor + kActorPosX);
        float z = *reinterpret_cast<const float*>(actor + kActorPosZ);
        if (actor[kActorShownKind] != kSwitchAlways) {
            // A room actor's icon: a chest whose location is checked shows open, as when it is opened in the game.
            if (g_Chests.empty()) return;
            std::string level = LevelSystem::GetCurrentLevel();
            for (const auto& chest : g_Chests) {
                if (chest.level == level && std::fabs(chest.x - x) < 0.01f && std::fabs(chest.z - z) < 0.01f &&
                    AP::IsLocationChecked(chest.id)) {
                    g_ChestMatches++;
                    SetPane(icon, kIconClosedPane, false);
                    SetPane(icon, kIconOpenPane, true);
                    return;
                }
            }
            return;
        }
        for (const auto& group : g_Dungeon) {
            if (std::fabs(group.x - x) < 0.01f && std::fabs(group.z - z) < 0.01f) {
                g_IconMatches++;
                bool open = !AnyUnchecked(group.ids);
                SetPane(icon, open ? kIconClosedPane : kIconOpenPane, false);
                SetPane(icon, open ? kIconOpenPane : kIconClosedPane, true);
                return;
            }
        }
    }
};

namespace AP {
    void LoadMap() {
        // Written by the patcher (ap_map.py): "O <group> <local id>...", "D <group> <x> <z> <local id>...",
        // "C <level> <x> <z> <local id>" (a dungeon chest) and "G <group> <name>" (an overworld group's name) lines.
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(MAP_PATH, text))) {
            Logging.Log("[AP] no map tracker file");
            return;
        }
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::stringstream ss(line);
            std::string kind;
            ss >> kind;
            if (kind == "G") {
                size_t group;
                if (!(ss >> group)) continue;
                std::string name;
                std::getline(ss, name);
                if (g_GroupNames.size() <= group) g_GroupNames.resize(group + 1);
                g_GroupNames[group] = name.empty() ? name : name.substr(1);
                continue;
            }
            if (kind == "C") {
                Chest chest{};
                if (ss >> chest.level >> chest.x >> chest.z >> chest.id) g_Chests.push_back(chest);
                continue;
            }
            size_t group;
            if (!(ss >> group)) continue;
            std::vector<int> ids;
            if (kind == "O") {
                for (int id; ss >> id;) ids.push_back(id);
                if (g_Overworld.size() <= group) g_Overworld.resize(group + 1);
                g_Overworld[group] = ids;
            } else if (kind == "D") {
                DungeonGroup dg{};
                ss >> dg.x >> dg.z;
                for (int id; ss >> id;) dg.ids.push_back(id);
                g_Dungeon.push_back(dg);
            }
        }
        g_Loaded = g_Enabled = true;
        DungeonMapFlags::InstallAtOffset(offset_manager.Offset(kDungeonMapFlags));
        DungeonIconUpdate::InstallAtOffset(offset_manager.Offset(kDungeonIconUpdate));
        MarkerAdded::InstallAtOffset(offset_manager.Offset(kMarkerAdded));
        MemoryButton::InstallAtOffset(offset_manager.Offset(kMemoryButton));
        FieldMemoryButton::InstallAtOffset(offset_manager.Offset(kFieldMemoryButton));
        Logging.Log("[AP] map tracker: " + std::to_string(g_Overworld.size()) + " overworld and " +
                    std::to_string(g_Dungeon.size()) + " dungeon icons, " + std::to_string(g_Chests.size()) +
                    " chests");
    }

    bool MapFlag(std::string_view name, bool& value) {
        if (!g_Enabled || name.size() < 6) return false;
        Look look;
        int group = ParseIcon(name, look);
        if (group >= 0) {
            value = !g_IconsHidden && static_cast<size_t>(group) < g_Overworld.size() &&
                    GroupLook(g_Overworld[group]) == look;
            return true;
        }
        if (StartsWith(name, kTraversePrefix) || StartsWith(name, kOpenPrefix)) {
            value = true;
            return true;
        }
        return false;
    }

    int MapLabelGroup(std::string_view label) {
        Look look;
        return g_Enabled ? ParseIcon(label, look) : -1;
    }

    std::string MapLabelText(int group) {
        // The group's name, after "<in logic>/<unchecked> " when it has several locations and some are unchecked.
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return "";
        std::string name = static_cast<size_t>(group) < g_GroupNames.size() ? g_GroupNames[group] : "";
        const auto& ids = g_Overworld[group];
        int unchecked = 0, reachable = 0;
        for (int id : ids) {
            if (IsLocationChecked(id)) continue;
            unchecked++;
            if (Reachable(id)) reachable++;
        }
        if (ids.size() < 2 || unchecked == 0) return name;
        return std::to_string(reachable) + "/" + std::to_string(unchecked) + " " + name;
    }

    void TickMap() {
        if (!g_Enabled) return;
        u8 dungeon = *reinterpret_cast<u8*>(g_BaseAddress + kDungeonIndex);
        u8** entries = *reinterpret_cast<u8***>(g_BaseAddress + kDungeonItems);
        if (dungeon == 0xff || entries == nullptr || entries[dungeon] == nullptr) return;
        *reinterpret_cast<u64*>(entries[dungeon] + kDungeonVisited) = ~0ULL;
    }

    std::string MapCaptionText() {
        // Offline, the looks are those of the last logic the client sent, or all in logic before it sent any.
        if (!g_Enabled) return "";
        if (AP::IsServerConnected()) return "Toggle out of logic";
        return AP::HasLogic() ? "Offline: toggle out of logic" : "Offline: logic unknown";
    }

    void SetMapMode(int mode) {
        g_Enabled = g_Loaded && mode != 0;
        g_IconsHidden = mode == 2;
    }

    std::string MapState() {
        size_t looks[kLooks] = {};
        for (const auto& ids : g_Overworld) looks[GroupLook(ids)]++;
        size_t open = g_Overworld.size() - looks[Collected];
        return "map=" + std::string(g_Enabled ? "1" : "0") + " icons=" + std::to_string(open) + "/" +
               std::to_string(g_Overworld.size()) + " looks=" + std::to_string(looks[InLogic]) + "," +
               std::to_string(looks[Partial]) + "," + std::to_string(looks[OutOfLogic]) + "," +
               std::to_string(looks[Collected]) + " logic=" + (HasLogic() ? "1" : "0") +
               " server=" + (IsServerConnected() ? "1" : "0") +
               " hooks=" + std::to_string(g_FlagsCalls) + "," + std::to_string(g_IconCalls) + "," +
               std::to_string(g_IconMatches) + "," + std::to_string(g_ChestMatches) + " out=" +
               (g_OutHidden ? "hidden" : "shown");
    }
}
