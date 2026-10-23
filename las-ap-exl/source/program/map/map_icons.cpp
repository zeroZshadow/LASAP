// The map tracker's overworld icons (DESIGN.md, Map tracker): their looks, hover texts and buttons.
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"
#include "map/map_tracker_impl.hpp"
#include <cstdlib>
#include <cstring>

namespace AP::MapTracker {
    bool g_OutHidden = false;
    Map::OverworldMap* g_MarkerPage = nullptr;

namespace {
    using Game::Ui::PlaySound;

    // Field map (game 1.0.0)
    // The X button, which opens the memories: in the overworld map's KeyWait state w21 = X pressed (x19 page); the
    // dungeon map's is DungeonMap_MemoriesButton (bool(page*)).
    constexpr uintptr_t kFieldMemoryButton = 0xe95278;
    // In the overworld map's KeyWait state, after it reads A (w0, page x19), which opens the pin menu. The page's
    // hovered marker is set later in the same update, so it is the last frame's.
    constexpr uintptr_t kFieldPinButton = 0xe950f4;
    // In the map's icon setup, after a shown icon's marker joined the page's markers: x19 page, x26 marker.
    constexpr uintptr_t kMarkerAdded = 0xe917c8;

    // Whether a made icon shows: its look is the group's, and it is not an out-of-logic icon toggled off.
    bool LookShown(int group, Look look) {
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return false;
        return GroupLook(group) == look && !(look == OutOfLogic && g_OutHidden);
    }

    void SetMarkerVisible(Map::MapMarker* marker, bool visible) { Ui::SetVisible(MarkerPane(marker), visible); }

    // A on the overworld map: marks the hovered icon's location skipped, or not. False (the game takes the press)
    // unless the icon is one of a single unchecked location.
    bool ToggleSkipped(Map::OverworldMap* page) {
        Look look;
        int group = HoveredIcon(page, look);
        if (group < 0 || static_cast<size_t>(group) >= g_Overworld.size()) return false;
        const auto& ids = g_Overworld[group];
        if (!Skippable(ids)) return false;
        AP::SetLocationSkipped(ids[0], !AP::IsLocationSkipped(ids[0]));
        if (page == g_MarkerPage) ApplyLooks(page);
        PlaySound("SE_SY_DECIDE");
        return true;
    }

    HOOK_DEFINE_INLINE(MarkerAdded) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            g_MarkerPage = reinterpret_cast<Map::OverworldMap*>(ctx->X[19]);
            auto* marker = reinterpret_cast<Map::MapMarker*>(ctx->X[26]);
            Look look;
            int group = MarkerIcon(marker, look);
            if (group >= 0 && !LookShown(group, look)) SetMarkerVisible(marker, false);
        }
    };

    HOOK_DEFINE_INLINE(FieldPinButton) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            auto* page = reinterpret_cast<Map::OverworldMap*>(ctx->X[19]);
            if (g_Panel.page != nullptr) {
                TickList(&page->wrapper);
                ctx->W[0] = 0;
                return;
            }
            if (!g_Enabled || g_IconsHidden) return;
    #ifdef LASAP_TEST
            if (TestFieldPin(page)) {
                ctx->W[0] = 0;
                return;
            }
    #endif
            if ((ctx->W[0] & 1) == 0) return;
            Look hovered;
            int group = HoveredIcon(page, hovered);
            if (Listable(group)) {
                OpenList(&page->wrapper, g_Overworld[group], g_GroupNames[group], group);
                ctx->W[0] = 0;  // no pin menu
            } else if (ToggleSkipped(page)) {
                ctx->W[0] = 0;
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(MapPageDtor) {
        static void Callback(Map::OverworldMap* page) {
            if (&page->wrapper == g_Panel.page) DropList();
            if (page == g_MarkerPage) g_MarkerPage = nullptr;
            Orig(page);
        }
    };

    HOOK_DEFINE_INLINE(FieldMemoryButton) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            if (ctx->W[21] == 0 || !g_Enabled) return;
            ToggleOutOfLogic(reinterpret_cast<const void*>(ctx->X[19]));
            ctx->W[21] = 0;  // no memories
        }
    };

    HOOK_DEFINE_TRAMPOLINE(MemoryButton) {
        static bool Callback(Map::DungeonMap* page) {
            bool pressed = Orig(page);
            if (!pressed || !g_Enabled) return pressed;
            ToggleOutOfLogic(page);
            return false;  // no memories
        }
    };
}

    bool StartsWith(std::string_view s, const char* prefix) {
        return s.substr(0, std::strlen(prefix)) == prefix;
    }

    bool Reachable(int id) {
        return !AP::HasLogic() || AP::IsInLogic(id);
    }

    // Checked, or marked skipped by the player: the map counts it as done.
    bool Done(int id) {
        return AP::IsLocationChecked(id) || AP::IsLocationSkipped(id);
    }

    // A group whose other name shows: a shuffled dungeon's until its flag is set.
    bool NameHidden(size_t group) {
        auto hidden = g_HiddenNames.find(group);
        return hidden != g_HiddenNames.end() && !EventFlags::CheckFlag(hidden->second.first);
    }

    // The look of an icon of these locations; none counts as in logic while its name is hidden, as its look would tell
    // which dungeon its door leads into.
    Look IdsLook(const std::vector<int>& ids, bool nameHidden) {
        size_t left = 0, reachable = 0;
        for (int id : ids) {
            if (Done(id)) continue;
            left++;
            if (!nameHidden && Reachable(id)) reachable++;
        }
        if (left == 0) return Collected;
        return reachable == left ? InLogic : reachable > 0 ? Partial : OutOfLogic;
    }

    Look GroupLook(size_t group) {
        return IdsLook(g_Overworld[group], NameHidden(group));
    }

    // A group A marks skipped or not: one location, not checked.
    bool Skippable(const std::vector<int>& ids) {
        return ids.size() == 1 && !AP::IsLocationChecked(ids[0]);
    }

    // Whether a group's icon in this look is made: the look shown, and every look skipping or a logic update can give
    // it while the map is open (collected and out of logic with an unchecked location, in logic too unless its name is
    // hidden, partly with two), so the open map swaps them.
    bool LookMade(size_t group, Look look) {
        if (GroupLook(group) == look) return true;
        size_t unchecked = 0;
        for (int id : g_Overworld[group]) unchecked += AP::IsLocationChecked(id) ? 0 : 1;
        bool hidden = NameHidden(group);
        switch (look) {
            case Collected:
            case OutOfLogic: return unchecked > 0;
            case InLogic: return unchecked > 0 && !hidden;
            case Partial: return unchecked > 1 && !hidden;
            default: return false;
        }
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

    Map::MapMarker* MarkerOf(const Game::SharedPtr& marker) { return static_cast<Map::MapMarker*>(marker.ptr); }

    // The group and look of a map marker's row, or -1.
    int MarkerIcon(const Map::MapMarker* marker, Look& look) {
        const Map::UiFieldMapIconsRow* row = marker->row;
        if (row == nullptr || row->showFlag.data == nullptr) return -1;
        return ParseIcon(row->showFlag.View(), look);
    }

    Ui::Pane* MarkerPane(Map::MapMarker* marker) {
        return Game::CallVirtual<Ui::Layout*()>(marker, Ui::LayoutWrapperVirtual::kLayout)->root;
    }

    // Shows the page's markers of ours whose look shows (LookShown) and hides the others.
    void ApplyLooks(Map::OverworldMap* page) {
        for (const Game::SharedPtr& sp : page->markers) {
            Map::MapMarker* marker = MarkerOf(sp);
            Look look;
            int group = marker != nullptr ? MarkerIcon(marker, look) : -1;
            if (group >= 0) SetMarkerVisible(marker, LookShown(group, look));
        }
    }

    // The page's hovered marker of ours, or null.
    Map::MapMarker* HoveredMarker(const Map::OverworldMap* page) {
        s32 index = page->hoveredMarker;
        if (index < 0 || static_cast<size_t>(index) >= page->markers.size()) return nullptr;
        return MarkerOf(page->markers[index]);
    }

    // The group and look of the page's hovered marker, or -1.
    int HoveredIcon(const Map::OverworldMap* page, Look& look) {
        Map::MapMarker* marker = HoveredMarker(page);
        return marker != nullptr ? MarkerIcon(marker, look) : -1;
    }

    // X on a map page (overworld or dungeon).
    void ToggleOutOfLogic(const void* page) {
        g_OutHidden = !g_OutHidden;
        if (page == g_MarkerPage) ApplyLooks(g_MarkerPage);
        if (page == g_DungeonPage) ApplyDots();
        PlaySound(g_OutHidden ? "SE_SY_MAP_PIN_OFF" : "SE_SY_MAP_PIN_ON");
    }

    // An icon's count, in its hover text and its check list's header: "<to do>/<unchecked>", its locations in logic
    // neither checked nor skipped over those not checked (skipping one lowers the first, not the second). Empty when
    // every location is checked.
    std::string CountText(const std::vector<int>& ids) {
        int unchecked = 0, todo = 0;
        for (int id : ids) {
            if (AP::IsLocationChecked(id)) continue;
            unchecked++;
            if (!Done(id) && Reachable(id)) todo++;
        }
        return unchecked == 0 ? "" : std::to_string(todo) + "/" + std::to_string(unchecked);
    }

    // An icon's hover text: its name after its count when it has several locations and some are unchecked, or after
    // "Skipped: " when its one location is skipped.
    std::string IconText(const std::vector<int>& ids, const std::string& name) {
        if (Skippable(ids) && AP::IsLocationSkipped(ids[0])) return "Skipped: " + name;
        std::string count = CountText(ids);
        if (ids.size() < 2 || count.empty()) return name;
        return count + " " + name;
    }

    void InstallIconHooks() {
        MarkerAdded::InstallAtPtr(Game::Addr(kMarkerAdded));
        MemoryButton::InstallAtFuncPtr(Game::DungeonMap_MemoriesButton.Ptr());
        FieldMemoryButton::InstallAtPtr(Game::Addr(kFieldMemoryButton));
        FieldPinButton::InstallAtPtr(Game::Addr(kFieldPinButton));
        MapPageDtor::InstallAtFuncPtr(Game::OverworldMap_Dtor.Ptr());
    }
}
