// The map tracker's overworld icons (DESIGN.md, Map tracker): their looks, hover texts and buttons.
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "ap/ap_text.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"
#include "map/map_tracker_impl.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace AP::MapTracker {
    bool g_OutHidden = false;
    Map::OverworldMap* g_MarkerPage = nullptr;
    std::vector<Ring> g_Rings;

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
        if (group < 0 || static_cast<size_t>(group) >= Groups().size()) return false;
        return GroupLook(group) == look && !(look == OutOfLogic && g_OutHidden);
    }

    void SetMarkerVisible(Map::MapMarker* marker, bool visible) { Ui::SetVisible(MarkerPane(marker), visible); }

    // Each ring shows while its group's icon does and a hint still shows there.
    void ApplyRings() {
        for (Ring& ring : g_Rings) {
            bool shown = Ringed(ring.group) && LookShown(static_cast<int>(ring.group), GroupLook(ring.group));
            ShowRing(ring.marker.get(), GroupIds(ring.group).size() == 1, shown);
        }
    }

    // After the page's setup: a ring under the first marker of each group with a hint shown.
    void MakeRings(Map::OverworldMap* page) {
        g_Rings.clear();
        Ui::Pane* pinRoot = page->mapPin.pane;
        if (pinRoot == nullptr) return;
        std::vector<bool> ringed(Groups().size(), false);
        for (const Game::SharedPtr<Map::MapMarker>& sp : page->markers) {
            Map::MapMarker* marker = MarkerOf(sp);
            Look look;
            int group = marker != nullptr ? MarkerIcon(marker, look) : -1;
            if (group < 0 || static_cast<size_t>(group) >= ringed.size() || ringed[group] || !Ringed(group)) continue;
            ringed[group] = true;
            Game::Shared<Map::MapMarker> ring =
                MakeRing(&page->page.wrapper, pinRoot, MarkerRoot(&marker->part.wrapper));
            if (ring.get() != nullptr) g_Rings.push_back({static_cast<size_t>(group), std::move(ring)});
        }
        ApplyRings();
    }

    // A on the overworld map: marks the hovered icon's location skipped, or not. False (the game takes the press)
    // unless the icon is one of a single unchecked location.
    bool ToggleSkipped(Map::OverworldMap* page) {
        Look look;
        int group = HoveredIcon(page, look);
        if (group < 0 || static_cast<size_t>(group) >= Groups().size()) return false;
        Ids ids = GroupIds(group);
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
                TickList(&page->page.wrapper);
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
                OpenList(&page->page.wrapper, GroupIds(group), GroupName(group), group);
                ctx->W[0] = 0;  // no pin menu
            } else if (ToggleSkipped(page)) {
                ctx->W[0] = 0;
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(MapPageSetup) {
        static void Callback(Map::OverworldMap* page) {
            Orig(page);
            if (!g_Enabled || page != g_MarkerPage) return;
            MakeRings(page);
            if (!g_Rings.empty()) BuildHintStrip(&page->page.wrapper);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(MapPageDtor) {
        static void Callback(Map::OverworldMap* page) {
            if (&page->page.wrapper == g_Panel.page) DropList();
            DropHintStrip(&page->page.wrapper);
            if (page == g_MarkerPage) {
                g_Rings.clear();
                g_MarkerPage = nullptr;
            }
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

    bool Reachable(LocationId id) {
        return !AP::HasLogic() || AP::IsInLogic(id);
    }

    // A dungeon location whose dungeon's door can be reached.
    bool DoorReachable(LocationId id) {
        return !AP::HasLogic() || AP::IsDoorOpen(id);
    }

    bool Done(LocationId id) {
        return AP::IsLocationChecked(id) || AP::IsLocationSkipped(id);
    }

    bool NameHidden(size_t group) {
        Ref flag = Groups()[group].hiddenFlag;
        return flag.size > 0 && !EventFlags::CheckFlag(TextOf(flag));
    }

    // While its name is hidden the look is its door's, in logic or out of logic, as its checks' look would tell which
    // dungeon the door leads into.
    Look IdsLook(Ids ids, bool nameHidden) {
        size_t left = 0, reachable = 0;
        for (LocationId id : ids) {
            if (Done(id)) continue;
            left++;
            if (nameHidden ? DoorReachable(id) : Reachable(id)) reachable++;
        }
        if (left == 0) return Collected;
        if (nameHidden) return reachable == left ? InLogic : OutOfLogic;
        return reachable == left ? InLogic : reachable > 0 ? Partial : OutOfLogic;
    }

    Look GroupLook(size_t group) {
        return IdsLook(GroupIds(group), NameHidden(group));
    }

    bool Skippable(Ids ids) {
        return ids.size() == 1 && !AP::IsLocationChecked(ids[0]);
    }

    // The look shown, and every look skipping or a logic update can give it while the map is open (collected, in logic
    // and out of logic with an unchecked location, partly with two unless its name is hidden), so the open map swaps
    // them.
    bool LookMade(size_t group, Look look) {
        if (GroupLook(group) == look) return true;
        size_t unchecked = 0;
        for (LocationId id : GroupIds(group)) unchecked += AP::IsLocationChecked(id) ? 0 : 1;
        switch (look) {
            case Collected:
            case InLogic:
            case OutOfLogic: return unchecked > 0;
            case Partial: return unchecked > 1 && !NameHidden(group);
            default: return false;
        }
    }

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

    Map::MapMarker* MarkerOf(const Game::SharedPtr<Map::MapMarker>& marker) { return marker.ptr; }

    int MarkerIcon(const Map::MapMarker* marker, Look& look) {
        const Map::UiFieldMapIconsRow* row = marker->row;
        if (row == nullptr || row->showFlag.data == nullptr) return -1;
        return ParseIcon(row->showFlag.View(), look);
    }

    Ui::Pane* MarkerPane(Map::MapMarker* marker) {
        return Game::CallVirtual<Ui::Layout*()>(marker, Ui::LayoutWrapperVirtual::kLayout)->root;
    }

    void ApplyLooks(Map::OverworldMap* page) {
        for (const Game::SharedPtr<Map::MapMarker>& sp : page->markers) {
            Map::MapMarker* marker = MarkerOf(sp);
            Look look;
            int group = marker != nullptr ? MarkerIcon(marker, look) : -1;
            if (group >= 0) SetMarkerVisible(marker, LookShown(group, look));
        }
        if (page == g_MarkerPage) ApplyRings();
    }

    Map::MapMarker* HoveredMarker(const Map::OverworldMap* page) {
        s32 index = page->hoveredMarker;
        if (index < 0 || static_cast<size_t>(index) >= page->markers.size()) return nullptr;
        return MarkerOf(page->markers[index]);
    }

    int HoveredIcon(const Map::OverworldMap* page, Look& look) {
        Map::MapMarker* marker = HoveredMarker(page);
        return marker != nullptr ? MarkerIcon(marker, look) : -1;
    }

    void ToggleOutOfLogic(const void* page) {
        g_OutHidden = !g_OutHidden;
        if (page == g_MarkerPage) ApplyLooks(g_MarkerPage);
        if (page == g_DungeonPage) ApplyDots();
        PlaySound(g_OutHidden ? "SE_SY_MAP_PIN_OFF" : "SE_SY_MAP_PIN_ON");
    }

    // In its hover text and its check list's header: its locations in logic neither checked nor skipped over those not
    // checked (skipping one lowers the first, not the second). Empty when every location is checked.
    std::string CountText(Ids ids) {
        int unchecked = 0, todo = 0;
        for (LocationId id : ids) {
            if (AP::IsLocationChecked(id)) continue;
            unchecked++;
            if (!Done(id) && Reachable(id)) todo++;
        }
        return unchecked == 0 ? "" : std::to_string(todo) + "/" + std::to_string(unchecked);
    }

    bool HintShown(LocationId id) {
        return AP::IsHinted(id) && !Done(id) && !AP::HintedItemText(id).empty();
    }

    bool AnyHintShown(Ids ids) {
        return std::any_of(ids.begin(), ids.end(), HintShown);
    }

    bool Ringed(size_t group) {
        return !NameHidden(group) && AnyHintShown(GroupIds(group));
    }

    Game::Shared<Map::MapMarker> MakeRing(Ui::LayoutWrapper* page, Ui::Pane* parent, Ui::Pane* icon) {
        // The icon's own pane among the parent's children: an overworld marker's root pane sits in the pane its
        // layout was hung by. Its place is the panes' offsets up to the parent.
        Game::Shared<Map::MapMarker> made;
        Ui::Pane* sibling = icon;
        float x = 0, y = 0;
        for (; sibling != nullptr && sibling->parent != parent; sibling = sibling->parent) {
            x += sibling->x;
            y += sibling->y;
        }
        if (sibling == nullptr) return made;
        x += sibling->x;
        y += sibling->y;
        Game::MapMarker_Make(made.Reset());
        Map::MapMarker* ring = MarkerOf(made.View());
        if (ring == nullptr) return made;
        Game::MapMarker_SetupPart(ring);
        Game::LayoutWrapper_AttachBefore(&ring->part.wrapper, page, parent, sibling);
        Ui::MoveTo(MarkerRoot(&ring->part.wrapper), x, y);
        return made;
    }

    void ShowRing(Map::MapMarker* ring, bool single, bool shown) {
        if (ring == nullptr) return;
        Ui::Animator* animator = ring->setMark.animator;
        float frame = static_cast<float>(kRingFrame + (single ? 1 : 0));
        bool framed = animator != nullptr && frame < Game::Animator_FrameCount(animator);
        if (framed) animator->frame = frame;
        Ui::SetVisible(MarkerRoot(&ring->part.wrapper), shown && framed);
        Game::CallVirtual<void()>(ring, Map::MapMarkerVirtual::kUpdate);
    }

    std::vector<std::string> HintItems(Ids ids) {
        std::vector<std::string> items;
        for (LocationId id : ids) {
            if (HintShown(id)) items.push_back(AP::HintedItemText(id));
        }
        return items;
    }

    // Its name after its count when it has several locations and some are unchecked, or after
    // "Skipped: " when its one location is skipped.
    std::string IconText(Ids ids, std::string_view name) {
        if (Skippable(ids) && AP::IsLocationSkipped(ids[0])) return std::string("Skipped: ").append(name);
        std::string count = CountText(ids);
        if (ids.size() < 2 || count.empty()) return std::string(name);
        return count.append(" ").append(name);
    }

    void InstallIconHooks() {
        MarkerAdded::InstallAtPtr(Game::Addr(kMarkerAdded));
        MemoryButton::InstallAtFuncPtr(Game::DungeonMap_MemoriesButton.Ptr());
        FieldMemoryButton::InstallAtPtr(Game::Addr(kFieldMemoryButton));
        FieldPinButton::InstallAtPtr(Game::Addr(kFieldPinButton));
        MapPageSetup::InstallAtFuncPtr(Game::OverworldMap_Setup.Ptr());
        MapPageDtor::InstallAtFuncPtr(Game::OverworldMap_Dtor.Ptr());
    }
}
