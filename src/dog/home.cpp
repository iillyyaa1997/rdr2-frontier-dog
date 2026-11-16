#include "dog/home.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/fs.h"
#include "core/log.h"
#include "dog/companion.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

namespace fd::home {

using namespace game;

struct HomeDog {
    int slot = -1;
    Ped ped = 0;
};

static bool g_inCamp = false, g_known = false, g_hidden = false;
static Vector3 g_camp;
static int g_nextScan = 0, g_nextCare = 0;
static long long g_lastCareMinute = -1;
static HomeDog g_dogs[3];
static game::UiPrompt g_take[3], g_free[3];

static std::string campFile() { return fs::join(fs::join(ctx().dataDir, "Dogs"), "camp.dat"); }

bool inCamp() { return g_inCamp; }

bool campKnown(Vector3& at) {
    if (!g_known) return false;
    at = g_camp;
    return true;
}

void start() {
    KvFile kv;
    if (kv.load(campFile())) {
        auto p = kv.getFloats("camp.pos");
        if (p.size() == 3) {
            g_camp = Vector3(float(p[0]), float(p[1]), float(p[2]));
            g_known = true;
        }
    }
    for (int i = 0; i < 3; ++i) {
        g_take[i].create("INPUT_INTERACT_OPTION1", ctx().t("prompt.take_along"), 1000);
        g_free[i].create("INPUT_INTERACT_OPTION2", ctx().t("prompt.let_go"), 3000);
    }
}

static void despawnAll() {
    for (auto& h : g_dogs) {
        deletePed(h.ped);
        h.slot = -1;
    }
    for (int i = 0; i < 3; ++i) { g_take[i].show(false); g_free[i].show(false); }
}

static void scanCamp() {
    int peds[256];
    int n = worldGetAllPeds(peds, 256);
    Hash gang = MISC::GET_HASH_KEY("REL_GANG_DUTCHS");
    Vector3 pp = pos(player()), sum(0, 0, 0);
    int count = 0;
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (p == player() || !alive(p) || !PED::IS_PED_HUMAN(p) || PED::IS_PED_ON_MOUNT(p)) continue;
        if (PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) != gang || dist(p, player()) > 70.0f) continue;
        Vector3 q = pos(p);
        sum = add(sum, q);
        ++count;
    }
    bool camp = count >= ctx().tune("camp.min_gang", 4) && !inTown(pp) && !onMission();
    if (camp) {
        Vector3 c = scale(sum, 1.0f / count);
        if (!g_known || dist(c, g_camp) > 40.0f) {
            g_camp = c;
            g_known = true;
            KvFile kv;
            char buf[96];
            std::snprintf(buf, sizeof buf, "%.2f,%.2f,%.2f", c.x, c.y, c.z);
            kv.set("camp.pos", buf);
            kv.save(campFile());
            FD_INFO("camp found (%d gang members) at %s", count, buf);
        }
    }
    bool now_ = camp || (g_inCamp && g_known && dist(pp, g_camp) < 110.0f && !onMission());
    if (now_ != g_inCamp) {
        g_inCamp = now_;
        FD_INFO(g_inCamp ? "Arthur is in camp" : "Arthur left camp");
        if (!g_inCamp) despawnAll();
    }
}

static void spawnHomeDogs() {
    auto& c = ctx();
    int k = 0;
    for (int s = 0; s < DogStore::kSlots && k < 3; ++s) {
        if (!c.store.used(s) || c.store.at(s).where != Where::Home) continue;
        bool shown = false;
        for (auto& h : g_dogs)
            if (h.slot == s && exists(h.ped)) shown = true;
        if (shown) { ++k; continue; }
        int idx = -1;
        for (int i = 0; i < 3; ++i)
            if (g_dogs[i].slot < 0 || !exists(g_dogs[i].ped)) { idx = i; break; }
        if (idx < 0) break;
        const DogRecord& d = c.store.at(s);
        float a = 0.9f + idx * 0.8f;
        Vector3 want = add(g_camp, Vector3(std::cos(a) * 12.0f, std::sin(a) * 12.0f, 0)), at;
        if (!safeCoord(want, at)) at = want;
        Ped p = spawnPed(d.model.empty() ? "a_c_doghobo_01" : d.model, at, 0, 2500);
        if (!p) continue;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
        ENTITY::SET_ENTITY_INVINCIBLE(p, true);
        DECORATOR::DECOR_SET_INT(p, "FD_shop", 1);
        if (d.preset >= 0) PED::_EQUIP_META_PED_OUTFIT_PRESET(p, d.preset, false);
        if (d.scale > 0 && d.scale != 100) {
            PED::_SET_PED_SCALE(p, d.scale / 100.0f);
            PED::SET_PED_CAPSULE(p, 0.6f);
        }
        const BreedDef* b = c.breeds.find(d.breed);
        const char* scen = (b && b->habits.getBool("camp_sleep_curled", false)) ? "WORLD_ANIMAL_DOG_SLEEPING"
                          : (idx == 1 ? "WORLD_ANIMAL_DOG_SITTING" : "WORLD_ANIMAL_DOG_RESTING");
        TASK::TASK_START_SCENARIO_IN_PLACE_HASH(p, MISC::GET_HASH_KEY(scen), -1, true, 0, -1.0f, false);
        PED::_SET_PED_PROMPT_NAME(p, literal(d.name));
        g_dogs[idx] = {s, p};
        ++k;
        FD_INFO("home dog %s in camp", d.name.c_str());
    }
}

// at home: fed, rested, bonding grows slowly
static void homeCare() {
    long long m = gameMinutes();
    if (g_lastCareMinute < 0) g_lastCareMinute = m;
    long long dm = m - g_lastCareMinute;
    if (dm < 60) return;
    g_lastCareMinute = m;
    auto& c = ctx();
    for (int s = 0; s < DogStore::kSlots; ++s) {
        if (!c.store.used(s) || c.store.at(s).where != Where::Home) continue;
        DogRecord& d = c.store.at(s);
        d.hunger = 0;
        d.healthFrac = 1.0;
        d.dirt = std::max(0.0, d.dirt - dm / 60.0 * 2);
        if (d.limp && d.limpUntil && m >= d.limpUntil) d.limp = false;
        d.bondPoints += c.tunef("home.bond_per_hour", 0.5) * (dm / 60.0);
        double need2 = c.tunef("bond.points.2", 100), need3 = c.tunef("bond.points.3", 400);
        d.bondLevel = d.bondPoints >= need3 ? 3 : (d.bondPoints >= need2 ? 2 : 1);
        c.store.save(s);
    }
}

void tick() {
    if (!ctx().feature("CampHome")) return;
    int t = now();
    if (t >= g_nextScan) {
        g_nextScan = t + 3000;
        scanCamp();
        if (g_inCamp) spawnHomeDogs();
    }
    if (t >= g_nextCare) {
        g_nextCare = t + 10000;
        homeCare();
    }
    if (!g_inCamp) return;
    bool hide = inCutscene();
    if (hide != g_hidden) {
        g_hidden = hide;
        for (auto& h : g_dogs)
            if (exists(h.ped)) ENTITY::SET_ENTITY_VISIBLE(h.ped, !hide);
    }
    for (int i = 0; i < 3; ++i) {
        HomeDog& h = g_dogs[i];
        if (h.slot < 0 || !exists(h.ped) || !ctx().store.used(h.slot)) { g_take[i].show(false); g_free[i].show(false); continue; }
        const DogRecord& d = ctx().store.at(h.slot);
        bool closeTo = dist(h.ped, player()) < 3.5f && !hide;
        g_take[i].setTarget(h.ped);
        g_free[i].setTarget(h.ped);
        g_take[i].show(closeTo);
        g_free[i].show(closeTo);
        if (g_take[i].fired()) {
            Ped p = h.ped;
            int s = h.slot;
            h = HomeDog{};
            g_take[i].show(false);
            g_free[i].show(false);
            companion().takeFromCamp(s, p);
            helpText(ctx().f("help.taken_along", {{"name", ctx().store.at(s).name}}, ctx().store.at(s).female));
        } else if (g_free[i].fired()) {
            std::string name = d.name;
            bool fem = d.female;
            ctx().store.remove(h.slot);
            DECORATOR::DECOR_SET_INT(h.ped, "FD_shop", 0);
            ENTITY::SET_ENTITY_INVINCIBLE(h.ped, false);
            TASK::TASK_WANDER_STANDARD(h.ped, 10.0f, 10);
            ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&h.ped);
            h = HomeDog{};
            helpText(ctx().f("help.let_go", {{"name", name}}, fem));
            FD_INFO("home dog %s let go for good", name.c_str());
        }
    }
}

}  // namespace fd::home
