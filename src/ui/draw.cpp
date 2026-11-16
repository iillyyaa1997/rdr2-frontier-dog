#include "ui/draw.h"

#include "game/game.h"
#include "sdk/natives.gen.h"

namespace fd::ui {

void sprite(const char* dict, const char* name, float x, float y, float w, float h, int r, int g, int b, int a,
            float heading) {
    if (!TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(dict)) TXD::REQUEST_STREAMED_TEXTURE_DICT(dict, false);
    GRAPHICS::DRAW_SPRITE(dict, name, x + w / 2, y + h / 2, w, h, heading, r, g, b, a, true);
}

void rect(float x, float y, float w, float h, int r, int g, int b, int a) {
    GRAPHICS::DRAW_RECT(x + w / 2, y + h / 2, w, h, r, g, b, a, true, false);
}

void text(const std::string& t, float x, float y, float scale, int r, int g, int b, int a, const char* font) {
    UIDEBUG::_BG_SET_TEXT_COLOR(r, g, b, a);
    UIDEBUG::_BG_SET_TEXT_SCALE(scale, scale);
    fd::invoke<void>(0xBE5261939FBECB8CULL, false);  // SET_TEXT_CENTRE (not in the current database)
    std::string tpl = std::string("<FONT FACE='") + font + "' COLOR='#ffffff'>" + t + "</FONT>";
    const char* inner = game::literal(tpl);
    UIDEBUG::_BG_DISPLAY_TEXT(MISC::VAR_STRING(42, "COLOR_STRING", 0, inner), x, y);
}

}  // namespace fd::ui
