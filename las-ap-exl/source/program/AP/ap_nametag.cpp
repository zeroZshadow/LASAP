// Multiplayer ghosts' name tags (DESIGN.md, Ghosts): each shown ghost's slot name above its head, small, in the game's
// action prompt (Gm_HUDInteract.bflyt of Game.arc: text in a dark rounded window, without its button picture) built as
// a layout of its own and placed on the screen every frame. All on the game thread.
#include "AP/ap.hpp"
#include "Game/framework.hpp"
#include "Game/mem.hpp"
#include "Game/ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    using namespace Game::Ui;
    using Game::Field;

    // Priority 1: over the HUD (0), under the menus, the map, fades and the message window. The text's extent is
    // returned through x8. Its text is set through the game's message pipeline (AP::SetPaneText) and queued for its
    // glyphs here (Game/ui.hpp).
    constexpr int kPriority = 1;
    constexpr size_t kHolderPane = 0x10;
    // Gm_HUDInteract: a group (alpha 0 in the file: an animation fades the prompt in, scale 2/3) holding a window, its
    // text and the button's picture, the window and the text placed from their left edge. The tag shows the group,
    // hides the picture, scales the root, centres the text on the root and stretches the window around it.
    constexpr const char* kLayoutFile = "Gm_HUDInteract.bflyt", *kGroupPane = "N_Interact_00",
                          *kWindowPane = "W_Interact_00", *kTextPane = "T_Interact_00", *kPicturePane = "P_pict_00";
    constexpr size_t kNameMax = 16;  // Archipelago's slot names
    // The text box holds 13 characters (14 with the terminator in the file); the tag gives it room for kTextChars
    // UTF-16 units.
    constexpr u16 kTextChars = 2 * kNameMax;
    constexpr float kTextWidth = 1000;  // the text box's width: the pipeline breaks lines wider than that (150 in the file)
    // The look: the prompt at this scale, the text and the window at these alphas, the window this much wider and
    // higher than the text, the tag this far above the projected point (layout units).
    float g_Scale = 0.6f, g_Raise = 0, g_PadX = 24, g_PadY = 8;
    // The text box's line holds more room above the letters than below them: the text goes up this part of its
    // height, so the letters (capital top to baseline) sit in the window's middle.
    constexpr float kTextLift = 0.14f;
    u8 g_Alpha = 230, g_BackAlpha = 200;
    // Projection (WorldToLayout, 0xf88c40): through the camera's view and projection (Game::GetCamera); layout
    // coordinates are NDC times (640, 360), y up. Hidden when the head is behind the camera or off screen.
    constexpr float kHalfWidth = 640, kHalfHeight = 360, kMargin = 120, kNear = 0.01f;

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

    void Drop(Tag& tag) {
        if (tag.text != nullptr) Unqueue(tag.text);
        Delete(tag.wrapper);
        tag = Tag{};
    }

    bool Build(Tag& tag) {
        if (!GameArchiveLoaded()) return false;
        tag.wrapper = BuildLayout(kLayoutFile, kPriority);
        tag.layout = LayoutOf(tag.wrapper);
        tag.root = RootOf(tag.wrapper);
        tag.text = FindPane(tag.root, kTextPane);
        tag.window = FindPane(tag.root, kWindowPane);
        u8* group = FindPane(tag.root, kGroupPane);
        u8* picture = FindPane(tag.root, kPicturePane);
        if (tag.text == nullptr || tag.window == nullptr || group == nullptr || picture == nullptr ||
            !Allocate(tag.text, kTextChars)) {
            Drop(tag);
            return false;
        }
        // the group at the root's scale, the window at the text's
        SetAlpha(group, 255);
        ScaleTo(group, 1);
        ScaleTo(tag.window, 1);
        SetVisible(picture, false);
        Field<float>(tag.text, kPaneWidth) = kTextWidth;
        SetAlpha(tag.text, g_Alpha);
        SetAlpha(tag.window, g_BackAlpha);
        ScaleTo(tag.root, g_Scale);
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
        Rect extent = TextExtent(tag.text);
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
        QueueGlyphs(tag.text);
        tag.measured = false;
        Layout(tag);
    }

    // A world position on the screen in layout coordinates, or false when it is behind the camera or off screen.
    bool Project(const Game::Vec3& at, float& x, float& y) {
        const u8* camera = Game::GetCamera();
        if (camera == nullptr) return false;
        const float* view = &Field<float>(camera, Game::kCameraView);
        const float* proj = &Field<float>(camera, Game::kCameraProjection);
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
        if (!tag.measured && GlyphsDrawn(tag.text)) {
            Layout(tag);
            tag.measured = true;
        }
        float x, y;
        if (!Project(head, x, y)) {
            SetDrawn(tag, false);
            return;
        }
        MoveTo(tag.root, x, y);
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
            s += tag.wrapper == nullptr ? '-' : !tag.drawn ? 'h' : GlyphsDrawn(tag.text) ? 'd' : 'g';
        return s;
    }
}
