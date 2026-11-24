#include "dog/care.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "dog/behaviour.h"
#include "dog/companion.h"
#include "dog/hunting.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

namespace fd::care {

using namespace game;

static game::UiPrompt g_brush, g_heal;
static long long g_lastMinute = -1;
static int g_lastHealth = 0, g_lastHealthAt = 0, g_brushDoneAt = 0, g_healDoneAt = 0, g_sitSince = 0, g_restTick = 0,
           g_dirtTick = 0, g_sulkUntil = 0, g_limpAssert = 0;
static bool g_resting = false, g_healOffered = false, g_limpShown = false;
static int g_dirtLook = 0;  // 0 clean, 1 dusty, 2 muddy (as last applied)

static const char* kMedicine[] = {"CONSUMABLE_HORSE_MEDICINE", "CONSUMABLE_HORSE_STIMULANT", "CONSUMABLE_HEALTH_CURE"};

bool offersButton1() { return g_healOffered; }

void start() {
    g_brush.create("INPUT_CONTEXT_Y", ctx().t("prompt.brush"), 1000);
    g_heal.create("INPUT_INTERACT_OPTION1", ctx().t("prompt.heal"), 1000, 3);
    g_heal.setRank(30);
    g_brush.setRank(55);
}

static void cleanLook(Ped dog) {
    PED::_SET_PED_DIRT_CLEANED(dog, 0.0f, -1, true, true);
    PED::CLEAR_PED_ENV_DIRT(dog);
    PED::CLEAR_PED_BLOOD_DAMAGE(dog);
    PED::CLEAR_PED_WETNESS(dog);
    g_dirtLook = 0;
}

// D134: the game's dirt is not driven for dogs; try the game's mud decals (femga ped_decals) on our counter
static void applyDirtLook(Ped dog, double dirt) {
    if (!ctx().ini.getBool("VisibleDirt", true)) return;
    int want = dirt >= 85 ? 2 : (dirt >= 55 ? 1 : 0);
    if (want == g_dirtLook) return;
    if (want == 0) {
        cleanLook(dog);
        return;
    }
    PED::APPLY_PED_DAMAGE_PACK(dog, "CC_DUSTY_full_Body_A", 0.0f, want == 2 ? 1.0f : 0.6f);
    if (want == 2)
        for (const char* p : {"PD_Mud_Back_NoFade", "PD_Mud_Left_NoFade", "PD_Mud_Right_NoFade", "PD_Mud_Feet_NoFade"})
            PED::APPLY_PED_DAMAGE_PACK(dog, p, 0.0f, 1.0f);
    g_dirtLook = want;
    FD_INFO("dirt look %d (counter %.0f)", want, dirt);
}

void tick() {
    auto& comp = companion();
    if (!comp.present()) return;
    DogRecord* d = comp.record();
    if (!d || d->woundedUntil > 0) return;
    auto& c = ctx();
    Ped dog = comp.ped();
    int t = now();
    bool close = dist(dog, player()) < 3.0f && !PED::IS_PED_ON_MOUNT(player());

    // a big hit at once (30 % of max health) makes it limp for a while
    int hp = ENTITY::GET_ENTITY_HEALTH(dog), maxHp = std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(dog, false));
    if (g_lastHealth && g_lastHealth - hp >= maxHp * 0.3 && !d->limp) {
        d->limp = true;
        d->limpUntil = gameMinutes() + c.tune("care.limp_minutes", 30) * 60 / 2;  // ~30 real minutes of game time
    }
    if (t - g_lastHealthAt > 3000 || hp > g_lastHealth) { g_lastHealth = hp; g_lastHealthAt = t; }

    long long m = gameMinutes();
    if (g_lastMinute < 0) g_lastMinute = m;
    long long dm = std::clamp<long long>(m - g_lastMinute, 0, 600);
    if (dm > 0) {
        g_lastMinute = m;
        double hours = c.tunef("hunger.hours", 48) * (c.hasTrait(*d, "frugal") ? 2.0 : 1.0);
        d->hunger = std::min(100.0, d->hunger + dm * 100.0 / (hours * 60.0));
        if (d->limp && d->limpUntil && m >= d->limpUntil) d->limp = false;
    }
    if (t - g_dirtTick > 60000) {  // own dirt: +1 a real minute outdoors, Tidy half
        g_dirtTick = t;
        if (!INTERIOR::GET_INTERIOR_FROM_ENTITY(dog)) d->dirt = std::min(100.0, d->dirt + (c.hasTrait(*d, "tidy") ? 0.5 : 1.0));
    }
    if (ENTITY::IS_ENTITY_IN_WATER(dog) && d->dirt > 0) { d->dirt = 0; cleanLook(dog); }  // a swim washes it
    applyDirtLook(dog, d->dirt);
    if (d->limp != g_limpShown || (d->limp && t >= g_limpAssert)) {
        PED::_FAKE_SET_PED_LOCO_INJURED(dog, d->limp);  // re-asserted every 5 s: tasks may reset it
        g_limpShown = d->limp;
        g_limpAssert = t + 5000;
    }

    // brush: offered as soon as the coat is a little dirty (24.11: he never found it - it waited for dirt 40, 40 real
    // minutes outdoors without a swim, and then came last in a full prompt group); a dirty dog's brush outranks Praise
    bool dirty = d->dirt >= c.tunef("care.brush_from", 10);
    bool veryDirty = d->dirt >= c.tunef("care.brush_bond_from", 40);
    g_brush.setRank(veryDirty ? 25 : 55);
    g_brush.setTarget(dog);
    g_brush.show(close && dirty && !g_brushDoneAt && !hunting::busy());
    if (g_brush.fired()) {
        TASK::TASK_ANIMAL_INTERACTION(player(), dog, MISC::GET_HASH_KEY("Interaction_Dog_Patting"), 0, true);
        g_brushDoneAt = t + 5000;
    }
    if (g_brushDoneAt && t >= g_brushDoneAt) {
        g_brushDoneAt = 0;
        bool wasDirty = d->dirt >= c.tunef("care.brush_bond_from", 40);
        d->dirt = 0;
        cleanLook(dog);
        // the full bond for a dirty coat, a little for a quick brush (no farming: the cooldown is shared)
        comp.addBond(c.tunef(wasDirty ? "bond.brush" : "bond.brush_light", wasDirty ? 5 : 1), "brush",
                     c.tune("bond.brush_cooldown_ms", 300000));
        const BreedDef* b = c.breeds.find(d->breed);
        int sulk = b ? int(b->habits.getInt("sulk_after_groom", 0)) : 0;
        if (sulk > 0) {  // the Shiba sits with her back to Arthur for a while (old D56)
            g_sulkUntil = t + sulk * 1000;
            Vector3 dp = pos(dog), pp = pos(player());
            ENTITY::SET_ENTITY_HEADING(dog, std::atan2(-(dp.x - pp.x), dp.y - pp.y) * 180.0f / 3.14159265f);
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_SITTING"), sulk * 1000, true, 0, -1.0f, false);
        }
        comp.save();
    }

    // heal a limp
    const char* med = nullptr;
    for (const char* x : kMedicine)
        if (itemCount(x) > 0) { med = x; break; }
    g_healOffered = close && d->limp && med && !behaviour::feedOffered();
    g_heal.setTarget(dog);
    g_heal.show(g_healOffered);
    if (g_healOffered && g_heal.fired() && takeItem(med)) {
        TASK::TASK_ANIMAL_INTERACTION(player(), dog, MISC::GET_HASH_KEY("Interaction_Dog_Patting"), 0, true);
        g_healDoneAt = t + 4000;
    }
    if (g_healDoneAt && t >= g_healDoneAt) {
        g_healDoneAt = 0;
        d->limp = false;
        d->limpUntil = 0;
        ENTITY::SET_ENTITY_HEALTH(dog, maxHp, 0);
        comp.addBond(c.tunef("bond.heal", 4), "heal", 120000);
        comp.save();
    }

    // rest by Arthur when he sits (campfire, chair, ground) outside
    bool sitting = PED::IS_PED_ON_FOOT(player()) && PED::IS_PED_USING_ANY_SCENARIO(player()) && !INTERIOR::GET_INTERIOR_FROM_ENTITY(player());
    g_sitSince = sitting ? (g_sitSince ? g_sitSince : t) : 0;
    bool idle = !hunting::busy() && !behaviour::busy() && !comp.staying() && t > g_sulkUntil;
    if (!g_resting && g_sitSince && t - g_sitSince > 4000 && idle && dist(dog, player()) < 25.0f) {
        g_resting = true;
        Vector3 f = ENTITY::GET_ENTITY_FORWARD_VECTOR(player());
        Vector3 spot = add(pos(player()), Vector3(f.y * 1.4f, -f.x * 1.4f, 0));
        const BreedDef* b = c.breeds.find(d->breed);
        bool curls = b && b->habits.getBool("camp_sleep_curled", false);
        int seq = 0;
        TASK::CLEAR_PED_TASKS(dog, true, true);
        TASK::OPEN_SEQUENCE_TASK(&seq);
        TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(0, spot.x, spot.y, spot.z, 1.0f, 15000, 0.5f, 0, 0.0f);
        TASK::TASK_TURN_PED_TO_FACE_ENTITY(0, player(), 1000, 0, 0, 0);
        TASK::TASK_START_SCENARIO_IN_PLACE_HASH(0, MISC::GET_HASH_KEY(curls ? "WORLD_ANIMAL_DOG_SLEEPING" : "WORLD_ANIMAL_DOG_RESTING"), -1, true, 0, -1.0f, false);
        TASK::CLOSE_SEQUENCE_TASK(seq);
        TASK::TASK_PERFORM_SEQUENCE(dog, seq);
        TASK::CLEAR_SEQUENCE_TASK(&seq);
        g_restTick = t;
        FD_INFO("rests by Arthur");
    } else if (g_resting && (!sitting || hunting::busy() || behaviour::busy())) {
        g_resting = false;
        comp.follow();
    }
    if (g_resting && t - g_restTick > 10000) {
        g_restTick = t;
        d->hunger = std::max(0.0, d->hunger - 1.0);
        ENTITY::SET_ENTITY_HEALTH(dog, std::min(maxHp, hp + maxHp / 20), 0);
    }
}

}  // namespace fd::care
