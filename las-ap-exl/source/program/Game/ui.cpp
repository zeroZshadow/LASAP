#include "Game/ui.hpp"
#include "Game/mem.hpp"
#include "Game/sym.hpp"
#include <algorithm>

namespace Game::Ui {
    namespace {
        constexpr size_t kFindPane = 0x60;
        // Text box virtuals: AllocateStringBuffer (box, gfx device, length), the text's extent.
        constexpr size_t kTextAllocate = 0xe8, kTextExtent = 0x120;
        constexpr int kArchiveSet = 0;  // Game.arc

        Manager& TheManager() { return *Game::UiManager; }

        // The gfx device the UI manager builds layouts with: the graphics object's +8.
        void* GfxDevice() {
            u8* graphics = *Graphics_ptr;
            void** device = At<void**>(graphics, 8);
            return device != nullptr ? *device : nullptr;
        }
    }

    void SetVisible(Pane* pane, bool visible) {
        if (pane == nullptr) return;
        pane->flags = (pane->flags & ~kPaneVisible) | (visible ? kPaneVisible : 0);
    }

    void SetAlpha(Pane* pane, int alpha) {
        if (pane == nullptr) return;
        u8 a = static_cast<u8>(std::clamp(alpha, 0, 255));
        pane->alpha = a;
        pane->globalAlpha = a;
    }

    void MoveTo(Pane* pane, float x, float y) {
        if (pane == nullptr) return;
        pane->x = x;
        pane->y = y;
        pane->flags |= kPaneDirty;
    }

    void ScaleTo(Pane* pane, float x, float y) {
        if (pane == nullptr) return;
        pane->scaleX = x;
        pane->scaleY = y;
        pane->flags |= kPaneDirty;
    }

    void ScaleTo(Pane* pane, float scale) { ScaleTo(pane, scale, scale); }

    Pane* FindPane(Pane* root, const char* name) {
        if (root == nullptr) return nullptr;
        return CallVirtual<Pane*(const char*, bool)>(root, kFindPane, name, true);
    }

    TextBox* FindText(Pane* root, const char* name) { return reinterpret_cast<TextBox*>(FindPane(root, name)); }

    LayoutWrapper* FindUi(int id) {
        ListNode* end = &TheManager().uis.end;
        for (ListNode* node = end->next; node != end; node = node->next) {
            auto* ui = Field<LayoutWrapper*>(node, sizeof(ListNode));
            if (ui != nullptr && CallVirtual<int()>(ui, LayoutWrapperVirtual::kUiId) == id) return ui;
        }
        return nullptr;
    }

    void PlaySound(const char* name) {
        StrView view{name};
        Sound_Play(&view, nullptr, 1.0f, 1.0f);
    }

    void QueueGlyphs(TextBox* box) {
        GlyphRequest request{nullptr, nullptr, box};
        UiManager_QueueGlyphs(&TheManager(), &request, kArchiveSet);
    }

    void Unqueue(TextBox* box) {
        if (box == nullptr) return;
        for (List& queue : TheManager().glyphQueues) {
            for (ListNode* node = queue.end.next; node != &queue.end;) {
                ListNode* next = node->next;
                auto* glyphs = reinterpret_cast<GlyphNode*>(node);
                if (glyphs->textBox == box && glyphs->count == nullptr) {
                    node->previous->next = next;
                    next->previous = node->previous;
                    queue.size--;
                    operator_delete(node);
                }
                node = next;
            }
        }
    }

    bool GlyphsDrawn(const TextBox* box) { return box != nullptr && box->glyphsReady != 0; }

    bool Allocate(TextBox* box, u16 chars) {
        if (box == nullptr) return false;
        if (box->capacity > chars) return true;
        void* device = GfxDevice();
        if (device == nullptr) return false;
        CallVirtual<void(void*, u16)>(box, kTextAllocate, device, chars);
        return box->capacity > chars;
    }

    Rect TextExtent(TextBox* box) { return CallVirtual<Rect()>(box, kTextExtent); }

    bool GameArchiveLoaded() { return TheManager().gameArchive != nullptr; }

    LayoutWrapper* BuildLayout(const char* file, int priority) {
        auto* wrapper = static_cast<LayoutWrapper*>(operator_new(sizeof(LayoutWrapper)));
        StrView view{file};
        LayoutWrapper_Build(wrapper, &view, priority, kArchiveSet, true);
        return wrapper;
    }

    void Attach(LayoutWrapper* wrapper, LayoutWrapper* page, Pane* pageRoot) {
        LayoutWrapper_AttachTo(wrapper, page, pageRoot);
    }

    void Delete(LayoutWrapper* wrapper) {
        if (wrapper != nullptr) CallVirtual<void()>(wrapper, LayoutWrapperVirtual::kDeletingDtor);
    }

    Layout* LayoutOf(LayoutWrapper* wrapper) { return wrapper != nullptr ? wrapper->layout : nullptr; }

    Pane* RootOf(LayoutWrapper* wrapper) {
        Layout* layout = LayoutOf(wrapper);
        return layout != nullptr ? layout->root : nullptr;
    }

    Animation GetAnimation(LayoutWrapper* wrapper, const char* name) {
        StrView view{name};
        return CallVirtual<Animation(const StrView*)>(wrapper, LayoutWrapperVirtual::kAnimation, &view);
    }
}
