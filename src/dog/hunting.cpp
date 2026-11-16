#include "dog/hunting.h"

#include <algorithm>
#include <deque>
#include <vector>

#include "core/log.h"
#include "dog/behaviour.h"
#include "dog/carry.h"
#include "dog/companion.h"
#include "game/game.h"
#include "mod/context.h"

namespace fd::hunting {

using namespace game;

static CarryTask g_carry;
static std::deque<Ped> g_queue;
static Ped g_mark = 0;
static int g_markUntil = 0, g_markOrdered = 0;

bool busy() { return g_carry.active() || (g_mark && now() < g_markUntil); }

void giveUp() {
    g_carry.release();
    g_queue.clear();
    g_mark = 0;
}

static float fetchRange(const DogRecord& d) {
    int f = d.skill("fetch");
    float r = ctx().hasTrait(d, "long_retrieve") ? 120.0f : (f >= 3 ? 70.0f : (f == 2 ? 55.0f : 40.0f));
    if (d.hunger >= 75) r *= 0.75f;      // a hungry dog works worse (old D14)
    else if (d.dirt >= 60) r *= 0.85f;   // and a dirty one too
    return r;
}

static float staminaFactor(int s) {
    static const float f[] = {0.4f, 0.4f, 0.6f, 0.8f, 1.0f, 1.25f};
    return f[std::clamp(s, 1, 5)];
}

static void mark(Ped carcass) {
    g_mark = carcass;
    g_markUntil = now() + 30000;
    g_markOrdered = 0;
}

// false = the dog cannot bring it (it marks it instead)
static bool startCarry(Ped dog, DogRecord& d, Ped carcass) {
    if (!ctx().feature("CarryHuntedAnimals")) return false;
    if (dist(player(), carcass) > fetchRange(d)) return false;
    CarryJob job;
    job.carcass = carcass;
    job.size = CarryTask::sizeOf(carcass, &job.length);
    int fetch = d.skill("fetch"), soft = d.skill("soft_mouth"), strength = d.stats[Strength];
    bool tenacious = ctx().hasTrait(d, "tenacious");
    bool can = job.size == CarrySize::Small ||
               (job.size == CarrySize::Medium && fetch >= 2 && (strength >= 2 || tenacious)) ||
               (job.size == CarrySize::Large && fetch >= 3 && (strength >= 3 || tenacious));
    if (!can) return false;
    if (ENTITY::IS_ENTITY_IN_WATER(carcass)) {
        const BreedDef* b = ctx().breeds.find(d.breed);
        int shyBelow = b ? int(b->habits.getInt("water_shy_bond", 0)) : 0;
        bool shy = shyBelow > 0 && d.bondLevel < shyBelow;
        bool swims = !shy && (ctx().hasTrait(d, "diver") || fetch >= 2);
        if (!swims) return false;
        job.inWater = true;
    }
    if (job.size == CarrySize::Large) {
        float base = float(ctx().tunef("carry.pull_force", 20));
        job.pull = strength >= 3 ? base * (1.0f + 0.6f * (strength - 3)) : base * 0.5f;
        if (ctx().hasTrait(d, "hauler")) job.pull *= 1.3f;
    }
    bool keeps = (job.size == CarrySize::Small && soft >= 2) || (job.size == CarrySize::Large && soft >= 3) ||
                 (job.size == CarrySize::Medium && soft >= 2 && ctx().hasTrait(d, "soft_mouth"));
    job.losesQuality = !keeps;
    job.tenacious = tenacious;
    job.staminaSeconds = float(ctx().tunef("carry.stamina_seconds", 45)) * staminaFactor(d.stats[Stamina]) *
                         (ctx().hasTrait(d, "hauler") ? 2.0f : 1.0f);
    job.speed = job.size == CarrySize::Small ? 3.0f : 1.0f;
    Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
    if (job.size != CarrySize::Small && ctx().feature("CarryToHorse") && alive(horse) && PED::IS_PED_ON_FOOT(player()) &&
        dist(horse, player()) < float(ctx().tunef("carry.to_horse_range", 60)) && dist(carcass, horse) < dist(carcass, player()) + 20)
        job.horse = horse;
    g_carry.start(dog, job);
    return true;
}

static void onKill(Ped carcass) {
    auto& comp = companion();
    DogRecord* d = comp.record();
    if (!comp.present() || !d || d->woundedUntil > 0 || comp.staying() || !behaviour::autonomyAllowed()) return;
    float away = dist(player(), carcass);
    if (away < 20.0f || away > fetchRange(*d) * 1.75f) return;
    const BreedDef* b = ctx().breeds.find(d->breed);
    int minQ = b ? int(b->habits.getInt("min_fetch_quality", 0)) : 0;  // the Shiba sniffs a poor one and leaves it (old D52)
    int q = PED::_GET_PED_QUALITY(carcass);
    if (q >= 0 && q < minQ) {
        mark(carcass);
        FD_INFO("fetch: quality %d below %d, sniffs and leaves it", q, minQ);
        return;
    }
    if (g_carry.active()) {
        g_queue.push_back(carcass);  // a shot during a carry waits (0.11.3, verified)
        FD_INFO("fetch: queued");
        return;
    }
    if (!startCarry(comp.ped(), *d, carcass)) mark(carcass);
    comp.addBond(ctx().tunef("bond.fetch", 2), "fetch", ctx().tune("bond.fetch_cooldown_ms", 20000));
    comp.markActive();
}

// dead animals Arthur killed near him, once a second: arrows, knives and scripted kills give no bullet-impact event
static void scanKills() {
    static int next = 0;
    static std::vector<Ped> seen;
    if (now() < next) return;
    next = now() + 1000;
    int peds[256];
    int n = worldGetAllPeds(peds, 256);
    for (int i = 0; i < n; ++i) {
        Ped e = peds[i];
        if (!exists(e) || !ENTITY::IS_ENTITY_DEAD(e) || !ENTITY::GET_IS_ANIMAL(e) || dist(e, player()) > 130.0f) continue;
        if (PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(e)) || ENTITY::_GET_IS_PREDATOR(e)) continue;
        if (PED::GET_PED_SOURCE_OF_DEATH(e) != player()) continue;
        if (std::find(seen.begin(), seen.end(), e) != seen.end()) continue;
        seen.push_back(e);
        if (seen.size() > 64) seen.erase(seen.begin());
        onKill(e);
    }
}

void onEvent(Hash type, int index) {
    static const Hash kImpact = MISC::GET_HASH_KEY("EVENT_SHOT_FIRED_BULLET_IMPACT");
    if (type != kImpact) return;
    struct { alignas(8) int e; } ev{};
    if (!SCRIPT::GET_EVENT_DATA(0, index, (Any*)&ev, 1)) return;
    Ped e = ev.e;
    if (exists(e) && ENTITY::IS_ENTITY_DEAD(e) && ENTITY::GET_IS_ANIMAL(e) && !PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(e)) &&
        !ENTITY::_GET_IS_PREDATOR(e) && PED::GET_PED_SOURCE_OF_DEATH(e) == player()) {
        static Ped last = 0;
        if (e == last) return;  // several impacts on one kill
        last = e;
        onKill(e);
    }
}

static void afterDelivery(DogRecord& d) {
    auto& c = ctx();
    auto& comp = companion();
    if (addSkillUse(d, "fetch", c.tune("skill.uses.2", 10), c.tune("skill.uses.3", 25)))
        helpText(c.f("help.skill_up", {{"name", d.name}, {"skill", c.t("skill.fetch")}, {"level", std::to_string(d.skill("fetch"))}}, d.female));
    if (g_carry.size() != CarrySize::Small) {
        d.dirt = std::min(100.0, d.dirt + c.tunef("dirt.per_haul", 8));
        double cost = c.tunef("carry.core_cost", 8) * (g_carry.size() == CarrySize::Large ? 1.0 : 0.5) / staminaFactor(d.stats[Stamina]);
        d.hunger = std::min(100.0, d.hunger + cost);
    }
    if (addSkillUse(d, "soft_mouth", c.tune("skill.uses.2", 10), c.tune("skill.uses.3", 25)))
        helpText(c.f("help.skill_up", {{"name", d.name}, {"skill", c.t("skill.soft_mouth")}, {"level", std::to_string(d.skill("soft_mouth"))}}, d.female));
    for (const auto& step : addDeed(d, c.breeds, "hunt", 1, c.rng)) FD_INFO("hardening: %s", step.c_str());
    comp.save();
}

bool bring(Ped carcass, bool keepQuality, bool body, bool pelt, bool toHorse) {
    auto& comp = companion();
    DogRecord* d = comp.record();
    if (!comp.present() || !d || g_carry.active()) return false;
    CarryJob job;
    job.carcass = carcass;
    job.size = pelt ? CarrySize::Small : CarryTask::sizeOf(carcass, &job.length);
    job.body = body;
    job.pelt = pelt;
    job.losesQuality = !keepQuality;
    job.speed = job.size == CarrySize::Small ? 3.0f : 1.0f;
    job.staminaSeconds = float(ctx().tunef("carry.stamina_seconds", 45)) * staminaFactor(d->stats[Stamina]);
    if (job.size == CarrySize::Large || body) {
        float base = float(ctx().tunef("carry.pull_force", 20));
        int s = d->stats[Strength];
        job.pull = s >= 3 ? base * (1.0f + 0.6f * (s - 3)) : base * 0.5f;
        if (body && job.size != CarrySize::Large) job.size = CarrySize::Large;
    }
    Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
    if (toHorse && alive(horse)) job.horse = horse;
    g_carry.start(comp.ped(), job);
    return true;
}

void tick() {
    auto& comp = companion();
    scanKills();
    if (!comp.present()) {
        if (g_carry.active()) g_carry.release();
        return;
    }
    DogRecord* d = comp.record();
    if (!d) return;
    if (d->woundedUntil > 0) {
        if (g_carry.active()) g_carry.release();
        return;
    }
    if (!g_carry.active()) g_carry.update();  // idle: only the stow-on-horse check runs
    if (g_carry.active()) {
        g_carry.update();
        if (!g_carry.active()) {
            if (g_carry.deliveries()) afterDelivery(*d);
            while (!g_queue.empty()) {
                Ped next = g_queue.front();
                g_queue.pop_front();
                if (exists(next) && !PED::_GET_CARRIER_AS_PED(next) && startCarry(comp.ped(), *d, next)) break;
            }
            if (!g_carry.active() && !comp.staying()) comp.follow();
        }
        return;
    }
    if (g_mark) {
        Ped dog = comp.ped();
        if (!exists(g_mark) || now() > g_markUntil) {
            g_mark = 0;
            if (!comp.staying()) comp.follow();
        } else if (dist(dog, g_mark) > 2.5f) {
            if (now() - g_markOrdered > 4000) {
                Vector3 p = pos(g_mark);
                TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(dog, p.x, p.y, p.z, 3.0f, -1, 1.5f, 0, 0.0f);
                g_markOrdered = now();
            }
        } else if (g_markUntil - now() > 7000) {
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_BARKING_UP"), 6000, true, 0, -1.0f, false);
            g_markUntil = now() + 6500;
        }
    }
}

}  // namespace fd::hunting
