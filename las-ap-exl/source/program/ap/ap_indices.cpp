// Inventory bits of seashells, heart pieces, golden leaves, heart containers, bottles and chamber stones (DESIGN.md,
// Item model: remote items): the bits the seed's vanilla sources set (VanillaBits), which received items leave free,
// and the Memories map's seashell and heart piece marks, each set once its source is done.
#include "ap/ap.hpp"
#include "ap/ap_patcher.hpp"
#include "config/file_handler.hpp"
#include "game/addr.hpp"
#include "loggers.hpp"
#include <map>
#include <sstream>
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
        // "<kind> <bit> <local id or -1>" lines (patcher las_ap_patch.write_indices)
        std::string text;
        if (R_FAILED(FileHandler::ReadFile(INDICES_PATH, text))) {
            Logging.Log("[AP] no item indices file");
            return;
        }
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::stringstream ss(line);
            char kind;
            int index, local_id;
            if (!(ss >> kind >> index >> local_id) || index < 0 || index >= 64) continue;
            if (local_id < 0) g_Kinds[kind].vanilla |= 1ULL << index;
            else g_Kinds[kind].shuffled[index] = AP::LocationId{local_id};
        }
        HeartMarkerTest::InstallAtPtr(Game::Addr(kHeartMarkerTest));
        ShellMarkerTest::InstallAtPtr(Game::Addr(kShellMarkerTest));
    }

    u64 VanillaBits(char kind) {
        auto it = g_Kinds.find(kind);
        return it == g_Kinds.end() ? 0 : it->second.vanilla;
    }
}
