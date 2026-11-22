#include "ui/core.h"

#include <algorithm>
#include <cmath>
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

// D148: our own paw core icon (tools/gen_paw_core.py), a texture dictionary streamed by LML (frontier_dog_hud.ytd)
static const char* kPawDict = "frontier_dog_hud";
static bool pawTexturesReady() {
    if (!TXD::DOES_STREAMED_TEXTURE_DICT_EXIST(kPawDict)) return false;
    if (TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(kPawDict)) return true;
    TXD::REQUEST_STREAMED_TEXTURE_DICT(kPawDict, false);
    return false;
}

static int g_forceUntil = 0;
void coreForce(int untilMs) { g_forceUntil = untilMs; }

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
    bool wanted = comp.present() && d && !HUD::IS_HUD_HIDDEN() && (slotShown(player) || slotShown(horse) || t < g_forceUntil);
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
    int maxHp = std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(dog, true));
    float health = std::clamp(float(ENTITY::GET_ENTITY_HEALTH(dog)) / maxHp, 0.0f, 1.0f);
    float coreFill = float(std::clamp(1.0 - d->hunger / 100.0, 0.0, 1.0));
    // D148, like Arthur's cores: the grey track is the most health the dog can have (it grows with bond: health.max
    // 800/960/1120, less when Ailing), the white meter what it has, both on one scale; the game's tracks are 10 arcs
    // (rpg_meter_track_0..9, one per attribute level) and its meter is absolute ((N+1) % of the circle)
    static const float kTrackEnd[10] = {.117f, .222f, .317f, .406f, .5f, .6f, .689f, .778f, .889f, 1.0f};  // measured 16.11
    float topHp = 1;
    for (double v : c.tuning.getFloats("health.max")) topHp = std::max(topHp, float(v));
    int track = std::clamp(int(std::lround(std::min(1.0f, maxHp / topHp) * 10)) - 1, 0, 9);
    int meter = std::clamp(int(std::lround(health * kTrackEnd[track] * 100)) - 1, 0, 99);
    // the ring box keeps the ring at the size checked in game 16.11 (it was drawn 1.05 x the core box); the disc and
    // the icon follow the approved constructor: disc 0.8, icon 0.9 of the ring box
    float rw = w * 1.05f, rh = h * 1.05f;
    auto centred = [&](float k) { return std::pair<float, float>(curX + (w - rw * k) / 2, curY + (h - rh * k) / 2); };
    auto [bx, by] = centred(0.8f);
    sprite("rpg_textures", "rpg_background", bx, by, rw * 0.8f, rh * 0.8f, 0, 0, 0, a);
    auto [mx, my] = centred(1.0f);
    sprite("rpg_meter_track", ("rpg_meter_track_" + std::to_string(track)).c_str(), mx, my, rw, rh, 109, 109, 109, a);
    sprite("rpg_meter", ("rpg_meter_" + std::to_string(meter)).c_str(), mx, my, rw, rh, 229, 229, 229, a);
    // the icon: our paw, drained from the top in 16 states like the game's hearts; red below 30 % fill (D148 4A),
    // blinking red while wounded (5C)
    bool wounded = d->woundedUntil > 0;
    bool red = wounded ? (game::now() / 500) % 2 == 0 : coreFill < 0.3f;
    int ir = red ? 255 : 229, ig = red ? 0 : 229, ib = red ? 0 : 229;
    int state = std::clamp(int(std::lround(coreFill * 16)) - 1, 0, 15);
    auto [ix, iy] = centred(0.9f);
    if (pawTexturesReady()) {
        sprite(kPawDict, ("paw_core_" + std::to_string(state)).c_str(), ix, iy, rw * 0.9f, rh * 0.9f, ir, ig, ib, a);
    } else {
        // no streamed texture (LML missing or not loaded yet): the game's own paw blip, its drained top dimmed by a
        // dark rect over the paw only (it lies inside the disc)
        float pk = 0.62f;
        auto [px, py] = centred(pk);
        float pw = rw * pk, ph = rh * pk;
        sprite("blips", "blip_animal_dead", px, py, pw, ph, ir, ig, ib, a);
        float top = py + ph * 7 / 32, bot = py + ph * 28 / 32, line = bot - coreFill * (bot - top);
        if (line > top) rect(px + pw * 0.16f, top, pw * 0.68f, line - top, 0, 0, 0, int(a * 0.44f));
    }
}

}  // namespace fd::ui
