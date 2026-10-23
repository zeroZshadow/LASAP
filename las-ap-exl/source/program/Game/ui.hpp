#pragma once
// The game's UI (nn::ui2d through its UI manager): panes, text boxes and layouts of Game.arc built at runtime
// (TESTING.md, "Layouts at runtime", "Text box glyphs"; scripts/re/UI.md).
#include "lib.hpp"
#include "Game/stl.hpp"
#include <cstddef>

namespace Game::Ui {
    // An nn::ui2d pane. Virtual +0x60: FindPaneByName(name, recursive).
    struct Pane {
        const uintptr_t* vtable;
        u8 pad_8[0x28];
        float x, y;                // 0x30: translate
        u8 pad_38[0x10];
        float scaleX, scaleY;      // 0x48
        float width, height;       // 0x50
        u8 flags;                  // 0x58: kPaneVisible, kPaneInfluencedAlpha, kPaneDirty
        u8 alpha;                  // 0x59
        u8 globalAlpha;            // 0x5a: set with the alpha (the layout does not compute it again)
    };
    static_assert(offsetof(Pane, x) == 0x30 && offsetof(Pane, y) == 0x34);
    static_assert(offsetof(Pane, scaleX) == 0x48 && offsetof(Pane, scaleY) == 0x4c);
    static_assert(offsetof(Pane, width) == 0x50 && offsetof(Pane, height) == 0x54);
    static_assert(offsetof(Pane, flags) == 0x58 && offsetof(Pane, alpha) == 0x59 && offsetof(Pane, globalAlpha) == 0x5a);
    constexpr u8 kPaneVisible = 0x01, kPaneInfluencedAlpha = 0x02, kPaneDirty = 0x10;

    // An nn::ui2d text box. Virtual +0xe8: AllocateStringBuffer(gfx device, length); +0x120: the text's extent.
    struct TextBox {
        Pane pane;
        u8 pad_60[0xe8 - sizeof(Pane)];
        u8 colorTop[4];            // 0xe8: the text's colours, RGBA, linear (drawn as sRGB, so 0x5c shows as 0xa2)
        u8 colorBottom[4];         // 0xec
        u8 pad_f0[0x20];
        u16 capacity;              // 0x110: UTF-16 units, with the terminator
        u8 pad_112[0x6];
        float italicRatio;         // 0x118: the glyphs' slant, 0 upright
        u8 pad_11c[0x190 - 0x11c];
        u8 glyphsReady;            // 0x190: set once the font has drawn its glyphs
    };
    static_assert(offsetof(TextBox, colorTop) == 0xe8 && offsetof(TextBox, colorBottom) == 0xec);
    static_assert(offsetof(TextBox, capacity) == 0x110 && offsetof(TextBox, italicRatio) == 0x118);
    static_assert(offsetof(TextBox, glyphsReady) == 0x190);

    // A text object as MessageText_SetLabel and AP::SetPaneText fill it: the module keeps 0x18-byte ones.
    struct TextHolder {
        u8 pad_0[0x10];
        TextBox* box;              // 0x10
    };
    static_assert(offsetof(TextHolder, box) == 0x10 && sizeof(TextHolder) == 0x18);

    // An nn::ui2d layout, which the UI manager animates and calculates each frame while `calculate` is set and draws
    // in the HUD pass, sorted by priority, while `draw` is set.
    struct Layout {
        const uintptr_t* vtable;
        u8 pad_8[0x10];
        Pane* root;                // 0x18
        u8 pad_20[0x50];
        u8 calculate;              // 0x70
        u8 draw;                   // 0x71
    };
    static_assert(offsetof(Layout, root) == 0x18);
    static_assert(offsetof(Layout, calculate) == 0x70 && offsetof(Layout, draw) == 0x71);

    // The game's layout wrapper: a layout of Game.arc and its animations. UIs (the pause menu's pages, the map
    // cursor, the map markers) are wrappers.
    struct LayoutWrapper {
        const uintptr_t* vtable;   // LayoutWrapperVirtual
        u8 pad_8[0x10];
        Layout* layout;            // 0x18
        u8 pad_20[0x58];
    };
    static_assert(offsetof(LayoutWrapper, layout) == 0x18 && sizeof(LayoutWrapper) == 0x78);
    // Its virtuals: the deleting destructor; its layout and root pane (also for parts without a layout of their
    // own); an animation by name (StrView*, the result through x8); the UI's id.
    namespace LayoutWrapperVirtual {
        constexpr size_t kDeletingDtor = 0x18, kLayout = 0x48, kRootPane = 0x50, kAnimation = 0x58, kUiId = 0x60;
    }

    // A layout animation's animator: the frame the layout's update applies (Animator_FrameCount its frame count).
    struct Animator {
        const uintptr_t* vtable;
        u8 pad_8[0x18];
        float frame;               // 0x20
    };
    static_assert(offsetof(Animator, frame) == 0x20);

    // A layout's animation as LayoutWrapperVirtual::kAnimation returns it: its owner and its animator.
    struct Animation {
        SharedPtr owner;
        Animator* animator;
    };
    static_assert(offsetof(Animation, animator) == 0x10 && sizeof(Animation) == 0x18);

    // A UI's state machine (Navi, the pages, the map markers): its state, 0xff none.
    struct StateMachine {
        u8 pad_0[0xd];
        u8 state;                  // 0xd
    };
    static_assert(offsetof(StateMachine, state) == 0xd);

    // The UI manager (the static Game::UiManager).
    struct Manager {
        u8 pad_0[0x88];
        void* gameArchive;         // 0x88: Game.arc's accessor, set once the game's UI is loaded
        u8 pad_90[0x950 - 0x90];
        List uis;                  // 0x950: node +0x10 the UI (a LayoutWrapper)
        u8 pad_968[0xab40 - 0x968];
        List glyphQueues[2];       // 0xab40: GlyphNode
    };
    static_assert(offsetof(Manager, gameArchive) == 0x88 && offsetof(Manager, uis) == 0x950);
    static_assert(offsetof(Manager, glyphQueues) == 0xab40);

    // A text box queued for its glyphs: no `count` for those queued without an owner.
    struct GlyphNode {
        ListNode node;
        void* owner;               // 0x10
        void* count;               // 0x18
        TextBox* textBox;          // 0x20
    };
    static_assert(offsetof(GlyphNode, count) == 0x18 && offsetof(GlyphNode, textBox) == 0x20);

    // The setters do nothing for a null pane.
    void SetVisible(Pane* pane, bool visible);
    void SetAlpha(Pane* pane, int alpha);
    void MoveTo(Pane* pane, float x, float y);
    void ScaleTo(Pane* pane, float x, float y);
    void ScaleTo(Pane* pane, float scale);
    // A pane under root by name (recursive), or null (also for a null root).
    Pane* FindPane(Pane* root, const char* name);
    // A text box under root by name, or null.
    TextBox* FindText(Pane* root, const char* name);

    // The UI of that id in the UI manager's list (Navi 5, overworld map 0xf), or null.
    LayoutWrapper* FindUi(int id);
    constexpr int kNaviUi = 5;
    // Plays a sound effect by name, as the menus do.
    void PlaySound(const char* name);

    // Text boxes. A text box draws nothing until the font has drawn all its glyphs. The game's text box hands each
    // new string to a callback (+0x160) that, for the layouts of a scene, queues it with the UI manager, which then
    // has its glyphs drawn on a worker thread; a layout built here has the default callback, which does nothing, so
    // its text boxes are queued here, without an owner, and taken out of the queue again when dropped before the
    // manager empties it.
    void QueueGlyphs(TextBox* box);
    void Unqueue(TextBox* box);
    bool GlyphsDrawn(const TextBox* box);
    // Room for that many UTF-16 units in a text box (ui2d's AllocateStringBuffer, which frees the old buffer); false
    // if it has less.
    bool Allocate(TextBox* box, u16 chars);
    // The text's extent as laid out, in the text box, unscaled.
    struct Rect {
        float left, top, right, bottom;
        Rect() {}
        ~Rect() {}  // not trivial: returned through x8, as the game's function does
    };
    Rect TextExtent(TextBox* box);

    // Layouts built through the UI manager (a pause menu page is such a wrapper). Game.arc can be read once the
    // game's UI is loaded.
    bool GameArchiveLoaded();
    // The wrapper of a new layout of that file at that priority (1: over the HUD (0), under the menus), or null.
    LayoutWrapper* BuildLayout(const char* file, int priority);
    // Hangs the layout under a page's root pane, so it draws with the page.
    void Attach(LayoutWrapper* wrapper, LayoutWrapper* page, Pane* pageRoot);
    // Takes the layout out (the wrapper's deleting destructor).
    void Delete(LayoutWrapper* wrapper);
    Layout* LayoutOf(LayoutWrapper* wrapper);
    // The wrapper's root pane, or null.
    Pane* RootOf(LayoutWrapper* wrapper);
    // A layout's animation by name; Release(animation.owner) drops it.
    Animation GetAnimation(LayoutWrapper* wrapper, const char* name);
}
