// Inventory bits of seashells, heart pieces, golden leaves, heart containers, bottles and chamber stones (DESIGN.md,
// Received items).
//
// The game counts these items by bits: collecting one sets the bit of its source, and the count is the number of
// bits set. The patcher writes INDICES_PATH: per source its kind, its vanilla bit and the local id of its location
// when shuffled. A source left vanilla sets its own bit when collected, so a received item takes a bit that no
// vanilla source of the seed sets (VanillaBits); otherwise that source would add nothing.
//
// The Memories map marks bit i of the seashells and heart pieces at the vanilla place of source i. Received items
// take the bits of shuffled sources, so those marks would name places unrelated to them: the module marks source i
// once it is done instead: a vanilla source by its bit, a shuffled one by its location's check.
#include "AP/ap.hpp"
#include "offsets.hpp"
#include "loggers.hpp"
#include "Config/file_handler.hpp"
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
        std::map<int, int> shuffled;    // bit -> local id of its shuffled source
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
            else g_Kinds[kind].shuffled[index] = local_id;
        }
        HeartMarkerTest::InstallAtOffset(offset_manager.Offset(kHeartMarkerTest));
        ShellMarkerTest::InstallAtOffset(offset_manager.Offset(kShellMarkerTest));
    }

    u64 VanillaBits(char kind) {
        auto it = g_Kinds.find(kind);
        return it == g_Kinds.end() ? 0 : it->second.vanilla;
    }
}
