// Multiplayer ghosts' name tags (DESIGN.md, Ghosts): each shown ghost's slot name above its head, small, in the game's
// action prompt (Gm_HUDInteract.bflyt of Game.arc: text in a dark rounded window, without its button picture) built as
// a layout of its own and placed on the screen every frame. All on the game thread.
#include "AP/ap.hpp"
#include "Game/framework.hpp"
#include "offsets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    struct StringView { const char* ptr; u64 len; };

    // Layouts at runtime (game 1.0.0): the layout wrapper's constructor (wrapper 0x78 bytes, {name, length}*,
    // priority, archive set 0 = Game.arc, flag) builds the layout through the UI manager (the global object at
    // 0x1c971c0; +0x88 its Game.arc accessor, set once the game's UI is loaded), which animates and calculates it
    // each frame while its +0x70 is set and draws it in the HUD pass, sorted by priority, while its +0x71 is set.
    // The wrapper's deleting destructor (vtable +0x18) takes it out. Priority 1: over the HUD (0), under the menus,
    // the map, fades and the message window.
    constexpr uintptr_t kOperatorNew = 0x1431170, kWrapperCtor = 0xe59100, kUiManager = 0x1c971c0;
    constexpr size_t kUiGameArchive = 0x88, kWrapperSize = 0x78, kWrapperLayout = 0x18, kWrapperDelete = 0x18 / 8;
    constexpr size_t kLayoutRoot = 0x18, kLayoutCalculate = 0x70, kLayoutDraw = 0x71;
    constexpr int kPriority = 1;
    // nn::ui2d panes: +0x30 translate x, y; +0x48 scale x; +0x50 size x; +0x58 flags (bit 4: matrix dirty); +0x59
    // alpha, +0x5a global alpha (set with the alpha here: the layout does not compute it again). The root pane's
    // virtual +0x60 finds a pane by name (pane, name, recursive). The game's text box: virtual +0x120 the text's
    // extent (left, top, right, bottom, returned through x8). Its text is set through the game's message pipeline
    // (AP::SetPaneText). The text box draws nothing until the font has drawn all its glyphs (+0x190, set by its
    // calculation once they are). The game's text box hands each new string to a callback (+0x160) that, for the
    // layouts of a scene, queues the text box with the UI manager (0xeda8d0: manager, {owner, shared count, text box},
    // archive set); the manager then has its glyphs drawn on a worker thread. Ours has the default callback, which
    // does nothing, so the tag queues its text box itself, without an owner. The queues: two std::lists at manager
    // +0xab40 (+0x18 each: end node {last, first}, size), nodes {previous, next, owner, count, text box, set}, emptied
    // by the manager on the game thread once the glyphs are drawn; a tag dropped before then takes its text box out.
    constexpr size_t kPaneX = 0x30, kPaneY = 0x34, kPaneScaleX = 0x48, kPaneScaleY = 0x4c, kPaneWidth = 0x50,
                     kPaneHeight = 0x54, kPaneFlags = 0x58, kPaneAlpha = 0x59, kPaneGlobalAlpha = 0x5a;
    constexpr u8 kPaneVisible = 0x01, kPaneDirty = 0x10;
    constexpr size_t kFindPane = 0x60 / 8, kTextRect = 0x120 / 8, kHolderPane = 0x10, kTextGlyphsReady = 0x190;
    constexpr uintptr_t kQueueGlyphs = 0xeda8d0, kOperatorDelete = 0x1431110;
    constexpr size_t kUiGlyphQueues = 0xab40, kQueueStride = 0x18, kQueueSize = 0x10, kNodeCount = 0x18,
                     kNodeTextBox = 0x20;
    constexpr int kArchiveSet = 0;  // Game.arc
    // Gm_HUDInteract: a group (alpha 0 in the file: an animation fades the prompt in, scale 2/3) holding a window, its
    // text and the button's picture, the window and the text placed from their left edge. The tag shows the group,
    // hides the picture, scales the root, centres the text on the root and stretches the window around it.
    constexpr const char* kLayoutFile = "Gm_HUDInteract.bflyt", *kGroupPane = "N_Interact_00",
                          *kWindowPane = "W_Interact_00", *kTextPane = "T_Interact_00", *kPicturePane = "P_pict_00";
    constexpr size_t kNameMax = 16;  // Archipelago's slot names
    // The text box holds 13 characters (14 with the terminator in the file); the tag gives it room for kTextChars
    // UTF-16 units through ui2d's TextBox::AllocateStringBuffer (virtual +0xe8: text box, gfx device, length), which
    // frees the old buffer. The gfx device the UI manager builds layouts with: *(the graphics object at 0x1cc8b30)[1].
    constexpr u16 kTextChars = 2 * kNameMax;
    constexpr size_t kTextAllocate = 0xe8 / 8, kTextCapacity = 0x110;
    constexpr uintptr_t kGraphics = 0x1cc8b30;
    constexpr float kTextWidth = 1000;  // the text box's width: the pipeline breaks lines wider than that (150 in the file)
    // The look: the prompt at this scale, the text and the window at these alphas, the window this much wider and
    // higher than the text, the tag this far above the projected point (layout units).
    float g_Scale = 0.6f, g_Raise = 0, g_PadX = 24, g_PadY = 8;
    // The text box's line holds more room above the letters than below them: the text goes up this part of its
    // height, so the letters (capital top to baseline) sit in the window's middle.
    constexpr float kTextLift = 0.14f;
    u8 g_Alpha = 230, g_BackAlpha = 200;
    // Projection (WorldToLayout, 0xf88c40): the camera at Framework +0x560, view rows at +0x20..+0x50 (the last the
    // translation), projection rows at +0xa0..+0xd0; layout coordinates are NDC times (640, 360), y up. Hidden when
    // the head is behind the camera or off screen.
    constexpr size_t kFrameworkCamera = 0x560, kCameraView = 0x20, kCameraProjection = 0xa0;
    constexpr float kHalfWidth = 640, kHalfHeight = 360, kMargin = 120, kNear = 0.01f;

    struct Rect {
        float left, top, right, bottom;
        Rect() {}
        ~Rect() {}  // not trivial: returned through x8, as the game's function does
    };

    struct GlyphRequest {
        void* owner;
        void* count;
        void* textBox;
    };

    struct Tag {
        u8* wrapper = nullptr;
        u8* layout = nullptr;
        u8* root = nullptr;
        u8* text = nullptr;
        u8* window = nullptr;
        std::string name;
        bool drawn = false;
        bool measured = false;  // laid out with the name's glyphs drawn
        float extent[4] = {};   // the text's extent it was laid out with (left, top, right, bottom)
        alignas(8) u8 holder[0x18] = {};  // a text object for AP::SetPaneText: +0x10 the text pane
    };
    Tag g_Tags[AP::GhostNet::SLOTS];

    template<typename T>
    T GameFn(uintptr_t offset) { return reinterpret_cast<T>(g_BaseAddress + offset_manager.Offset(offset)); }

    uintptr_t* VTable(const void* object) { return *reinterpret_cast<uintptr_t* const*>(object); }

    template<typename T>
    T& Field(void* base, size_t offset) { return *reinterpret_cast<T*>(static_cast<u8*>(base) + offset); }

    u8* FindPane(u8* root, const char* name) {
        return reinterpret_cast<u8* (*)(void*, const char*, bool)>(VTable(root)[kFindPane])(root, name, true);
    }

    u8* UiManager() { return reinterpret_cast<u8*>(g_BaseAddress + kUiManager); }

    // Takes a text box out of the UI manager's glyph queues (nodes without an owner: the tags').
    void Unqueue(u8* textBox) {
        for (int i = 0; i < 2; i++) {
            u8* end = UiManager() + kUiGlyphQueues + i * kQueueStride;
            for (u8* node = Field<u8*>(end, 8); node != end;) {
                u8* next = Field<u8*>(node, 8);
                if (Field<u8*>(node, kNodeTextBox) == textBox && Field<void*>(node, kNodeCount) == nullptr) {
                    u8* previous = Field<u8*>(node, 0);
                    Field<u8*>(previous, 8) = next;
                    Field<u8*>(next, 0) = previous;
                    Field<u64>(end, kQueueSize)--;
                    GameFn<void (*)(void*)>(kOperatorDelete)(node);
                }
                node = next;
            }
        }
    }

    void Drop(Tag& tag) {
        if (tag.text != nullptr) Unqueue(tag.text);
        if (tag.wrapper != nullptr)
            reinterpret_cast<void (*)(void*)>(VTable(tag.wrapper)[kWrapperDelete])(tag.wrapper);
        tag = Tag{};
    }

    bool Build(Tag& tag) {
        if (*reinterpret_cast<void**>(g_BaseAddress + kUiManager + kUiGameArchive) == nullptr) return false;
        tag.wrapper = GameFn<u8* (*)(size_t)>(kOperatorNew)(kWrapperSize);
        StringView file{kLayoutFile, strlen(kLayoutFile)};
        GameFn<void (*)(void*, StringView*, int, int, bool)>(kWrapperCtor)(tag.wrapper, &file, kPriority, 0, true);
        tag.layout = Field<u8*>(tag.wrapper, kWrapperLayout);
        tag.root = tag.layout != nullptr ? Field<u8*>(tag.layout, kLayoutRoot) : nullptr;
        tag.text = tag.root != nullptr ? FindPane(tag.root, kTextPane) : nullptr;
        tag.window = tag.root != nullptr ? FindPane(tag.root, kWindowPane) : nullptr;
        u8* group = tag.root != nullptr ? FindPane(tag.root, kGroupPane) : nullptr;
        u8* picture = tag.root != nullptr ? FindPane(tag.root, kPicturePane) : nullptr;
        u8* graphics = *reinterpret_cast<u8**>(g_BaseAddress + kGraphics);
        void* device = graphics != nullptr && Field<void**>(graphics, 8) != nullptr ? *Field<void**>(graphics, 8) : nullptr;
        if (tag.text == nullptr || tag.window == nullptr || group == nullptr || picture == nullptr || device == nullptr) {
            Drop(tag);
            return false;
        }
        reinterpret_cast<void (*)(void*, void*, u16)>(VTable(tag.text)[kTextAllocate])(tag.text, device, kTextChars);
        if (Field<u16>(tag.text, kTextCapacity) <= kTextChars) {
            Drop(tag);
            return false;
        }
        // the group at the root's scale, the window at the text's
        Field<u8>(group, kPaneAlpha) = 255;
        Field<u8>(group, kPaneGlobalAlpha) = 255;
        Field<float>(group, kPaneScaleX) = 1;
        Field<float>(group, kPaneScaleY) = 1;
        Field<float>(tag.window, kPaneScaleX) = 1;
        Field<float>(tag.window, kPaneScaleY) = 1;
        Field<u8>(picture, kPaneFlags) &= ~kPaneVisible;
        Field<float>(tag.text, kPaneWidth) = kTextWidth;
        Field<u8>(tag.text, kPaneAlpha) = g_Alpha;
        Field<u8>(tag.text, kPaneGlobalAlpha) = g_Alpha;
        Field<u8>(tag.window, kPaneAlpha) = g_BackAlpha;
        Field<u8>(tag.window, kPaneGlobalAlpha) = g_BackAlpha;
        Field<float>(tag.root, kPaneScaleX) = g_Scale;
        Field<float>(tag.root, kPaneScaleY) = g_Scale;
        Field<u8*>(tag.holder, kHolderPane) = tag.text;
        Field<u8>(tag.layout, kLayoutCalculate) = 0;
        Field<u8>(tag.layout, kLayoutDraw) = 0;
        return true;
    }

    // At most kNameMax characters of a UTF-8 name, without control characters (the message pipeline's tags).
    std::string Clip(const std::string& name) {
        std::string out;
        size_t chars = 0;
        for (size_t i = 0; i < name.size() && chars < kNameMax; chars++) {
            u8 c = static_cast<u8>(name[i]);
            size_t n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            if (c >= 0x20) out.append(name, i, n);
            i += n;
        }
        return out;
    }

    // Centred on the root: the text's middle at x 0, the window's bottom at g_Raise, the window around the text.
    void Layout(Tag& tag) {
        Rect extent = reinterpret_cast<Rect (*)(void*)>(VTable(tag.text)[kTextRect])(tag.text);
        tag.extent[0] = extent.left;
        tag.extent[1] = extent.top;
        tag.extent[2] = extent.right;
        tag.extent[3] = extent.bottom;
        if (std::isfinite(extent.left + extent.right + extent.top + extent.bottom)) {
            float width = std::fabs(extent.right - extent.left), height = std::fabs(extent.top - extent.bottom);
            float middle = g_Raise + g_PadY + height / 2;
            Field<float>(tag.text, kPaneX) = -(extent.left + extent.right) / 2;
            Field<float>(tag.text, kPaneY) = middle - (extent.top + extent.bottom) / 2 + kTextLift * height;
            Field<float>(tag.window, kPaneX) = -width / 2 - g_PadX;
            Field<float>(tag.window, kPaneY) = middle;
            Field<float>(tag.window, kPaneWidth) = width + 2 * g_PadX;
            Field<float>(tag.window, kPaneHeight) = height + 2 * g_PadY;
        }
        Field<u8>(tag.text, kPaneFlags) |= kPaneDirty;
        Field<u8>(tag.window, kPaneFlags) |= kPaneDirty;
    }

    void SetName(Tag& tag, const std::string& name) {
        AP::SetPaneText(tag.holder, Clip(name));
        tag.name = name;
        GlyphRequest request{nullptr, nullptr, tag.text};
        GameFn<bool (*)(void*, GlyphRequest*, int)>(kQueueGlyphs)(UiManager(), &request, kArchiveSet);
        tag.measured = false;
        Layout(tag);
    }

    // A world position on the screen in layout coordinates, or false when it is behind the camera or off screen.
    bool Project(const Game::Vec3& at, float& x, float& y) {
        Game::Framework* fmwk = Game::GetFramework();
        const u8* camera = fmwk != nullptr ? *reinterpret_cast<u8* const*>(reinterpret_cast<u8*>(fmwk) + kFrameworkCamera)
                                           : nullptr;
        if (camera == nullptr) return false;
        const float* view = reinterpret_cast<const float*>(camera + kCameraView);
        const float* proj = reinterpret_cast<const float*>(camera + kCameraProjection);
        float v[4];
        for (int i = 0; i < 4; i++) v[i] = at.x * view[i] + at.y * view[4 + i] + at.z * view[8 + i] + view[12 + i];
        v[3] = at.x * view[3] + at.y * view[7] + at.z * view[11] + 1;
        float clip[4];
        for (int i = 0; i < 4; i++) clip[i] = v[0] * proj[i] + v[1] * proj[4 + i] + v[2] * proj[8 + i] + v[3] * proj[12 + i];
        if (!(clip[3] > kNear)) return false;
        x = clip[0] / clip[3] * kHalfWidth;
        y = clip[1] / clip[3] * kHalfHeight;
        return std::fabs(x) < kHalfWidth + kMargin && std::fabs(y) < kHalfHeight + kMargin;
    }

    void SetDrawn(Tag& tag, bool drawn) {
        if (tag.layout == nullptr || tag.drawn == drawn) return;
        Field<u8>(tag.layout, kLayoutCalculate) = drawn;
        Field<u8>(tag.layout, kLayoutDraw) = drawn;
        tag.drawn = drawn;
    }
}

namespace AP::NameTag {
    void Show(int n, const Game::Vec3& head, const std::string& name) {
        if (n < 0 || n >= GhostNet::SLOTS) return;
        Tag& tag = g_Tags[n];
        if (tag.wrapper == nullptr && !Build(tag)) return;
        if (name != tag.name) SetName(tag, name);
        // again once the font has drawn the name's glyphs: the extent can change with them
        if (!tag.measured && Field<u8>(tag.text, kTextGlyphsReady)) {
            Layout(tag);
            tag.measured = true;
        }
        float x, y;
        if (!Project(head, x, y)) {
            SetDrawn(tag, false);
            return;
        }
        Field<float>(tag.root, kPaneX) = x;
        Field<float>(tag.root, kPaneY) = y;
        Field<u8>(tag.root, kPaneFlags) |= kPaneDirty;
        SetDrawn(tag, true);
    }

    void Hide(int n) {
        if (n >= 0 && n < GhostNet::SLOTS) SetDrawn(g_Tags[n], false);
    }

    void DropAll() {
        for (auto& tag : g_Tags) Drop(tag);
    }

    void Style(float scale, int alpha, int backAlpha, float raise, float padX, float padY) {
        DropAll();
        g_Scale = scale;
        g_Alpha = static_cast<u8>(std::clamp(alpha, 0, 255));
        g_BackAlpha = static_cast<u8>(std::clamp(backAlpha, 0, 255));
        g_Raise = raise;
        g_PadX = padX;
        g_PadY = padY;
    }

#ifdef LASAP_TEST
    // DBG puppet tags: each tag's text extent (left, top, right, bottom) as laid out
    std::string Info() {
        std::string s = "TAGS";
        for (int n = 0; n < GhostNet::SLOTS; n++) {
            const Tag& tag = g_Tags[n];
            if (tag.wrapper == nullptr) continue;
            char buf[96];
            snprintf(buf, sizeof(buf), " | %d %.1f,%.1f,%.1f,%.1f%s", n, tag.extent[0], tag.extent[1], tag.extent[2],
                     tag.extent[3], tag.measured ? " measured" : "");
            s += buf;
        }
        return s;
    }
#endif

    std::string State() {
        std::string s = "tags=";
        for (auto& tag : g_Tags)
            s += tag.wrapper == nullptr ? '-' : !tag.drawn ? 'h' : Field<u8>(tag.text, kTextGlyphsReady) ? 'd' : 'g';
        return s;
    }
}
