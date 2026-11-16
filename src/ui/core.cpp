#include "ui/core.h"

#include <algorithm>
#include <string>

#include "dog/companion.h"
#include "game/game.h"
#include "mod/context.h"
#include "ui/draw.h"

namespace fd::ui {

static int slotState(int slot) { return HUD::_GET_HUD_VISIBILITY_SLOT_STATE(slot); }

void coreTick() {
    auto& c = ctx();
    if (!c.ini.getBool("ShowCore", true)) return;
    auto& comp = companion();
    DogRecord* d = comp.record();
    static float alpha = 0, curX = -1, curY = 0, curW = 38.0f / 1920.0f;
    static int lastTick = 0, hiddenSince = -1;
    int t = game::now();
    int dt = std::clamp(t - lastTick, 0, 200);
    lastTick = t;
    int player = slotState(39), horse = slotState(42), radar = slotState(17), cash = slotState(3);
    bool wanted = comp.present() && d && !HUD::IS_HUD_HIDDEN() && (player == 3 || horse == 3 || comp.hudActive());
    if (wanted) {
        hiddenSince = -1;
        alpha += float(dt) / 100.0f;
    } else {
        if (hiddenSince < 0) hiddenSince = t;
        if (t - hiddenSince >= 2000) alpha -= float(dt) / 250.0f;
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0 || !comp.present() || !d) return;

    bool revealed = cash == 1 || cash == 3;
    bool large = revealed || c.ini.getBool("RadarExpanded", false);
    float tx = float(c.ini.getFloat(large ? "CoresX" : "CoresSmallX", large ? 0.1969 : 0.1542));
    float ty = float(c.ini.getFloat(large ? "CoresY" : "CoresSmallY", large ? 0.8519 : 0.8870));
    float tw = (large ? 52.0f : 38.0f) / 1920.0f;
    if (radar == 4 && player == 3 && !revealed) {  // no radar: the game's cores stand in a row at the bottom left
        tx = float(c.ini.getFloat("CoresRowX", 0.0951));
        ty = float(c.ini.getFloat("CoresRowY", 0.9306));
        tw = 38.0f / 1920.0f;
    }
    if (curX < 0) { curX = tx; curY = ty; }
    curX += (tx - curX) * 0.2f;
    curY += (ty - curY) * 0.2f;
    curW += (tw - curW) * 0.2f;

    int a = int(alpha * 255);
    float w = curW, h = curW * 16 / 9;
    Ped dog = comp.ped();
    float health = float(ENTITY::GET_ENTITY_HEALTH(dog)) / std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(dog, true));
    float coreFill = float(std::clamp(1.0 - d->hunger / 100.0, 0.0, 1.0));
    int meter = std::clamp(int(health * 100) - 1, 0, 99);
    auto centred = [&](float k) { return std::pair<float, float>(curX + (w - w * k) / 2, curY + (h - h * k) / 2); };
    auto [bx, by] = centred(0.9f);
    sprite("rpg_textures", "rpg_background", bx, by, w * 0.9f, h * 0.9f, 0, 0, 0, a);
    sprite("rpg_meter_track", "rpg_meter_track_9", curX, curY, w, h, 109, 109, 109, a);
    auto [mx, my] = centred(1.05f);
    sprite("rpg_meter", ("rpg_meter_" + std::to_string(meter)).c_str(), mx, my, w * 1.05f, h * 1.05f, 229, 229, 229, a);
    bool wounded = d->woundedUntil > 0;
    if (wounded) {
        auto [ox, oy] = centred(1.1f);
        sprite("rpg_textures", "rpg_agitation", ox, oy, w * 1.1f, w * 1.1f * 16 / 9, 255, 0, 0, a);
    } else {
        int state = std::max(0, int(coreFill * 16) - 1);
        bool low = coreFill < 0.3f;
        sprite("rpg_core_health", ("core_state_" + std::to_string(state)).c_str(), bx, by, w * 0.9f, h * 0.9f,
               low ? 255 : 229, low ? 0 : 229, low ? 0 : 229, a);
    }
    float iw = w * 0.5f, ih = iw * 16 / 9;
    sprite("blips", "blip_animal", curX + (w - iw) / 2, my + h * 1.05f - ih * 3 / 4, iw, ih, 229, 229, 229, a);
}

}  // namespace fd::ui
