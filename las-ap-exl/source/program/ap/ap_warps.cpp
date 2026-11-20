// Warp Points (DESIGN.md, Warp points): each shuffled warp point opens only by its item. The game opens a point by
// setting its bit of Inventory +0xb8 in four places: a warp hole as Link steps in, a Manbo platform once its tile is
// dug, the rock over a platform as it is lifted, and the event action Link.AddWarpPoint (Manbo's song, for his pond).
// An inline hook before each store keeps a shuffled point's bit as it was; the received item sets it. A hole or
// platform whose bit is clear starts the warp that lifts Link out again, as with fewer than two points open.
#include "ap/ap.hpp"
#include "ap/ap_bridge.hpp"
#include "config/seed_file.hpp"
#include "game/actors/warp_point.hpp"
#include "game/addr.hpp"
#include "game/data/inventory.hpp"
#include "game/hooks/eventflags.hpp"
#include "game/sym.hpp"
#include "hooks.hpp"
#include "loggers.hpp"
#include <charconv>

namespace AP::Bridge {
namespace {
    constexpr int kWarpPoints = 64;  // the bits of Inventory +0xb8
    constexpr const char* kWarpKey = "$WARP:";  // a received warp point item: $WARP:<index> (lasr.WARP_ITEMS)
    // Before each store of a point's bit, `orr x8, x10, x8`: x8 the point's bit, x10 the bits before. The hole's
    // (AreaWarpHoleEventBox, on stepping in), the platform's (ObjWarpManbo, its tile dug), the rock's (ObjRock, lifted)
    // and WarpPoint_Activate's (Link.AddWarpPoint, which gets there with the bit set too)
    constexpr uintptr_t kHoleStore = 0x8b540, kPlatformStore = 0xcbcf64, kRockStore = 0xc49a24,
                        kActivateStore = 0xdf86e4;

    // The rocks over platforms (scripts/re/UI.md, Warp points): each rock's LEB switch 0, a global flag
    struct RockFlag {
        s32 index;
        const char* flag;
    };
    constexpr RockFlag kRockFlags[] = {{5, "WarpManboDanpei"}, {8, "WarpManboShellHouse"}, {9, "WarpTalTalMtWest"}};

    // The seed file's warps: the shuffled points' bits
    u64 g_Shuffled = 0;
    // A shuffled hole's or platform's warp starts while its bit is clear: no point counts as open
    bool g_Closed = false;

    u64 Bit(s32 index) { return index >= 0 && index < kWarpPoints ? u64(1) << index : 0; }

    bool ClosedPoint(s32 index) {
        auto* items = Game::Data::Inventory::Items();
        return (g_Shuffled & Bit(index)) != 0 && items != nullptr && (items->WarpPoints & Bit(index)) == 0;
    }

    // The store about to set a point's bit (x8) sets nothing for a shuffled point.
    void Keep(exl::hook::nx64::InlineCtx* ctx) { ctx->X[8] &= ~g_Shuffled; }

    HOOK_DEFINE_INLINE(HoleStore) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) { Keep(ctx); }
    };
    HOOK_DEFINE_INLINE(PlatformStore) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) { Keep(ctx); }
    };
    HOOK_DEFINE_INLINE(RockStore) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) { Keep(ctx); }
    };
    HOOK_DEFINE_INLINE(ActivateStore) {
        static void Callback(exl::hook::nx64::InlineCtx* ctx) { Keep(ctx); }
    };

    HOOK_DEFINE_TRAMPOLINE(CountActivated) {
        static s32 Callback() { return g_Closed ? 0 : Orig(); }
    };
    HOOK_DEFINE_TRAMPOLINE(HoleStartEvent) {
        static void Callback(void* out, Game::Actors::AreaWarpHoleEventBox* hole) {
            g_Closed = ClosedPoint(hole->warpIndex);
            Orig(out, hole);
            g_Closed = false;
        }
    };
    HOOK_DEFINE_TRAMPOLINE(PlatformStartEvent) {
        static void Callback(void* out, Game::Actors::ObjWarpManbo* platform) {
            g_Closed = ClosedPoint(platform->warpIndex);
            Orig(out, platform);
            g_Closed = false;
        }
    };
}

    // The seed file's warps: "<index>" per shuffled warp point (patcher las_ap_patch.warp_lines)
    void InstallWarpPoints() {
        SeedFile::ForEach("warps", [](Fields& f) {
            if (auto index = f.Num<int>()) g_Shuffled |= Bit(*index);
        });
        Logging.Log("[AP] shuffled warp points: %016llx", static_cast<unsigned long long>(g_Shuffled));
        if (g_Shuffled == 0) return;
        HoleStore::InstallAtOffset(Game::Offset(kHoleStore));
        PlatformStore::InstallAtOffset(Game::Offset(kPlatformStore));
        RockStore::InstallAtOffset(Game::Offset(kRockStore));
        ActivateStore::InstallAtOffset(Game::Offset(kActivateStore));
        CountActivated::InstallAtFuncPtr(Game::WarpPoint_CountActivated.Ptr());
        HoleStartEvent::InstallAtFuncPtr(Game::AreaWarpHole_StartEvent.Ptr());
        PlatformStartEvent::InstallAtFuncPtr(Game::ObjWarpManbo_StartEvent.Ptr());
    }

    bool IsWarpKey(const std::string& key) { return key.starts_with(kWarpKey); }

    void GrantWarp(const std::string& key) {
        auto* items = Game::Data::Inventory::Items();
        std::string_view digits = std::string_view(key).substr(std::char_traits<char>::length(kWarpKey));
        int index = -1;
        std::from_chars(digits.data(), digits.data() + digits.size(), index);
        u64 bit = Bit(index);
        if (items == nullptr || bit == 0) {
            AP::Log("unknown warp point " + key);
            return;
        }
        items->WarpPoints |= bit;
        // A rock over the platform is not made once its flag is set, which lifting it does
        for (const auto& rock : kRockFlags)
            if (rock.index == index) EventFlags::SetFlag(rock.flag, true);
    }
}
