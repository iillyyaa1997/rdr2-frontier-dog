#include "dog/companion.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "dog/coat.h"
#include "core/strutil.h"
#include "dog/behaviour.h"
#include "dog/care.h"
#include "dog/hunting.h"
#include "dog/strays.h"
#include "game/game.h"
#include "mod/context.h"

namespace fd {

using namespace game;

Companion& companion() {
    static Companion c;
    return c;
}

static const char* kDecor = "FD_companion";
static constexpr float kFollowGap = 25.0f;   // farther than this an idle dog runs back on its own (old D8)
static constexpr int kStuckCheckMs = 6000;

DogRecord* Companion::record() { return slot_ >= 0 && ctx().store.used(slot_) ? &ctx().store.at(slot_) : nullptr; }

float Companion::whistleRange() const {
    const DogRecord* d = slot_ >= 0 && ctx().store.used(slot_) ? &ctx().store.at(slot_) : nullptr;
    auto ranges = ctx().tuning.getFloats("whistle.range");
    int lvl = d ? d->bondLevel : 1;
    double r = lvl - 1 < int(ranges.size()) ? ranges[lvl - 1] : 220.0;
    if (d && ctx().hasTrait(*d, "sharp_ears")) r *= ctx().tunef("trait.sharp_ears.range", 4.0 / 3.0);
    return float(r);
}


void Companion::save() {
    if (slot_ >= 0) ctx().store.save(slot_);
    dirty_ = false;
    saveAt_ = now() + 30000;
}

// ---------------------------------------------------------------- lifecycle

void Companion::start() {
    stayPrompt_.create("INPUT_CONTEXT_X", ctx().t("prompt.stay"));
    praisePrompt_.create("INPUT_INTERACT_OPTION1", ctx().t("prompt.praise"));
    homePrompt_.create("INPUT_INTERACT_OPTION2", ctx().t("prompt.home"), 1500);
    tamePrompt_.create("INPUT_INTERACT_OPTION1", ctx().t("prompt.tame"), 1000);
    revivePrompt_.create("INPUT_INTERACT_OPTION1", ctx().t("prompt.revive"), 1500, 2);
    // the dog's group shows 5 prompts at most (game::UiPrompt::applyFrame): what matters first
    revivePrompt_.setRank(5);
    stayPrompt_.setRank(20);
    praisePrompt_.setRank(30);
    tamePrompt_.setRank(30);
    homePrompt_.setRank(40);
    slot_ = ctx().store.withArthur();
    if (slot_ < 0) return;
    // after a script reload the old ped may still be around: take it over instead of spawning a second dog
    int peds[256];
    int n = worldGetAllPeds(peds, 256);
    for (int i = 0; i < n; ++i)
        if (alive(peds[i]) && DECORATOR::DECOR_GET_INT(peds[i], kDecor) == 1 && dist(peds[i], player()) < 300) {
            FD_INFO("taking over the dog left by a previous script session");
            setup(peds[i]);
            return;
        }
    spawn(true);
}

void Companion::spawn(bool outOfView, const Vector3* fixed) {
    DogRecord* d = record();
    if (!d) return;
    Vector3 at;
    Vector3 p = pos(player());
    if (fixed) at = *fixed;
    else if (!(outOfView && hiddenSpotNear(p, 30, 60, at)) && !safeCoord(add(p, Vector3(-3, -3, 0)), at)) at = add(p, Vector3(-3, -3, 0));
    std::string model = d->model.empty() ? "a_c_doghobo_01" : d->model;
    Ped dog = spawnPed(model, at, 0);
    if (!dog && model != "a_c_doghobo_01") dog = spawnPed("a_c_doghobo_01", at, 0);  // a model that always loads
    if (!dog) {
        FD_ERROR("could not create the dog (%s)", model.c_str());
        return;
    }
    int presets = PED::GET_NUM_META_PED_OUTFITS(dog);
    if (d->preset < 0 && presets > 0) {
        d->preset = ctx().rng.range(0, presets - 1);
        dirty_ = true;
    }
    coat::dress(dog, *d);
    setup(dog);
    lastArrival_ = now();
    FD_INFO("dog %s spawned %.0f m from Arthur (model %s, preset %d, scale %d)", d->name.c_str(), dist(dog, player()),
            model.c_str(), d->preset, d->scale);
}

static bool invincible() { return ctx().ini.getBool("Invincible", false); }

static int dogMaxHealth(const DogRecord& d) {
    return maxHealth(d, ctx().tuning.getFloats("health.max"), ctx().tuning.getFloats("health.strength"),
                     ctx().tunef("ailing.health_cut", 0.25));
}
// Invincible=1 (asked for on Nexus 22.11). SET_ENTITY_INVINCIBLE alone let APPLY_DAMAGE_TO_PED through (window 22.11:
// 300 -> 150 -> 0, the dog died), so the entity is also made undamageable and its health topped up every frame.
static void protect(Ped dog) {
    if (!invincible()) return;
    ENTITY::SET_ENTITY_INVINCIBLE(dog, true);
    ENTITY::SET_ENTITY_CAN_BE_DAMAGED(dog, false);
}

static int dogMaxHealth(const DogRecord& d);

// A wounded dog waiting for a reviver is untouchable (24.11: Мики went down in a shoot-out with lawmen and was dead 38 s
// later). Undamageable stops bullets (window 24.11: a wounded dog shot for 300 and 1000 kept 92 hp), but damage a
// script applies (APPLY_DAMAGE_TO_PED, what a damage/bleeding mod uses) still went through and 1000 killed it at once, so
// while it is down its health sits on a big buffer, topped up every frame; the real maximum comes back when it gets up.
static void guardWounded(Ped dog) {
    ENTITY::SET_ENTITY_INVINCIBLE(dog, true);
    ENTITY::SET_ENTITY_CAN_BE_DAMAGED(dog, false);
    int buffer = int(ctx().tunef("wounded.health_buffer", 20000));
    if (ENTITY::GET_ENTITY_MAX_HEALTH(dog, false) < buffer) ENTITY::SET_ENTITY_MAX_HEALTH(dog, buffer);
    if (ENTITY::GET_ENTITY_HEALTH(dog) < buffer) ENTITY::SET_ENTITY_HEALTH(dog, buffer, 0);
}

static void unguardWounded(Ped dog, const DogRecord* d) {
    ENTITY::SET_ENTITY_INVINCIBLE(dog, false);
    ENTITY::SET_ENTITY_CAN_BE_DAMAGED(dog, true);
    if (d) {
        int maxHp = dogMaxHealth(*d);
        ENTITY::SET_ENTITY_MAX_HEALTH(dog, maxHp);
        if (ENTITY::GET_ENTITY_HEALTH(dog) > maxHp) ENTITY::SET_ENTITY_HEALTH(dog, maxHp, 0);
    }
    protect(dog);
}

void Companion::setup(Ped dog) {
    DogRecord* d = record();
    ped_ = dog;
    Ped pl = player();
    int group = PED::GET_PED_GROUP_INDEX(pl);
    if (!PED::DOES_GROUP_EXIST(group)) {
        group = PED::CREATE_GROUP(0);
        PED::SET_PED_AS_GROUP_LEADER(pl, group, true);
    }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(dog, true, false);
    PED::SET_PED_AS_GROUP_MEMBER(dog, group);
    PED::SET_PED_CAN_BE_TARGETTED_BY_PLAYER(dog, PLAYER::PLAYER_ID(), false);
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(dog, PED::GET_PED_RELATIONSHIP_GROUP_HASH(pl));
    DECORATOR::DECOR_SET_INT(dog, kDecor, 1);
    PED::SET_PED_CONFIG_FLAG(dog, 154, true);             // follow the leader's threat response
    FLOCK::SET_ANIMAL_TUNING_BOOL_PARAM(dog, 44, false);  // no flee decision while alerted
    FLOCK::SET_ANIMAL_TUNING_BOOL_PARAM(dog, 32, true);   // never retreats from melee
    for (int p : {74, 80, 115, 117, 119}) FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(dog, p, 1.0f);  // fights instead of fleeing
    FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(dog, 165, 5.0f);
    PED::_SET_PED_SCENT(dog, 0);
    if (d) {
        int maxHp = dogMaxHealth(*d);
        ENTITY::SET_ENTITY_MAX_HEALTH(dog, maxHp);
        ENTITY::SET_ENTITY_HEALTH(dog, std::max(1, int(maxHp * std::clamp(d->healthFrac, 0.2, 1.0))), 0);
        if (d->scale > 0 && d->scale != 100) PED::_SET_PED_SCALE(dog, d->scale / 100.0f);
    }
    // collision proof as the companion always was; explosion + headshot proof only if the player wants it (old D81)
    ENTITY::SET_ENTITY_PROOFS(dog, ctx().ini.getBool("ProtectFromInstantDeath", false) ? (8 | 4 | 128) : 8, false);
    protect(dog);  // Invincible=1: nothing hurts it, so it never goes down wounded either

    if (MAP::DOES_BLIP_EXIST(blip_)) MAP::REMOVE_BLIP(&blip_);
    blip_ = MAP::BLIP_ADD_FOR_ENTITY(0x19365607, dog);  // BLIP_STYLE_COMPANION
    MAP::SET_BLIP_SPRITE(blip_, MISC::GET_HASH_KEY("blip_animal"), true);  // the paw (old D82)
    MAP::BLIP_ADD_MODIFIER(blip_, MISC::GET_HASH_KEY("BLIP_MODIFIER_RADAR_EDGE_ALWAYS"));
    MAP::BLIP_ADD_MODIFIER(blip_, 0x0B93E613);
    MAP::_SET_BLIP_NAME(blip_, literal(d && !d->name.empty() ? d->name : ctx().t("dog.default_name")));
    // the focus title shows the dog's name, not the model's species ("Siberian Husky" on the Shiba, 16.11)
    PED::_SET_PED_PROMPT_NAME(dog, literal(d && !d->name.empty() ? d->name : ctx().t("dog.default_name")));

    for (auto* p : {&stayPrompt_, &praisePrompt_, &homePrompt_, &revivePrompt_}) p->setTarget(dog);
    stay_ = false;
    lastPlayerPos_ = pos(pl);
    lastPosAt_ = now();
}

void Companion::releasePed(bool runOff) {
    if (MAP::DOES_BLIP_EXIST(blip_)) MAP::REMOVE_BLIP(&blip_);
    blip_ = 0;
    for (auto* p : {&stayPrompt_, &praisePrompt_, &homePrompt_, &revivePrompt_}) p->show(false);
    if (exists(ped_)) {
        DECORATOR::DECOR_SET_INT(ped_, kDecor, 0);
        PED::REMOVE_PED_FROM_GROUP(ped_);
        if (runOff && alive(ped_)) {
            // it trots away and is removed once out of sight (Companion::tick keeps no handle; the game cleans it up)
            TASK::CLEAR_PED_TASKS(ped_, true, true);
            TASK::TASK_WANDER_STANDARD(ped_, 10.0f, 10);
            ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&ped_);
        } else {
            deletePed(ped_);
        }
    }
    ped_ = 0;
}

void Companion::sendHome(bool runOff) {
    DogRecord* d = record();
    if (!d) return;
    hunting::giveUp();
    FD_INFO("dog %s goes home", d->name.c_str());
    d->where = Where::Home;
    if (alive(ped_)) d->healthFrac = double(ENTITY::GET_ENTITY_HEALTH(ped_)) / std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(ped_, false));
    save();
    releasePed(runOff);
    slot_ = -1;
}

void Companion::callFromHomeAt(int slot, const Vector3& at) {
    if (slot < 0 || !ctx().store.used(slot)) return;
    if (slot_ >= 0) sendHome(true);
    slot_ = slot;
    ctx().store.at(slot).where = Where::WithArthur;
    save();
    spawn(false, &at);
}

bool Companion::adopt(Ped ped, DogRecord d) {
    auto& c = ctx();
    if (slot_ < 0 && c.store.count() >= DogStore::kSlots) {
        helpText(c.t("help.kennel_full"));
        return false;
    }
    if (slot_ >= 0) sendHome(true);  // the dog that was with Arthur goes home (D98)
    if (c.store.count() >= DogStore::kSlots) {
        helpText(c.t("help.kennel_full"));
        return false;
    }
    d.where = Where::WithArthur;
    int s = c.store.put(d);
    if (s < 0) return false;
    slot_ = s;
    ENTITY::FREEZE_ENTITY_POSITION(ped, false);
    ENTITY::SET_ENTITY_INVINCIBLE(ped, false);
    coat::dress(ped, c.store.at(s));
    setup(ped);
    return true;
}

void Companion::takeFromCamp(int slot, Ped ped) {
    if (slot < 0 || !ctx().store.used(slot)) return;
    if (slot_ >= 0) sendHome(false);  // in camp: it becomes one of the home dogs there
    slot_ = slot;
    ctx().store.at(slot).where = Where::WithArthur;
    save();
    DECORATOR::DECOR_SET_INT(ped, "FD_shop", 0);
    ENTITY::SET_ENTITY_INVINCIBLE(ped, false);
    setup(ped);
    follow();
}

void Companion::releaseForSale() {
    DogRecord* d = record();
    if (!d) return;
    int s = slot_;
    releasePed(true);
    ctx().store.remove(s);
    slot_ = -1;
}

bool Companion::isShopOrHomeDog(Ped p) const { return p && DECORATOR::DECOR_GET_INT(p, "FD_shop") == 1; }

void Companion::callFromHome(int slot) {
    if (slot < 0 || !ctx().store.used(slot)) return;
    if (slot_ >= 0) sendHome(true);
    slot_ = slot;
    ctx().store.at(slot).where = Where::WithArthur;
    save();
    spawn(true);
}

// ---------------------------------------------------------------- per frame

void Companion::tick() {
    if (blipFxUntil_ && now() >= blipFxUntil_) {
        if (MAP::DOES_BLIP_EXIST(blip_) && blipFx_) MAP::BLIP_REMOVE_MODIFIER(blip_, blipFx_);
        blipFx_ = 0;
        blipFxUntil_ = 0;
    }
    updateStrays();
    updateAway();
    if (slot_ < 0) return;
    if (updateWounded()) return;
    if (!ped_ || !exists(ped_)) {
        // the game removed the ped (far away, a cutscene, a reload): bring it back out of view
        ped_ = 0;
        if (!onMission() && playerInControl() && now() - lastArrival_ > 5000) spawn(true);
        return;
    }
    if (ENTITY::IS_ENTITY_DEAD(ped_)) {
        died();
        return;
    }
    // a scaled-down dog has a shrunken collision capsule and falls through the ground: 0.6 every frame keeps it up
    // (window 0.11, 15.11: sinkings 14 -> 1 with 0.6)
    if (DogRecord* d = record(); d && d->scale > 0 && d->scale < 100) PED::SET_PED_CAPSULE(ped_, float(ctx().tunef("capsule.scaled", 0.6)));
    if (pendingFollowAt_ && now() >= pendingFollowAt_) {
        pendingFollowAt_ = 0;
        follow();
    }
    updateFollow();
    updatePrompts();
    updateBond();
    updateHealth();
    if (dirty_ && now() >= saveAt_) save();
}

bool Companion::aimedFromAfar() const {
    if (!ped_) return false;
    Player id = PLAYER::PLAYER_ID();
    float dd = dist(ped_, player());
    if (dd < 8.0f || dd > whistleRange()) return false;
    static const Hash kAim = MISC::GET_HASH_KEY("INPUT_AIM");
    if (!PLAYER::IS_PLAYER_FREE_AIMING(id) && !PAD::IS_CONTROL_PRESSED(0, kAim) && !PAD::IS_DISABLED_CONTROL_PRESSED(0, kAim))
        return false;
    Entity fa = 0;
    if (PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(id, &fa) && fa == ped_) return true;
    // the reticle near the dog: the camera's look direction within ~1.2 m of it at its distance
    Vector3 cam = CAMERA::GET_GAMEPLAY_CAM_COORD();
    Vector3 rot = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
    float pitch = rot.x * 0.0174533f, yaw = rot.z * 0.0174533f;
    float fx = -std::sin(yaw) * std::cos(pitch), fy = std::cos(yaw) * std::cos(pitch), fz = std::sin(pitch);
    Vector3 d = pos(ped_);
    float vx = d.x - cam.x, vy = d.y - cam.y, vz = d.z + 0.4f - cam.z;
    float len = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (len < 1.0f) return false;
    float cosang = (fx * vx + fy * vy + fz * vz) / len;
    float allowed = std::atan(1.5f / len) + 0.026f;  // ~1.5 m around the dog + 1.5 degrees
    if (cosang < std::cos(allowed)) return false;
    return ENTITY::HAS_ENTITY_CLEAR_LOS_TO_ENTITY(player(), ped_, 17);
}

void Companion::updatePrompts() {
    DogRecord* d = record();
    float dd = dist(ped_, player());
    bool close = dd < 4.0f && !PED::IS_PED_ON_MOUNT(player());
    // Stay / Follow and Send home while Arthur aims at the dog from any distance it hears him (16.11: from afar only
    // "Show info" was there); praise needs it close
    Entity focus = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
    // beyond the game's focus range the commands come from aiming at the dog and show as plain prompts bottom right
    // (the dog's own prompt group shows only while the game focuses on it: 18.11 his «подсказки на короткой дистанции»)
    bool afar = focus != ped_ && ctx().feature("FarCommands") && aimedFromAfar();
    for (auto* p : {&stayPrompt_, &homePrompt_}) {
        if (afar) p->setGroup(0);
        else p->setTarget(ped_);
    }
    bool command = close || ((focus == ped_ || afar) && dd < whistleRange());
    std::string stayText = ctx().t(stay_ ? "prompt.follow" : "prompt.stay", d && d->female);
    std::string homeText = ctx().t("prompt.home", d && d->female);
    if (afar && d && !d->name.empty()) {  // a plain prompt has no group title: the dog's name goes in front
        stayText = d->name + ": " + stayText;
        homeText = d->name + ": " + homeText;
    }
    stayPrompt_.setText(stayText);
    homePrompt_.setText(homeText);
    stayPrompt_.show(command);
    // Praise in words from afar and from the saddle; up close on foot the game's own Pet takes over (24.11 his rule: no
    // two buttons for the same thing in one place - «похвалить издалека, погладить вблизи»)
    if (afar) praisePrompt_.setGroup(0);
    else praisePrompt_.setTarget(ped_);
    std::string praiseText = ctx().t("prompt.praise", d && d->female);
    if (afar && d && !d->name.empty()) praiseText = d->name + ": " + praiseText;
    praisePrompt_.setText(praiseText);
    praisePrompt_.show(!close && (focus == ped_ || afar) && dd < whistleRange());
    homePrompt_.show(command && !onMission() && !behaviour::trackOffered());
    if (stayPrompt_.fired()) {
        if (d) addDeed(*d, ctx().breeds, "command", 1, ctx().rng);
        if (stay_) {
            follow();
            speech(player(), PED::IS_PED_MALE(ped_) ? "HORSE_FOLLOW_ME_MALE" : "HORSE_FOLLOW_ME_FEMALE");
        } else {
            stay();
            speech(player(), PED::IS_PED_MALE(ped_) ? "HORSE_STAY_HERE_MALE" : "HORSE_STAY_HERE_FEMALE");
        }
    }
    if (praisePrompt_.fired()) praise();
    if (homePrompt_.fired()) {
        helpText(ctx().f("help.went_home", {{"name", d ? d->name : ""}}, d && d->female));
        sendHome(true);
    }
}

void Companion::follow() {
    stay_ = false;
    Ped pl = player();
    FLOCK::SET_ANIMAL_TUNING_FLOAT_PARAM(ped_, 165, 5.0f);
    TASK::TASK_CLEAR_LOOK_AT(ped_);
    if (!PED::IS_PED_GROUP_MEMBER(ped_, PED::GET_PED_GROUP_INDEX(pl), false)) {
        PED::SET_PED_AS_GROUP_MEMBER(ped_, PED::GET_PED_GROUP_INDEX(pl));
        TASK::CLEAR_PED_TASKS(ped_, true, true);
        TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(ped_, pl, 0, 0, 0, 3.0f, 100000, 5.0f, false, false, false, false, false, false);
    } else {
        TASK::CLEAR_PED_TASKS(ped_, true, true);
    }
    FD_INFO("command: follow");
}

void Companion::stay() {
    stay_ = true;
    // out of Arthur's group while it stays: a group member walks back to the leader whatever its task (16.11: sat
    // 10 m away, was next to Arthur 15 s later); follow() puts it back
    if (PED::IS_PED_GROUP_MEMBER(ped_, PED::GET_PED_GROUP_INDEX(player()), false)) PED::REMOVE_PED_FROM_GROUP(ped_);
    TASK::CLEAR_PED_TASKS(ped_, true, true);
    TASK::TASK_START_SCENARIO_IN_PLACE_HASH(ped_, MISC::GET_HASH_KEY("WORLD_ANIMAL_DOG_SITTING"), -1, true, 0, -1.0f, false);
    FD_INFO("command: stay");
}

// Praise (24.11): it used to play the game's own patting, so it was the game's Pet twice over (+7 at once, Nexus 23.11
// «no difference between petting and praise»). Now Arthur says it - the first of praise.speech the game has for him
// (each line tried once, the one that plays is kept) - and the dog answers with a happy bark, turned to him. Worth more
// right after a deed.
void Companion::noteDeed(const char* what) {
    lastDeedAt_ = now();
    FD_DEBUG("deed: %s", what);
}

void Companion::praise() {
    if (!present()) return;
    static int s_line = -1;  // the speech line that worked
    auto lines = ctx().tuning.getList("praise.speech");
    const char* sex = PED::IS_PED_MALE(ped_) ? "_MALE" : "_FEMALE";
    bool said = false;
    if (s_line >= 0 && s_line < int(lines.size())) said = speech(player(), (lines[s_line] + sex).c_str());
    for (int i = 0; !said && i < int(lines.size()); ++i) {
        if (speech(player(), (lines[i] + sex).c_str())) {
            s_line = i;
            said = true;
            FD_INFO("praise: Arthur says %s%s", lines[i].c_str(), sex);
        }
    }
    if (!said && s_line < 0) {
        s_line = int(lines.size());  // none of them: quiet from now on, no retries every time
        FD_INFO("praise: none of the %d speech lines played - a quiet praise", int(lines.size()));
    }
    AUDIO::PLAY_ANIMAL_VOCALIZATION(ped_, "BARK_HAPPY", true);
    if (!stay_ && !PED::IS_PED_ON_MOUNT(player())) TASK::TASK_TURN_PED_TO_FACE_ENTITY(ped_, player(), 1500, 0, 0, 0);
    addBond(ctx().tunef("bond.praise", 3), "praise", ctx().tune("bond.praise_cooldown_ms", 120000));
    if (now() - lastDeedAt_ < ctx().tune("praise.deed_window_ms", 30000)) {
        addBond(ctx().tunef("bond.praise_deed", 2), "praise_deed", 0);
        lastDeedAt_ = -1000000;
        FD_INFO("praise right after a deed");
    }
}

// the paw answers a whistle the way the horse's blip does (19.11 his wish): the game's own horse modifiers, a pulse when
// the dog hears it, a flash when it is out of range; taken off after 3 s so the next whistle plays it again
void Companion::blipWhistle(bool inRange) {
    if (!MAP::DOES_BLIP_EXIST(blip_)) return;
    if (blipFx_) MAP::BLIP_REMOVE_MODIFIER(blip_, blipFx_);
    blipFx_ = MISC::GET_HASH_KEY(inRange ? "BLIP_MODIFIER_PLAYER_HORSE_IN_RANGE_WHISTLE" : "BLIP_MODIFIER_PLAYER_HORSE_OUT_OF_RANGE_WHISTLE");
    MAP::BLIP_ADD_MODIFIER(blip_, blipFx_);
    blipFxUntil_ = now() + 3000;
}

void Companion::whistle() {
    DogRecord* d = record();
    if (!d) return;
    float range = whistleRange();
    float dd = dist(ped_, player());
    blipWhistle(dd <= range);
    if (dd > range) {
        if (!onMission() && arriveOutOfView()) FD_INFO("whistle from beyond the range: the dog comes like the horse does");
        else helpText(ctx().f("help.out_of_range", {{"name", d->name}}, d->female));
        return;
    }
    if (INTERIOR::GET_INTERIOR_FROM_ENTITY(player()) && !ctx().ini.getBool("AllowEnteringInteriors", false)) return;
    // breed temper: the whistle delay by bonding level (Shiba 2000/1000/0 ms, old D47); Obedient comes at once
    int delay = 0;
    const BreedDef* b = ctx().breeds.find(d->breed);
    if (b) {
        auto delays = b->habits.getFloats("obey_delay_ms");
        if (d->bondLevel - 1 < int(delays.size())) delay = int(delays[d->bondLevel - 1]);
    }
    bool obedient = ctx().hasTrait(*d, "obedient");
    if (obedient) delay = 0;
    if (hunting::busy()) {
        if (!obedient) { FD_INFO("whistle: the dog brings the carcass first"); return; }
        hunting::giveUp();
    }
    if (delay > 0) {
        TASK::TASK_LOOK_AT_ENTITY(ped_, player(), delay, 0, 0, 0);
        pendingFollowAt_ = now() + delay;
        FD_INFO("whistle: the dog takes its time, %d ms", delay);
        return;
    }
    follow();
}

bool Companion::arriveOutOfView() {
    Vector3 spot;
    if (!hiddenSpotNear(pos(player()), 25, 60, spot)) {
        lastArrival_ = now();  // try again in 15 s, not every frame (16.11 log flood)
        FD_INFO("no spot out of view for the dog");
        return false;
    }
    ENTITY::SET_ENTITY_COORDS(ped_, spot.x, spot.y, spot.z, false, false, false, true);
    TASK::CLEAR_PED_TASKS(ped_, true, true);
    TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(ped_, player(), 0, 0, 0, 3.0f, 100000, 5.0f, false, false, false, false, false, false);
    lastArrival_ = now();
    stuckSince_ = 0;
    FD_INFO("dog arrives out of view, %.0f m", dist(spot, pos(player())));
    return true;
}

void Companion::updateFollow() {
    Ped pl = player();
    int t = now();
    Vector3 pp = pos(pl);
    bool jumped = lastPosAt_ && t - lastPosAt_ < 3000 && dist(pp, lastPlayerPos_) > 150.0f;  // fast travel, train, cutscene
    lastPlayerPos_ = pp;
    lastPosAt_ = t;
    if (jumped) jumpPending_ = true;  // kept until handled: the jump frame may fall while the dog is busy (window 16.11)
    float dd = dist(ped_, pl);
    float range = whistleRange();
    bool busy = PED::IS_PED_IN_COMBAT(ped_, 0) || TASK::IS_PED_IN_WRITHE(ped_) || behaviour::busy();
    bool inside = INTERIOR::GET_INTERIOR_FROM_ENTITY(pl) && !ctx().ini.getBool("AllowEnteringInteriors", false);
    // While Arthur rides and the dog is more than follow.boost_from_m behind, it sprints after him at once and runs
    // faster than a dog can: following as a group member alone is a trot, it fell behind even a trotting horse (22.11:
    // 3.8 m/s behind 4.2 m/s, 9 -> 24 m in 40 s) and ran only from kFollowGap (25 m) on. The rate override lasts one
    // frame, so it is set every frame it applies; the sprint task is given again only once it has ended (it ends at 4 m,
    // then the dog keeps to Arthur's side as a group member).
    bool mountedBehind = !stay_ && !busy && !inside && PED::IS_PED_ON_MOUNT(pl) && dd > float(ctx().tunef("follow.boost_from_m", 8));
    if (mountedBehind) {
        PED::SET_PED_MOVE_RATE_OVERRIDE(ped_, float(ctx().tunef("follow.mounted_rate", 1.5)));
        if (t >= nextSprint_) {
            nextSprint_ = t + 1500;
            static const Hash kFollowTask = MISC::GET_HASH_KEY("SCRIPT_TASK_FOLLOW_TO_OFFSET_OF_ENTITY");
            int st = TASK::GET_SCRIPT_TASK_STATUS(ped_, kFollowTask, true);
            if (st != 0 && st != 1) {  // 0 = about to start, 1 = running
                TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(ped_, pl, 0, 0, 0, 3.0f, 100000, 4.0f, false, false, false, false, false, false);
                if (!sprinting_) FD_INFO("dog %.0f m behind the horse: sprints after Arthur (task status was %d)", dd, st);
                sprinting_ = true;
            }
        }
    } else if (sprinting_ && dd <= float(ctx().tunef("follow.boost_from_m", 8))) {
        sprinting_ = false;
        FD_DEBUG("dog caught up with the horse (%.0f m)", dd);
    }
    if (stay_ || busy || inside) {
        stuckSince_ = 0;
        return;
    }
    // riding away from a dog that is stuck or cannot keep up (22.11: it stood at a rock 60-73 m back at the gallop): it
    // comes after Arthur out of his view, like after a fast travel
    bool mountedLost = PED::IS_PED_ON_MOUNT(pl) && dd > float(ctx().tunef("follow.mounted_arrive_m", 60)) &&
                       lastFollowDist_ > 0 && dd > lastFollowDist_ - 2.0f;
    if (((jumpPending_ && dd > 25.0f) || dd > range * 2 || mountedLost) && !onMission() && t - lastArrival_ > 15000) {
        if (arriveOutOfView()) { jumpPending_ = false; return; }
    }
    if (dd <= kFollowGap) {
        jumpPending_ = false;
        stuckSince_ = 0;
        return;
    }
    // left behind or stuck: run to Arthur again (re-issued at most every 6 s while not closing in)
    if (t >= nextFollowCheck_) {
        nextFollowCheck_ = t + kStuckCheckMs;
        if (!stuckSince_ || dd > lastFollowDist_ - 2.0f) {
            TASK::CLEAR_PED_TASKS(ped_, true, true);
            TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(ped_, pl, 0, 0, 0, 3.0f, 100000, 5.0f, false, false, false, false, false, false);
            FD_INFO("dog left behind at %.0f m: running to Arthur", dd);
        }
        stuckSince_ = t;
        lastFollowDist_ = dd;
    }
}

// ---------------------------------------------------------------- bonding (old D78)

void Companion::addBond(double points, const std::string& reason, int cooldownMs) {
    DogRecord* d = record();
    if (!d || points <= 0) return;
    int t = now();
    auto it = cooldownUntil_.find(reason);
    if (it != cooldownUntil_.end() && t < it->second) return;
    cooldownUntil_[reason] = t + cooldownMs;
    if (ctx().hasTrait(*d, "affectionate")) points *= ctx().tunef("trait.affectionate.bond", 4.0 / 3.0);
    d->bondPoints += points;
    dirty_ = true;
    double need2 = ctx().tunef("bond.points.2", 100), need3 = ctx().tunef("bond.points.3", 400);
    int lvl = d->bondPoints >= need3 ? 3 : (d->bondPoints >= need2 ? 2 : 1);
    FD_DEBUG("bond +%.1f (%s) = %.1f", points, reason.c_str(), d->bondPoints);
    if (lvl > d->bondLevel) {
        d->bondLevel = lvl;
        FD_INFO("bonding level %d for %s", lvl, d->name.c_str());
        std::string grew;
        for (int i : applyBondGrowth(*d)) {
            grew += (grew.empty() ? "" : ", ") + ctx().t("stat." + std::to_string(i + 1)) + " +1";
            FD_INFO("bond: stat %d +1 (now %d)", i + 1, d->stats[i]);
        }
        if (alive(ped_)) ENTITY::SET_ENTITY_MAX_HEALTH(ped_, dogMaxHealth(*d));
        std::string note = ctx().f("help.bond_level", {{"name", d->name}, {"level", std::to_string(lvl)}}, d->female);
        if (!grew.empty()) note += " · " + grew;
        helpText(note);
        save();
    }
}

void Companion::updateBond() {
    int t = now();
    if (t < nextBondTick_) return;
    nextBondTick_ = t + ctx().tune("bond.together_ms", 120000);
    if (!stay_ && dist(ped_, player()) < whistleRange() && !TASK::IS_PED_IN_WRITHE(ped_))
        addBond(ctx().tunef("bond.together", 1), "together", 0);
}

void Companion::updateHealth() {
    DogRecord* d = record();
    if (!d) return;
    int maxHp = std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(ped_, false));
    if (invincible() && alive(ped_) && ENTITY::GET_ENTITY_HEALTH(ped_) < maxHp) {
        static int loggedAt = 0;  // damage that got through is worth knowing about, once a second at most
        if (now() - loggedAt > 1000) {
            loggedAt = now();
            FD_INFO("invincible dog took damage anyway: health %d of %d, topped up", ENTITY::GET_ENTITY_HEALTH(ped_), maxHp);
        }
        ENTITY::SET_ENTITY_HEALTH(ped_, maxHp, 0);
    }
    double frac = double(ENTITY::GET_ENTITY_HEALTH(ped_)) / maxHp;
    if (std::fabs(frac - d->healthFrac) > 0.02) {
        d->healthFrac = frac;
        dirty_ = true;
    }
}

// ---------------------------------------------------------------- wounded (old D26–D28, D79, D81)
// Ordinary hits put the dog down wounded: it waits 5 / 15 / 30 real minutes by bonding (Hardy x1.5) for a Horse Reviver
// (or our Dog Reviver, D133). Not saved in time -> it loses a grade (D107). An explosion or a headshot can kill outright.

void Companion::startWounded() {
    DogRecord* d = record();
    if (!d) return;
    auto waits = ctx().tuning.getFloats("wounded.wait_min");
    double minutes = d->bondLevel - 1 < int(waits.size()) ? waits[d->bondLevel - 1] : 5.0;
    if (ctx().hasTrait(*d, "hardy")) minutes *= ctx().tunef("trait.hardy.wait", 1.5);
    Vector3 at = pos(ped_);
    d->woundedUntil = unixNow() + (long long)(minutes * 60);
    d->woundedX = at.x; d->woundedY = at.y; d->woundedZ = at.z;
    save();
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped_, true, true);
    // window 16.11: a dog wounded in a fight was dead one second later (the attacker finished it or a death already in
    // progress): keep a health floor and turn its attackers on Arthur
    guardWounded(ped_);
    int peds[256];
    int n = worldGetAllPeds(peds, 256);
    for (int i = 0; i < n; ++i) {  // whoever fights the dog turns on Arthur instead of finishing it
        Ped p = peds[i];
        if (p == ped_ || p == player() || !alive(p) || dist(p, ped_) > 50.0f || !PED::IS_PED_IN_COMBAT(p, ped_)) continue;
        TASK::CLEAR_PED_TASKS(p, true, true);
        TASK::TASK_COMBAT_PED(p, player(), 0, 16);
    }
    if (TASK::IS_PED_IN_WRITHE(ped_)) PED::SET_PAUSE_PED_WRITHE_BLEEDOUT(ped_, true);
    else if (!PED::IS_PED_INCAPACITATED(ped_)) {
        TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped_, true, true);
        TASK::TASK_ANIMAL_WRITHE(ped_, 0, 0);
    }
    helpText(ctx().f("help.wounded", {{"name", d->name}, {"min", std::to_string(int(minutes + 0.5))}}, d->female), 10000);
    FD_INFO("wounded: waits %.0f min", minutes);
}

void Companion::endWounded(bool revived) {
    DogRecord* d = record();
    if (exists(ped_)) unguardWounded(ped_, d);
    woundedGraceUntil_ = now() + 8000;  // the writhe flag lingers a few seconds after a revive
    if (d) {
        d->woundedUntil = 0;
        if (revived) d->limp = true;  // limps until healed (care)
        save();
    }
    if (MAP::DOES_BLIP_EXIST(woundedBlip_)) MAP::REMOVE_BLIP(&woundedBlip_);
    woundedBlip_ = 0;
    revivePrompt_.show(false);
    FD_INFO(revived ? "wounded dog revived" : "wounded dog was not helped in time");
}

bool Companion::updateWounded() {
    DogRecord* d = record();
    if (!d) return false;
    bool there = exists(ped_), living = there && !ENTITY::IS_ENTITY_DEAD(ped_);
    bool down = living && (TASK::IS_PED_IN_WRITHE(ped_) || PED::IS_PED_INCAPACITATED(ped_));
    if (d->woundedUntil <= 0) {
        if (!living) return false;
        double pct = ctx().tunef("wounded.health_pct", 12);
        bool low = ENTITY::GET_ENTITY_HEALTH(ped_) * 100.0 <= pct * ENTITY::GET_ENTITY_MAX_HEALTH(ped_, false);
        if ((down || low) && now() >= woundedGraceUntil_) startWounded();
        return false;
    }
    if (living) {
        guardWounded(ped_);  // every frame: a lawman's next bullet must not finish it
        // the revive prompt: a Dog Reviver (if the item exists) or the game's Horse Reviver
        const char* item = itemCount("CONSUMABLE_DOG_REVIVER") > 0 ? "CONSUMABLE_DOG_REVIVER"
                         : (itemCount("CONSUMABLE_HORSE_REVIVER") > 0 ? "CONSUMABLE_HORSE_REVIVER" : nullptr);
        bool close = dist(ped_, player()) < 2.5f;
        revivePrompt_.setText(item ? ctx().t("prompt.revive") : ctx().t("prompt.need_reviver"));
        revivePrompt_.show(dist(ped_, player()) < 6.0f, item && close);
        for (auto* p : {&stayPrompt_, &praisePrompt_, &homePrompt_}) p->show(false);
        if (item && revivePrompt_.fired() && takeItem(item)) {
            PED::_INCAPACITATED_REVIVE(ped_, ped_);
            TASK::CLEAR_PED_TASKS(ped_, true, true);
            endWounded(true);  // the real maximum first, then half of it
            ENTITY::SET_ENTITY_HEALTH(ped_, ENTITY::GET_ENTITY_MAX_HEALTH(ped_, false) / 2, 0);
            helpText(ctx().f("help.revived", {{"name", d->name}}, d->female));
            follow();
            return false;
        }
    }
    if (now() < nextWoundedCheck_) return true;
    nextWoundedCheck_ = now() + 1000;
    if (unixNow() >= d->woundedUntil) {
        endWounded(false);
        died();  // not saved in time: one grade lower (or gone, if it was ailing)
        return true;
    }
    if (living) {
        if (TASK::IS_PED_IN_WRITHE(ped_)) PED::SET_PAUSE_PED_WRITHE_BLEEDOUT(ped_, true);
        return true;  // a wounded dog gets no follow orders or prompts of the healthy dog
    }
    if (there) return false;  // dead outright: the normal death path
    // the ped is gone (Arthur died far away, a reload): the dog still lies where it fell
    Vector3 at(d->woundedX, d->woundedY, d->woundedZ);
    if (!woundedBlip_) {
        woundedBlip_ = MAP::BLIP_ADD_FOR_COORDS(0x19365607, at.x, at.y, at.z);
        MAP::SET_BLIP_SPRITE(woundedBlip_, MISC::GET_HASH_KEY("blip_animal"), true);
        MAP::BLIP_ADD_MODIFIER(woundedBlip_, MISC::GET_HASH_KEY("BLIP_MODIFIER_RADAR_EDGE_ALWAYS"));
        MAP::_SET_BLIP_NAME(woundedBlip_, literal(d->name));
    }
    if (dist(pos(player()), at) < 80.0f) {
        Ped dog = spawnPed(d->model.empty() ? "a_c_doghobo_01" : d->model, at, 0);
        if (dog) {
            coat::dress(dog, *d);
            setup(dog);
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(dog, true, true);
            guardWounded(dog);
            TASK::TASK_ANIMAL_WRITHE(dog, 0, 0);
            if (MAP::DOES_BLIP_EXIST(woundedBlip_)) MAP::REMOVE_BLIP(&woundedBlip_);
            woundedBlip_ = 0;
            FD_INFO("the wounded dog is back where it fell");
        }
    }
    return true;
}

// ---------------------------------------------------------------- dogs away (nursed, training, at the vet)

void Companion::updateAway() {
    if (now() < nextAwayCheck_) return;
    nextAwayCheck_ = now() + 10000;
    long long m = gameMinutes();
    for (int i = 0; i < DogStore::kSlots; ++i) {
        if (!ctx().store.used(i)) continue;
        DogRecord& d = ctx().store.at(i);
        if (d.where != Where::Away || m < d.awayUntil || d.awayReason == "courier") continue;  // the courier: delivery module
        std::string reason = d.awayReason;
        if (reason.rfind("training:", 0) == 0) {
            std::string sk = reason.substr(9);
            d.skillLevel[sk] = std::min(3, d.skill(sk) + 1);
            d.skillUses[sk] = 0;
        } else if (reason == "cure") {
            cure(d, ctx().breeds, ctx().rng, ctx().chances);
        } else if (reason == "pedigree") {
            makePedigree(d, ctx().breeds);
        }
        d.where = Where::Home;  // picked up at the stable comes with the stable module; until then it walks home
        d.awayReason.clear();
        d.awayUntil = 0;
        ctx().store.save(i);
        FD_INFO("dog %s is back home (%s)", d.name.c_str(), reason.c_str());
        helpText(ctx().f("help.back_home." + (reason.empty() ? std::string("away") : reason.substr(0, reason.find(':'))),
                         {{"name", d.name}, {"grade", ctx().gradeText(d)}}, d.female), 7000);
    }
}

// ---------------------------------------------------------------- death -> grade down (D107–D113)

void Companion::died() {
    DogRecord* d = record();
    if (!d) return;
    std::string name = d->name;
    bool female = d->female;
    Grade before = d->grade;
    if (exists(ped_)) {  // what killed it (24.11 Мики: no clue in the log)
        Entity by = PED::GET_PED_SOURCE_OF_DEATH(ped_);
        FD_INFO("dog death: cause 0x%08X, by %d (model 0x%08X), wounded %d, damageable %d", PED::GET_PED_CAUSE_OF_DEATH(ped_), by,
                by && ENTITY::DOES_ENTITY_EXIST(by) ? ENTITY::GET_ENTITY_MODEL(by) : 0, int(d->woundedUntil > 0),
                int(ENTITY::_GET_ENTITY_CAN_BE_DAMAGED(ped_)));
    }
    d->woundedUntil = 0;
    if (MAP::DOES_BLIP_EXIST(woundedBlip_)) MAP::REMOVE_BLIP(&woundedBlip_);
    woundedBlip_ = 0;
    revivePrompt_.show(false);
    bool lives = gradeDown(*d, ctx().breeds, ctx().rng);
    FD_INFO("dog %s died: grade %d -> %s", name.c_str(), int(before), lives ? "one grade lower, nursed in camp" : "gone for good");
    releasePed(false);
    if (!lives) {
        helpText(ctx().f("help.died_for_good", {{"name", name}}, female), 8000);
        ctx().store.remove(slot_);
        slot_ = -1;
        return;
    }
    d->where = Where::Away;
    d->awayReason = "nursed";
    d->awayStable.clear();
    d->awayUntil = gameMinutes() + 1440;
    d->healthFrac = 1.0;
    helpText(ctx().f("help.nursed", {{"name", name}, {"grade", ctx().gradeText(*d)}}, female), 8000);
    save();
    slot_ = -1;
}

// ---------------------------------------------------------------- events

void Companion::onEvent(Hash type, int index) {
    static const Hash kWhistle = MISC::GET_HASH_KEY("EVENT_PED_WHISTLE");
    static const Hash kInteraction = MISC::GET_HASH_KEY("EVENT_PED_ANIMAL_INTERACTION");
    static const Hash kPatting = MISC::GET_HASH_KEY("Interaction_Dog_Patting");
    struct Data { alignas(8) int a; alignas(8) int b; alignas(8) int c; alignas(8) int d; alignas(8) int e; } ev{};
    if (type == kWhistle) {
        if (SCRIPT::GET_EVENT_DATA(0, index, (Any*)&ev, 2) && ev.a == player() && present()) whistle();
    } else if (type == kInteraction) {
        if (!SCRIPT::GET_EVENT_DATA(0, index, (Any*)&ev, 3) || ev.a != player() || ev.c != int(kPatting)) return;
        if (present() && ev.b == ped_) {
            addBond(ctx().tunef("bond.pat", 4), "pat", ctx().tune("bond.pat_cooldown_ms", 120000));
        } else if (alive(ev.b) && ev.b != ped_ && !isShopOrHomeDog(ev.b) && ctx().breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(ev.b))) {
            tameCandidate_ = ev.b;  // patted a stray: offer to take it along (old flow, own code)
            tameCandidateUntil_ = now() + 30000;
            tamePrompt_.setTarget(ev.b);
        }
    }
}

// ---------------------------------------------------------------- strays

void Companion::updateStrays() {
    if (!tameCandidate_) return;
    if (now() > tameCandidateUntil_ || !alive(tameCandidate_) || dist(tameCandidate_, player()) > 6.0f) {
        tamePrompt_.show(false);
        tameCandidate_ = 0;
        return;
    }
    tamePrompt_.show(true);
    if (tamePrompt_.fired()) tame(tameCandidate_);
}

void Companion::tame(Ped stray) {
    tamePrompt_.show(false);
    tameCandidate_ = 0;
    auto& c = ctx();
    if (c.store.count() >= DogStore::kSlots && slot_ < 0) {
        helpText(c.t("help.kennel_full"));
        return;
    }
    const BreedDef* b = c.breeds.byAnimalType(ENTITY::_GET_PED_ANIMAL_TYPE(stray));
    if (!b) b = c.breeds.find("mutt");
    if (strays::wary(stray)) {
        helpText(c.t("help.stray_wary"));
        return;
    }
    DogRecord d = strays::cardFor(stray);
    strays::forget(stray);
    // the stray keeps the look it has (its own model); the name is the standard one (old D73)
    for (const auto& m : b->models)
        if (MISC::GET_HASH_KEY(m.c_str()) == ENTITY::GET_ENTITY_MODEL(stray)) d.model = m;
    d.female = !PED::IS_PED_MALE(stray);
    d.name = c.t("dog.default_name");
    d.where = Where::WithArthur;
    if (!adopt(stray, d)) return;
    FD_INFO("tamed a stray %s: grade %d, stats %d", b->id.c_str(), int(d.grade), d.statSum());
    helpText(c.f("help.tamed", {{"grade", c.gradeText(d)}, {"breed", c.breedText(d)}}, d.female), 7000);
}

}  // namespace fd
