// The popup's model in front of the world (DESIGN.md, Showing received items): Link walks on while it shows, also
// into a doorway, whose roof would cut into it. The renderer's draw tree has group nodes (DrawGroup_Draw: each child
// in turn, the context's pass id set from it) and leaf passes, which bind their states and draw their sorted items
// (ShapeDrawItem_Execute). The popup model's opaque shapes are held back in their pass and drawn once the pass's node
// is done, after everything else in it: first all of them with the depth test always passing, their depth written
// over the world's, then all of them again with the usual test (less or equal), which keeps each pixel's nearest part
// of the model. Its depth stays in the depth buffer, so what draws later is hidden behind it. Translucent shapes
// (render layers 2 and 3, the glass of the bottles and the magnifying lens) draw in later passes of their own, as the
// game draws them; the glass shows what is on screen behind it. The shadow pass draws the model as usual. Runs on the
// render threads; each context holds its own items.
#include "ap/ap_popup.hpp"
#include "game/model.hpp"
#include "game/sym.hpp"
#include <atomic>

namespace AP::Popup {
namespace {
    constexpr size_t kContextDepthState = 0x98;   // the depth-stencil state bound (GfxContext_SetDepthStencilState)
    constexpr size_t kContextPass = 0x188;        // u8: the pass being drawn
    constexpr size_t kItemKey = 0x10;             // u64 sort key: bits 54-55 the shape's render layer
    constexpr size_t kItemInstance = 0x30;
    constexpr size_t kNodeChildren = 0x40;        // a vector of node pointers
    constexpr size_t kNodePass = 0x8, kNodeEnabled = 0x9, kNodeDraw = 0x38;
    constexpr u8 kShadowPass = 1;
    constexpr int kKeyLayerShift = 54;
    // nn::gfx ComparisonFunction
    constexpr u8 kLessEqual = 3, kAlways = 7;
    constexpr u16 kDepthTest = 1, kDepthWrite = 2;
    constexpr int kHeldMax = 32;

    std::atomic<const void*> g_Instance{nullptr};  // the popup's model instance
    bool g_OnTop = true;
    Game::DepthStencilState g_Over{}, g_Nearest{};
    bool g_StatesReady = false;

    struct Held { u8* item; u8 pass; };
    struct Pending {
        std::atomic<u8*> context{nullptr};
        int depth = 0;                 // group draws under way
        int count = 0;
        Held items[kHeldMax];
    };
    Pending g_Pending[8];
#ifdef LASAP_TEST
    std::atomic<u32> g_HeldBy[64];   // DBG popdraw: items held per pass id
#endif

    void BuildState(Game::DepthStencilState* state, void* device, u8 func) {
        Game::DepthStencilStateInfo info;
        Game::DepthStencilStateInfo_SetDefault(&info);
        info.depthFunc = func;
        info.flags = (info.flags & ~0xf) | kDepthTest | kDepthWrite;
        *state = {};
        Game::DepthStencilState_Initialize(state, device, &info);
    }

    bool BuildStates() {
        u8* graphics = *Game::Graphics_ptr;
        u8* holder = graphics != nullptr ? *reinterpret_cast<u8**>(graphics + 8) : nullptr;
        if (holder == nullptr) return false;
        void* device = *reinterpret_cast<void**>(holder);
        BuildState(&g_Over, device, kAlways);
        BuildState(&g_Nearest, device, kLessEqual);
        return true;
    }

    Pending* PendingOf(u8* context, bool claim) {
        for (auto& p : g_Pending) {
            if (p.context.load(std::memory_order_acquire) == context) return &p;
        }
        if (!claim) return nullptr;
        for (auto& p : g_Pending) {
            u8* none = nullptr;
            if (p.context.compare_exchange_strong(none, context, std::memory_order_acq_rel)) return &p;
        }
        return nullptr;
    }

    bool Opaque(const u8* item) {
        return (*reinterpret_cast<const u64*>(item + kItemKey) >> kKeyLayerShift & 3) <= 1;
    }

    HOOK_DEFINE_TRAMPOLINE(ExecuteItem) {
        static u64 Callback(u8* item, u8* context) {
            const void* instance = g_Instance.load(std::memory_order_acquire);
            if (instance != nullptr && *reinterpret_cast<void**>(item + kItemInstance) == instance &&
                context[kContextPass] != kShadowPass && Opaque(item)) {
                Pending* p = PendingOf(context, false);
                if (p != nullptr && p->depth > 0 && p->count < kHeldMax) {
                    p->items[p->count++] = {item, context[kContextPass]};
#ifdef LASAP_TEST
                    g_HeldBy[context[kContextPass] & 63].fetch_add(1, std::memory_order_relaxed);
#endif
                    return 0;
                }
            }
            return Orig(item, context);
        }
    };

    void DrawHeld(const Held& held, void* state, u8* context) {
        context[kContextPass] = held.pass;
        Game::GfxContext_SetDepthStencilState(context, state);
        ExecuteItem::Orig(held.item, context);
    }

    // The items held in the pass just drawn: over the world, then nearest first
    void Flush(Pending* p, u8* context) {
        u8 pass = context[kContextPass];
        void* bound = *reinterpret_cast<void**>(context + kContextDepthState);
        for (int i = 0; i < p->count; i++) DrawHeld(p->items[i], &g_Over, context);
        for (int i = 0; i < p->count; i++) DrawHeld(p->items[i], &g_Nearest, context);
        p->count = 0;
        if (bound != nullptr) Game::GfxContext_SetDepthStencilState(context, bound);
        context[kContextPass] = pass;
    }

    // DrawGroup_Draw, with the popup's held items drawn after each child
    HOOK_DEFINE_TRAMPOLINE(DrawGroup) {
        static void Callback(u8* node, u8* context) {
            Pending* p = PendingOf(context, true);
            if (p == nullptr) {
                Orig(node, context);
                return;
            }
            p->depth++;
            u8** child = *reinterpret_cast<u8***>(node + kNodeChildren);
            u8** end = *reinterpret_cast<u8***>(node + kNodeChildren + 8);
            for (; child != end; child++) {
                u8* c = *child;
                if (c[kNodeEnabled] == 0) continue;
                context[kContextPass] = c[kNodePass];
                Game::CallVirtual<void(u8*)>(c, kNodeDraw, context);
                if (p->count > 0) Flush(p, context);
            }
            p->depth--;
        }
    };
}

    void InstallDrawHooks() {
        ExecuteItem::InstallAtFuncPtr(Game::ShapeDrawItem_Execute.Ptr());
        DrawGroup::InstallAtFuncPtr(Game::DrawGroup_Draw.Ptr());
    }

    void DrawOnTop(const void* instance) {
        if (instance != nullptr && !g_StatesReady) g_StatesReady = BuildStates();
        g_Instance.store(g_OnTop && g_StatesReady ? instance : nullptr, std::memory_order_release);
    }

#ifdef LASAP_TEST
    void SetOnTop(bool on) { g_OnTop = on; }

    std::string DrawStats() {
        std::string out;
        for (int i = 0; i < 64; i++) {
            u32 n = g_HeldBy[i].exchange(0, std::memory_order_relaxed);
            if (n != 0) out += " " + std::to_string(i) + ":" + std::to_string(n);
        }
        return out;
    }
#endif
}
