// The dungeon page's dots (DESIGN.md, Map tracker, Dungeon dots). The page's setup shows each room on its floor
// (ShowCellFloor), which also gives the rooms by name; after it, each dot of the dungeon gets a map marker placed as
// the page places its room icons and hung after them under N_MapPin_00, so the pins draw over the dots. The floor
// buttons show the rooms again, and the dots of each room follow it.
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "game/hooks/level.hpp"
#include "game/mem.hpp"
#include "game/sym.hpp"
#include "loggers.hpp"
#include "map/map_tracker_impl.hpp"
#include <algorithm>
#include <cmath>

namespace AP::MapTracker {
    Map::DungeonMap* g_DungeonPage = nullptr;
    std::map<std::string, std::pair<Map::Area*, bool>, std::less<>> g_Rooms;
    std::vector<DotMarker> g_DotMarkers;
    int g_DotHovered = -1;
    int g_DotsMade = 0;

namespace {
    using Game::Ui::PlaySound;

    // Dungeon map screen (game 1.0.0)
    constexpr uintptr_t kDungeonMapFlags = 0xe76204;    // w8 = DungeonItems flags byte (bit0 compass, bit1 map)
    // The dungeon map page (Game::Map::DungeonMap, scripts/re/UI.md "Dungeon map"): its setup and destructor;
    // DungeonMap_ShowCellFloor shows a room on the shown floor (its UiDungeonArea row's floor == the page's
    // shownFloor); KeyWait runs MoveCursor, PinButton () -> A pressed, then UpdateHover, the pins' hover.
    // DungeonMap_PositionIcon: an icon's position under N_MapPin_00. A dot within kHoverRadius of the cursor's size
    // is hovered; the cursor eases onto it while the stick is idle (the page's cursor speed kIdleSpeed).
    constexpr float kIdleSpeed = 4.25f, kEase = 0.3f, kHoverRadius = 0.2f;

    const std::pair<Map::Area*, bool>* FindRoom(const std::string& room) {
        auto it = g_Rooms.find(room);
        return it != g_Rooms.end() ? &it->second : nullptr;
    }

    // A room's name as the dots have it: its file's ("level/Lv01TailCave/Lv01TailCave_03G.leb") without the folder
    // and the extension.
    std::string RoomName(const Map::Area* room) {
        std::string_view path(room->path, std::min<u64>(room->pathLength, sizeof(room->path)));
        size_t slash = path.rfind('/');
        if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
        if (path.size() > 4 && path.substr(path.size() - 4) == ".leb") path.remove_suffix(4);
        return std::string(path);
    }

    // A dot shows while its room is on the shown floor, unless its look is out of logic and those are toggled off.
    bool DotShown(const DotMarker& dm) {
        return dm.onFloor && !(g_OutHidden && IdsLook(g_Dots[dm.dot].ids, false) == OutOfLogic);
    }

    void SetMarkerState(Map::MapMarker* marker, u8 state) {
        Game::MapMarker_ChangeState(&marker->machine, state);
    }

    void DropDots() {
        for (auto& dm : g_DotMarkers) Game::ReleaseOwner(dm.control);
        g_DotMarkers.clear();
        g_DotHovered = -1;
        g_DungeonPage = nullptr;
    }

    // After the dungeon page's setup: the dots of the current dungeon whose room the page has.
    void MakeDots(Map::DungeonMap* page) {
        DropDots();
        g_DungeonPage = page;
        std::string level = LevelSystem::GetCurrentLevel() + "_";
        Ui::Pane* pinRoot = page->mapPin;
        Ui::Pane* pinOffset = page->pinOffset;
        if (pinRoot == nullptr || pinOffset == nullptr) return;
        size_t missing = 0;
        for (size_t d = 0; d < g_Dots.size(); d++) {
            const Dot& dot = g_Dots[d];
            if (dot.room.compare(0, level.size(), level) != 0) continue;
            const auto* room = FindRoom(dot.room);
            if (room == nullptr) {
                missing++;
                continue;
            }
            Game::SharedPtr made{}, roomPtr{room->first, nullptr};
            Game::MapMarker_Make(&made);
            Map::MapMarker* marker = MarkerOf(made);
            if (marker == nullptr) continue;
            Game::MapMarker_SetupPart(marker);
            Game::LayoutWrapper_AttachBefore(&marker->wrapper, &page->wrapper, pinRoot, pinOffset);
            float at[2] = {};
            Game::DungeonMap_PositionIcon(dot.x, 0, dot.z, page, at, &roomPtr);
            Ui::MoveTo(MarkerRoot(&marker->wrapper), at[0], at[1]);
            DotMarker dm{d, marker, made.ctrl};
            // under N_MapPosition_00, which moves after the setup (DotAt)
            dm.x = at[0] + pinRoot->x;
            dm.y = at[1] + pinRoot->y;
            dm.onFloor = room->second;
            g_DotMarkers.push_back(dm);
        }
        g_DotsMade += static_cast<int>(g_DotMarkers.size());
        ApplyDots();
        Logging.Log("[AP] map: %zu dungeon dots, %zu without their room", g_DotMarkers.size(), missing);
    }

    // A room shown or hidden for the shown floor: its dots follow it (a hidden dot loses the hover on the next frame
    // of KeyWait).
    void ShowRoomDots(const std::string& name, bool onFloor) {
        for (auto& dm : g_DotMarkers) {
            if (g_Dots[dm.dot].room == name) dm.onFloor = onFloor;
        }
        ApplyDots();
    }

    void HideCursorName() {
        Map::MapCursor* cursor = FindCursor();
        if (cursor != nullptr) cursor->nameHidden = 0x101;
    }

    // After the dungeon page moves its cursor: with the stick idle the cursor eases onto the hovered dot, as onto a
    // hovered pin.
    void EaseOntoDot(Map::DungeonMap* page) {
        if (g_DotHovered < 0 || page->hoveredPin != nullptr || page->info != 0 || page->cursorSpeed != kIdleSpeed) {
            return;
        }
        float x, y;
        DotAt(page, g_DotMarkers[g_DotHovered], x, y);
        page->cursorX += (x - page->cursorX) * kEase;
        page->cursorY += (y - page->cursorY) * kEase;
    }

    HOOK_DEFINE_INLINE(DungeonMapFlags) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) {
            g_FlagsCalls++;
            if (g_Enabled) ctx->W[8] |= 3;  // compass and map
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DungeonIconUpdate) {
        static void Callback(Map::DungeonMapIcon* icon, const Map::MapActorDesc* actor) {
            Orig(icon, actor);
            g_IconCalls++;
            if (!g_Enabled || g_Chests.empty()) return;
            // A chest of the slot's: its dot stands for it, so its icon goes (shown flag clear, which the floor's
            // set-shown keeps).
            std::string level = LevelSystem::GetCurrentLevel();
            for (const auto& chest : g_Chests) {
                if (chest.level == level && std::fabs(chest.x - actor->x) < 0.01f && std::fabs(chest.z - actor->z) < 0.01f) {
                    g_ChestMatches++;
                    icon->shown = 0;
                    Ui::SetVisible(MarkerRoot(&icon->wrapper), false);
                    return;
                }
            }
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DungeonPageSetup) {
        static void Callback(Map::DungeonMap* page) {
            g_Rooms.clear();
            Orig(page);
            if (g_Enabled) MakeDots(page);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DungeonPageDtor) {
        static void Callback(Map::DungeonMap* page) {
            if (&page->wrapper == g_Panel.page) DropList();
            if (page == g_DungeonPage) DropDots();
            Orig(page);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(ShowCellFloor) {
        static void Callback(Map::DungeonMap* page, u8 cell, Game::SharedPtr* room, const Map::UiDungeonAreaRow* area) {
            Orig(page, cell, room, area);
            if (!g_Enabled || room == nullptr || room->ptr == nullptr || area == nullptr) return;
            auto* roomArea = static_cast<Map::Area*>(room->ptr);
            std::string name = RoomName(roomArea);
            bool onFloor = area->floor == page->shownFloor;
            g_Rooms[name] = {roomArea, onFloor};
            if (page == g_DungeonPage) ShowRoomDots(name, onFloor);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DungeonMoveCursor) {
        static void Callback(Map::DungeonMap* page, bool move) {
            Orig(page, move);
            if (page == g_DungeonPage) EaseOntoDot(page);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DungeonUpdateHover) {
        static void Callback(Map::DungeonMap* page) {
            Orig(page);
            if (page == g_DungeonPage) UpdateDotHover(page);
        }
    };

    // A in the dungeon page's KeyWait. The page is not passed: it is the one with dots.
    HOOK_DEFINE_TRAMPOLINE(DungeonPinButton) {
        static bool Callback() {
            bool pressed = Orig();
            Map::DungeonMap* page = g_DungeonPage;
            if (page == nullptr || page->machine.state != Map::kKeyWait) return pressed;
            if (g_Panel.page == &page->wrapper) {
                TickList(&page->wrapper);
                return false;
            }
    #ifdef LASAP_TEST
            if (TestDotPressed()) pressed = true;
    #endif
            const std::vector<int>* ids = HoveredDotIds();
            if (!pressed || ids == nullptr || page->hoveredPin != nullptr) return pressed;
            if (ids->size() > 1) {
                OpenList(&page->wrapper, *ids, g_Dots[g_DotMarkers[g_DotHovered].dot].name, -1);
            } else if (Skippable(*ids)) {
                AP::SetLocationSkipped((*ids)[0], !AP::IsLocationSkipped((*ids)[0]));
                ApplyDots();
                ShowDotName();
                PlaySound("SE_SY_DECIDE");
            } else {
                PlaySound("SE_SY_BEEP");
            }
            return false;  // no pin
        }
    };
}

    // A map marker's or room icon's root pane.
    Ui::Pane* MarkerRoot(Ui::LayoutWrapper* wrapper) {
        return Game::CallVirtual<Ui::Pane*()>(wrapper, Ui::LayoutWrapperVirtual::kRootPane);
    }

    // Each dot's look (its SetMark frame, smaller for one check) and whether it shows.
    void ApplyDots() {
        for (auto& dm : g_DotMarkers) {
            const Dot& dot = g_Dots[dm.dot];
            Ui::Animator* animator = dm.marker->setMark;
            int look = IdsLook(dot.ids, false);
            float frame = static_cast<float>(kGameMarkFrames + (dot.ids.size() == 1 ? kLooks : 0) + look);
            if (animator != nullptr && frame < Game::Animator_FrameCount(animator)) animator->frame = frame;
            Ui::SetVisible(MarkerRoot(&dm.marker->wrapper), DotShown(dm));
        }
    }

    // A dot's position in the cursor's units (N_MapOrigin_00's): N_MapPosition_00 moves after the page's setup (y 48
    // in Tail Cave), as the pins' hover reads it each frame.
    void DotAt(const Map::DungeonMap* page, const DotMarker& dm, float& x, float& y) {
        const Ui::Pane* position = page->mapPosition;
        x = dm.x + (position != nullptr ? position->x : 0);
        y = dm.y + (position != nullptr ? position->y : 0);
    }

    const std::vector<int>* HoveredDotIds() {
        return g_DotHovered >= 0 ? &g_Dots[g_DotMarkers[g_DotHovered].dot].ids : nullptr;
    }

    // A name on the map cursor: Place:<label>, whose text LabelText gives.
    void ShowCursorName(std::string_view placeLabel) {
        Map::MapCursor* cursor = FindCursor();
        if (cursor == nullptr) return;
        std::string label = "Place:" + std::string(placeLabel);
        Game::StrView view{label};
        Game::MessageRef ref{};
        u8* manager = *Game::MessageManager_ptr;
        if (manager == nullptr) return;
        Game::MessageManager_Find(&ref, manager, &view);
        if (ref.index >= 0) Game::MapCursor_ShowName(cursor, &ref);
        Game::ReleaseOwner(ref.ctrl);
    }

    // The hovered dot's name on the map cursor.
    void ShowDotName() {
        ShowCursorName(AP::MAP_DOT_LABEL);
    }

    // Once a frame of the dungeon page's KeyWait, after its pins' hover: the nearest shown dot within the pins'
    // radius, unless a pin is hovered or Y shows the dungeon's info.
    void UpdateDotHover(Map::DungeonMap* page) {
        for (auto& dm : g_DotMarkers) {
            Game::CallVirtual<void()>(dm.marker, Map::MapMarkerVirtual::kUpdate);
        }
        int hovered = -1;
        Map::MapCursor* cursor = FindCursor();
        bool free = page->hoveredPin == nullptr && page->info == 0 && !ListOpen() && cursor != nullptr;
        if (free) {
            float radius = cursor->size * kHoverRadius * page->cursorScale;
            float cx = page->cursorX, cy = page->cursorY;
            float best = radius * radius;
            for (size_t i = 0; i < g_DotMarkers.size(); i++) {
                const DotMarker& dm = g_DotMarkers[i];
                if (!DotShown(dm)) continue;
                float x, y;
                DotAt(page, dm, x, y);
                float dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
                if (d2 <= best) {
                    best = d2;
                    hovered = static_cast<int>(i);
                }
            }
        }
        if (hovered == g_DotHovered) return;
        if (g_DotHovered >= 0) SetMarkerState(g_DotMarkers[g_DotHovered].marker, Map::kMarkerOff);
        bool had = g_DotHovered >= 0;
        g_DotHovered = hovered;
        if (hovered >= 0) {
            SetMarkerState(g_DotMarkers[hovered].marker, Map::kMarkerOn);
            ShowDotName();
        } else if (had && page->hoveredPin == nullptr) {
            HideCursorName();
        }
    }

    void InstallDotHooks() {
        DungeonMapFlags::InstallAtPtr(Game::Addr(kDungeonMapFlags));
        DungeonIconUpdate::InstallAtFuncPtr(Game::DungeonMapIcon_Update.Ptr());
        DungeonPageSetup::InstallAtFuncPtr(Game::DungeonMap_Setup.Ptr());
        DungeonPageDtor::InstallAtFuncPtr(Game::DungeonMap_Dtor.Ptr());
        ShowCellFloor::InstallAtFuncPtr(Game::DungeonMap_ShowCellFloor.Ptr());
        DungeonMoveCursor::InstallAtFuncPtr(Game::DungeonMap_MoveCursor.Ptr());
        DungeonUpdateHover::InstallAtFuncPtr(Game::DungeonMap_UpdateHover.Ptr());
        DungeonPinButton::InstallAtFuncPtr(Game::DungeonMap_PinButton.Ptr());
    }
}
