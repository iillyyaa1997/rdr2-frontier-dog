#include "dog/abilities.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "core/log.h"
#include "core/strutil.h"
#include "dog/behaviour.h"
#include "dog/companion.h"
#include "dog/carry.h"
#include "dog/hunting.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

// Skills and traits that make the dog act on its own (old D13, D14, D43, D50, D60, D70, D88; skill table
// analysis/02-skills.md). Numbers are data (Companion.dat [Abilities]); everything respects «Не мешай», story
// missions and towns (behaviour::autonomyAllowed + town check).
namespace fd::abilities {

using namespace game;

static game::UiPrompt g_flush, g_body, g_pelt;
static int g_scanAt = 0, g_slowAt = 0;
// running jobs
static Ped g_hunt = 0, g_trail = 0, g_flushGame = 0, g_gripMan = 0, g_guardFoe = 0, g_fearlessFoe = 0;
static int g_huntUntil = 0, g_trailUntil = 0, g_holdUntil = 0, g_flushUntil = 0, g_gripUntil = 0, g_guardUntil = 0,
           g_fearlessUntil = 0, g_herdUntil = 0, g_herdCooldown = 0, g_pointUntil = 0, g_zoomUntil = 0, g_nextZoom = 0;
static float g_trailRange = 150;
static int g_holdMs = 0;
static Vector3 g_flushBehind, g_lastRoadPos;
static bool g_flushCircle = false, g_roadInit = false;
static std::map<int, int> g_noticed;  // ped -> time (pointing, ambush, guard bark, lawmen growl)
static int g_swimSince = 0;

bool busy() {
    int t = now();
    return (g_hunt && t < g_huntUntil) || (g_trail && t < g_trailUntil) || t < g_holdUntil || (g_flushGame && t < g_flushUntil) ||
           (g_gripMan && t < g_gripUntil) || (g_guardFoe && t < g_guardUntil) || (g_fearlessFoe && t < g_fearlessUntil) ||
           t < g_herdUntil || t < g_pointUntil || t < g_zoomUntil;
}

void start() {
    auto& c = ctx();
    g_flush.create("INPUT_CONTEXT_Y", c.t("prompt.flush"), 0, 2);
    g_body.create(c.ini.get("AttackControl", "INPUT_INTERACT_OPTION1"), c.t("prompt.bring_body"), 800, 2);
    g_pelt.create("INPUT_CONTEXT_Y", c.t("prompt.pelt_to_horse"), 800, 2);
}

static float noseFactor(const DogRecord& d) {
    static const float f[] = {0.55f, 0.55f, 0.7f, 0.85f, 1.0f, 1.25f};
    return f[std::clamp(d.stats[Nose], 1, 5)];
}

static bool recently(Ped p, int ms) {
    auto it = g_noticed.find(p);
    return it != g_noticed.end() && now() - it->second < ms;
}

static void deed(DogRecord& d, const char* id, int units) {
    for (const auto& step : addDeed(d, ctx().breeds, id, units, ctx().rng)) {
        FD_INFO("hardening: %s", step.c_str());
        if (step == "hardened")
            helpText(ctx().f("help.hardened", {{"name", d.name}, {"grade", ctx().gradeText(d)}}, d.female), 8000);
    }
}

static void growl(Ped dog, const DogRecord& d) {
    const BreedDef* b = ctx().breeds.find(d.breed);
    if (b && b->habits.getBool("quiet_alert", false)) AUDIO::PLAY_ANIMAL_VOCALIZATION(dog, "BARK_HAPPY", true);
    else TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_GUARD_GROWL"), 4000, true, 0, -1.0f, false);
}

// ---------------------------------------------------------------- Huntress (Shiba signature, D50)
static void huntress(Ped dog, DogRecord& d) {
    if (g_hunt) {
        if (!exists(g_hunt) || now() > g_huntUntil) { g_hunt = 0; return; }
        if (ENTITY::IS_ENTITY_DEAD(g_hunt)) {
            if (PED::GET_PED_SOURCE_OF_DEATH(g_hunt) == dog) {
                PED::_SET_PED_DAMAGE_CLEANLINESS(g_hunt, 2);  // a clean kill: the pelt stays perfect
                FD_INFO("huntress: caught it, brings it");
                hunting::bring(g_hunt, true, false, false, false);
                deed(d, "hunt", 1);
            }
            g_hunt = 0;
            return;
        }
        TASK::TASK_COMBAT_PED(dog, g_hunt, 0, 16);
        int jaw = PED::GET_PED_BONE_INDEX(dog, 58559);
        Vector3 jp = jaw >= 0 ? ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(dog, jaw) : pos(dog);
        if (dist(jp, pos(g_hunt)) < 1.3f) ENTITY::SET_ENTITY_HEALTH(g_hunt, 0, dog);  // catch assist (0.11: prey outran her)
        if (dist(g_hunt, player()) > 120.0f) g_hunt = 0;
        return;
    }
    if (!ctx().hasTrait(d, "huntress") || hunting::busy()) return;
    auto prey = ctx().breeds.raw().getList("huntress.prey");
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!alive(p) || !ENTITY::GET_IS_ANIMAL(p) || dist(p, dog) > 40.0f || dist(p, player()) > 60.0f) continue;
        if (PED::_GET_PED_QUALITY(p) < 2) continue;  // three stars only
        Hash m = ENTITY::GET_ENTITY_MODEL(p);
        bool listed = false;
        for (const auto& x : prey)
            if (MISC::GET_HASH_KEY(x.c_str()) == m) listed = true;
        if (!listed) continue;
        g_hunt = p;
        g_huntUntil = now() + 60000;
        TASK::CLEAR_PED_TASKS(dog, true, true);
        companion().markActive();
        FD_INFO("huntress: goes after a three-star animal");
        return;
    }
}

// ---------------------------------------------------------------- trail a wounded animal + hold at bay
static void trail(Ped dog, DogRecord& d) {
    int t = now();
    if (g_trail) {
        if (!exists(g_trail) || ENTITY::IS_ENTITY_DEAD(g_trail)) {
            if (addSkillUse(d, "trail", ctx().tune("skill.uses.2", 10), ctx().tune("skill.uses.3", 25)))
                helpText(ctx().f("help.skill_up", {{"name", d.name}, {"skill", ctx().t("skill.trail")}, {"level", std::to_string(d.skill("trail"))}}, d.female));
            g_trail = 0;
            return;
        }
        bool tracker = ctx().hasTrait(d, "bloodhound_nose") || ctx().hasTrait(d, "tracker");
        if ((!tracker && ENTITY::IS_ENTITY_IN_WATER(g_trail)) || dist(g_trail, player()) > g_trailRange || t > g_trailUntil) {
            FD_INFO("trail: went cold");
            g_trail = 0;
            if (!companion().staying()) companion().follow();
            return;
        }
        if (t < g_holdUntil) return;
        if (dist(dog, g_trail) < 4.0f) {
            if (g_holdMs > 0) {
                TASK::CLEAR_PED_TASKS(g_trail, true, true);
                TASK::TASK_STAND_STILL(g_trail, g_holdMs);
                TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_BARKING_GROUND"), g_holdMs, true, 0, -1.0f, false);
                g_holdUntil = t + g_holdMs;
                addSkillUse(d, "hold", ctx().tune("skill.uses.2", 10), ctx().tune("skill.uses.3", 25));
                FD_INFO("trail: holds it at bay %d s", g_holdMs / 1000);
                g_holdMs = 0;
            } else {
                TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_BARKING_UP"), 6000, true, 0, -1.0f, false);
                g_holdUntil = t + 6000;
            }
            return;
        }
        static int orderAt = 0;
        if (t - orderAt > 2500) {
            Vector3 a = pos(g_trail);
            TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(dog, a.x, a.y, a.z, 2.5f, -1, 2.0f, 0, 0.0f);
            orderAt = t;
        }
        return;
    }
    if (hunting::busy() || !behaviour::autonomyAllowed()) return;
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!alive(p) || !ENTITY::GET_IS_ANIMAL(p) || p == dog || dist(p, player()) > 80.0f) continue;
        if (PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(p)) || ctx().breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(p))) continue;
        int hp = ENTITY::GET_ENTITY_HEALTH(p), mx = std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(p, false));
        if (hp >= mx * 0.9 || !ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(p, player(), true, true) || !PED::IS_PED_FLEEING(p)) continue;
        if (recently(p, 300000)) continue;
        g_noticed[p] = now();
        int lvl = d.skill("trail");
        g_trailRange = (lvl >= 3 ? 450.0f : (lvl == 2 ? 300.0f : 150.0f)) * noseFactor(d);
        if (ctx().hasTrait(d, "tracker")) g_trailRange = std::max(g_trailRange, 700.0f);
        if (ctx().hasTrait(d, "bloodhound_nose")) g_trailRange = 2000.0f;  // never loses it
        float length;
        bool big = CarrySize::Large == fd::CarryTask::sizeOf(p, &length);
        int hold = d.skill("hold");
        bool canHold = d.stats[Courage] >= 3 || !big;
        g_holdMs = !canHold ? 0 : (ctx().hasTrait(d, "bay_holder") ? 60000 : (hold >= 3 ? 30000 : (hold == 2 ? 20000 : (big ? 0 : 10000))));
        g_trail = p;
        g_trailUntil = now() + 120000;
        TASK::CLEAR_PED_TASKS(dog, true, true);
        companion().markActive();
        FD_INFO("trail: a wounded animal, range %.0f m, hold %d s", g_trailRange, g_holdMs / 1000);
        return;
    }
}

// ---------------------------------------------------------------- pointing hidden game
static void pointing(Ped dog, DogRecord& d) {
    if (now() < g_pointUntil || hunting::busy() || dist(dog, player()) > 20.0f) return;
    int lvl = d.skill("point");
    float range = (lvl >= 3 ? 40.0f : (lvl == 2 ? 25.0f : 15.0f)) * noseFactor(d);
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!alive(p) || !ENTITY::GET_IS_ANIMAL(p) || p == dog || recently(p, 600000) || dist(p, dog) > range) continue;
        if (ENTITY::_GET_IS_PREDATOR(p) || PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(p))) continue;
        if (ctx().breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(p))) continue;
        float length;
        CarrySize size = fd::CarryTask::sizeOf(p, &length);
        bool bird = ENTITY::_GET_IS_BIRD(p);
        if (!(bird || (lvl >= 2 && size == CarrySize::Small) || (lvl >= 3 && size == CarrySize::Medium))) continue;
        if (ENTITY::IS_ENTITY_ON_SCREEN(p) && dist(p, player()) < 15.0f) continue;  // Arthur already sees it
        g_noticed[p] = now();
        TASK::CLEAR_PED_TASKS(dog, true, true);
        TASK::TASK_TURN_PED_TO_FACE_ENTITY(dog, p, 4000, 0, 0, 0);
        g_pointUntil = now() + 4500;
        addSkillUse(d, "point", ctx().tune("skill.uses.2", 10), ctx().tune("skill.uses.3", 25));
        companion().markActive();
        FD_INFO("pointing at game %.0f m", dist(p, dog));
        return;
    }
}

// ---------------------------------------------------------------- flushing on command
static void flushing(Ped dog, DogRecord& d) {
    int t = now();
    if (g_flushGame) {
        if (!alive(g_flushGame) || t > g_flushUntil || ENTITY::IS_ENTITY_IN_AIR(g_flushGame, 0) || PED::IS_PED_FLEEING(g_flushGame)) {
            if (alive(g_flushGame)) FD_INFO("flush: raised it %.0f m from Arthur", dist(g_flushGame, player()));
            addSkillUse(d, "flush", ctx().tune("skill.uses.2", 10), ctx().tune("skill.uses.3", 25));
            g_flushGame = 0;
            if (!companion().staying()) companion().follow();
            return;
        }
        static int orderAt = 0;
        if (t - orderAt > 3000) {
            orderAt = t;
            if (g_flushCircle && dist(pos(dog), g_flushBehind) > 2.5f && t < g_flushUntil - 10000) {
                TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(dog, g_flushBehind.x, g_flushBehind.y, g_flushBehind.z, 2.0f, -1, 1.0f, 0, 0.0f);
            } else {
                g_flushCircle = false;
                TASK::TASK_GO_TO_ENTITY(dog, g_flushGame, -1, 1.0f, 3.0f, 0, 0);
                AUDIO::PLAY_ANIMAL_VOCALIZATION(dog, "BARK_HAPPY", true);
            }
        }
        g_flush.show(false);
        return;
    }
    int lvl = d.skill("flush");
    float range = (lvl >= 3 ? 30.0f : (lvl == 2 ? 25.0f : 20.0f)) * (ctx().hasTrait(d, "flusher") ? 1.5f : 1.0f);
    Ped found = 0;
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n && !found; ++i) {
        Ped p = peds[i];
        if (!alive(p) || !ENTITY::GET_IS_ANIMAL(p) || p == dog || dist(p, dog) > range) continue;
        bool bird = ENTITY::_GET_IS_BIRD(p) && !ENTITY::IS_ENTITY_IN_AIR(p, 0);
        float length;
        bool smallGround = lvl >= 3 && !ENTITY::_GET_IS_PREDATOR(p) && fd::CarryTask::sizeOf(p, &length) == CarrySize::Small;
        if (bird || smallGround) found = p;
    }
    bool close = dist(dog, player()) < 4.0f;
    g_flush.setTarget(dog);
    g_flush.show(close && found);
    if (found && g_flush.fired()) {
        g_flushGame = found;
        g_flushUntil = t + 25000;
        Vector3 gp = pos(found), pp = pos(player());
        float dx = gp.x - pp.x, dy = gp.y - pp.y, l = std::max(0.5f, std::sqrt(dx * dx + dy * dy));
        g_flushBehind = Vector3(gp.x + dx / l * 7.0f, gp.y + dy / l * 7.0f, gp.z);
        g_flushCircle = lvl >= 2;  // drives it towards Arthur from the far side
        TASK::CLEAR_PED_TASKS(dog, true, true);
        companion().markActive();
    }
}

// ---------------------------------------------------------------- guarding Arthur's horse (Stay near it)
static void guarding(Ped dog, DogRecord& d) {
    Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
    int t = now();
    if (g_guardFoe) {
        if (!alive(g_guardFoe) || t > g_guardUntil || dist(g_guardFoe, horse) > 30.0f) {
            g_guardFoe = 0;
            Vector3 hp = pos(horse);
            TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(dog, hp.x + 2, hp.y, hp.z, 2.0f, -1, 1.0f, 0, 0.0f);  // back to the post
            g_guardUntil = t + 6000;
            deed(d, "guard", 1);
        }
        return;
    }
    if (!companion().staying() || !alive(horse) || dist(dog, horse) > 12.0f || dist(player(), horse) < 15.0f) return;
    int lvl = d.skill("guard");
    if (lvl >= 3 || ctx().hasTrait(d, "horse_herder")) PED::SET_PED_CONFIG_FLAG(horse, 312, true);  // no flight from gunshots
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!alive(p) || p == dog || p == horse || p == player()) continue;
        float dh = dist(p, horse);
        bool human = PED::IS_PED_HUMAN(p);
        bool predator = !human && ENTITY::_GET_IS_PREDATOR(p);
        if (!human && !predator) continue;
        if (human && (PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) == MISC::GET_HASH_KEY("REL_COP") ||
                      PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) == MISC::GET_HASH_KEY("REL_GANG_DUTCHS"))) continue;
        bool thief = human && (dh < 2.0f || PED::GET_MOUNT(p) == horse);
        if (lvl >= 2 && ((thief && !recently(p, 30000)) || (predator && dh < 25.0f))) {
            g_guardFoe = p;
            g_guardUntil = t + 15000;
            g_noticed[p] = t;
            TASK::TASK_COMBAT_PED(dog, p, 0, 16);
            companion().markActive();
            FD_INFO("guard: goes for the %s at the horse", thief ? "thief" : "predator");
            return;
        }
        if ((human ? dh < 10.0f : dh < 30.0f) && !recently(p, 20000)) {
            g_noticed[p] = t;
            growl(dog, d);
            FD_INFO("guard: barks at a %s %.0f m from the horse", human ? "stranger" : "predator", dh);
            return;
        }
    }
}

// ---------------------------------------------------------------- Herder of the horse (Australian shepherd, D88)
static void herder(Ped dog, DogRecord& d) {
    if (!ctx().hasTrait(d, "horse_herder") || now() < g_herdCooldown) return;
    Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
    if (!alive(horse) || PED::GET_MOUNT(player()) == horse || !PED::IS_PED_FLEEING(horse) || dist(horse, player()) > 60.0f) return;
    static int chaseSince = 0;
    if (!chaseSince) {
        chaseSince = now();
        TASK::TASK_GO_TO_ENTITY(dog, horse, -1, 2.0f, 3.0f, 0, 0);
        g_herdUntil = now() + 8000;
        return;
    }
    if (now() - chaseSince > 2000) {  // after a short chase the horse stops and walks back to Arthur
        TASK::CLEAR_PED_TASKS(horse, true, true);
        TASK::TASK_GO_TO_ENTITY(horse, player(), -1, 3.0f, 1.0f, 0, 0);
        g_herdCooldown = now() + 30000;
        chaseSince = 0;
        FD_INFO("herder: stopped the bolting horse");
    }
}

// ---------------------------------------------------------------- courage vs predators, Fearless, Grip
static void predatorsAndGrip(Ped dog, DogRecord& d) {
    int t = now();
    if (g_fearlessFoe) {
        if (!alive(g_fearlessFoe) || t > g_fearlessUntil) {
            if (g_fearlessFoe && !alive(g_fearlessFoe)) deed(d, "fight", 1);
            g_fearlessFoe = 0;
        }
    }
    if (!g_fearlessFoe) {
        int peds[128];
        int n = worldGetAllPeds(peds, 128);
        for (int i = 0; i < n; ++i) {
            Ped p = peds[i];
            if (!alive(p) || !ENTITY::_GET_IS_PREDATOR(p) || dist(p, player()) > 15.0f || !PED::IS_PED_IN_COMBAT(p, player())) continue;
            float len;
            CarrySize size = fd::CarryTask::sizeOf(p, &len);
            int courage = d.stats[Courage];
            bool fearless = ctx().hasTrait(d, "fearless");
            bool stands = fearless || (size != CarrySize::Large && courage >= 3) || (size == CarrySize::Large && courage >= 4);
            if (!stands) continue;
            g_fearlessFoe = p;
            g_fearlessUntil = t + (fearless ? 6000 : 4000);
            TASK::TASK_COMBAT_PED(dog, p, 0, 16);
            TASK::TASK_COMBAT_PED(p, dog, 0, 16);  // draws the predator off Arthur
            companion().markActive();
            FD_INFO("courage: the dog draws the predator off Arthur");
            break;
        }
    }
    // Grip: a man running from Arthur that the dog was sent at is knocked down
    if (ctx().hasTrait(d, "grip")) {
        Ped target = 0;
        if (PED::IS_PED_IN_COMBAT(dog, 0)) {
            int peds[64];
            int n = worldGetAllPeds(peds, 64);
            for (int i = 0; i < n; ++i)
                if (alive(peds[i]) && PED::IS_PED_HUMAN(peds[i]) && PED::IS_PED_FLEEING(peds[i]) && dist(peds[i], dog) < 2.0f) target = peds[i];
        }
        if (target && !recently(target, 10000)) {
            g_noticed[target] = t;
            PED::SET_PED_TO_RAGDOLL(target, 2000, 3500, 0, false, false, "");
            g_gripMan = target;
            g_gripUntil = t + 4000;
            FD_INFO("grip: the dog takes the runner down");
        }
    }
}

// ---------------------------------------------------------------- ambush sense, lawmen growl
static void senses(Ped dog, DogRecord& d) {
    int lvl = d.skill("ambush");
    float range = (lvl >= 3 ? 40.0f : (lvl == 2 ? 30.0f : 20.0f)) * noseFactor(d) * (ctx().hasTrait(d, "watchdog") ? 4.0f / 3.0f : 1.0f);
    bool wanted = PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0;
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!alive(p) || !PED::IS_PED_HUMAN(p) || p == player()) continue;
        float dd = dist(p, dog);
        bool cop = PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) == MISC::GET_HASH_KEY("REL_COP");
        if (cop && wanted && dd < 15.0f && !recently(p, 30000)) {
            g_noticed[p] = now();
            growl(dog, d);
            FD_INFO("growls at a lawman (Arthur is wanted)");
            return;
        }
        // a hostile man standing still out of sight: someone lying in wait (rel 5 hate / 6 dislike seen in game)
        int rel = PED::GET_RELATIONSHIP_BETWEEN_PEDS(p, player());
        if ((rel == 5 || rel == 6) && dd < range && PED::IS_PED_STOPPED(p) && !ENTITY::IS_ENTITY_ON_SCREEN(p) && !recently(p, 120000)) {
            g_noticed[p] = now();
            growl(dog, d);
            addSkillUse(d, "ambush", ctx().tune("skill.uses.2", 10), ctx().tune("skill.uses.3", 25));
            deed(d, "guard", 1);
            companion().markActive(8000);
            FD_INFO("senses a hostile man %.0f m away", dd);
            return;
        }
    }
}

// ---------------------------------------------------------------- bodies and pelts
static void bodiesAndPelts(Ped dog, DogRecord& d) {
    // aim at a dead man 6 m+ away within the fetch range: «Притащить тело» (strength 3 or Tenacious)
    Entity target = 0;
    bool aiming = PLAYER::GET_PLAYER_TARGET_ENTITY(PLAYER::PLAYER_ID(), &target);
    bool can = aiming && exists(target) && ENTITY::IS_ENTITY_A_PED(target) && PED::IS_PED_HUMAN(target) && ENTITY::IS_ENTITY_DEAD(target) &&
               dist(target, player()) > 6.0f && dist(target, player()) < 70.0f && !hunting::busy() &&
               (d.stats[Strength] >= 3 || ctx().hasTrait(d, "tenacious"));
    g_body.setTarget(can ? target : 0);
    g_body.show(can);
    if (can && g_body.fired() && hunting::bring(target, true, true, false, false)) companion().markActive();
    // Arthur carries a pelt next to the dog: «Отнести к лошади»
    Entity carried = PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(player());
    Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
    bool pelt = carried && !ENTITY::IS_ENTITY_A_PED(carried) && alive(horse) && dist(dog, player()) < 4.0f && dist(horse, player()) < 80.0f &&
                !hunting::busy();
    g_pelt.setTarget(dog);
    g_pelt.show(pelt);
    if (pelt && g_pelt.fired()) {
        Vector3 f = ENTITY::GET_ENTITY_FORWARD_VECTOR(player());
        Vector3 at = add(pos(player()), scale(f, 1.0f));
        TASK::TASK_PLACE_CARRIED_ENTITY_AT_COORD(player(), carried, at.x, at.y, at.z, 1.0f, 0);
        hunting::bring(carried, true, false, true, true);
    }
}

// ---------------------------------------------------------------- the Shiba's habits (generic switches)
static void habits(Ped dog, DogRecord& d) {
    const BreedDef* b = ctx().breeds.find(d.breed);
    if (!b) return;
    int t = now();
    // zoomies: a few fast laps round Arthur every N minutes
    int every = int(b->habits.getInt("zoomies_min", 0));
    if (every > 0 && !g_nextZoom) g_nextZoom = t + every * 60000;
    if (every > 0 && t >= g_nextZoom && !busy() && !hunting::busy() && dist(dog, player()) < 10.0f && !PED::IS_PED_ON_MOUNT(player())) {
        g_nextZoom = t + every * 60000;
        Vector3 p = pos(player());
        int seq = 0;
        TASK::OPEN_SEQUENCE_TASK(&seq);
        for (int k = 0; k < 8; ++k) {
            float a = k * 0.785f;
            TASK::TASK_GO_STRAIGHT_TO_COORD(0, p.x + std::cos(a) * 5, p.y + std::sin(a) * 5, p.z, 3.0f, -1, 0.0f, 1.0f, 0);
        }
        TASK::CLOSE_SEQUENCE_TASK(seq);
        TASK::TASK_PERFORM_SEQUENCE(dog, seq);
        TASK::CLEAR_SEQUENCE_TASK(&seq);
        g_zoomUntil = t + 9000;
        FD_INFO("habit: zoomies");
    }
    // keeps away from strangers closer than 3.5 m
    if (b->habits.getBool("shy_of_strangers", false) && !busy()) {
        int peds[64];
        int n = worldGetAllPeds(peds, 64);
        for (int i = 0; i < n; ++i) {
            Ped p = peds[i];
            if (!alive(p) || !PED::IS_PED_HUMAN(p) || p == player() || dist(p, dog) > 3.5f || recently(p, 15000)) continue;
            int rel = PED::GET_RELATIONSHIP_BETWEEN_PEDS(p, player());
            if (rel == 1 || rel == 2 || PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) == MISC::GET_HASH_KEY("REL_GANG_DUTCHS")) continue;
            g_noticed[p] = t;
            TASK::TASK_SMART_FLEE_PED(dog, p, 6.0f, 2500, 0, 1.0f, 0);
            FD_INFO("habit: steps away from a stranger");
            break;
        }
    }
}

// ---------------------------------------------------------------- deeds for mutt hardening
static void roadAndWater(Ped dog, DogRecord& d) {
    Vector3 p = pos(dog);
    if (!g_roadInit) { g_lastRoadPos = p; g_roadInit = true; }
    if (dist(p, g_lastRoadPos) > 1000.0f && dist(dog, player()) < 60.0f) {
        g_lastRoadPos = p;
        deed(d, "road", 1);  // a kilometre together
    }
    if (PED::IS_PED_SWIMMING(dog)) {
        if (!g_swimSince) g_swimSince = now();
        if (now() - g_swimSince > 5000) {
            g_swimSince = now() + 600000;  // one per swim
            deed(d, "water", 1);
        }
    } else if (g_swimSince && now() < g_swimSince - 590000) {
        g_swimSince = 0;
    }
}

void tick() {
    auto& comp = companion();
    if (!comp.present()) return;
    DogRecord* d = comp.record();
    if (!d || d->woundedUntil > 0) return;
    Ped dog = comp.ped();
    bool allowed = behaviour::autonomyAllowed() && !inTown(pos(player()));
    flushing(dog, *d);
    bodiesAndPelts(dog, *d);
    int t = now();
    if (t < g_scanAt) return;
    g_scanAt = t + 500;
    guarding(dog, *d);
    herder(dog, *d);
    predatorsAndGrip(dog, *d);
    if (allowed && !comp.staying()) {
        huntress(dog, *d);
        trail(dog, *d);
    }
    if (t >= g_slowAt) {
        g_slowAt = t + 2000;
        if (allowed && !comp.staying() && !busy()) pointing(dog, *d);
        senses(dog, *d);
        habits(dog, *d);
        roadAndWater(dog, *d);
        for (auto it = g_noticed.begin(); it != g_noticed.end();)
            it = (t - it->second > 900000) ? g_noticed.erase(it) : std::next(it);
    }
}

}  // namespace fd::abilities
