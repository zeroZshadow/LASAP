// Map tracker (DESIGN.md, Map tracker): the pause map shows the slot's unchecked locations, and the whole overworld
// and every dungeon room are revealed.
//
// The patcher adds a UiFieldMapIcons row per overworld spot holding checks (a group). The map shows a row's icon
// while the event flag named in the row is set; those flags (MAP_FLAG_PREFIX<group>) are answered here: set while a
// location of the group is unchecked. Hovering an icon shows Place:MAP_FLAG_PREFIX<group>, whose text names the
// unchecked locations (ap_text.cpp). The overworld's areas show once their UI_FieldMapTraverse_<area> flag is set
// (UI_FieldMapOpen_<area> once the reveal has played); both read as set.
//
// In dungeons the map draws the rooms Link visited and, with the compass, the chests (open or closed). Both come
// from the dungeon's DungeonItems entry: its visited-room mask is filled while Link is in the dungeon (the rooms then
// count as visited in the save), and an inline hook makes the compass and map owned for the map screen only, so the
// items keep their use. Dungeon checks that are not chests get UiDungeonIcon rows (chest icons) from the patcher;
// the icon's update shows them open once all of the room's checks are done.
#include "AP/ap.hpp"
#include "offsets.hpp"
#include "symbols.hpp"
#include "loggers.hpp"
#include "Config/file_handler.hpp"
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

    constexpr const char* kTraversePrefix = "UI_FieldMapTraverse_";
    constexpr const char* kOpenPrefix = "UI_FieldMapOpen_";

    struct DungeonGroup {
        float x, z;
        std::vector<int> ids;
    };

    bool g_Loaded = false;
    bool g_Enabled = false;  // loaded and not switched off for a test (DBG map 0)
    std::vector<std::vector<int>> g_Overworld;  // group -> local ids
    std::vector<DungeonGroup> g_Dungeon;
    std::map<int, std::string> g_Names;         // local id -> location name
    int g_FlagsCalls = 0, g_IconCalls = 0, g_IconMatches = 0;  // for DBG map

    bool StartsWith(std::string_view s, const char* prefix) {
        return s.substr(0, std::strlen(prefix)) == prefix;
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

HOOK_DEFINE_TRAMPOLINE(DungeonIconUpdate) {
    static void Callback(u8* icon, const u8* actor) {
        Orig(icon, actor);
        g_IconCalls++;
        if (!g_Enabled || actor[kActorShownKind] != kSwitchAlways) return;
        float x = *reinterpret_cast<const float*>(actor + kActorPosX);
        float z = *reinterpret_cast<const float*>(actor + kActorPosZ);
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
        // Written by the patcher (ap_map.py): "O <group> <local id>...", "D <group> <x> <z> <local id>..." and
        // "N <local id> <location name>" lines.
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
            if (kind == "N") {
                int id;
                ss >> id;
                std::string name;
                std::getline(ss, name);
                g_Names[id] = name.empty() ? name : name.substr(1);
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
        Logging.Log("[AP] map tracker: " + std::to_string(g_Overworld.size()) + " overworld and " +
                    std::to_string(g_Dungeon.size()) + " dungeon icons");
    }

    bool MapFlag(std::string_view name, bool& value) {
        if (!g_Enabled || name.size() < 6) return false;
        if (StartsWith(name, MAP_FLAG_PREFIX)) {
            size_t group = std::strtoul(std::string(name.substr(std::strlen(MAP_FLAG_PREFIX))).c_str(), nullptr, 10);
            value = group < g_Overworld.size() && AnyUnchecked(g_Overworld[group]);
            return true;
        }
        if (StartsWith(name, kTraversePrefix) || StartsWith(name, kOpenPrefix)) {
            value = true;
            return true;
        }
        return false;
    }

    int MapLabelGroup(std::string_view label) {
        if (!g_Enabled || !StartsWith(label, MAP_FLAG_PREFIX)) return -1;
        return std::atoi(std::string(label.substr(std::strlen(MAP_FLAG_PREFIX))).c_str());
    }

    std::string MapLabelText(int group) {
        // The unchecked locations of the group on one line (the hover box has one), as many as fit, then "+<rest>".
        constexpr size_t kWidth = 36;
        std::vector<std::string> names;
        if (group >= 0 && static_cast<size_t>(group) < g_Overworld.size()) {
            for (int id : g_Overworld[group]) {
                if (!IsLocationChecked(id)) names.push_back(g_Names.count(id) ? g_Names[id] : std::to_string(id));
            }
        }
        std::string text;
        size_t shown = 0;
        for (; shown < names.size(); shown++) {
            std::string next = shown == 0 ? names[0] : text + ", " + names[shown];
            if (shown > 0 && next.size() > kWidth) break;
            text = next;
        }
        if (shown < names.size()) text += " +" + std::to_string(names.size() - shown);
        return text;
    }

    void TickMap() {
        if (!g_Enabled) return;
        u8 dungeon = *reinterpret_cast<u8*>(g_BaseAddress + kDungeonIndex);
        u8** entries = *reinterpret_cast<u8***>(g_BaseAddress + kDungeonItems);
        if (dungeon == 0xff || entries == nullptr || entries[dungeon] == nullptr) return;
        *reinterpret_cast<u64*>(entries[dungeon] + kDungeonVisited) = ~0ULL;
    }

    void SetMapEnabled(bool enabled) {
        g_Enabled = g_Loaded && enabled;
    }

    std::string MapState() {
        size_t open = 0, total = 0;
        for (const auto& ids : g_Overworld) {
            total++;
            if (AnyUnchecked(ids)) open++;
        }
        return "map=" + std::string(g_Enabled ? "1" : "0") + " icons=" + std::to_string(open) + "/" +
               std::to_string(total) + " hooks=" + std::to_string(g_FlagsCalls) + "," + std::to_string(g_IconCalls) + "," + std::to_string(g_IconMatches);
    }
}
