#pragma once
// The ghosts' name tags (ghost_nametag.cpp): ghost slot n's slot name in a small window over a world position.
#include "lib.hpp"
#include <string>

namespace Game { struct Vec3; }

namespace AP::NameTag {
    // Shows the tag over that point (hidden while it is behind the camera or off screen).
    void Show(int n, const Game::Vec3& head, const std::string& name);
    void Hide(int n);
    // While a level loads: the tags are built again in the next.
    void DropAll();
    // DBG puppet tagstyle: the tags' scale, the text's and the window's alpha, the height above the point and the
    // window's margins around the text (layout units); built again.
    void Style(float scale, int alpha, int backAlpha, float raise, float padX, float padY);
    // STATE's tags=: per slot "-" none, "h" hidden, "g" shown while the font draws the name's glyphs, "d" drawn.
#ifdef LASAP_TEST
    std::string State();
    // DBG puppet tags: each tag's text extent as laid out
    std::string Info();
#endif
}
