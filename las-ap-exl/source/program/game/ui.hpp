#pragma once
// The game's UI (nn::ui2d through its UI manager): panes, text boxes and layouts of Game.arc built at runtime
// (TESTING.md, "Layouts at runtime", "Text box glyphs"; scripts/re/UI.md).
#include "game/stl.hpp"
#include "lib.hpp"
#include <cstddef>

namespace Game::Ui {
    // An nn::ui2d pane (ui2d_Pane_vtable; 0xd8 bytes). Virtual +0x60: FindPaneByName(name, recursive).
    struct Pane {
        const uintptr_t* vtable;
        ListNode link;             // 0x8: the pane's place in its parent's children
        Pane* parent;              // 0x18
        ListNode children;         // 0x20: nodes at child +0x8
        float x, y, z;             // 0x30: translate
        float rotate[3];           // 0x3c
        float scaleX, scaleY;      // 0x48
        float width, height;       // 0x50
        u8 flags;                  // 0x58: kPane*
        u8 alpha;                  // 0x59
        u8 globalAlpha;            // 0x5a: alpha times the parent's; Calculate sets it each frame for a visible pane
        u8 basePosition;           // 0x5b: bits 0-1 horizontal, 2-3 vertical origin
        u8 flagsEx;                // 0x5c: bit 4 ready to draw (set by Calculate, cleared by the text setters)
        u8 pad_5d[0x3];
        u32 systemFlags;           // 0x60: a bit per system data entry type in extUserData
        u8 pad_64[0xc];
        float globalMatrix[3][4];  // 0x70: computed by Calculate
        const float (*userMatrix)[4];  // 0xa0: with kPaneUserMatrix
        const void* extUserData;   // 0xa8
        char name[0x19];           // 0xb0: 24 characters
        char userData[0x9];        // 0xc9
        u8 pad_d2[0x6];
    };
    static_assert(offsetof(Pane, parent) == 0x18 && offsetof(Pane, children) == 0x20);
    static_assert(offsetof(Pane, x) == 0x30 && offsetof(Pane, rotate) == 0x3c);
    static_assert(offsetof(Pane, scaleX) == 0x48 && offsetof(Pane, scaleY) == 0x4c);
    static_assert(offsetof(Pane, width) == 0x50 && offsetof(Pane, height) == 0x54);
    static_assert(offsetof(Pane, flags) == 0x58 && offsetof(Pane, alpha) == 0x59);
    static_assert(offsetof(Pane, globalAlpha) == 0x5a && offsetof(Pane, flagsEx) == 0x5c);
    static_assert(offsetof(Pane, systemFlags) == 0x60 && offsetof(Pane, globalMatrix) == 0x70);
    static_assert(offsetof(Pane, extUserData) == 0xa8 && offsetof(Pane, name) == 0xb0);
    static_assert(offsetof(Pane, userData) == 0xc9 && sizeof(Pane) == 0xd8);
    // kPaneVisible and kPaneCalculated together draw the pane; kPaneDirty asks for the global matrix again.
    constexpr u8 kPaneVisible = 0x01, kPaneInfluencedAlpha = 0x02, kPaneLocationAdjust = 0x04,
                 kPaneUserAllocated = 0x08, kPaneDirty = 0x10, kPaneUserMatrix = 0x20, kPaneUserGlobalMatrix = 0x40,
                 kPaneCalculated = 0x80;

    // The game's text box (GameTextBox_Make, vtable 0x1c52940; 0x1a0 bytes): an nn::ui2d text box (ui2d_TextBox_vtable,
    // 0x158 bytes) and the game's fields. Virtual +0xe8: AllocateStringBuffer(gfx device, length); +0x120: the text's
    // extent.
    struct TextBox {
        Pane pane;
        char16_t* text;            // 0xd8: UTF-8 bytes when utf8
        const char* textId;        // 0xe0: the resource's text id
        u8 colorTop[4];            // 0xe8: the text's colours, RGBA, linear (drawn as sRGB, so 0x5c shows as 0xa2)
        u8 colorBottom[4];         // 0xec
        void* font;                // 0xf0: nn::font::Font
        float fontSize[2];         // 0xf8
        float lineSpace;           // 0x100
        float charSpace;           // 0x104
        void* tagProcessor;        // 0x108: the TagProcessor that draws button tags; null without extended tags
        u16 capacity;              // 0x110: UTF-16 units, with the terminator
        u16 length;                // 0x112
        u16 bits;                  // 0x114: bits 0-1 line alignment, 2 text dirty, 3 shadow, 6 width limit
        u8 textPosition;           // 0x116: bits 0-1 horizontal (0 centre, 1 left, 2 right), 2-3 vertical
        u8 utf8;                   // 0x117
        float italicRatio;         // 0x118: the glyphs' slant, 0 upright
        float shadowOffset[2];     // 0x11c
        float shadowScale[2];      // 0x124
        u8 shadowColorTop[4];      // 0x12c
        u8 shadowColorBottom[4];   // 0x130
        float shadowItalicRatio;   // 0x134
        void* lineWidthOffset;     // 0x138
        void* material;            // 0x140
        void* drawBuffer;          // 0x148: font::DispStringBuffer
        void* perCharacterTransform;  // 0x150
        u8 pad_158[0x8];
        Function onSetString;      // 0x160: called by every game setter; a scene layout's queues the glyphs
        u8 glyphsReady;            // 0x190: set once the font has drawn its glyphs
        u8 field_191;              // 0x191: set before a game string is set
        u8 notDrawn;               // 0x192: until the first DrawSelf
        u8 pad_193[0xd];
    };
    static_assert(offsetof(TextBox, text) == 0xd8 && offsetof(TextBox, colorTop) == 0xe8);
    static_assert(offsetof(TextBox, colorBottom) == 0xec && offsetof(TextBox, tagProcessor) == 0x108);
    static_assert(offsetof(TextBox, capacity) == 0x110 && offsetof(TextBox, bits) == 0x114);
    static_assert(offsetof(TextBox, italicRatio) == 0x118 && offsetof(TextBox, shadowItalicRatio) == 0x134);
    static_assert(offsetof(TextBox, perCharacterTransform) == 0x150 && offsetof(TextBox, onSetString) == 0x160);
    static_assert(offsetof(TextBox, glyphsReady) == 0x190 && sizeof(TextBox) == 0x1a0);

    // A text box as LayoutWrapper_FindTextBox returns it and MessageText_SetLabel and AP::SetPaneText fill it: the
    // layout's shared_ptr and the box.
    struct TextHolder {
        SharedPtr layout;
        TextBox* box;              // 0x10
    };
    static_assert(offsetof(TextHolder, box) == 0x10 && sizeof(TextHolder) == 0x18);

    // A pane as LayoutWrapper_FindPane returns it.
    struct PaneHandle {
        SharedPtr layout;
        Pane* pane;                // 0x10
    };
    static_assert(offsetof(PaneHandle, pane) == 0x10 && sizeof(PaneHandle) == 0x18);

    // The game's nn::ui2d layout (vtable 0x1c520c8; 0x78 bytes), which the UI manager animates and calculates each
    // frame while `calculate` is set and draws in the HUD pass, sorted by priority, while `draw` is set.
    struct Layout {
        const uintptr_t* vtable;
        ListNode animators;        // 0x8: Animator::link
        Pane* root;                // 0x18: an nn::ui2d::Parts pane
        void* groups;              // 0x20
        float size[2];             // 0x28
        const char* name;          // 0x30: its animations are <name>_<animation>.bflan
        const void* extUserData;   // 0x38
        void* resources;           // 0x40: the archive's resource accessor
        ListNode parts;            // 0x48: the parts panes' layouts
        void* field_58;            // 0x58
        SharedPtr archive;         // 0x60: the UI archive it was built from
        u8 calculate;              // 0x70
        u8 draw;                   // 0x71
        u8 pad_72[0x6];
    };
    static_assert(offsetof(Layout, root) == 0x18 && offsetof(Layout, name) == 0x30);
    static_assert(offsetof(Layout, resources) == 0x40 && offsetof(Layout, archive) == 0x60);
    static_assert(offsetof(Layout, calculate) == 0x70 && offsetof(Layout, draw) == 0x71 && sizeof(Layout) == 0x78);

    // The game's layout wrapper (LayoutWrapper_Build, vtable 0x1c27230; 0x78 bytes): a layout of Game.arc and its
    // animations. UIs (the pause menu's pages, the map cursor, the map markers) are wrappers.
    struct LayoutWrapper {
        const uintptr_t* vtable;   // LayoutWrapperVirtual
        WeakPtr weakThis;          // 0x8
        Layout* layout;            // 0x18: a shared_ptr
        void* layoutCtrl;          // 0x20
        u64 field_28;              // 0x28
        WeakPtr parentLayout;      // 0x30: the layout its root hangs in, once attached
        SharedPtr attachLayout;    // 0x40: the parent wrapper's layout while attached
        Pane* attachPane;          // 0x50: the pane its root hangs under; the layout calculates itself only without
        SharedPtr homeKeep;        // 0x58: released when the root leaves its first parent
        Pane* homePane;            // 0x68: the root's own parent before it was attached
        u8 active;                 // 0x70: updated each frame while set; once cleared, the manager closes the UI
        u8 openPending;            // 0x71: the next update opens the UI again
        u8 activeOnOpen;           // 0x72
        u8 archiveSet;             // 0x73: 0 Game.arc, 1 StartUp.arc
        u8 pad_74[0x4];
    };
    static_assert(offsetof(LayoutWrapper, layout) == 0x18 && offsetof(LayoutWrapper, parentLayout) == 0x30);
    static_assert(offsetof(LayoutWrapper, attachPane) == 0x50 && offsetof(LayoutWrapper, homePane) == 0x68);
    static_assert(offsetof(LayoutWrapper, active) == 0x70 && offsetof(LayoutWrapper, archiveSet) == 0x73);
    static_assert(sizeof(LayoutWrapper) == 0x78);
    // Its virtuals: the deleting destructor; Update, run each frame while active; its layout and root pane (also for
    // parts without a layout of their own); an animation by name (StrView*, the result through x8); the UI's id.
    namespace LayoutWrapperVirtual {
        constexpr size_t kDeletingDtor = 0x18, kUpdate = 0x28, kLayout = 0x48, kRootPane = 0x50, kAnimation = 0x58,
                         kUiId = 0x60;
    }

    // A layout animation's animator (an nn::ui2d::GroupArrayAnimator, ui2d_GroupArrayAnimator_vtable; 0x58 bytes and
    // the group pointers): the frame the layout's update applies (Animator_FrameCount its frame count).
    struct Animator {
        const uintptr_t* vtable;
        ListNode link;             // 0x8: in Layout::animators
        const u8* resource;        // 0x18: the animation block, its frame count at +0x8
        float frame;               // 0x20
        bool enabled;              // 0x24
        u8 pad_25[0x3];
        void* textures;            // 0x28
        void* bindPairs;           // 0x30
        u16 bindPairCount;         // 0x38
        u16 bindPairMax;           // 0x3a
        float speed;               // 0x3c: frames per step, 0 stopped
        s32 playType;              // 0x40: 0 once, 1 loop, 2 back and forth
        u32 playState;             // 0x44: bit 0 stopped at an end, 1 wrapped at the end, 2 at the start
        void** groups;             // 0x48
        s32 groupCount;            // 0x50
        u8 pad_54[0x4];
    };
    static_assert(offsetof(Animator, resource) == 0x18 && offsetof(Animator, frame) == 0x20);
    static_assert(offsetof(Animator, speed) == 0x3c && offsetof(Animator, playState) == 0x44);
    static_assert(offsetof(Animator, groups) == 0x48 && sizeof(Animator) == 0x58);

    // A layout's animation as LayoutWrapperVirtual::kAnimation returns it: the layout's shared_ptr and the animator.
    struct Animation {
        SharedPtr owner;
        Animator* animator;
    };
    static_assert(offsetof(Animation, animator) == 0x10 && sizeof(Animation) == 0x18);

    // A part (Part_vtable; 0x90 bytes): a wrapper bound to an L_ part pane of its page (Part_Bind).
    struct Part {
        LayoutWrapper wrapper;
        PaneHandle pane;           // 0x78
    };
    static_assert(offsetof(Part, pane) == 0x78 && sizeof(Part) == 0x90);

    // The text tag processor (TagProcessor_Ctor, vtable 0x1bda908; 0x118 bytes) a text box's tagProcessor points at:
    // it draws the button tags of the guide and the message window.
    struct TagProcessor {
        const uintptr_t* vtable;
        u32 color;                 // 0x8
        u8 pad_c[0x4];
        SharedPtr rubyFont;        // 0x10: ui/jpn_ruby.bffnt
        u8 pad_20[0x8];
        SharedPtr pictogramFont;   // 0x28: ui/pictogram.bffnt
        u8 pad_38[0xd0];
        float scale[2];            // 0x108: 1, 1
        s32 field_110;             // 0x110: -1
        u8 pad_114[0x4];
    };
    static_assert(offsetof(TagProcessor, rubyFont) == 0x10 && offsetof(TagProcessor, pictogramFont) == 0x28);
    static_assert(offsetof(TagProcessor, scale) == 0x108 && sizeof(TagProcessor) == 0x118);

    // The UI manager (the static Game::UiManager, UiManager_Ctor).
    struct Manager {
        u8 pad_0[0x88];
        SharedPtr archives[6];     // 0x88: per archive set s, slots s, s + 2 and s + 4; slot 0 Game.arc, set once the
                                   // game's UI is loaded
        u8 pad_e8[0x20];
        void* hudPass;             // 0x108
        Function factories[44];    // 0x110: per UI id
        List uis;                  // 0x950: node +0x10 the UI (a LayoutWrapper), +0x18 its count
        List requests;             // 0x968
        List closes;               // 0x980: UI ids to close at the next update
        List removals;             // 0x998
        u8 font[0x38];             // 0x9b0
        u8 glyphCache[0x1ec8];     // 0x9e8
        s32 field_28b0;            // 0x28b0
        s32 glyphWorkerState;      // 0x28b4: 1 running, 2 done
        u8 glyphWorker[0x78];      // 0x28b8
        s32 field_2930;            // 0x2930
        u8 pad_2934[0x4];
        u8 lockGroups[32][0x410];  // 0x2938: a font lock group per UI or archive id
        s32 glyphQueueIndex;       // 0xab38: the queue new boxes go to
        u8 pad_ab3c[0x4];
        List glyphQueues[2];       // 0xab40: GlyphNode
        u8 field_ab70;             // 0xab70
    };
    static_assert(offsetof(Manager, archives) == 0x88 && offsetof(Manager, hudPass) == 0x108);
    static_assert(offsetof(Manager, factories) == 0x110 && offsetof(Manager, uis) == 0x950);
    static_assert(offsetof(Manager, closes) == 0x980 && offsetof(Manager, font) == 0x9b0);
    static_assert(offsetof(Manager, glyphWorkerState) == 0x28b4 && offsetof(Manager, lockGroups) == 0x2938);
    static_assert(offsetof(Manager, glyphQueueIndex) == 0xab38 && offsetof(Manager, glyphQueues) == 0xab40);
    static_assert(offsetof(Manager, field_ab70) == 0xab70);

    // A text box queued for its glyphs: no `count` for those queued without an owner.
    struct GlyphNode {
        ListNode node;
        void* owner;               // 0x10
        void* count;               // 0x18: the owner's shared_ptr count
        TextBox* textBox;          // 0x20
        s32 archiveSet;            // 0x28
        u8 pad_2c[0x4];
    };
    static_assert(offsetof(GlyphNode, count) == 0x18 && offsetof(GlyphNode, textBox) == 0x20);
    static_assert(sizeof(GlyphNode) == 0x30);

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
