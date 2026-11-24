#include "dog/delivery.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/fs.h"
#include "core/log.h"
#include "dog/coat.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "dog/home.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

namespace fd::delivery {

using namespace game;

// Journey (D144, operator «1» 16.11): the courier really leaves the camp; while he is far from Arthur (the game does not
// move peds that far: a rider 220-264 m away stood still for a minute) he is a map blip going along the road nodes at a
// horse's pace; within live range he becomes the real rider at that spot (if Arthur does not see it) and rides up.
enum class Stage { Idle, Message, Journey, Riding, Handover, Leaving };
static Stage g_stage = Stage::Idle;
static int g_slot = -1, g_stageAt = 0, g_orderedAt = 0, g_progressAt = 0;
static float g_lastDist = 0;
static Ped g_rider = 0, g_horse = 0, g_dog = 0, g_messenger = 0;
static game::UiPrompt g_campNext, g_campOrder, g_message, g_messageNext;  // g_messageNext: which dog the passer-by asks for
static int g_campIndex = 0, g_whistleSince = 0;
static std::vector<Vector3> g_telegraphs;
static Vector3 g_virt, g_wp;  // the courier while out of range (on the map) and his next road point
static bool g_hasWp = false;
static Blip g_blip = 0;
static int g_fee = 0, g_lastStep = 0, g_lastSave = 0, g_holdSince = 0, g_messageWaitMs = 15000;

static std::string stateFile() { return fs::join(fs::join(ctx().dataDir, "Dogs"), "courier.dat"); }
static float flat(const Vector3& a, const Vector3& b) { return std::hypot(a.x - b.x, a.y - b.y); }
static float speed() { return float(ctx().tunef("courier.speed_mps", 9)); }
static float liveRange() { return float(ctx().tunef("courier.live_range", 140)); }
static void clearJourney();

bool active() { return g_stage != Stage::Idle; }

void start() {
    g_campNext.create("INPUT_CONTEXT_X", ctx().t("stable.next"));
    g_campOrder.create("INPUT_INTERACT_OPTION1", "", 1200);
    g_message.create("INPUT_INTERACT_OPTION1", ctx().t("prompt.message"), 1000, 2);
    g_messageNext.create("INPUT_CONTEXT_X", ctx().t("stable.next"), 0, 2);
    KvFile tel;
    g_telegraphs.clear();  // again after a save load (the script restarts, the list stays)
    if (tel.load(fs::join(ctx().dataDir, "Telegraph.dat")))
        for (const auto& k : tel.getList("telegraphs")) {
            auto v = tel.getFloats("telegraph." + k);
            if (v.size() == 2) g_telegraphs.push_back(Vector3(float(v[0]), float(v[1]), 0));
        }
    // a courier on the road when the game was closed goes on from where he was
    KvFile kv;
    if (kv.load(stateFile())) {
        int slot = int(kv.getInt("slot", -1));
        auto p = kv.getFloats("pos");
        auto& store = ctx().store;
        if (slot >= 0 && slot < DogStore::kSlots && store.used(slot) && store.at(slot).uid == kv.get("uid") && p.size() == 3 &&
            store.at(slot).where == Where::Away && store.at(slot).awayReason == "courier") {
            g_slot = slot;
            g_fee = int(kv.getInt("fee", 0));
            g_virt = Vector3(float(p[0]), float(p[1]), float(p[2]));
            g_hasWp = false;
            g_stage = Stage::Journey;
            g_stageAt = now();
            FD_INFO("delivery: the courier with %s is still on the way", store.at(slot).name.c_str());
        } else {
            clearJourney();
        }
    }
    // a dog left 'with the courier' without a journey (the game closed while the rider was close) is back home
    for (int s = 0; s < DogStore::kSlots; ++s) {
        if (s == g_slot || !ctx().store.used(s)) continue;
        DogRecord& d = ctx().store.at(s);
        if (d.where == Where::Away && d.awayReason == "courier") {
            d.where = Where::Home;
            d.awayReason.clear();
            d.awayUntil = 0;
            ctx().store.save(s);
            FD_INFO("delivery: %s was with a courier who never arrived - back home", d.name.c_str());
        }
    }
}

static bool atTelegraph() {
    Vector3 p = pos(player());
    for (const auto& t : g_telegraphs) {
        float dx = t.x - p.x, dy = t.y - p.y;
        if (dx * dx + dy * dy < 12.0f * 12.0f) return true;
    }
    return false;
}

static std::vector<int> homeSlots() {
    std::vector<int> out;
    for (int s = 0; s < DogStore::kSlots; ++s)
        if (ctx().store.used(s) && ctx().store.at(s).where == Where::Home && s != companion().slot()) out.push_back(s);
    return out;
}

static void removeBlip() {
    if (g_blip && MAP::DOES_BLIP_EXIST(g_blip)) MAP::REMOVE_BLIP(&g_blip);
    g_blip = 0;
}

static void courierBlip(Entity onEntity, const Vector3& at) {
    if (!g_blip || !MAP::DOES_BLIP_EXIST(g_blip) || onEntity) {
        removeBlip();
        g_blip = onEntity ? MAP::BLIP_ADD_FOR_ENTITY(0x19365607, onEntity) : MAP::BLIP_ADD_FOR_COORDS(0x19365607, at.x, at.y, at.z);
        MAP::SET_BLIP_SPRITE(g_blip, MISC::GET_HASH_KEY("blip_ambient_horse"), true);
        MAP::BLIP_ADD_MODIFIER(g_blip, MISC::GET_HASH_KEY("BLIP_MODIFIER_RADAR_EDGE_ALWAYS"));
        if (g_slot >= 0 && ctx().store.used(g_slot)) {
            const DogRecord& d = ctx().store.at(g_slot);
            MAP::_SET_BLIP_NAME(g_blip, literal(ctx().f("blip.courier", {{"name", d.name}}, d.female)));
        }
        return;
    }
    MAP::SET_BLIP_COORDS(g_blip, at.x, at.y, at.z);
}

static void saveJourney() {
    if (g_slot < 0 || !ctx().store.used(g_slot)) return;
    KvFile kv;
    kv.setInt("schema", 1);
    kv.setInt("slot", g_slot);
    kv.set("uid", ctx().store.at(g_slot).uid);
    kv.setInt("fee", g_fee);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.1f,%.1f,%.1f", g_virt.x, g_virt.y, g_virt.z);
    kv.set("pos", buf);
    kv.save(stateFile());
}

static void clearJourney() {
    fs::remove(stateFile());
    fs::remove(stateFile() + ".bak");
}

// the dog travels with the courier: not at home (no copy in camp, not offered again), not with Arthur yet
static void setTravelling(bool on) {
    if (g_slot < 0 || !ctx().store.used(g_slot)) return;
    DogRecord& d = ctx().store.at(g_slot);
    if (on) {
        d.where = Where::Away;
        d.awayReason = "courier";
        d.awayUntil = gameMinutes() + 24 * 60;  // safety: never stuck away if the journey is lost
    } else if (d.where == Where::Away && d.awayReason == "courier") {
        d.where = Where::Home;
        d.awayReason.clear();
        d.awayUntil = 0;
    }
    ctx().store.save(g_slot);
}

static void cleanup() {
    for (Ped* p : {&g_rider, &g_horse}) {
        if (exists(*p)) ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(p);
        *p = 0;
    }
    removeBlip();
    clearJourney();
    g_stage = Stage::Idle;
    g_slot = -1;
    g_hasWp = false;
    g_holdSince = 0;
}

static std::string horseModel() { return ctx().ini.get("CourierHorse", "A_C_Horse_KentuckySaddle_Black"); }
static std::string riderModel() { return ctx().ini.get("CourierModel", "A_M_M_RANCHER_01"); }
static std::string dogModel() {
    const DogRecord& d = ctx().store.at(g_slot);
    return d.model.empty() ? "a_c_doghobo_01" : d.model;
}

// streams the party's models in before the courier comes into range, so the spawn itself does not wait
static void preloadModels() {
    for (const std::string& m : {horseModel(), riderModel(), dogModel()}) {
        Hash h = MISC::GET_HASH_KEY(m.c_str());
        if (STREAMING::IS_MODEL_VALID(h)) STREAMING::REQUEST_MODEL(h, false);
    }
}

// a start out of sight near Arthur (no camp known, or the camp is close): on a road on the camp's side
static bool nearStart(Vector3& from) {
    Vector3 pp = pos(player()), road, camp;
    Vector3 dir(0, 1, 0);
    if (home::campKnown(camp) && dist(camp, pp) > 10) {
        float l = dist(camp, pp);
        dir = Vector3((camp.x - pp.x) / l, (camp.y - pp.y) / l, 0);
    }
    // window 16.11: a rider started 220-264 m away never moved (speed 0 for a minute) - far peds are not simulated
    for (float r : {140.0f, 120.0f, 160.0f, 100.0f}) {
        Vector3 want = add(pp, scale(dir, r));
        if (PATH::GET_CLOSEST_VEHICLE_NODE(want.x, want.y, want.z, &road, 0, 3.0f, 0.0f) && dist(road, pp) > 90.0f &&
            !CAMERA::IS_SPHERE_VISIBLE(road.x, road.y, road.z + 1.0f, 2.0f)) {
            from = road;
            return true;
        }
    }
    return hiddenSpotNear(pp, 90, 150, from);
}

static bool spawnPartyAt(const Vector3& from) {
    const DogRecord& d = ctx().store.at(g_slot);
    g_horse = spawnPed(horseModel(), from, 0, 3000);
    if (!g_horse) g_horse = spawnPed("A_C_Horse_Morgan_Bay", from, 0, 3000);
    g_rider = spawnPed(riderModel(), add(from, Vector3(1, 0, 0)), 0, 3000);
    g_dog = spawnPed(dogModel(), add(from, Vector3(-1.5f, -1.5f, 0)), 0, 3000);
    if (!g_horse || !g_rider || !g_dog) {
        deletePed(g_horse); deletePed(g_rider); deletePed(g_dog);
        return false;
    }
    for (Ped p : {g_horse, g_rider, g_dog}) ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
    PED::SET_PED_ONTO_MOUNT(g_rider, g_horse, -1, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_rider, true);
    ENTITY::SET_ENTITY_INVINCIBLE(g_dog, true);
    DECORATOR::DECOR_SET_INT(g_dog, "FD_shop", 1);
    coat::dress(g_dog, d);
    if (d.scale > 0 && d.scale != 100) PED::_SET_PED_SCALE(g_dog, d.scale / 100.0f);
    TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(g_dog, g_horse, 1.5f, -1.0f, 0, 3.0f, -1, 2.0f, true, false, false, false, false, false);
    courierBlip(g_horse, from);
    g_stage = Stage::Riding;
    g_stageAt = g_orderedAt = g_progressAt = now();
    g_lastDist = 1e9f;
    FD_INFO("delivery: rider with %s %.0f m away", d.name.c_str(), dist(from, pos(player())));
    return true;
}

// ---- the journey on the map

// the next point: the road node nearest to a probe ahead towards Arthur, if it brings the courier closer; else straight
static void nextWaypoint(const Vector3& target) {
    float l = flat(g_virt, target);
    float step = float(ctx().tunef("courier.step_m", 60));
    g_hasWp = true;
    if (l <= step) {
        g_wp = Vector3(target.x, target.y, g_virt.z);
        return;
    }
    Vector3 probe(g_virt.x + (target.x - g_virt.x) / l * step, g_virt.y + (target.y - g_virt.y) / l * step, g_virt.z);
    PATH::REQUEST_PATH_NODES_IN_AREA_THIS_FRAME(std::min(g_virt.x, probe.x) - 200, std::min(g_virt.y, probe.y) - 200,
                                                std::max(g_virt.x, probe.x) + 200, std::max(g_virt.y, probe.y) + 200);
    Vector3 node;
    if (PATH::GET_CLOSEST_VEHICLE_NODE(probe.x, probe.y, probe.z, &node, 0, 3.0f, 0.0f)) {
        float hop = flat(g_virt, node);
        if (flat(node, target) < l - 5.0f && hop > 5.0f && hop < step * 2.0f) {
            g_wp = node;
            return;
        }
    }
    g_wp = probe;  // no road that way (or its nodes are not loaded): across country
}

static void journey() {
    int t = now();
    if (!g_lastStep) g_lastStep = t;
    float dt = (t - g_lastStep) / 1000.0f;
    if (dt < 0.5f) return;
    g_lastStep = t;
    if (g_slot < 0 || !ctx().store.used(g_slot)) {
        cleanup();
        return;
    }
    Vector3 target = pos(player());
    float away = flat(g_virt, target);
    if (away > liveRange()) {
        float budget = speed() * std::min(dt, 5.0f);  // a long pause (menus) does not jump the courier ahead
        for (int guard = 0; budget > 0.01f && guard < 8; ++guard) {
            if (!g_hasWp || flat(g_virt, g_wp) < 1.0f) nextWaypoint(target);
            float d = flat(g_virt, g_wp);
            if (d < 0.01f) { g_hasWp = false; continue; }
            float k = std::min(1.0f, budget / d);
            g_virt = Vector3(g_virt.x + (g_wp.x - g_virt.x) * k, g_virt.y + (g_wp.y - g_virt.y) * k, g_virt.z + (g_wp.z - g_virt.z) * k);
            budget -= d * k;
            if (k >= 1.0f) g_hasWp = false;
        }
        courierBlip(0, g_virt);
        if (t - g_lastSave > 10000) {
            g_lastSave = t;
            saveJourney();
        }
        if (flat(g_virt, target) < liveRange() * 1.6f) preloadModels();
        return;
    }
    // in range: the real rider takes over where the courier is, unless Arthur looks right there (then he waits a little)
    if (onMission()) return;
    preloadModels();
    if (!g_holdSince) g_holdSince = t;
    Vector3 from;
    bool ok = safeCoord(g_virt, from) && flat(from, g_virt) < 30.0f &&
              !CAMERA::IS_SPHERE_VISIBLE(from.x, from.y, from.z + 1.0f, 2.5f);
    if (!ok && t - g_holdSince > 20000) ok = nearStart(from);
    if (!ok) return;
    clearJourney();
    if (!spawnPartyAt(from)) {
        if (t - g_holdSince > 60000) {
            FD_WARN("delivery: the courier could not be placed near Arthur");
            setTravelling(false);
            giveCash(g_fee * 100);
            helpText(ctx().t("help.courier_no_way"));
            cleanup();
        }
        return;
    }
}

bool order(int slot, const char* how) {
    auto& c = ctx();
    if (active() || slot < 0 || !c.store.used(slot)) return false;
    if (onMission() || PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) {
        helpText(c.t("help.courier_refuses"));
        return false;
    }
    Vector3 camp, pp = pos(player());
    bool campKnown = home::campKnown(camp);
    float meters = campKnown ? flat(camp, pp) : 3000.0f;
    int fee = deliveryFee(meters, c.econ);
    if (!takeCash(fee * 100)) {
        helpText(c.t("help.no_money"));
        return false;
    }
    g_slot = slot;
    g_fee = fee;
    const DogRecord& d = c.store.at(slot);
    if (campKnown && meters > liveRange()) {
        // he leaves the camp on the road nearest to it
        Vector3 road;
        g_virt = camp;
        if (PATH::GET_CLOSEST_VEHICLE_NODE(camp.x, camp.y, camp.z, &road, 0, 3.0f, 0.0f) && flat(road, camp) < 300.0f) g_virt = road;
        g_hasWp = false;
        g_lastStep = g_lastSave = g_holdSince = 0;
        g_stage = Stage::Journey;
        g_stageAt = now();
        setTravelling(true);
        saveJourney();
        courierBlip(0, g_virt);
        int minutes = std::max(1, int(std::ceil(meters * float(c.tunef("courier.road_factor", 1.25)) / speed() / 60.0f)));
        helpText(c.f("help.courier_left", {{"name", d.name}, {"min", std::to_string(minutes)}, {"fee", std::to_string(fee)}}, d.female), 8000);
        FD_INFO("delivery ordered (%s), fee $%d: the courier leaves camp, %.0f m, ~%d min", how, fee, meters, minutes);
        return true;
    }
    // no camp known yet, or the camp is close: he appears out of sight nearby
    Vector3 from;
    setTravelling(true);
    if (!nearStart(from) || !spawnPartyAt(from)) {
        setTravelling(false);
        giveCash(fee * 100);
        helpText(c.t("help.courier_no_way"));
        cleanup();
        return false;
    }
    helpText(c.f("help.courier_coming", {{"name", d.name}, {"fee", std::to_string(fee)}}, d.female), 7000);
    FD_INFO("delivery ordered (%s), fee $%d", how, fee);
    return true;
}

static void riding() {
    int t = now();
    if (!alive(g_rider) || !alive(g_horse) || !exists(g_dog)) {
        FD_WARN("delivery: the courier or the dog is gone");
        if (exists(g_dog)) deletePed(g_dog);
        setTravelling(false);
        cleanup();
        return;
    }
    float d = dist(g_rider, player());
    // the dog runs beside the horse and must keep up (window 16.11 #3: at a gallop it was 48 m behind at the handover)
    float lag = dist(g_dog, g_horse);
    bool dogClose = lag < 12.0f;
    if ((d < 9.0f || (!PED::IS_PED_ON_MOUNT(player()) && d < 12.0f)) && (dogClose || t - g_stageAt > 90000)) {
        TASK::TASK_STAND_STILL(g_rider, 4000);
        g_stage = Stage::Handover;
        g_stageAt = t;
        return;
    }
    bool waiting = d < 12.0f;  // at Arthur: waits for the dog
    float pace = waiting ? 0.0f : (lag > 25.0f ? 1.0f : (lag > 12.0f ? 1.5f : (d > 60 ? 2.0f : 1.5f)));
    if (t - g_orderedAt > 3000) {
        g_orderedAt = t;
        if (waiting) TASK::TASK_STAND_STILL(g_rider, 3500);
        else TASK::TASK_GO_TO_ENTITY(g_rider, player(), -1, 7.0f, pace, 0, 0);
        if (lag > 12.0f) TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(g_dog, g_horse, 1.5f, -1.0f, 0, 3.0f, -1, 2.0f, true, false, false, false, false, false);
    }
    if (t - g_progressAt > 30000) {
        bool stuck = g_lastDist - d < 10.0f;
        FD_INFO("delivery: rider %.0f m from Arthur (was %.0f), mounted %d, speed %.1f", d, g_lastDist,
                int(PED::IS_PED_ON_MOUNT(g_rider)), ENTITY::GET_ENTITY_SPEED(g_horse));
        g_lastDist = d;
        g_progressAt = t;
        if (stuck && d >= 110.0f) {
            // far and not moving (window 16.11: stood 2 min at 222 m): move the party to a road closer, out of sight
            Vector3 spot, road;
            Vector3 pp = pos(player());
            if (hiddenSpotNear(pp, 70, 110, spot)) {
                if (PATH::GET_CLOSEST_VEHICLE_NODE(spot.x, spot.y, spot.z, &road, 0, 3.0f, 0.0f) && dist(road, pp) > 50.0f &&
                    !CAMERA::IS_SPHERE_VISIBLE(road.x, road.y, road.z + 1.0f, 2.0f))
                    spot = road;
                ENTITY::SET_ENTITY_COORDS(g_horse, spot.x, spot.y, spot.z, false, false, false, true);
                if (!PED::IS_PED_ON_MOUNT(g_rider)) PED::SET_PED_ONTO_MOUNT(g_rider, g_horse, -1, true);
                ENTITY::SET_ENTITY_COORDS(g_dog, spot.x - 1.5f, spot.y - 1.5f, spot.z, false, false, false, true);
                TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(g_dog, g_horse, 1.5f, -1.0f, 0, 3.0f, -1, 2.0f, true, false, false, false, false, false);
                g_orderedAt = 0;
                g_lastDist = dist(spot, pp);
                FD_INFO("delivery: the rider was stuck far away, now on the way %.0f m out of sight", g_lastDist);
            }
        } else if (stuck && d < 110.0f) {
            // where a horse cannot get through (a cliff, a house): the dog runs the last stretch (D115)
            FD_INFO("delivery: the rider cannot get closer, the dog runs the rest");
            g_stage = Stage::Handover;
            g_stageAt = t - 4000;
        }
    }
    if (t - g_stageAt > 600000) {  // ten minutes: give up honestly
        helpText(ctx().t("help.courier_no_way"));
        deletePed(g_dog);
        setTravelling(false);
        cleanup();
    }
}

static void handover() {
    if (now() - g_stageAt < 2500) return;
    if (g_slot >= 0 && ctx().store.used(g_slot) && exists(g_dog)) {
        DogRecord& rec = ctx().store.at(g_slot);
        rec.awayReason.clear();
        rec.awayUntil = 0;
        ENTITY::SET_ENTITY_INVINCIBLE(g_dog, false);
        companion().takeFromCamp(g_slot, g_dog);
        const DogRecord& d = ctx().store.at(g_slot);
        helpText(ctx().f("help.courier_done", {{"name", d.name}}, d.female));
    }
    g_dog = 0;
    removeBlip();
    if (alive(g_rider)) TASK::TASK_WANDER_STANDARD(g_rider, 10.0f, 10);
    g_stage = Stage::Leaving;
    g_stageAt = now();
}

// Arthur sitting at the fire of a camp he set up: the game shows its camp prompts (sleep, craft, leave) in the prompt
// group 0x6898122F. The order joins that group, as the Contracts mod's prompt does (its .asi puts prompts there; checked
// in game 22.11). As a free prompt it took the whole camp panel away (reported on Nexus 22.11, seen in game too).
// The camp fire seat is scenario type 0x3CD3DC55 (two camps, 22.11); other seats get no order prompt.
constexpr int kCampPromptGroup = 0x6898122F;
constexpr unsigned kCampFireSeat = 0x3CD3DC55u;
static bool g_atCampFire = false;

#ifdef FD_TEST
// 'scen': what Arthur sits at
void testScenario() {
    Ped pl = player();
    int point = TASK::_GET_SCENARIO_POINT_PED_IS_USING(pl, true);
    FD_INFO("test: scenario any=%d point=%d type=%08X pedType=%08X interior=%d campHome=%d",
            int(PED::IS_PED_USING_ANY_SCENARIO(pl)), point, point > 0 ? unsigned(TASK::_GET_SCENARIO_POINT_TYPE(point)) : 0u,
            unsigned(TASK::_GET_SCENARIO_POINT_TYPE_PED_IS_USING(pl)), int(INTERIOR::GET_INTERIOR_FROM_ENTITY(pl)),
            int(home::inCamp()));
}
#endif

void tick() {
    if (!ctx().feature("Delivery")) return;
    auto& c = ctx();
    int t = now();
    switch (g_stage) {
        case Stage::Journey: journey(); return;
        case Stage::Riding: riding(); return;
        case Stage::Handover: handover(); return;
        case Stage::Leaving:
            if (t - g_stageAt > 20000 && (!exists(g_rider) || !ENTITY::IS_ENTITY_ON_SCREEN(g_rider) || dist(g_rider, player()) > 120))
                cleanup();
            return;
        case Stage::Message:
            if (t - g_stageAt > g_messageWaitMs) {  // he rides to camp with the message first
                if (exists(g_messenger)) ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&g_messenger);
                g_messenger = 0;
                g_stage = Stage::Idle;
                order(g_slot, "passer-by");
            }
            return;
        default:
            break;
    }
    auto slots = homeSlots();
    // (a) Arthur's own campfire in the wild
    bool sitting = PED::IS_PED_ON_FOOT(player()) && PED::IS_PED_USING_ANY_SCENARIO(player()) &&
                   unsigned(TASK::_GET_SCENARIO_POINT_TYPE_PED_IS_USING(player())) == kCampFireSeat;
    if (sitting != g_atCampFire) {
        g_atCampFire = sitting;
        if (sitting) FD_INFO("Arthur sits at his camp fire: the order joins the camp prompts");
    }
    bool telegraph = PED::IS_PED_ON_FOOT(player()) && atTelegraph();  // (b) a telegram from the post office
    g_campOrder.setGroup(sitting ? kCampPromptGroup : 0);
    g_campNext.setGroup(sitting ? kCampPromptGroup : 0);
    if ((sitting || telegraph) && !slots.empty()) {
        g_campIndex %= int(slots.size());
        const DogRecord& d = c.store.at(slots[g_campIndex]);
        g_campOrder.setText(c.f(sitting ? "prompt.send_for" : "prompt.telegram", {{"name", d.name}}, d.female));
        g_campNext.show(slots.size() > 1);
        g_campOrder.show(true);
        if (g_campNext.fired()) g_campIndex = (g_campIndex + 1) % int(slots.size());
        if (g_campOrder.fired()) order(slots[g_campIndex], sitting ? "campfire" : "telegram");
    } else {
        g_campNext.show(false);
        g_campOrder.show(false);
    }
    // (d) a passer-by takes the message to camp for $2
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    // passers-by outdoors only: indoors it sat in the bartender's menu and hid Care Overhaul's canteen refill (22.11).
    // Only the game's ambient people: a man a script made (a stranger with a job, the sawmill foreman asking about the
    // wolves on 24.11, a bounty, another mod's NPC) is a mission entity or carries a blip, and our «Другое» took the place
    // of the game's «Принять» there
    bool canAsk = focus && alive(focus) && PED::IS_PED_HUMAN(focus) && !slots.empty() && dist(focus, player()) < 4.0f &&
                  !INTERIOR::GET_INTERIOR_FROM_ENTITY(player()) && !ENTITY::IS_ENTITY_A_MISSION_ENTITY(focus) &&
                  !MAP::GET_BLIP_FROM_ENTITY(focus) &&
                  PED::GET_PED_RELATIONSHIP_GROUP_HASH(focus) != MISC::GET_HASH_KEY("REL_COP") &&
                  PED::GET_PED_RELATIONSHIP_GROUP_HASH(focus) != MISC::GET_HASH_KEY("REL_GANG_DUTCHS") &&
                  !PED::IS_PED_IN_COMBAT(focus, 0);
    if (canAsk) {
        // which dog: the same pick as at the campfire and the telegraph («Другое» goes through the dogs at home)
        g_campIndex %= int(slots.size());
        const DogRecord& d = c.store.at(slots[g_campIndex]);
        g_message.setTarget(focus);
        g_message.setText(c.f("prompt.message_for", {{"name", d.name}, {"price", std::to_string(c.econ.messengerPrice)}}, d.female));
        g_message.show(true);
        g_messageNext.setTarget(focus);
        g_messageNext.show(slots.size() > 1);
        if (g_messageNext.fired()) g_campIndex = (g_campIndex + 1) % int(slots.size());
        if (g_message.fired() && takeCash(c.econ.messengerPrice * 100)) {
            g_messenger = focus;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_messenger, true, true);
            TASK::CLEAR_PED_TASKS(g_messenger, true, true);
            TASK::TASK_WANDER_STANDARD(g_messenger, 10.0f, 10);  // off with the message, in sight
            g_slot = slots[g_campIndex % int(slots.size())];
            g_stage = Stage::Message;
            g_stageAt = t;
            Vector3 camp;
            g_messageWaitMs = home::campKnown(camp)
                                  ? std::max(15000, int(flat(camp, pos(player())) * c.tunef("courier.road_factor", 1.25) / speed() * 1000.0f))
                                  : 15000;
            helpText(c.t("help.message_sent"));
        }
    } else {
        g_message.show(false);
        g_messageNext.show(false);
    }
    // (e) a long whistle hold (off by default)
    if (c.ini.getBool("DeliveryWhistleHold", false) && !companion().present() && !slots.empty()) {
        bool held = PAD::IS_CONTROL_PRESSED(0, MISC::GET_HASH_KEY("INPUT_WHISTLE"));
        g_whistleSince = held ? (g_whistleSince ? g_whistleSince : t) : 0;
        if (g_whistleSince && t - g_whistleSince > 2000) {
            g_whistleSince = 0;
            order(slots[g_campIndex % int(slots.size())], "whistle hold");  // the dog last picked
        }
    }
}

}  // namespace fd::delivery
