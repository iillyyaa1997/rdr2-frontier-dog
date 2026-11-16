#include "dog/delivery.h"

#include <cmath>

#include "core/fs.h"
#include "core/log.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "dog/home.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"

namespace fd::delivery {

using namespace game;

enum class Stage { Idle, Message, Riding, Handover, Leaving };
static Stage g_stage = Stage::Idle;
static int g_slot = -1, g_stageAt = 0, g_orderedAt = 0, g_progressAt = 0;
static float g_lastDist = 0;
static Ped g_rider = 0, g_horse = 0, g_dog = 0, g_messenger = 0;
static game::UiPrompt g_campNext, g_campOrder, g_message;
static int g_campIndex = 0, g_whistleSince = 0;
static std::vector<Vector3> g_telegraphs;

bool active() { return g_stage != Stage::Idle; }

void start() {
    g_campNext.create("INPUT_CONTEXT_X", ctx().t("stable.next"));
    g_campOrder.create("INPUT_INTERACT_OPTION1", "", 1200);
    g_message.create("INPUT_INTERACT_OPTION1", ctx().t("prompt.message"), 1000, 2);
    KvFile tel;
    if (tel.load(fs::join(ctx().dataDir, "Telegraph.dat")))
        for (const auto& k : tel.getList("telegraphs")) {
            auto v = tel.getFloats("telegraph." + k);
            if (v.size() == 2) g_telegraphs.push_back(Vector3(float(v[0]), float(v[1]), 0));
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

static void cleanup() {
    for (Ped* p : {&g_rider, &g_horse}) {
        if (exists(*p)) ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(p);
        *p = 0;
    }
    g_stage = Stage::Idle;
    g_slot = -1;
}

static bool spawnCourier() {
    const DogRecord& d = ctx().store.at(g_slot);
    Vector3 pp = pos(player()), from, road;
    Vector3 camp;
    // start on a road out of sight, on Arthur's side towards the camp when it is known
    Vector3 dir(0, 1, 0);
    if (home::campKnown(camp) && dist(camp, pp) > 10) {
        float l = dist(camp, pp);
        dir = Vector3((camp.x - pp.x) / l, (camp.y - pp.y) / l, 0);
    }
    bool ok = false;
    // window 16.11: a rider started 220-264 m away never moved (speed 0 for a minute) - far peds are not simulated
    for (float r : {140.0f, 120.0f, 160.0f, 100.0f}) {
        Vector3 want = add(pp, scale(dir, r));
        if (PATH::GET_CLOSEST_VEHICLE_NODE(want.x, want.y, want.z, &road, 0, 3.0f, 0.0f) && dist(road, pp) > 90.0f &&
            !CAMERA::IS_SPHERE_VISIBLE(road.x, road.y, road.z + 1.0f, 2.0f)) {
            from = road;
            ok = true;
            break;
        }
    }
    if (!ok && !hiddenSpotNear(pp, 90, 150, from)) return false;
    g_horse = spawnPed(ctx().ini.get("CourierHorse", "A_C_Horse_KentuckySaddle_Black"), from, 0, 3000);
    if (!g_horse) g_horse = spawnPed("A_C_Horse_Morgan_Bay", from, 0, 3000);
    g_rider = spawnPed(ctx().ini.get("CourierModel", "A_M_M_RANCHER_01"), add(from, Vector3(1, 0, 0)), 0, 3000);
    g_dog = spawnPed(d.model.empty() ? "a_c_doghobo_01" : d.model, add(from, Vector3(-1.5f, -1.5f, 0)), 0, 3000);
    if (!g_horse || !g_rider || !g_dog) {
        deletePed(g_horse); deletePed(g_rider); deletePed(g_dog);
        return false;
    }
    for (Ped p : {g_horse, g_rider, g_dog}) ENTITY::SET_ENTITY_AS_MISSION_ENTITY(p, true, true);
    PED::SET_PED_ONTO_MOUNT(g_rider, g_horse, -1, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_rider, true);
    ENTITY::SET_ENTITY_INVINCIBLE(g_dog, true);
    DECORATOR::DECOR_SET_INT(g_dog, "FD_shop", 1);
    if (d.preset >= 0) PED::_EQUIP_META_PED_OUTFIT_PRESET(g_dog, d.preset, false);
    if (d.scale > 0 && d.scale != 100) PED::_SET_PED_SCALE(g_dog, d.scale / 100.0f);
    TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(g_dog, g_horse, 1.5f, -1.0f, 0, 3.0f, -1, 2.0f, true, false, false, false, false, false);
    FD_INFO("delivery: rider with %s starts %.0f m away", d.name.c_str(), dist(from, pp));
    return true;
}

bool order(int slot, const char* how) {
    auto& c = ctx();
    if (active() || slot < 0 || !c.store.used(slot)) return false;
    if (onMission() || PLAYER::GET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID()) > 0) {
        helpText(c.t("help.courier_refuses"));
        return false;
    }
    Vector3 camp;
    float km = home::campKnown(camp) ? dist(camp, pos(player())) : 3000.0f;
    int fee = deliveryFee(km, c.econ);
    if (!takeCash(fee * 100)) {
        helpText(c.t("help.no_money"));
        return false;
    }
    g_slot = slot;
    g_stage = Stage::Riding;
    g_stageAt = g_orderedAt = g_progressAt = now();
    g_lastDist = 1e9f;
    if (!spawnCourier()) {
        giveCash(fee * 100);
        helpText(c.t("help.courier_no_way"));
        cleanup();
        return false;
    }
    const DogRecord& d = c.store.at(slot);
    helpText(c.f("help.courier_coming", {{"name", d.name}, {"fee", std::to_string(fee)}}, d.female), 7000);
    FD_INFO("delivery ordered (%s), fee $%d", how, fee);
    return true;
}

static void riding() {
    int t = now();
    if (!alive(g_rider) || !alive(g_horse) || !exists(g_dog)) {
        FD_WARN("delivery: the courier or the dog is gone");
        if (exists(g_dog)) deletePed(g_dog);
        cleanup();
        return;
    }
    float d = dist(g_rider, player());
    if (d < 9.0f || (!PED::IS_PED_ON_MOUNT(player()) && d < 12.0f)) {
        TASK::TASK_STAND_STILL(g_rider, 4000);
        g_stage = Stage::Handover;
        g_stageAt = t;
        return;
    }
    if (t - g_orderedAt > 5000) {
        g_orderedAt = t;
        TASK::TASK_GO_TO_ENTITY(g_rider, player(), -1, 7.0f, d > 60 ? 3.0f : 1.5f, 0, 0);
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
        cleanup();
    }
}

static void handover() {
    if (now() - g_stageAt < 2500) return;
    if (g_slot >= 0 && ctx().store.used(g_slot) && exists(g_dog)) {
        ENTITY::SET_ENTITY_INVINCIBLE(g_dog, false);
        companion().takeFromCamp(g_slot, g_dog);
        const DogRecord& d = ctx().store.at(g_slot);
        helpText(ctx().f("help.courier_done", {{"name", d.name}}, d.female));
    }
    g_dog = 0;
    if (alive(g_rider)) TASK::TASK_WANDER_STANDARD(g_rider, 10.0f, 10);
    g_stage = Stage::Leaving;
    g_stageAt = now();
}

void tick() {
    if (!ctx().feature("Delivery")) return;
    auto& c = ctx();
    int t = now();
    switch (g_stage) {
        case Stage::Riding: riding(); return;
        case Stage::Handover: handover(); return;
        case Stage::Leaving:
            if (t - g_stageAt > 20000 && (!exists(g_rider) || !ENTITY::IS_ENTITY_ON_SCREEN(g_rider) || dist(g_rider, player()) > 120))
                cleanup();
            return;
        case Stage::Message:
            if (t - g_stageAt > 15000) {
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
                   !INTERIOR::GET_INTERIOR_FROM_ENTITY(player()) && !home::inCamp() && !inTown(pos(player()));
    bool telegraph = PED::IS_PED_ON_FOOT(player()) && atTelegraph();  // (b) a telegram from the post office
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
    bool canAsk = focus && alive(focus) && PED::IS_PED_HUMAN(focus) && !slots.empty() && dist(focus, player()) < 4.0f &&
                  PED::GET_PED_RELATIONSHIP_GROUP_HASH(focus) != MISC::GET_HASH_KEY("REL_COP") &&
                  PED::GET_PED_RELATIONSHIP_GROUP_HASH(focus) != MISC::GET_HASH_KEY("REL_GANG_DUTCHS") &&
                  !PED::IS_PED_IN_COMBAT(focus, 0);
    if (canAsk) {
        g_message.setTarget(focus);
        g_message.setText(c.f("prompt.message", {{"price", std::to_string(c.econ.messengerPrice)}}));
        g_message.show(true);
        if (g_message.fired() && takeCash(c.econ.messengerPrice * 100)) {
            g_messenger = focus;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_messenger, true, true);
            TASK::CLEAR_PED_TASKS(g_messenger, true, true);
            TASK::TASK_WANDER_STANDARD(g_messenger, 10.0f, 10);  // off with the message, in sight
            g_slot = slots[0];
            g_stage = Stage::Message;
            g_stageAt = t;
            helpText(c.t("help.message_sent"));
        }
    } else {
        g_message.show(false);
    }
    // (e) a long whistle hold (off by default)
    if (c.ini.getBool("DeliveryWhistleHold", false) && !companion().present() && !slots.empty()) {
        bool held = PAD::IS_CONTROL_PRESSED(0, MISC::GET_HASH_KEY("INPUT_WHISTLE"));
        g_whistleSince = held ? (g_whistleSince ? g_whistleSince : t) : 0;
        if (g_whistleSince && t - g_whistleSince > 2000) {
            g_whistleSince = 0;
            order(slots[0], "whistle hold");
        }
    }
}

}  // namespace fd::delivery
