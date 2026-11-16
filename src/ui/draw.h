// Screen drawing with the game's own textures and fonts (coordinates 0..1, top-left based).
#pragma once
#include <string>

namespace fd::ui {

void sprite(const char* dict, const char* name, float x, float y, float w, float h, int r, int g, int b, int a,
            float heading = 0);
void rect(float x, float y, float w, float h, int r, int g, int b, int a);
void text(const std::string& t, float x, float y, float scale, int r, int g, int b, int a, const char* font = "$body");

}  // namespace fd::ui
