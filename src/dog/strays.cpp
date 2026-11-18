#include "dog/strays.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "core/fs.h"
#include "core/kvfile.h"
#include "core/log.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "dog/home.h"
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
    bool beggar = false;  // waits by a butcher's or a saloon's door
    Vector3 home;         // where a beggar waits
    int bornAt = 0;
    std::string model;    // the model it was made from (a remembered stray comes back the same)
    int memId = -1;       // in the remembered list (fed once, 18.11 his pick)
};

// what Arthur can feed a stray from his satchel
static const char* kFood[] = {"CONSUMABLE_DOG_FOOD", "PROVISION_GAME_MEAT", "PROVISION_GAMEY_BIRD_MEAT",
                              "PROVISION_PLUMP_BIRD_MEAT", "PROVISION_TENDER_PORK", "PROVISION_PRIME_BEEF"};
static const char* foodInSatchel() {
    for (const char* f : kFood)
        if (game::itemCount(f) > 0) return f;
    return nullptr;
}

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

static void forgetImpl(Ped dog);
void forget(Ped dog) { forgetImpl(dog); }

struct PendingStray {
    SpawnRequest req;
    DogRecord card;
    Vector3 area;
    bool beggar = false;
    int memId = -1;
};
static std::vector<PendingStray> g_pending;  // streams in over frames (stepSpawn), never a blocking wait

// ---- remembered strays: a fed stray is kept (look, grade, trust) in Dogs/Strays.dat and met again in the same parts
// (18.11: «я покормил одну дворняжку, но я не знаю где она теперь» — it was gone once he rode off)
struct Remembered {
    int id = 0;
    DogRecord card;
    int trust = 0;
    long long lastFedDay = -1;
    Vector3 where;
    std::string model;
    Ped ped = 0;
    int goneAt = 0;
    bool pending = false;
};
static std::vector<Remembered> g_mem;
static bool g_memLoaded = false;
static int g_memNextId = 1, g_nextMemCheck = 0;

static std::string memPath() { return fs::join(fs::join(ctx().dataDir, "Dogs"), "Strays.dat"); }

static void memLoad() {
    if (g_memLoaded) return;
    g_memLoaded = true;
    KvFile kv;
    if (!kv.load(memPath())) return;
    for (const auto& idText : kv.getList("strays")) {
        int id = std::atoi(idText.c_str());
        std::string pre = "stray." + idText + ".";
        KvFile card;
        for (const auto& key : kv.keysWithPrefix(pre + "card."))
            card.set(key.substr(pre.size() + 5), kv.get(key));
        Remembered r;
        r.id = id;
        r.card.fromKv(card);
        r.trust = int(kv.getInt(pre + "trust", 0));
        r.lastFedDay = kv.getInt(pre + "fed_day", -1);
        auto w = kv.getFloats(pre + "where");
        if (w.size() == 3) r.where = Vector3(float(w[0]), float(w[1]), float(w[2]));
        r.model = kv.get(pre + "model");
        if (!r.model.empty()) g_mem.push_back(r);
        g_memNextId = std::max(g_memNextId, id + 1);
    }
    FD_INFO("remembered strays: %zu", g_mem.size());
}

static void memSave() {
    KvFile kv;
    std::vector<std::string> ids;
    for (const auto& r : g_mem) {
        std::string idText = std::to_string(r.id), pre = "stray." + idText + ".";
        ids.push_back(idText);
        KvFile card = r.card.toKv();
        for (const auto& key : card.keys()) kv.set(pre + "card." + key, card.get(key));
        kv.setInt(pre + "trust", r.trust);
        kv.setInt(pre + "fed_day", r.lastFedDay);
        kv.set(pre + "where", std::to_string(r.where.x) + "," + std::to_string(r.where.y) + "," + std::to_string(r.where.z));
        kv.set(pre + "model", r.model);
    }
    kv.setList("strays", ids);
    kv.save(memPath());
}

static void memRemember(Ped p, Known& k) {
    memLoad();
    if (k.model.empty()) k.model = k.card.model;  // a dog the game itself made: the model its card was read from
    if (k.model.empty()) return;
    Remembered* r = nullptr;
    for (auto& m : g_mem)
        if (m.id == k.memId) r = &m;
    if (!r) {
        if (g_mem.size() >= 8) {  // the one fed longest ago makes room
            auto oldest = std::min_element(g_mem.begin(), g_mem.end(),
                                           [](const Remembered& a, const Remembered& b) { return a.lastFedDay < b.lastFedDay; });
            g_mem.erase(oldest);
        }
        g_mem.push_back(Remembered{});
        r = &g_mem.back();
        r->id = g_memNextId++;
        k.memId = r->id;
    }
    r->card = k.card;
    r->trust = k.trust;
    r->lastFedDay = k.lastFedDay;
    r->where = pos(p);
    r->model = k.model;
    r->ped = p;
    memSave();
    FD_INFO("stray %d remembered at %.0f %.0f, trust %d", r->id, r->where.x, r->where.y, r->trust);
}

static void forgetImpl(Ped dog) {  // tamed: it is Arthur's dog now, no longer a stray to meet
    auto it = g_known.find(dog);
    if (it != g_known.end() && it->second.memId >= 0) {
        int id = it->second.memId;
        g_mem.erase(std::remove_if(g_mem.begin(), g_mem.end(), [id](const Remembered& r) { return r.id == id; }), g_mem.end());
        memSave();
    }
    g_known.erase(dog);
}

// a remembered stray Arthur has left behind comes back when he is in its parts again (50-120 m from where it was fed)
static void memTick(int t) {
    if (t < g_nextMemCheck) return;
    g_nextMemCheck = t + 5000;
    memLoad();
    if (g_mem.empty() || !g_pending.empty() || onMission() || INTERIOR::GET_INTERIOR_FROM_ENTITY(player())) return;
    Vector3 pp = pos(player());
    for (auto& m : g_mem) {
        if (m.ped && exists(m.ped) && !ENTITY::IS_ENTITY_DEAD(m.ped)) continue;
        if (m.pending || t - m.goneAt < 60000) continue;
        float d = std::hypot(pp.x - m.where.x, pp.y - m.where.y);
        if (d < 50.0f || d > 120.0f) continue;
        PendingStray ps;
        ps.card = m.card;
        ps.req.model = m.model;
        ps.req.fallback = "a_c_doghobo_01";
        ps.req.at = m.where;
        ps.area = m.where;
        ps.memId = m.id;
        m.pending = true;
        m.ped = 0;
        g_pending.push_back(ps);
        FD_INFO("remembered stray %d: Arthur is %.0f m from its place, it comes out", m.id, d);
        return;  // one at a time
    }
}


static int oursAround() {
    int n = int(g_pending.size());
    for (auto& [p, k] : g_known)
        if (k.ours && exists(p)) ++n;
    return n;
}

static Ped g_beggar = 0;
static bool g_begging = false;

// a fed stray that turns up by the gang camp later (D106 idea 5: it follows Arthur for a while, or waits near camp)
struct CampVisitor {
    DogRecord card;
    long long untilDay = -1;
    bool spawned = false;
};
static CampVisitor g_visitor;
static int g_nextVisitor = 0;

static void setupStray(Ped p, DogRecord card, const Vector3& at, bool beggar, bool keepLook = false) {
    auto& c = ctx();
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
    int presets = PED::GET_NUM_META_PED_OUTFITS(p);
    if (presets > 0) {
        if (!keepLook || card.preset < 0 || card.preset >= presets) card.preset = c.rng.range(0, presets - 1);
        PED::_EQUIP_META_PED_OUTFIT_PRESET(p, card.preset, false);
    }
    if (card.scale != 100) {
        PED::_SET_PED_SCALE(p, card.scale / 100.0f);
        if (card.scale < 100) PED::SET_PED_CAPSULE(p, 0.6f);
    }
    card.female = !PED::IS_PED_MALE(p);
    TASK::TASK_WANDER_IN_AREA(p, at.x, at.y, at.z, beggar ? 4.0f : 25.0f, 2.0f, 4.0f, 0);
    Known k;
    k.card = card;
    k.ours = true;
    k.beggar = beggar;
    k.home = at;
    if (beggar) {
        g_beggar = p;
        g_begging = false;
    }
    k.bornAt = now();
    g_known[p] = k;
}

static void stepStrays() {
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (Ped p = stepSpawn(it->req)) {
            PendingStray ps = *it;
            g_pending.erase(it);
            setupStray(p, ps.card, ps.area, ps.beggar, ps.memId >= 0);
            g_known[p].model = ps.req.onFallback ? ps.req.fallback : ps.req.model;
            for (auto& m : g_mem)
                if (m.id == ps.memId) {  // a remembered stray is back: its trust and feeding day too
                    g_known[p].memId = m.id;
                    g_known[p].trust = m.trust;
                    g_known[p].lastFedDay = m.lastFedDay;
                    m.ped = p;
                    m.pending = false;
                    FD_INFO("remembered stray %d is back near %.0f %.0f", m.id, m.where.x, m.where.y);
                }
            return;  // at most one ped per frame
        }
        if (it->req.done)
            for (auto& m : g_mem)
                if (m.id == it->memId) {
                    m.pending = false;
                    m.goneAt = now();
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

// Beggars (D106 idea 3): now and then a lone mutt waits by the door of a butcher or a saloon (Beggars.dat, RDOMap
// shops) - one roll per place per game day; when Arthur comes by with food in his satchel it trots up and sits begging,
// and feeding it is the usual stray feeding (trust, maybe it tags along).
struct BeggarSpot {
    std::string id;
    Vector3 at;
    long long rolledDay = -1;
    bool wanted = false;  // the roll said yes, waiting for a spot out of view
};
static std::vector<BeggarSpot> g_spots;
static int g_nextBeggar = 0;
static std::string g_beggarWhy;
static bool g_beggarVerbose = false;  // set by the test hook: log every reason a beggar does not come

static void loadBeggarSpots() {
    g_spots.clear();  // again after a save load (the script restarts, the list stays)
    KvFile kv;
    if (kv.load(fs::join(ctx().dataDir, "Beggars.dat")))
        for (const auto& k : kv.getList("beggars")) {
            auto v = kv.getFloats("beggar." + k);
            if (v.size() == 2) g_spots.push_back({k, Vector3(float(v[0]), float(v[1]), 0)});
        }
    FD_INFO("beggar spots: %d", int(g_spots.size()));
}

static void tickBeggar(int t) {
    auto& c = ctx();
    if (t < g_nextBeggar) return;
    g_nextBeggar = t + 2000;
    Ped pl = player();
    Vector3 pp = pos(pl);
    if (g_beggar && !alive(g_beggar)) g_beggar = 0;
    if (g_beggar) {
        auto it = g_known.find(g_beggar);
        if (it == g_known.end()) { g_beggar = 0; return; }
        float d = dist(g_beggar, pl);
        float notice = float(c.tunef("strays.beggar_notice", 25));
        bool food = foodInSatchel() != nullptr && it->second.lastFedDay != gameDay();
        if (food && d < notice && !onMission()) {
            if (d > 2.5f) {
                TASK::TASK_GO_TO_ENTITY(g_beggar, pl, -1, 1.8f, 1.2f, 0, 0);
            } else if (!g_begging) {
                g_begging = true;
                TASK::TASK_TURN_PED_TO_FACE_ENTITY(g_beggar, pl, 1500, 0, 0, 0);
                TASK::TASK_START_SCENARIO_IN_PLACE_HASH(g_beggar, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_SITTING"), -1, true, 0,
                                                       -1.0f, false);
                helpText(c.t("help.stray_begs"));
                FD_INFO("beggar sits by Arthur");
            }
        } else if (g_begging && (d > notice + 5.0f || !food)) {
            g_begging = false;  // he walked off or fed it: back to the door
            TASK::CLEAR_PED_TASKS(g_beggar, true, true);
            const Vector3& home = it->second.home;
            TASK::TASK_WANDER_IN_AREA(g_beggar, home.x, home.y, home.z, 4.0f, 2.0f, 4.0f, 0);
        }
        return;
    }
    // why no beggar now: logged once per change (always for a door with no unseen spot; the rest after the test hook)
    auto why = [](const char* reason, const std::string& at, bool always) {
        std::string w = std::string(reason) + at;
        if (w != g_beggarWhy && (always || g_beggarVerbose)) FD_INFO("beggar waits: %s", w.c_str());
        g_beggarWhy = w;
    };
    if (!g_pending.empty()) return why("a stray is still streaming in", "", false);
    if (onMission()) return why("a mission", "", false);
    if (INTERIOR::GET_INTERIOR_FROM_ENTITY(pl)) return why("Arthur is indoors", "", false);
    if (oursAround() >= c.tune("strays.cap", 3)) return why("the strays cap is full", "", false);
    float radius = float(c.tunef("strays.beggar_radius", 90));
    long long day = gameDay();
    for (auto& s : g_spots) {
        if (std::hypot(s.at.x - pp.x, s.at.y - pp.y) > radius) continue;
        if (s.rolledDay != day) {
            s.rolledDay = day;
            s.wanted = c.rng.chance(c.tunef("strays.beggar_chance", 0.6));
            FD_INFO("beggar roll at %s: %s", s.id.c_str(), s.wanted ? "yes" : "no");
        }
        if (!s.wanted) continue;
        Vector3 door(s.at.x, s.at.y, pp.z), at;
        if (!hiddenSpotNear(door, 5, 16, at, true))  // retry later: every spot by the door is in view or indoors
            return why("no unseen spot outdoors by ", s.id, true);
        const BreedDef* mutt = c.breeds.find("mutt");
        if (!mutt) return;
        PendingStray ps;
        ps.card = rollStray(*mutt, c.breeds, c.rng, c.chances, biasAt(at));
        ps.req.model = ps.card.model.empty() ? "a_c_doghobo_01" : ps.card.model;
        ps.req.fallback = "a_c_doghobo_01";
        ps.req.at = at;
        ps.area = at;
        ps.beggar = true;
        g_pending.push_back(ps);
        s.wanted = false;
        FD_INFO("beggar queued by %s, %.0f m from Arthur", s.id.c_str(), dist(at, pp));
        g_beggarWhy.clear();
        g_beggarVerbose = false;
        return;
    }
}

void spawnNow() { spawnMutts(); }

void hardNow() {
    auto& c = ctx();
    const BreedDef* mutt = c.breeds.find("mutt");
    Vector3 at;
    if (!mutt || !hiddenSpotNear(pos(player()), 20, 35, at, true)) {
        FD_INFO("test: no street-hardened mutt (no mutt breed or no unseen spot)");
        return;
    }
    PendingStray ps;
    ps.card = makeDog(*mutt, c.breeds, Grade::Pedigree, c.rng, c.chances, biasAt(at));
    ps.card.papers = false;
    ps.req.model = ps.card.model.empty() ? "a_c_doghobo_01" : ps.card.model;
    ps.req.fallback = "a_c_doghobo_01";
    ps.req.at = at;
    ps.area = at;
    g_pending.push_back(ps);
    FD_INFO("test: a street-hardened mutt queued %.0f m away", dist(at, pos(player())));
}

void visitorNow() {
    auto& c = ctx();
    const BreedDef* mutt = c.breeds.find("mutt");
    if (!mutt) return;
    g_visitor = CampVisitor{rollStray(*mutt, c.breeds, c.rng, c.chances, biasAt(pos(player()))),
                            gameDay() + c.tune("strays.camp_visit_days", 3), false};
    g_nextVisitor = 0;
    FD_INFO("test: stray will wait near camp until day %lld", g_visitor.untilDay);
}

void beggarNow() {
    if (g_beggar && alive(g_beggar)) {
        Vector3 b = pos(g_beggar);
        FD_INFO("test: the beggar is at %.1f %.1f %.1f, %.0f m from Arthur, %s", b.x, b.y, b.z, dist(g_beggar, player()),
                g_begging ? "begging" : "waiting");
        return;
    }
    g_beggarVerbose = true;
    g_beggarWhy.clear();
    float radius = float(ctx().tunef("strays.beggar_radius", 90));
    Vector3 pp = pos(player());
    int inReach = 0;
    for (auto& s : g_spots)
        if (std::hypot(s.at.x - pp.x, s.at.y - pp.y) <= radius) ++inReach;
    FD_INFO("test: beggar spots within %.0f m: %d", radius, inReach);
    for (auto& s : g_spots) {
        s.rolledDay = gameDay();
        s.wanted = true;
    }
    g_nextBeggar = 0;
}

void tick() {
    if (!ctx().feature("Strays")) return;
    auto& c = ctx();
    int t = now();
    if (!g_started) {
        g_started = true;
        g_feed.create("INPUT_INTERACT_OPTION1", c.t("prompt.feed"), 800);
        g_nextSpawn = t + 60000;
        loadBeggarSpots();
    }
    if (t >= g_nextClean) {
        g_nextClean = t + 5000;
        for (auto it = g_known.begin(); it != g_known.end();) {
            Ped p = it->first;
            bool gone = !exists(p) || ENTITY::IS_ENTITY_DEAD(p);
            bool distant = exists(p) && dist(p, player()) > 220.0f && !ENTITY::IS_ENTITY_ON_SCREEN(p);
            if (gone || (distant && it->second.ours)) {
                for (auto& m : g_mem)
                    if (m.id == it->second.memId) {
                        m.ped = 0;
                        m.goneAt = t;
                        m.pending = false;
                    }
                if (it->second.memId >= 0 && exists(p) && ENTITY::IS_ENTITY_DEAD(p)) {  // died: nothing to meet again
                    int id = it->second.memId;
                    g_mem.erase(std::remove_if(g_mem.begin(), g_mem.end(), [id](const Remembered& r) { return r.id == id; }), g_mem.end());
                    memSave();
                    FD_INFO("remembered stray %d died", id);
                }
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
    memTick(t);
    tickBeggar(t);
    if (t >= g_nextSpawn) {
        g_nextSpawn = t + c.tune("strays.spawn_every_ms", 240000);
        if (c.rng.chance(c.tunef("strays.spawn_chance", 0.5))) spawnMutts();
    }
    // the fed stray waiting near camp: it turns up by the camp's edge the next time Arthur is there
    if (g_visitor.untilDay >= 0 && t >= g_nextVisitor) {
        g_nextVisitor = t + 5000;
        Vector3 camp, at;
        if (gameDay() > g_visitor.untilDay) {
            g_visitor = CampVisitor{};
        } else if (!g_visitor.spawned && home::inCamp() && g_pending.empty() && home::campKnown(camp) &&
                   hiddenSpotNear(camp, 25, 45, at)) {
            PendingStray ps;
            ps.card = g_visitor.card;
            ps.req.model = ps.card.model.empty() ? "a_c_doghobo_01" : ps.card.model;
            ps.req.fallback = "a_c_doghobo_01";
            ps.req.at = at;
            ps.area = at;
            ps.beggar = false;
            g_pending.push_back(ps);
            g_visitor.spawned = true;
            FD_INFO("the fed stray waits near camp, %.0f m out", dist(at, camp));
        }
    }
    // a wary street-hardened mutt runs off when Arthur fires near it (D106 idea 4)
    bool shooting = PED::IS_PED_SHOOTING(player());
    // wary street-hardened mutts keep their distance; feeding them (once a game day) builds trust
    Ped pl = player();
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    bool offered = false;
    for (auto& [p, k] : g_known) {
        if (!alive(p) || p == companion().ped()) continue;
        float d = dist(p, pl);
        if (wary(p) && d < 6.0f && !PED::IS_PED_FLEEING(p)) TASK::TASK_SMART_FLEE_PED(p, pl, 12.0f, 4000, 0, 2.0f, 0);
        if (wary(p) && shooting && d < 40.0f && !PED::IS_PED_FLEEING(p)) {
            TASK::TASK_SMART_FLEE_PED(p, pl, 80.0f, 15000, 0, 3.0f, 0);
            FD_INFO("wary stray runs from the shots");
        }
        if (focus == p && d < 10.0f && !offered) {
            const char* have = foodInSatchel();
            long long day = gameDay();
            bool can = have && k.lastFedDay != day;
            g_feed.setTarget(p);
            if (have) g_feed.setText(c.f("prompt.feed_with", {{"food", c.t(std::string("food.") + have)}}));
            g_feed.show(can);
            offered = can;
            if (can && g_feed.fired() && takeItem(have)) {
                k.lastFedDay = day;
                ++k.trust;
                int unk = fd::invoke<int>(0x244430C13BA5258EULL, p, 0, 1, 0);
                TASK::TASK_EAT(p, unk, 0);
                FD_INFO("stray fed: trust %d", k.trust);
                memRemember(p, k);
                if (!k.card.isStreetHardened(c.breeds) && c.rng.chance(c.tunef("strays.follow_chance", 0.3))) {
                    if (c.rng.chance(0.5)) {
                        g_follower = p;  // it trots after Arthur for a while (D106 idea 5)
                        g_followUntil = t + 120000;
                    } else {
                        g_visitor = CampVisitor{k.card, gameDay() + c.tune("strays.camp_visit_days", 3), false};
                        FD_INFO("stray will wait near camp until day %lld", g_visitor.untilDay);
                    }
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
