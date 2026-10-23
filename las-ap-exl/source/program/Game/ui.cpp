#include "Game/ui.hpp"
#include "Game/mem.hpp"
#include "Game/stl.hpp"
#include "Game/sym.hpp"
#include <algorithm>

namespace Game::Ui {
    namespace {
        constexpr size_t kFindPane = 0x60;
        // The UI manager's UIs: a list at +0x950, the UI at node +0x10, its id from virtual +0x60.
        constexpr size_t kUiList = 0x950, kNodeUi = 0x10, kUiId = 0x60;
        constexpr size_t kUiGameArchive = 0x88;
        // The glyph queues: two std::lists at manager +0xab40 (+0x18 each: end node {last, first}, size), nodes
        // {previous, next, owner, count, text box, set}.
        constexpr size_t kUiGlyphQueues = 0xab40, kQueueStride = 0x18, kQueueSize = 0x10, kNodeCount = 0x18,
                         kNodeTextBox = 0x20;
        constexpr int kArchiveSet = 0;  // Game.arc
        // Text box: +0x190 set once the font has drawn its glyphs; virtual +0x120 the text's extent;
        // AllocateStringBuffer (virtual +0xe8: box, gfx device, length), the capacity at +0x110 (with the terminator).
        constexpr size_t kTextGlyphsReady = 0x190, kTextRect = 0x120, kTextAllocate = 0xe8, kTextCapacity = 0x110;
        constexpr size_t kWrapperSize = 0x78, kWrapperLayout = 0x18, kLayoutRoot = 0x18, kWrapperDelete = 0x18;

        u8* Manager() { return UiManager.Ptr(); }

        // The gfx device the UI manager builds layouts with: the graphics object's +8.
        void* GfxDevice() {
            u8* graphics = *Graphics_ptr;
            void** device = At<void**>(graphics, 8);
            return device != nullptr ? *device : nullptr;
        }
    }

    void SetVisible(u8* pane, bool visible) {
        if (pane == nullptr) return;
        Field<u8>(pane, kPaneFlags) = (Field<u8>(pane, kPaneFlags) & ~kPaneVisible) | (visible ? kPaneVisible : 0);
    }

    void SetAlpha(u8* pane, int alpha) {
        if (pane == nullptr) return;
        u8 a = static_cast<u8>(std::clamp(alpha, 0, 255));
        Field<u8>(pane, kPaneAlpha) = a;
        Field<u8>(pane, kPaneGlobalAlpha) = a;
    }

    void MoveTo(u8* pane, float x, float y) {
        if (pane == nullptr) return;
        Field<float>(pane, kPaneX) = x;
        Field<float>(pane, kPaneY) = y;
        Field<u8>(pane, kPaneFlags) |= kPaneDirty;
    }

    void ScaleTo(u8* pane, float x, float y) {
        if (pane == nullptr) return;
        Field<float>(pane, kPaneScaleX) = x;
        Field<float>(pane, kPaneScaleY) = y;
        Field<u8>(pane, kPaneFlags) |= kPaneDirty;
    }

    void ScaleTo(u8* pane, float scale) { ScaleTo(pane, scale, scale); }

    u8* FindPane(u8* root, const char* name) {
        if (root == nullptr) return nullptr;
        return CallVirtual<u8*(const char*, bool)>(root, kFindPane, name, true);
    }

    u8* FindUi(int id) {
        u8* end = Manager() + kUiList;
        for (u8* node = Field<u8*>(end, 8); node != end; node = Field<u8*>(node, 8)) {
            u8* ui = Field<u8*>(node, kNodeUi);
            if (ui != nullptr && CallVirtual<int()>(ui, kUiId) == id) return ui;
        }
        return nullptr;
    }

    void PlaySound(const char* name) {
        StrView view{name};
        Sound_Play(&view, nullptr, 1.0f, 1.0f);
    }

    void QueueGlyphs(u8* box) {
        GlyphRequest request{nullptr, nullptr, box};
        UiManager_QueueGlyphs(Manager(), &request, kArchiveSet);
    }

    void Unqueue(u8* box) {
        if (box == nullptr) return;
        for (int i = 0; i < 2; i++) {
            u8* end = Manager() + kUiGlyphQueues + i * kQueueStride;
            for (u8* node = Field<u8*>(end, 8); node != end;) {
                u8* next = Field<u8*>(node, 8);
                if (Field<u8*>(node, kNodeTextBox) == box && Field<void*>(node, kNodeCount) == nullptr) {
                    u8* previous = Field<u8*>(node, 0);
                    Field<u8*>(previous, 8) = next;
                    Field<u8*>(next, 0) = previous;
                    Field<u64>(end, kQueueSize)--;
                    operator_delete(node);
                }
                node = next;
            }
        }
    }

    bool GlyphsDrawn(const u8* box) { return box != nullptr && Field<u8>(box, kTextGlyphsReady) != 0; }

    bool Allocate(u8* box, u16 chars) {
        if (box == nullptr) return false;
        if (Field<u16>(box, kTextCapacity) > chars) return true;
        void* device = GfxDevice();
        if (device == nullptr) return false;
        CallVirtual<void(void*, u16)>(box, kTextAllocate, device, chars);
        return Field<u16>(box, kTextCapacity) > chars;
    }

    Rect TextExtent(u8* box) { return CallVirtual<Rect()>(box, kTextRect); }

    bool GameArchiveLoaded() { return Field<void*>(Manager(), kUiGameArchive) != nullptr; }

    u8* BuildLayout(const char* file, int priority) {
        u8* wrapper = static_cast<u8*>(operator_new(kWrapperSize));
        StrView view{file};
        LayoutWrapper_Build(wrapper, &view, priority, kArchiveSet, true);
        return wrapper;
    }

    void Attach(u8* wrapper, u8* page, u8* pageRoot) { LayoutWrapper_AttachTo(wrapper, page, pageRoot); }

    void Delete(u8* wrapper) {
        if (wrapper != nullptr) CallVirtual<void()>(wrapper, kWrapperDelete);
    }

    u8* LayoutOf(u8* wrapper) { return At<u8*>(wrapper, kWrapperLayout); }

    u8* RootOf(u8* wrapper) { return At<u8*>(LayoutOf(wrapper), kLayoutRoot); }
}
