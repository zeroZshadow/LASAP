// Inventory bits of seashells, heart pieces, golden leaves, heart containers, bottles and chamber stones (DESIGN.md,
// Item model: remote items): the bits the seed's vanilla sources set (VanillaBits), which received items leave free,
// and the Memories map's seashell and heart piece marks, each set once its source is done.
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "config/seed_file.hpp"
#include "game/addr.hpp"
#include "loggers.hpp"
#include <map>
#include <string>

namespace {
    // The Memories map's marker setup (game 1.0.0), at `tst x8, x9` before it adds the marker of row x21:
    // x8 = the inventory's bits, x9 = 1 << x21.
    constexpr uintptr_t kHeartMarkerTest = 0xe91c28;
    constexpr uintptr_t kShellMarkerTest = 0xe91ee0;

    struct Kind {
        u64 vanilla = 0;                // bits set by the seed's vanilla sources
        std::map<int, AP::LocationId> shuffled;  // bit -> the location of its shuffled source
    };
    std::map<char, Kind> g_Kinds;

    bool Marked(char kind, u64 bits, int index) {
        const Kind& k = g_Kinds[kind];
        if ((k.vanilla >> index) & 1) return (bits >> index) & 1;
        auto it = k.shuffled.find(index);
        return it != k.shuffled.end() && AP::IsLocationChecked(it->second);
    }

    void SetMarked(exl::hook::nx64::InlineCtx* ctx, char kind) {
        int index = static_cast<int>(ctx->X[21]);
        ctx->X[8] = Marked(kind, ctx->X[8], index) ? ctx->X[9] : 0;
    }
}

HOOK_DEFINE_INLINE(HeartMarkerTest) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) { SetMarked(ctx, 'H'); }
};

HOOK_DEFINE_INLINE(ShellMarkerTest) {
    static void Callback(exl::hook::nx64::InlineCtx* ctx) { SetMarked(ctx, 'S'); }
};

namespace AP {
    void LoadIndices() {
        // The seed file's indices: "<kind> <bit> <local id or -1>" lines (patcher las_ap_patch.index_lines)
        SeedFile::ForEach("indices", [](Fields& f) {
            std::string_view kind = f.Str();
            auto index = f.Num<int>();
            auto location = f.Num<int>();
            if (kind.size() != 1 || !index || !location || *index < 0 || *index >= 64) return;
            if (*location < 0) g_Kinds[kind[0]].vanilla |= 1ULL << *index;
            else g_Kinds[kind[0]].shuffled[*index] = AP::LocationId{*location};
        });
        HeartMarkerTest::InstallAtPtr(Game::Addr(kHeartMarkerTest));
        ShellMarkerTest::InstallAtPtr(Game::Addr(kShellMarkerTest));
    }

    u64 VanillaBits(char kind) {
        auto it = g_Kinds.find(kind);
        return it == g_Kinds.end() ? 0 : it->second.vanilla;
    }
}
