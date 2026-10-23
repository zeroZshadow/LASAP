// The puppets' dither (DESIGN.md, Ghosts): their draws get alpha-to-coverage, on the render thread.
#include "game/mem.hpp"
#include "game/sym.hpp"
#include "ghost/ghost_puppets_impl.hpp"
#include <algorithm>

namespace AP::Puppets {
    Span g_Spans[kPuppets + kInstSpans];
    std::pair<void*, u8*> g_Contexts[8] = {};
    u32 g_GhostDraws = 0;
    u32 g_InstDraws = 0;
    u32 g_DrawsBy[8] = {};
    bool g_Dither = true;
    u32 g_SkipMask = 0;

namespace {
    using Game::At;

    // Screen-space dither on the puppets only: the model file's shown albedos carry a constant alpha (render pass
    // 1), and their draws get an NVN multisample state with alpha-to-coverage. The renderer binds a material's
    // parameter block through the command buffer wrapper's bind-uniform-buffer (CmdBuf_BindUniformBuffer); an address
    // inside a puppet's blocks marks its draw. The context's cached raster-state setter (GfxContext_SetRasterState;
    // cache at +0xa0, which other code reads, the wrapper at +8) would bind the material's own state over it, so it
    // binds ours again until the draw (CmdBuf_DrawElements*); after the draw the cached state is bound again
    // (CmdBuf_BindRasterState), as the context believes it is.
    constexpr size_t kWrapperCmdBuf = 0xb8, kContextWrapper = 8, kContextRasterCache = 0xa0;
    alignas(16) u8 g_GhostMs[64];
    bool g_GhostMsReady = false;
    // The renderer records several command buffers (one wrapper each, a context each): the wrapper whose next
    // draw is a puppet's, and each wrapper's context.
    void* g_GhostPending = nullptr;
    bool g_PendingInst = false;    // the pending draw is an instanced model's
    void* g_SkipPending = nullptr;

    // An NVN function through the game's GOT slot.
    template<typename T>
    T NvnFn(Game::Global<void**> got) { return reinterpret_cast<T>(**got); }

    void BindGhostMs(void* cmdBuf) {
        if (!g_GhostMsReady) {
            NvnFn<void (*)(void*)>(Game::got_nvnMultisampleStateSetDefaults)(g_GhostMs);
            NvnFn<void (*)(void*, bool)>(Game::got_nvnMultisampleStateSetAlphaToCoverageEnable)(g_GhostMs, true);
            g_GhostMsReady = true;
        }
        NvnFn<void (*)(void*, const void*)>(Game::got_nvnCommandBufferBindMultisampleState)(cmdBuf, g_GhostMs);
    }

    // The span (puppet, or kInstSpan + instanced group) whose blocks hold this address, or -1.
    int PuppetBlock(u64 address) {
        for (int n = 0; n < kPuppets + kInstSpans; n++) {
            u64 hi = g_Spans[n].hi.load(std::memory_order_acquire);
            if (hi != 0 && address >= g_Spans[n].lo.load(std::memory_order_acquire) && address < hi) return n;
        }
        return -1;
    }

    u8* ContextOf(void* wrapper) {
        for (auto& c : g_Contexts) if (c.first == wrapper) return c.second;
        return nullptr;
    }

    void NoteContext(u8* context) {
        void* wrapper = *reinterpret_cast<void**>(context + kContextWrapper);
        for (auto& c : g_Contexts) {
            if (c.first == wrapper) { c.second = context; return; }
        }
        for (auto& c : g_Contexts) {
            if (c.first == nullptr) { c = {wrapper, context}; return; }
        }
    }

    int WrapperIndex(void* wrapper) {
        for (int i = 0; i < 8; i++) {
            if (g_Contexts[i].first == wrapper) return i;
        }
        return -1;
    }

    // After a puppet's draw on this wrapper: bind the raster state its context has cached.
    void EndGhostDraw(void* wrapper) {
        g_GhostDraws++;
        if (g_PendingInst) g_InstDraws++;
        int i = WrapperIndex(wrapper);
        if (i >= 0) g_DrawsBy[i]++;
        g_GhostPending = nullptr;
        u8* context = ContextOf(wrapper);
        if (context == nullptr) return;
        void* state = *reinterpret_cast<void**>(context + kContextRasterCache);
        if (state != nullptr) Game::CmdBuf_BindRasterState(wrapper, state);
    }

    HOOK_DEFINE_TRAMPOLINE(BindUniformBuffer) {
        static void Callback(u8* wrapper, int stage, int slot, u64* bufferOffset, u64 size) {
            Orig(wrapper, stage, slot, bufferOffset, size);
            int span = bufferOffset != nullptr ? PuppetBlock(bufferOffset[0] + bufferOffset[1]) : -1;
            if (span < 0) return;
            g_PendingInst = span >= kInstSpan;
            int i = WrapperIndex(wrapper);
            if (i >= 0 && (g_SkipMask >> i & 1)) {
                g_SkipPending = wrapper;
                return;
            }
            if (!g_Dither) {
                g_DrawsBy[i >= 0 ? i : 0]++;
                return;
            }
            BindGhostMs(*reinterpret_cast<void**>(wrapper + kWrapperCmdBuf));
            g_GhostPending = wrapper;
        }
    };

    HOOK_DEFINE_TRAMPOLINE(SetRasterState) {
        static void Callback(u8* context, void* state) {
            NoteContext(context);
            Orig(context, state);
            u8* wrapper = *reinterpret_cast<u8**>(context + kContextWrapper);
            if (g_GhostPending == nullptr || g_GhostPending != wrapper) return;
            BindGhostMs(*reinterpret_cast<void**>(wrapper + kWrapperCmdBuf));
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DrawElementsBaseVertex) {
        static void Callback(void* wrapper, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) {
            if (g_SkipPending != nullptr && g_SkipPending == wrapper) {
                g_SkipPending = nullptr;
                return;
            }
            Orig(wrapper, a, b, c, d, e, f);
            if (g_GhostPending != nullptr && g_GhostPending == wrapper) EndGhostDraw(wrapper);
        }
    };

    HOOK_DEFINE_TRAMPOLINE(DrawElementsInstanced) {
        static void Callback(void* wrapper, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) {
            if (g_SkipPending != nullptr && g_SkipPending == wrapper) {
                g_SkipPending = nullptr;
                return;
            }
            Orig(wrapper, a, b, c, d, e, f);
            if (g_GhostPending != nullptr && g_GhostPending == wrapper) EndGhostDraw(wrapper);
        }
    };
}

    void ClearSpan(int n) {
        g_Spans[n].hi.store(0, std::memory_order_release);
        g_Spans[n].lo.store(0, std::memory_order_release);
    }

    // The model's blocks (its materials' parameter blocks and its shapes' blocks, one span in its pool) as GPU
    // addresses, for the render thread.
    void PublishSpan(int n, const u8* model) {
        u64 gpu = At<u64>(At<const u8*>(model, kModelPool), kPoolGpu);
        u64 lo = ~0ull, hi = 0;
        u16 count = At<u16>(model, kModelMatCount);
        const u8* mats = At<const u8*>(model, kModelMats);
        for (u16 i = 0; i < count; i++) {
            const u8* mat = mats + i * kMatStride;
            u64 at = At<u64>(mat, kMatBlock);
            u32 copies = std::min(At<u32>(mat, kMatCopies), 4u);
            u64 size = (At<u64>(mat, kMatBlockSize) + 0xff) & ~0xffull;
            lo = std::min(lo, at);
            hi = std::max(hi, at + copies * size);
        }
        u16 shapes = At<u16>(model, kModelShapeCount);
        const u8* shape = At<const u8*>(model, kModelShapes);
        for (u16 i = 0; i < shapes; i++) {
            u64 at = At<u64>(shape + i * kShapeStride, kShapeBlock);
            lo = std::min(lo, at);
            hi = std::max(hi, at + 0x1000);
        }
        if (hi <= lo) return;
        if (g_Spans[n].lo.load(std::memory_order_relaxed) == gpu + lo &&
            g_Spans[n].hi.load(std::memory_order_relaxed) == gpu + hi) return;
        g_Spans[n].hi.store(0, std::memory_order_release);
        g_Spans[n].lo.store(gpu + lo, std::memory_order_release);
        g_Spans[n].hi.store(gpu + hi, std::memory_order_release);
    }

    void InstallDitherHooks() {
        BindUniformBuffer::InstallAtFuncPtr(Game::CmdBuf_BindUniformBuffer.Ptr());
        SetRasterState::InstallAtFuncPtr(Game::GfxContext_SetRasterState.Ptr());
        DrawElementsBaseVertex::InstallAtFuncPtr(Game::CmdBuf_DrawElementsBaseVertex.Ptr());
        DrawElementsInstanced::InstallAtFuncPtr(Game::CmdBuf_DrawElementsInstanced.Ptr());
    }
}
