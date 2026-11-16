#include "dog/strays.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "core/log.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

namespace fd::strays {

using namespace game;

struct Known {
    DogRecord card;
    int trust = 0;
    long long lastFedDay = -1;
    bool ours = false;  // spawned by the mod
    int bornAt = 0;
};

static std::map<int, Known> g_known;
static int g_nextSpawn = 0, g_nextClean = 0, g_followUntil = 0;
static Ped g_follower = 0;
static int g_nextFollow = 0;
static game::UiPrompt g_feed;
static bool g_started = false;

static TraitBias biasAt(const Vector3& at) {
    TraitBias b;
    unsigned zone = districtHash(at);
    const KvFile& raw = ctx().breeds.raw();
    for (const auto& k : raw.keysWithPrefix("region.")) {
        if (str::joaat(k.substr(7).c_str()) != zone) continue;
        for (const auto& part : raw.getList(k)) {
            auto kv = str::split(part, ':');
            double w;
            if (kv.size() == 2 && str::parseFloat(kv[1], w)) b[kv[0]] = w;
        }
    }
    return b;
}

DogRecord cardFor(Ped dog) {
    auto it = g_known.find(dog);
    if (it != g_known.end()) return it->second.card;
    auto& c = ctx();
    const BreedDef* b = c.breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(dog));
    if (!b) b = c.breeds.find("mutt");
    Known k;
    k.card = rollStray(*b, c.breeds, c.rng, c.chances, biasAt(pos(dog)));
    k.card.female = !PED::IS_PED_MALE(dog);
    for (const auto& m : b->models)
        if (MISC::GET_HASH_KEY(m.c_str()) == ENTITY::GET_ENTITY_MODEL(dog)) k.card.model = m;
    k.bornAt = now();
    g_known[dog] = k;
    FD_INFO("stray %s met: grade %d", b->id.c_str(), int(k.card.grade));
    return k.card;
}

bool wary(Ped dog) {
    auto it = g_known.find(dog);
    if (it == g_known.end()) return false;
    return it->second.card.isStreetHardened(ctx().breeds) && it->second.trust < ctx().tune("strays.trust_days", 2);
}

void forget(Ped dog) { g_known.erase(dog); }

struct PendingStray {
    SpawnRequest req;
    DogRecord card;
    Vector3 area;
};
static std::vector<PendingStray> g_pending;  // streams in over frames (stepSpawn), never a blocking wait

static int oursAround() {
    int n = int(g_pending.size());
    for (auto& [p, k] : g_known)
        if (k.ours && exists(p)) ++n;
    return n;
}

static void setupStray(Ped p, DogRecord card, const Vector3& at) {
    auto& c = ctx();
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
    int presets = PED::GET_NUM_META_PED_OUTFITS(p);
    if (presets > 0) {
        card.preset = c.rng.range(0, presets - 1);
        PED::_EQUIP_META_PED_OUTFIT_PRESET(p, card.preset, false);
    }
    if (card.scale != 100) {
        PED::_SET_PED_SCALE(p, card.scale / 100.0f);
        if (card.scale < 100) PED::SET_PED_CAPSULE(p, 0.6f);
    }
    card.female = !PED::IS_PED_MALE(p);
    TASK::TASK_WANDER_IN_AREA(p, at.x, at.y, at.z, 25.0f, 2.0f, 4.0f, 0);
    Known k;
    k.card = card;
    k.ours = true;
    k.bornAt = now();
    g_known[p] = k;
}

static void stepStrays() {
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (Ped p = stepSpawn(it->req)) {
            PendingStray ps = *it;
            g_pending.erase(it);
            setupStray(p, ps.card, ps.area);
            return;  // at most one ped per frame
        }
        it = it->req.done ? g_pending.erase(it) : it + 1;
    }
}

static void spawnMutts() {
    auto& c = ctx();
    int cap = c.tune("strays.cap", 3);
    if (!g_pending.empty() || oursAround() >= cap || onMission() || INTERIOR::GET_INTERIOR_FROM_ENTITY(player())) return;
    Vector3 pp = pos(player());
    const BreedDef* mutt = c.breeds.find("mutt");
    if (!mutt) return;
    bool nearTown = inTown(add(pp, Vector3(120, 0, 0))) || inTown(add(pp, Vector3(-120, 0, 0))) ||
                    inTown(add(pp, Vector3(0, 120, 0))) || inTown(add(pp, Vector3(0, -120, 0)));
    int pack = nearTown && c.rng.chance(c.tunef("strays.pack_chance", 0.35)) ? c.rng.range(2, 3) : 1;
    pack = std::min(pack, cap - oursAround());
    Vector3 at;
    if (!hiddenSpotNear(pp, 60, 95, at)) return;
    TraitBias bias = biasAt(at);
    for (int i = 0; i < pack; ++i) {
        DogRecord card = rollStray(*mutt, c.breeds, c.rng, c.chances, bias);
        if (pack > 1 && i == 0 && c.rng.chance(0.5)) {  // the leader of a pack is more often the street-hardened one
            card = makeDog(*mutt, c.breeds, Grade::Pedigree, c.rng, c.chances, bias);
            card.papers = false;
        }
        PendingStray ps;
        ps.req.model = card.model.empty() ? "a_c_doghobo_01" : card.model;
        ps.req.fallback = "a_c_doghobo_01";
        ps.req.at = add(at, Vector3(float(i) * 1.5f, 0, 0));
        ps.card = card;
        ps.area = at;
        g_pending.push_back(ps);
    }
    FD_INFO("strays: %d mutt(s) queued %.0f m away%s", pack, dist(at, pp), nearTown ? " near a town" : "");
}

void spawnNow() { spawnMutts(); }

void tick() {
    if (!ctx().feature("Strays")) return;
    auto& c = ctx();
    int t = now();
    if (!g_started) {
        g_started = true;
        g_feed.create("INPUT_INTERACT_OPTION1", c.t("prompt.feed"), 800);
        g_nextSpawn = t + 60000;
    }
    if (t >= g_nextClean) {
        g_nextClean = t + 5000;
        for (auto it = g_known.begin(); it != g_known.end();) {
            Ped p = it->first;
            bool gone = !exists(p) || ENTITY::IS_ENTITY_DEAD(p);
            bool distant = exists(p) && dist(p, player()) > 220.0f && !ENTITY::IS_ENTITY_ON_SCREEN(p);
            if (gone || (distant && it->second.ours)) {
                if (it->second.ours && exists(p)) {
                    Ped q = p;
                    deletePed(q);
                }
                it = g_known.erase(it);
            } else {
                ++it;
            }
        }
    }
    stepStrays();
    if (t >= g_nextSpawn) {
        g_nextSpawn = t + c.tune("strays.spawn_every_ms", 240000);
        if (c.rng.chance(c.tunef("strays.spawn_chance", 0.5))) spawnMutts();
    }
    // wary street-hardened mutts keep their distance; feeding them (once a game day) builds trust
    Ped pl = player();
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    bool offered = false;
    for (auto& [p, k] : g_known) {
        if (!alive(p) || p == companion().ped()) continue;
        float d = dist(p, pl);
        if (wary(p) && d < 6.0f && !PED::IS_PED_FLEEING(p)) TASK::TASK_SMART_FLEE_PED(p, pl, 12.0f, 4000, 0, 2.0f, 0);
        if (focus == p && d < 10.0f && !offered) {
            static const char* food[] = {"PROVISION_GAME_MEAT", "PROVISION_GAMEY_BIRD_MEAT", "PROVISION_PLUMP_BIRD_MEAT",
                                         "PROVISION_TENDER_PORK", "PROVISION_PRIME_BEEF", "CONSUMABLE_DOG_FOOD"};
            const char* have = nullptr;
            for (const char* f : food)
                if (itemCount(f) > 0) { have = f; break; }
            long long day = gameDay();
            bool can = have && k.lastFedDay != day;
            g_feed.setTarget(p);
            g_feed.show(can);
            offered = can;
            if (can && g_feed.fired() && takeItem(have)) {
                k.lastFedDay = day;
                ++k.trust;
                int unk = fd::invoke<int>(0x244430C13BA5258EULL, p, 0, 1, 0);
                TASK::TASK_EAT(p, unk, 0);
                FD_INFO("stray fed: trust %d", k.trust);
                if (!k.card.isStreetHardened(c.breeds) && c.rng.chance(c.tunef("strays.follow_chance", 0.3))) {
                    g_follower = p;  // it trots after Arthur for a while (D106 idea 5)
                    g_followUntil = t + 120000;
                }
                helpText(c.t(wary(p) ? "help.stray_wary" : "help.stray_trusts"));
            }
        }
    }
    if (!offered) g_feed.show(false);
    if (g_follower) {
        if (!alive(g_follower) || t > g_followUntil) {
            if (alive(g_follower)) TASK::TASK_WANDER_STANDARD(g_follower, 10.0f, 10);
            g_follower = 0;
        } else if (dist(g_follower, pl) > 8.0f && t >= g_nextFollow) {
            g_nextFollow = t + 4000;
            TASK::TASK_GO_TO_ENTITY(g_follower, pl, -1, 4.0f, 2.0f, 0, 0);
        }
    }
}

}  // namespace fd::strays
