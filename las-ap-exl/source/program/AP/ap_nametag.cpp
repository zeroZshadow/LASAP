// Multiplayer ghosts' name tags (DESIGN.md, Ghosts): each shown ghost's slot name above its head in small, see-through
// text, the game's area name banner (Gm_HUDLocation.bflyt of Game.arc: white text with a dark edge) built as a layout
// of its own and placed on the screen every frame. All on the game thread.
#include "AP/ap.hpp"
#include "Game/framework.hpp"
#include "offsets.hpp"
#include <algorithm>
#include <cmath>
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
    constexpr size_t kPaneX = 0x30, kPaneY = 0x34, kPaneScaleX = 0x48, kPaneScaleY = 0x4c, kPaneFlags = 0x58,
                     kPaneAlpha = 0x59, kPaneGlobalAlpha = 0x5a;
    constexpr u8 kPaneDirty = 0x10;
    constexpr size_t kFindPane = 0x60 / 8, kTextRect = 0x120 / 8, kHolderPane = 0x10, kTextGlyphsReady = 0x190;
    constexpr uintptr_t kQueueGlyphs = 0xeda8d0, kOperatorDelete = 0x1431110;
    constexpr size_t kUiGlyphQueues = 0xab40, kQueueStride = 0x18, kQueueSize = 0x10, kNodeCount = 0x18,
                     kNodeTextBox = 0x20;
    constexpr int kArchiveSet = 0;  // Game.arc
    // Gm_HUDLocation: the text in a group whose alpha is 0 in the file (an animation fades the banner in), placed at
    // the screen's left; the tag sets both alphas, scales the root and centres the text on the root.
    constexpr const char* kLayoutFile = "Gm_HUDLocation.bflyt", *kTextPane = "T_Location_00", *kGroupPane = "N_Location_00";
    constexpr size_t kNameMax = 30;  // the text box takes 40 characters
    // The look: the banner's text at this scale and alpha, this far above the projected point (layout units).
    float g_Scale = 0.45f, g_Raise = 0;
    u8 g_Alpha = 140;
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
        std::string name;
        bool drawn = false;
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
        u8* group = tag.root != nullptr ? FindPane(tag.root, kGroupPane) : nullptr;
        if (tag.text == nullptr || group == nullptr) {
            Drop(tag);
            return false;
        }
        Field<u8>(group, kPaneAlpha) = 255;
        Field<u8>(group, kPaneGlobalAlpha) = 255;
        Field<u8>(tag.text, kPaneAlpha) = g_Alpha;
        Field<u8>(tag.text, kPaneGlobalAlpha) = g_Alpha;
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

    void SetName(Tag& tag, const std::string& name) {
        AP::SetPaneText(tag.holder, Clip(name));
        tag.name = name;
        GlyphRequest request{nullptr, nullptr, tag.text};
        GameFn<bool (*)(void*, GlyphRequest*, int)>(kQueueGlyphs)(UiManager(), &request, kArchiveSet);
        // centred on the root: the text's middle at x 0, its bottom at g_Raise
        Rect extent = reinterpret_cast<Rect (*)(void*)>(VTable(tag.text)[kTextRect])(tag.text);
        if (std::isfinite(extent.left + extent.right + extent.bottom)) {
            Field<float>(tag.text, kPaneX) = -(extent.left + extent.right) / 2;
            Field<float>(tag.text, kPaneY) = g_Raise - std::min(extent.top, extent.bottom);
        }
        Field<u8>(tag.text, kPaneFlags) |= kPaneDirty;
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

    void Style(float scale, int alpha, float raise) {
        DropAll();
        g_Scale = scale;
        g_Alpha = static_cast<u8>(std::clamp(alpha, 0, 255));
        g_Raise = raise;
    }

    std::string State() {
        std::string s = "tags=";
        for (auto& tag : g_Tags)
            s += tag.wrapper == nullptr ? '-' : !tag.drawn ? 'h' : Field<u8>(tag.text, kTextGlyphsReady) ? 'd' : 'g';
        return s;
    }
}
