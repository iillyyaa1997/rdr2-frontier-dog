#include "ui/core.h"

#include <algorithm>
#include <string>

#include "dog/companion.h"
#include "game/game.h"
#include "mod/context.h"
#include "ui/draw.h"

namespace fd::ui {

static int slotState(int slot) { return HUD::_GET_HUD_VISIBILITY_SLOT_STATE(slot); }

// The game's core slot states as Care Overhaul's adaptive HUD reads them (RE of RDR2CareOverhaul.asi 16.11): 1 shown,
// 3 shown (eased), 2 hidden at once, 4 hidden after a delay. Its needs cores follow exactly this, nothing of their own.
static bool slotShown(int s) { return s == 1 || s == 3; }

void coreTick() {
    auto& c = ctx();
    if (!c.ini.getBool("ShowCore", true)) return;
    auto& comp = companion();
    DogRecord* d = comp.record();
    static float alpha = 0, curX = -1, curY = 0, curW = 38.0f / 1920.0f;
    static int lastTick = 0, hiddenSince = -1, hideDelay = 0, horseSeenAt = -100000;
    int t = game::now();
    int dt = std::clamp(t - lastTick, 0, 200);
    lastTick = t;
    float showEase = float(std::max(1LL, c.ini.getInt("CoreShowEaseMs", 100)));
    float hideEase = float(std::max(1LL, c.ini.getInt("CoreHideEaseMs", 250)));
    int player = slotState(39), horse = slotState(42);
    if (slotShown(horse)) horseSeenAt = t;
    // the horse's two cores stay on screen through the hide delay and fade too
    bool horseCores = t - horseSeenAt < c.ini.getInt("CoreHideDelayMs", 2000) + c.ini.getInt("CoreHideEaseMs", 250);
    // shown together with Arthur's or the horse's cores and only then (no timer of our own: it showed before the radar)
    bool wanted = comp.present() && d && !HUD::IS_HUD_HIDDEN() && (slotShown(player) || slotShown(horse));
    if (wanted) {
        hiddenSince = -1;
        alpha += float(dt) / showEase;
    } else {
        if (hiddenSince < 0) {
            hiddenSince = t;
            hideDelay = (player == 2 || player == 0) && (horse == 2 || horse == 0) ? 0 : c.ini.getInt("CoreHideDelayMs", 2000);
        }
        if (t - hiddenSince >= hideDelay) alpha -= float(dt) / hideEase;
        else if (alpha > 0) alpha += float(dt) / showEase;  // a show already started finishes first, as the game's own
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0 || !comp.present() || !d) return;

    // where the game's cores stand follows the minimap mode: around the expanded radar (chosen, or while the HUD is
    // revealed - a tap on Alt / D-pad down grows it), around the regular one, or in a row with no radar (off, compass)
    int layout = game::radarLayout();
    bool large = layout == 2;
    float tx = float(c.ini.getFloat(large ? "CoresX" : "CoresSmallX", large ? 0.1969 : 0.1542));
    float ty = float(c.ini.getFloat(large ? "CoresY" : "CoresSmallY", large ? 0.8519 : 0.8870));
    float tw = (large ? 52.0f : 38.0f) / 1920.0f;
    if (layout == 0) {  // no radar circle: the game's cores stand in a row at the bottom left, the horse's after Arthur's
        tx = float(c.ini.getFloat("CoresRowX", 0.0951)) + (horseCores ? 2 * float(c.ini.getFloat("CoresRowStep", 0.0238)) : 0.0f);
        ty = float(c.ini.getFloat("CoresRowY", 0.9306));
        tw = 38.0f / 1920.0f;
    }
    // the game's cores jump between the layouts, no glide (16.11); a bit larger than the measured spots - its ring
    // was ~33 px against the game's ~40 at 1080p - grown around the same centre
    float scale = float(c.ini.getFloat("CoreScale", 1.2));
    curX = tx - tw * (scale - 1) / 2;
    curY = ty - tw * 16 / 9 * (scale - 1) / 2;
    curW = tw * scale;

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
