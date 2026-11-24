#include "dog/behaviour.h"
#include "dog/stables.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "core/log.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "dog/abilities.h"
#include "dog/home.h"
#include "dog/hunting.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

namespace fd::behaviour {

using namespace game;

static game::UiPrompt g_attack, g_track, g_feed, g_leave;
static Ped g_attackTarget = 0, g_trackTarget = 0, g_trackable = 0;
static int g_attackUntil = 0, g_trackUntil = 0, g_warnUntil = 0, g_nextScan = 0;
static bool g_leaveMeAlone = false, g_feedOffered = false, g_trackOffered = false;
static unsigned g_leaveZone = 0;
static bool g_waitingOutside = false, g_hiddenForCutscene = false;
static std::map<int, int> g_warned;  // predator -> time it was last warned about

// what the dog eats from the satchel, in this order: its own food first, then raw meat, cooked meat last (17.11 he
// could not tell what was spent - the prompt names it now and a notice says what was eaten); the treat is not a meal
static const char* kFood[] = {"CONSUMABLE_DOG_FOOD", "PROVISION_GAMEY_BIRD_MEAT", "PROVISION_PLUMP_BIRD_MEAT",
                              "PROVISION_TENDER_PORK", "PROVISION_GAME_MEAT", "PROVISION_GRISTLY_MUTTON",
                              "PROVISION_MATURE_VENISON", "PROVISION_PRIME_BEEF", "PROVISION_BIG_GAME_MEAT",
                              "CONSUMABLE_GAMEY_BIRD_COOKED", "CONSUMABLE_PLUMP_BIRD_COOKED", "CONSUMABLE_TENDER_PORK_COOKED",
                              "CONSUMABLE_GAME_MEAT_COOKED", "CONSUMABLE_GRISTLY_MUTTON_COOKED",
                              "CONSUMABLE_MATURE_VENISON_COOKED", "CONSUMABLE_DOG_TREAT"};

static const char* foodInSatchel() {
    for (const char* f : kFood)
        if (itemCount(f) > 0) return f;
    return nullptr;
}

void start() {
    auto& c = ctx();
    g_attack.create(c.ini.get("AttackControl", "INPUT_INTERACT_OPTION1"), c.t("prompt.attack"), 600, 2);
    g_track.create("INPUT_INTERACT_OPTION2", c.t("prompt.track"), 1000);
    g_feed.create("INPUT_INTERACT_OPTION1", c.t("prompt.feed"), 0, 3);
    g_leave.create("INPUT_INTERACT_OPTION2", c.t("prompt.leave"), 1500);
    g_feed.setRank(30);
    g_attack.setRank(35);
    g_track.setRank(40);
    g_leave.setRank(65);
}

bool autonomyAllowed() { return !onMission() && !inCutscene() && !g_leaveMeAlone && !home::inCamp(); }

bool feedOffered() { return g_feedOffered; }
bool trackOffered() { return g_trackOffered; }

bool busy() { return now() < g_attackUntil || now() < g_trackUntil || now() < g_warnUntil || hunting::busy() || abilities::busy(); }

static bool attackable(Ped t, Ped dog) {
    if (!alive(t) || t == dog || t == player() || stables::isTrader(t)) return false;
    if (PED::GET_PED_RELATIONSHIP_GROUP_HASH(t) == MISC::GET_HASH_KEY("REL_GANG_DUTCHS")) return false;
    if (!PED::IS_PED_ON_FOOT(t) || PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(t))) return false;
    if (ctx().breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(t))) return false;  // not at other dogs
    if (ctx().ini.getBool("AttackAnimalsOnly", false) && !ENTITY::GET_IS_ANIMAL(t)) return false;
    return dist(t, dog) <= float(ctx().tune("attack.range", 60));
}

static void updateAttack(Ped dog, DogRecord& d) {
    Entity target = 0;
    bool aiming = PLAYER::GET_PLAYER_TARGET_ENTITY(PLAYER::PLAYER_ID(), &target) && attackable(target, dog);
    bool inside = INTERIOR::GET_INTERIOR_FROM_ENTITY(player()) && !ctx().ini.getBool("AllowEnteringInteriors", false);
    if (aiming && !inside && ctx().feature("AttackPrompt")) {
        g_attack.setText(ctx().f("prompt.attack", {{"name", d.name}}, d.female));
        g_attack.setTarget(target);
        g_attack.show(true);
        if (g_attack.fired()) {
            g_attackTarget = target;
            g_attackUntil = now() + 20000;  // busy while the target lives (old 0.11.2: no stuck-check recall mid-chase)
            TASK::TASK_COMBAT_PED(dog, target, 0, 16);
            speech(player(), PED::IS_PED_MALE(dog) ? "HORSE_RUN_AWAY_MALE" : "HORSE_RUN_AWAY_FEMALE");
            FD_INFO("command: attack");
        }
    } else {
        g_attack.show(false);
    }
    if (g_attackTarget && (!alive(g_attackTarget) || now() > g_attackUntil)) {
        if (!alive(g_attackTarget)) for (const auto& st : addDeed(d, ctx().breeds, "fight", 1, ctx().rng)) FD_INFO("hardening: %s", st.c_str());
        g_attackTarget = 0;
        g_attackUntil = 0;
        if (!companion().staying()) companion().follow();
    }
}

// Stands up for Arthur on its own (24.11, his pick «1»: «хочется чтобы она могла сама нападать»): goes for the nearest
// man or animal in a fight with Arthur within 30 m. Never lawmen (no bounty for a bite) or the gang; a timid dog
// (Courage 1-2, not Fearless; a breed's habit.defend_min_courage overrides the bar - the Shiba goes in at 2) only barks at them. Not on missions, in camp, under «Leave it», told to stay, while
// busy with game or indoors. AutoAttack=0 in the ini turns it off.
static int g_nextDefend = 0, g_barkAt = 0;

static void updateDefend(Ped dog, DogRecord& d) {
    auto& c = ctx();
    if (!c.feature("AutoAttack") || now() < g_nextDefend) return;
    g_nextDefend = now() + 1000;
    if (g_attackTarget && alive(g_attackTarget)) return;  // already on someone (sent, or defending)
    if (!autonomyAllowed() || companion().staying() || hunting::busy() || abilities::busy()) return;
    if (INTERIOR::GET_INTERIOR_FROM_ENTITY(player()) && !c.ini.getBool("AllowEnteringInteriors", false)) return;
    float range = float(c.tune("defend.range", 30));
    Hash cop = MISC::GET_HASH_KEY("REL_COP");
    Ped best = 0;
    float bd = 1e9f;
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!attackable(p, dog) || !PED::IS_PED_IN_COMBAT(p, player())) continue;
        if (PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) == cop) continue;
        float dp = dist(p, player());
        if (dp <= range && dp < bd) { bd = dp; best = p; }
    }
    if (!best) return;
    // a breed may go in braver than the rest: the Shiba from Courage 2 (24.11 «Сиба должна нападать даже при 2»)
    const BreedDef* breed = c.breeds.find(d.breed);
    long long minCourage = breed ? breed->habits.getInt("defend_min_courage", 0) : 0;
    if (minCourage <= 0) minCourage = c.tune("defend.min_courage", 3);
    if (d.stats[Courage] < minCourage && !c.hasTrait(d, "fearless")) {
        if (now() >= g_barkAt) {  // too timid to go in: it barks at them from Arthur's side
            g_barkAt = now() + 8000;
            g_warnUntil = now() + 4000;
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_BARK_GROWL"), 4000, true, 0, -1.0f, false);
            FD_INFO("defend: too timid, barks at a foe %.0f m from Arthur", bd);
        }
        return;
    }
    g_attackTarget = best;
    g_attackUntil = now() + 20000;
    TASK::TASK_COMBAT_PED(dog, best, 0, 16);
    FD_INFO("defend: goes for the %s fighting Arthur, %.0f m from him", PED::IS_PED_HUMAN(best) ? "man" : "animal", bd);
}

static void updateTrack(Ped dog) {
    if (g_trackable && (!alive(g_trackable) || dist(g_trackable, player()) > 150.0f)) g_trackable = 0;
    bool close = dist(dog, player()) < 4.0f;
    bool offer = g_trackable && !g_trackTarget && close && dist(g_trackable, player()) > 20.0f;
    g_track.setTarget(dog);
    g_track.show(offer);
    g_trackOffered = offer;
    if (offer && g_track.fired()) {
        g_trackTarget = g_trackable;
        g_trackUntil = now() + 180000;
        TASK::CLEAR_PED_TASKS(dog, true, true);
        TASK::TASK_GO_TO_ENTITY(dog, g_trackTarget, -1, 6.0f, 2.0f, 0, 0);
        FD_INFO("command: track");
    }
    if (g_trackTarget) {
        if (!alive(g_trackTarget) || now() > g_trackUntil || dist(dog, g_trackTarget) < 8.0f) {
            if (alive(g_trackTarget) && dist(dog, g_trackTarget) < 8.0f) {
                TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_BARK_GROWL"), 8000, true, 0, -1.0f, false);
                g_warnUntil = now() + 8000;
            }
            g_trackTarget = 0;
            g_trackUntil = 0;
        }
    }
}

static void updateFeed(Ped dog, DogRecord& d) {
    const char* food = foodInSatchel();
    bool close = dist(dog, player()) < 3.0f && !PED::IS_PED_ON_MOUNT(player());
    bool hungry = d.hunger >= ctx().tunef("hunger.feed_from", 20);
    g_feed.setTarget(dog);
    if (food) g_feed.setText(ctx().f("prompt.feed_with", {{"food", ctx().t(std::string("food.") + food)}}));
    g_feedOffered = close && hungry && food;
    g_feed.show(g_feedOffered);
    if (food && g_feed.fired() && takeItem(food)) {
        bool treat = std::string(food) == "CONSUMABLE_DOG_TREAT";
        d.hunger = std::max(0.0, d.hunger - ctx().tunef(treat ? "hunger.treat" : "hunger.meal", treat ? 20 : 60));
        int unk = fd::invoke<int>(0x244430C13BA5258EULL, dog, 0, 1, 0);
        TASK::TASK_EAT(dog, unk, 0);
        companion().addBond(ctx().tunef(treat ? "bond.treat" : "bond.feed", treat ? 5 : 8), treat ? "treat" : "feed",
                            ctx().tune(treat ? "bond.treat_cooldown_ms" : "bond.feed_cooldown_ms", 60000));
        FD_INFO("fed the dog: %s, hunger %.0f", food, d.hunger);
        helpText(ctx().f("help.fed", {{"name", d.name}, {"food", ctx().t(std::string("food.") + food)}}, d.female), 5000);
    }
}

// predators near Arthur: one warning per predator (bark, the core shows), no endless re-trigger (old 1.0 bug)
static void updatePredators(Ped dog, DogRecord& d) {
    if (now() < g_nextScan) return;
    g_nextScan = now() + 1500;
    if (!autonomyAllowed() || companion().staying()) return;
    int peds[128];
    int n = worldGetAllPeds(peds, 128);
    float range = float(ctx().tune("predator.warn_range", 40));
    if (ctx().hasTrait(d, "sharp_ears")) range *= float(ctx().tunef("trait.sharp_ears.range", 4.0 / 3.0));
    for (int i = 0; i < n; ++i) {
        Ped p = peds[i];
        if (!alive(p) || !ENTITY::GET_IS_ANIMAL(p) || p == dog) continue;
        if (PED::GET_PED_RELATIONSHIP_GROUP_HASH(p) != MISC::GET_HASH_KEY("REL_WILD_ANIMAL_PREDATOR")) continue;
        if (dist(p, player()) > range) continue;
        auto it = g_warned.find(p);
        if (it != g_warned.end() && now() - it->second < 120000) continue;
        g_warned[p] = now();
        const BreedDef* b = ctx().breeds.find(d.breed);
        bool quiet = b && b->habits.getBool("quiet_alert", false);
        if (quiet) AUDIO::PLAY_ANIMAL_VOCALIZATION(dog, "BARK_HAPPY", true);  // the Shiba's single quiet 'boof' (old D54)
        else TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_BARK_GROWL"), 6000, true, 0, -1.0f, false);
        g_warnUntil = now() + 6000;
        FD_INFO("predator warning at %.0f m", dist(p, player()));
        break;
    }
}

static void updateInterior(Ped dog) {
    bool inside = INTERIOR::GET_INTERIOR_FROM_ENTITY(player()) && !ctx().ini.getBool("AllowEnteringInteriors", false);
    if (inside && !g_waitingOutside && !companion().staying()) {
        g_waitingOutside = true;
        TASK::CLEAR_PED_TASKS(dog, true, true);
        TASK::TASK_START_SCENARIO_IN_PLACE_HASH(dog, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_SITTING"), -1, true, 0, -1.0f, false);
        FD_INFO("waits outside");
    } else if (!inside && g_waitingOutside) {
        g_waitingOutside = false;
        companion().follow();
    }
}

// «Не мешай» (old D15): hold on the dog; the dog stops doing things on its own until the place changes
static void updateLeave(Ped dog) {
    unsigned zone = districtHash(pos(player()));
    if (g_leaveMeAlone && zone != g_leaveZone) {
        g_leaveMeAlone = false;
        FD_INFO("«Не мешай» ends: a new place");
    }
    float dd = dist(dog, player());
    bool close = dd >= 4.0f && dd < 25.0f;  // from a distance, while focusing the dog (old key map)
    g_leave.setText(ctx().t(g_leaveMeAlone ? "prompt.leave_off" : "prompt.leave"));
    g_leave.setTarget(dog);
    g_leave.show(close && !g_trackable);
    if (g_leave.fired()) {
        g_leaveMeAlone = !g_leaveMeAlone;
        g_leaveZone = zone;
    }
}

static void updateCutscene(Ped dog) {
    bool hide = inCutscene() && dist(dog, player()) < 60.0f;
    if (hide != g_hiddenForCutscene) {
        g_hiddenForCutscene = hide;
        ENTITY::SET_ENTITY_VISIBLE(dog, !hide);  // out of shot while a scene plays (D129)
    }
}

void tick() {
    auto& comp = companion();
    if (!comp.present()) return;
    DogRecord* d = comp.record();
    if (!d || d->woundedUntil > 0) return;
    Ped dog = comp.ped();
    updateCutscene(dog);
    updateAttack(dog, *d);
    updateDefend(dog, *d);
    updateTrack(dog);
    updateFeed(dog, *d);
    updatePredators(dog, *d);
    updateInterior(dog);
    updateLeave(dog);
}

void onScriptEvent(int index) {
    if (SCRIPT::GET_EVENT_AT_INDEX(1, index) != MISC::GET_HASH_KEY("EVENT_NETWORK_SCRIPT_EVENT")) return;
    int data[100] = {0};
    if (!SCRIPT::GET_EVENT_DATA(1, index, (Any*)data, 50)) return;
    unsigned id = unsigned(data[0]);
    Ped target = data[2];  // second 8-byte slot (the event data are read as 32-bit halves)
    if (id == str::joaat("DOG_COMPANION_SET_TRACKABLE_TARGET") || id == str::joaat("FRONTIER_DOG_SET_TRACKABLE_TARGET")) {
        if (!alive(target)) target = data[1];
        if (alive(target) && dist(target, player()) <= 150.0f) {
            g_trackable = target;
            FD_INFO("trackable target from another mod");
        }
    } else if (id == str::joaat("FRONTIER_DOG_ATTACK") && companion().present() && alive(target)) {
        g_attackTarget = target;
        g_attackUntil = now() + 20000;
        TASK::TASK_COMBAT_PED(companion().ped(), target, 0, 16);
    }
}

}  // namespace fd::behaviour
