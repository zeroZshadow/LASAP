#pragma once
// The game's UI (nn::ui2d through its UI manager): panes, text boxes and layouts of Game.arc built at runtime
// (TESTING.md, "Layouts at runtime", "Text box glyphs").
#include "lib.hpp"

namespace Game::Ui {
    // nn::ui2d pane fields: +0x30 translate x, y; +0x48 scale x, y; +0x50 size x, y; +0x58 flags; +0x59 alpha, +0x5a
    // global alpha (set with the alpha: the layout does not compute it again).
    constexpr size_t kPaneX = 0x30, kPaneY = 0x34, kPaneScaleX = 0x48, kPaneScaleY = 0x4c, kPaneWidth = 0x50,
                     kPaneHeight = 0x54, kPaneFlags = 0x58, kPaneAlpha = 0x59, kPaneGlobalAlpha = 0x5a;
    constexpr u8 kPaneVisible = 0x01, kPaneInfluencedAlpha = 0x02, kPaneDirty = 0x10;
    // The setters do nothing for a null pane.
    void SetVisible(u8* pane, bool visible);
    void SetAlpha(u8* pane, int alpha);
    void MoveTo(u8* pane, float x, float y);
    void ScaleTo(u8* pane, float x, float y);
    void ScaleTo(u8* pane, float scale);
    // A pane under root by name (the root pane's virtual +0x60, recursive), or null (also for a null root).
    u8* FindPane(u8* root, const char* name);

    // The UI of that id in the UI manager's list (Navi 5, overworld map 0xf), or null.
    u8* FindUi(int id);
    constexpr int kNaviUi = 5;
    // Plays a sound effect by name, as the menus do.
    void PlaySound(const char* name);

    // Text boxes. A text box draws nothing until the font has drawn all its glyphs (+0x190, set by its calculation
    // once they are). The game's text box hands each new string to a callback (+0x160) that, for the layouts of a
    // scene, queues it with the UI manager, which then has its glyphs drawn on a worker thread; a layout built here
    // has the default callback, which does nothing, so its text boxes are queued here, without an owner, and taken
    // out of the queue again when dropped before the manager empties it.
    void QueueGlyphs(u8* box);
    void Unqueue(u8* box);
    bool GlyphsDrawn(const u8* box);
    // Room for that many UTF-16 units in a text box (ui2d's AllocateStringBuffer, which frees the old buffer); false
    // if it has less.
    bool Allocate(u8* box, u16 chars);
    // The text's extent as laid out (text box virtual +0x120), in the text box, unscaled.
    struct Rect {
        float left, top, right, bottom;
        Rect() {}
        ~Rect() {}  // not trivial: returned through x8, as the game's function does
    };
    Rect TextExtent(u8* box);

    // Layouts: a layout wrapper (0x78 bytes; +0x18 its layout, whose +0x18 is the root pane) builds a layout of
    // Game.arc through the UI manager, which animates and calculates it each frame while the layout's +0x70 is set
    // and draws it in the HUD pass, sorted by priority, while its +0x71 is set. A pause menu page is such a wrapper.
    // Game.arc can be read once the game's UI is loaded.
    bool GameArchiveLoaded();
    // The wrapper of a new layout of that file at that priority (1: over the HUD (0), under the menus), or null.
    u8* BuildLayout(const char* file, int priority);
    // Hangs the layout under a page's root pane, so it draws with the page.
    void Attach(u8* wrapper, u8* page, u8* pageRoot);
    // Takes the layout out (the wrapper's deleting destructor).
    void Delete(u8* wrapper);
    u8* LayoutOf(u8* wrapper);
    // The wrapper's root pane, or null.
    u8* RootOf(u8* wrapper);
    constexpr size_t kLayoutCalculate = 0x70, kLayoutDraw = 0x71;
}
