#include "dog/carry.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/log.h"
#include "game/game.h"
#include "mod/context.h"

namespace fd {

using namespace game;

static const char* kBack = "creatures_mammal@dog_pers@agitated@walk@one_step@back";
static const char* kBackTired = "creatures_mammal@dog_pers@agitated@walk@one_step@tired@back";
static constexpr int kJaw = 58559;   // SKEL_Jaw of the game's dogs (bones scan 15.11)
static constexpr int kHead = 21030;  // skel_head of the game's animals
static constexpr float kRope = 0.3f;
static constexpr int kCarriedByHuman = 5, kBeingPlacedOnGround = 6;

CarrySize CarryTask::sizeOf(Ped carcass, float* length) {
    Vector3 mn, mx;
    MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(carcass), &mn, &mx);
    float l = mx.y - mn.y;
    if (length) *length = l;
    float smallMax = float(ctx().tunef("carry.small_max", 0.8)), mediumMax = float(ctx().tunef("carry.medium_max", 1.6));
    if (l <= smallMax || ENTITY::_GET_IS_BIRD(carcass)) return CarrySize::Small;
    return l <= mediumMax ? CarrySize::Medium : CarrySize::Large;
}

void CarryTask::start(Ped dog, const CarryJob& job) {
    if (active()) release();
    dog_ = dog;
    job_ = job;
    stage_ = Stage::GoTo;
    stageAt_ = now();
    orderedAt_ = progressAt_ = restUntil_ = effortAt_ = stepAt_ = steps_ = stuckTries_ = 0;
    effort_ = 0;
    pullBoost_ = 1.0f;
    groundCheckAt_ = 0;
    standHeight_ = -1;
    rope_ = 0;
    attached_ = false;
    lastDelivered_ = false;
    walkForward_ = !(ctx().ini.getBool("CarryBackwardDrag", true) && job.size == CarrySize::Large);
    straight_ = false;
    TASK::CLEAR_PED_TASKS(dog_, true, true);
    FD_INFO("carry: start, size %d, %.2f m, pull %.0f, loses quality %d, stamina %.0f s", int(job.size), job.length, job.pull,
            int(job.losesQuality), job.staminaSeconds);
}

Vector3 CarryTask::grabPoint() const {
    if (job_.size != CarrySize::Small && !job_.pelt) {
        int head = PED::GET_PED_BONE_INDEX(job_.carcass, kHead);
        if (head >= 0) return ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(job_.carcass, head);
    }
    return pos(job_.carcass);
}

Vector3 CarryTask::jawPoint() const {
    int jaw = PED::GET_PED_BONE_INDEX(dog_, kJaw);
    return jaw >= 0 ? ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(dog_, jaw) : pos(dog_);
}

Ped CarryTask::dest() {
    if (job_.horse && (!alive(job_.horse) || !PED::IS_PED_ON_FOOT(player()) || dist(job_.horse, player()) > 90.0f)) job_.horse = 0;
    return job_.horse ? job_.horse : player();
}

void CarryTask::update() {
    if (stowCheckAt_ && now() >= stowCheckAt_) checkStow();
    if (!active()) return;
    int t = now();
    if (!exists(job_.carcass) || !alive(dog_)) {
        fail("the carcass or the dog is gone");
        return;
    }
    switch (stage_) {
        case Stage::GoTo: {
            keepOnGround(t);
            int st = ENTITY::GET_CARRIABLE_ENTITY_STATE(job_.carcass);
            if (job_.pelt && (st == kCarriedByHuman || st == kBeingPlacedOnGround)) {
                if (t - stageAt_ > 10000) fail("Arthur kept the pelt");
                return;
            }
            if (PED::_GET_CARRIER_AS_PED(job_.carcass) || PED::_GET_CARRIER_AS_MOUNT(job_.carcass)) {
                fail("someone else has it");
                return;
            }
            Vector3 g = grabPoint();
            if (dist(jawPoint(), g) < 0.9f || dist(pos(dog_), g) < 1.0f) {
                TASK::CLEAR_PED_TASKS(dog_, true, true);
                stage_ = Stage::Grab;
                stageAt_ = t;
                progressAt_ = 0;
                straight_ = false;
                FD_INFO("carry: takes it");
                return;
            }
            if (t - orderedAt_ > (job_.inWater ? 1500 : 4000)) {
                float gap = dist(pos(dog_), g);
                // no progress for 8 s on the navmesh: go straight at it (window 16.11: the dog never reached a rabbit)
                bool straightNow = job_.inWater || straight_ || (progressAt_ && t - progressAt_ > 8000 && lastGap_ - gap < 1.0f);
                if (!progressAt_ || lastGap_ - gap >= 1.0f) { progressAt_ = t; lastGap_ = gap; }
                if (straightNow && !straight_) { straight_ = true; FD_INFO("carry: no way on the navmesh, goes straight"); }
                if (straightNow) TASK::TASK_GO_STRAIGHT_TO_COORD(dog_, g.x, g.y, g.z, 3.0f, -1, 0.0f, 0.3f, 0);
                else TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(dog_, g.x, g.y, g.z, 3.0f, -1, 0.3f, 0, 0.0f);
                FD_INFO("carry: going, %.1f m to it (jaw %.1f m), nav status %d", gap, dist(jawPoint(), g),
                        TASK::GET_SCRIPT_TASK_STATUS(dog_, MISC::GET_HASH_KEY("SCRIPT_TASK_FOLLOW_NAV_MESH_TO_COORD"), false));
                orderedAt_ = t;
            }
            if (t - stageAt_ > 40000) fail("could not reach it in 40 s");
            return;
        }
        case Stage::Grab:
            keepOnGround(t);
            if (t - stageAt_ > 600) {
                take();
                stage_ = Stage::Return;
                stageAt_ = t;
                orderedAt_ = 0;
                straight_ = job_.inWater;
            }
            return;
        case Stage::Return: {
            keepOnGround(t);
            Ped to = dest();
            float d = dist(dog_, to);
            float arrive = to == job_.horse ? 2.6f : (walkForward_ ? 2.2f : 3.0f);
            if (d < arrive) {
                deliver();
                return;
            }
            if (tire(t)) return;
            if (walkForward_) {
                if (t - orderedAt_ > (straight_ ? 1500 : 5000)) {
                    if (straight_) {
                        Vector3 tp = pos(to);
                        TASK::TASK_GO_STRAIGHT_TO_COORD(dog_, tp.x, tp.y, tp.z, job_.speed, -1, 0.0f, 1.5f, 0);
                    } else {
                        TASK::TASK_GO_TO_ENTITY(dog_, to, -1, to == job_.horse ? 2.0f : 1.6f, job_.speed, 0, 0);
                    }
                    orderedAt_ = t;
                }
                if (job_.size == CarrySize::Large && rope_) pullCarcass(1.0f);
                if (!progressAt_) {
                    progressAt_ = t;
                    lastGap_ = d;
                } else if (t - progressAt_ > 8000) {
                    bool stuck = lastGap_ - d < 1.5f;
                    progressAt_ = t;
                    lastGap_ = d;
                    if (stuck && boostPull()) stuck = false;  // a large carcass: pull harder first
                    if (stuck && job_.tenacious) stuckTries_ = 0;   // Tenacious never drops it, keeps trying straight
                    if (stuck) {
                        if (++stuckTries_ == 1) {  // first: small game rigid, go straight
                            if (job_.size == CarrySize::Small && rope_) {
                                PHYSICS::DELETE_ROPE(&rope_);
                                rope_ = 0;
                                attachRigid();
                            }
                            straight_ = true;
                            orderedAt_ = 0;
                            TASK::CLEAR_PED_TASKS(dog_, true, true);
                        } else {
                            fail("still no progress: drops it where it is");
                            TASK::TASK_TURN_PED_TO_FACE_ENTITY(dog_, player(), 1500, 0, 0, 0);
                            return;
                        }
                    }
                }
            } else {
                stepBack(t, to);
            }
            int limit = job_.size == CarrySize::Large ? (job_.tenacious ? 480000 : 240000) : 120000;
            if (dist(dog_, player()) > 90.0f || t - stageAt_ > limit)
                fail(dist(dog_, player()) > 90.0f ? "Arthur is too far" : "took too long");
            return;
        }
        default:
            return;
    }
}

bool CarryTask::tire(int t) {
    int dt = effortAt_ ? t - effortAt_ : 0;
    effortAt_ = t;
    if (t < restUntil_) return true;
    if (job_.size == CarrySize::Small) return false;
    effort_ += dt / 1000.0f * (job_.size == CarrySize::Large ? 1.0f : 0.5f);
    if (effort_ < job_.staminaSeconds) return false;
    effort_ = job_.staminaSeconds * 0.25f;  // refill only to a quarter: half made it rest every 18 s (window 15.11 08:00)
    restUntil_ = t + 6000;
    progressAt_ = orderedAt_ = stepAt_ = 0;
    TASK::CLEAR_PED_TASKS(dog_, true, true);
    TASK::TASK_STAND_STILL(dog_, 6000);
    FD_INFO("carry: rests 6 s");
    return true;
}

void CarryTask::take() {
    ENTITY::SET_ENTITY_NO_COLLISION_ENTITY(job_.carcass, dog_, false);
    Vector3 p = pos(dog_);
    if (job_.pelt || (job_.size == CarrySize::Small && ctx().ini.getBool("CarrySmallRigid", false))) {
        attachRigid();
    } else if (job_.size == CarrySize::Small) {
        const char* spine = ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(job_.carcass, "SKEL_Spine2") >= 0 ? "SKEL_Spine2"
                          : (ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(job_.carcass, "SKEL_Spine0") >= 0 ? "SKEL_Spine0" : "skel_head");
        rope_ = PHYSICS::ADD_ROPE(p.x, p.y, p.z, 0, 0, 0, 0.05f, 14, 0.25f, 0.025f, 1.0f, false, true, true, 1.25f, false, nullptr, false);
        PHYSICS::_ATTACH_ENTITIES_TO_ROPE_2(rope_, job_.carcass, dog_, 0, 0, 0, 0, 0, 0, spine, "SKEL_Jaw");
        PHYSICS::ACTIVATE_PHYSICS(rope_);
        PHYSICS::_CREATE_ROPE_WINDING_ABILITY(rope_, nullptr, "noose01x_Rope_03", 0.05f, false);
        PHYSICS::_ROPE_CHANGE_VISIBILITY(&rope_, false);
        PHYSICS::ROPE_DRAW_SHADOW_ENABLED(&rope_, false);
    } else {
        rope_ = PHYSICS::ADD_ROPE(p.x, p.y, p.z, 0, 0, 0, kRope, 14, kRope + 0.2f, 0.1f, 1.0f, false, true, true, 1.25f, false, nullptr, false);
        bool human = job_.body || PED::IS_PED_HUMAN(job_.carcass);
        PHYSICS::_ATTACH_ENTITIES_TO_ROPE_2(rope_, job_.carcass, dog_, 0, 0, 0, 0, 0, 0, human ? "SKEL_Head" : "skel_head", "SKEL_Jaw");
        PHYSICS::ACTIVATE_PHYSICS(rope_);
        PHYSICS::_CREATE_ROPE_WINDING_ABILITY(rope_, nullptr, "noose01x_Rope_03", kRope, false);  // without it the rope holds nothing
        PHYSICS::_ROPE_CHANGE_VISIBILITY(&rope_, false);
        PHYSICS::ROPE_DRAW_SHADOW_ENABLED(&rope_, false);
    }
}

void CarryTask::attachRigid() {
    int jaw = PED::GET_PED_BONE_INDEX(dog_, kJaw);
    bool bigger = job_.length > 0.55f;
    ENTITY::ATTACH_ENTITY_TO_ENTITY(job_.carcass, dog_, jaw, bigger ? 0.22f : 0.14f, bigger ? 0.06f : 0.04f, 0.0f, 0.0f, 0.0f, 90.0f,
                                    true, true, false, true, 1, true, false, false);
    attached_ = true;
}

bool CarryTask::boostPull() {
    if (job_.size != CarrySize::Large) return false;
    float cap = float(job_.tenacious ? ctx().tunef("carry.pull_boost_max_tenacious", 3.0) : ctx().tunef("carry.pull_boost_max", 1.5));
    if (pullBoost_ >= cap) return false;
    pullBoost_ = std::min(cap, pullBoost_ * 1.3f);
    FD_INFO("carry: it barely moves, pulls harder (x%.2f, pull %.0f)", pullBoost_, job_.pull * pullBoost_);
    return true;
}

void CarryTask::pullCarcass(float scale) {
    float force = job_.pull * pullBoost_ * scale;
    if (force <= 0) return;
    int head = PED::GET_PED_BONE_INDEX(job_.carcass, kHead);
    Vector3 h = head >= 0 ? ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(job_.carcass, head) : pos(job_.carcass);
    Vector3 j = jawPoint();
    float dx = j.x - h.x, dy = j.y - h.y, gap = std::sqrt(dx * dx + dy * dy);
    float taut = (gap - kRope * 0.6f) / (kRope * 0.8f);  // proportional to the slack: no on/off jerks (0.9 smoke test)
    if (taut <= 0 || gap <= 0) return;
    force *= std::min(taut, 1.0f);
    ENTITY::APPLY_FORCE_TO_ENTITY(job_.carcass, 1, dx / gap * force, dy / gap * force, 0, 0, 0, 0, head >= 0 ? head : 0,
                                  false, true, true, false, true);
}

void CarryTask::stepBack(int t, Ped to) {
    const char* dict = (steps_ % 4 == 3) ? kBackTired : kBack;
    int period = (steps_ % 4 == 3) ? 1500 : 900;
    if (!stepAt_ || t - stepAt_ > period) {
        Vector3 dp = pos(dog_), tp = pos(to);
        ENTITY::SET_ENTITY_HEADING(dog_, std::atan2(-(dp.x - tp.x), dp.y - tp.y) * 180.0f / 3.14159265f);
        STREAMING::REQUEST_ANIM_DICT(dict);
        if (STREAMING::HAS_ANIM_DICT_LOADED(dict)) {
            TASK::TASK_PLAY_ANIM(dog_, dict, "move", 4.0f, -4.0f, -1, 1 << 15, 0.0f, false, 0, false, "", false);
            stepAt_ = t;
            ++steps_;
        }
        float d = dist(dog_, to);
        if (!progressAt_) {
            progressAt_ = t;
            lastGap_ = d;
        } else if (t - progressAt_ > 8000) {
            if (lastGap_ - d < 0.8f && boostPull()) {
                progressAt_ = t;
                lastGap_ = d;
                return;
            }
            if (lastGap_ - d < 0.8f) {  // backing up gets nowhere even at the full pull: walk forward with it instead
                walkForward_ = true;
                orderedAt_ = progressAt_ = 0;
                TASK::CLEAR_PED_TASKS(dog_, true, true);
                return;
            }
            progressAt_ = t;
            lastGap_ = d;
        }
    }
    float phase = float(t - stepAt_) / float(period);
    pullCarcass(phase < 0.5f ? 1.6f : 0.3f);
}

void CarryTask::keepOnGround(int t) {
    if (t < groundCheckAt_) return;
    groundCheckAt_ = t + 150;
    Vector3 p = pos(dog_);
    float ground = 0;
    if (!MISC::GET_GROUND_Z_FOR_3D_COORD(p.x, p.y, p.z + 3.0f, &ground, false)) return;
    float h = p.z - ground;
    if (!rope_) {
        // before the rope: learn how high the origin of this dog stands - only while it stands to take the carcass (a
        // running dog bobs higher: measured on the run, every later frame looked 'sunk' and the teleports pinned the dog
        // in place, window 16.11 #4)
        if (stage_ == Stage::Grab && h > 0.1f && h < 1.2f) standHeight_ = standHeight_ < 0 ? h : std::min(standHeight_, h);
        return;
    }
    float stand = standHeight_ > 0 ? standHeight_ : float(ctx().tunef("carry.stand_height", 0.4));
    float sunk = stand - h;
    if (sunk < float(ctx().tunef("carry.sink_tolerance", 0.3))) return;  // only a real sinking (16.11 #3: metres)
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(dog_, p.x, p.y, ground + stand, false, false, false);
    static int lastLog = 0;
    if (t - lastLog > 5000) {
        lastLog = t;
        FD_INFO("carry: the dog was pulled %.2f m into the ground (stands %.2f), put back on it", sunk, stand);
    }
}

void CarryTask::release() {
    if (attached_ && exists(job_.carcass)) {
        ENTITY::DETACH_ENTITY(job_.carcass, true, true);
        ENTITY::PLACE_ENTITY_ON_GROUND_PROPERLY(job_.carcass, true);
    }
    attached_ = false;
    if (rope_) {
        PHYSICS::DELETE_ROPE(&rope_);
        rope_ = 0;
    }
    stage_ = Stage::Idle;
}

void CarryTask::deliver() {
    release();
    if (!job_.body && !job_.pelt) {
        int q = PED::_GET_PED_QUALITY(job_.carcass);
        if (job_.losesQuality && q > 0) PED::_SET_PED_QUALITY(job_.carcass, q - 1);
        FD_INFO("carry: delivered, quality %d -> %d", q, PED::_GET_PED_QUALITY(job_.carcass));
    } else {
        FD_INFO("carry: delivered (%s)", job_.body ? "a body" : "a pelt");
    }
    TASK::TASK_TURN_PED_TO_FACE_ENTITY(dog_, player(), 1500, 0, 0, 0);
    // A pelt goes into the horse's pelt stack the game's way (_SET_PELT_FOR_HORSE), never TASK_CARRIABLE: that laid it
    // flat across the horse's side, flesh up, in the carcass slot, so Arthur could not stow a carcass and the pelt was not
    // in the horse's cargo (his play, 17.11). checkStow sees whether the horse took it; if not, it stays by the horse.
    bool pelt = job_.pelt || ENTITY::_GET_IS_CARRIABLE_PELT(job_.carcass);
    if (job_.horse && dist(dog_, job_.horse) < 4.0f && ctx().ini.getBool("CarryStowOnHorse", true)) {
        if (pelt) stowPeltOnHorse(job_.carcass, job_.horse);
        else stowOnHorse();
    }
    lastDelivered_ = true;
    ++deliveries_;
}

// Onto the horse (D145, verified 16.11 #4): what works in single player is what the game's SP hunters do
// (av_walk_horse_carry_deer.c): TASK_CARRIABLE(carcass, DEAD_CARRIABLE_<SPECIES>, horse, slot 0, flags 0); the game then
// puts it in the horse's slot 4 (Arthur's own stow slot). The MP stable recipe (slots 5/6/7, flags 256) does nothing in SP.
static Hash deadCarriableFor(Ped carcass) {
    for (const auto& b : {"deer", "buck", "goat", "pronghorn", "bighornram", "duck", "elk", "moose", "boar", "fox", "coyote",
                          "wolf", "cougar", "panther", "bear", "beaver", "rabbit", "raccoon", "turkey", "goosecanada", "pheasant"}) {
        std::string m = std::string("a_c_") + b + "_01";
        if (ENTITY::GET_ENTITY_MODEL(carcass) == MISC::GET_HASH_KEY(m.c_str())) {
            std::string up = b;
            for (char& ch : up) ch = char(std::toupper((unsigned char)ch));
            return MISC::GET_HASH_KEY(("DEAD_CARRIABLE_" + up).c_str());
        }
    }
    return 0;
}

int CarryTask::freeHorseSlot(Ped horse) {
    for (int slot : {7, 5, 6}) {
        CarriedInfo ci;
        if (!carriedInSlot(horse, slot, ci) || !exists(ci.entity)) return slot;
    }
    return -1;
}

bool CarryTask::stowCarcassOnHorse(Ped carcass, Ped horse, int slot, int flags, bool deadConfig) {
    Hash cfg = deadConfig ? deadCarriableFor(carcass) : ENTITY::_GET_ENTITY_CARRY_CONFIG(carcass);
    if (!cfg) cfg = deadConfig ? ENTITY::_GET_ENTITY_CARRY_CONFIG(carcass) : deadCarriableFor(carcass);
    if (!cfg || slot < 0) {
        FD_INFO("carry: no carry config (0x%08X) or no free slot on the horse (%d)", cfg, slot);
        return false;
    }
    TASK::_REQUEST_CARRIABLE_CONFIG(cfg);
    for (int i = 0; i < 50 && !TASK::_HAS_REQUESTED_CARRIABLE_CONFIG_LOADED(cfg); ++i) scriptWait(20);
    TASK::TASK_CARRIABLE(carcass, cfg, horse, slot, flags);
    FD_INFO("carry: onto the horse (config 0x%08X %s, slot %d, flags %d)", cfg, deadConfig ? "dead_carriable" : "own", slot, flags);
    stowCarcass_ = carcass;
    stowHorse_ = horse;
    stowSlot_ = slot;
    stowCheckAt_ = now() + 3000;
    return true;
}

// Window 16.11 #4 (06:05): slot 0 + flags 0 + DEAD_CARRIABLE_<SPECIES> = what the game's own SP script does
// (av_walk_horse_carry_deer.c): the fox got attached, carried by the horse, in its slot 4 (Arthur's own stow slot).
// Slots 5/6/7 with flags 256 (the MP stable script) did nothing in single player, near or far.
void CarryTask::stowOnHorse() {
    stowCarcassOnHorse(job_.carcass, job_.horse, int(ctx().tune("carry.stow_slot", 0)), int(ctx().tune("carry.stow_flags", 0)),
                       ctx().ini.getBool("CarryStowDeadConfig", true));
}

// the horse's pelt stack has 3 places: the game's own scripts (player_horse, shop_trapper 1491.50) read and clear
// _GET_PELT_FROM_HORSE(horse, 0..2) only
static constexpr int kPeltPlaces = 3;

static int peltsOnHorse(Ped horse) {
    int n = 0;
    for (int i = 0; i < kPeltPlaces; ++i)
        if (PED::_GET_PELT_FROM_HORSE(horse, i)) ++n;
    return n;
}

// _SET_PELT_FOR_HORSE takes the game's own pelt id (what _GET_PELT_FROM_HORSE returns - the game's MP stable script
// copies those between horses), not a pelt object: given the object it did nothing and the pelt stayed by the horse
// (his play, 18.11). The pelt goes in by its inventory item instead (_SET_PELT_FOR_HORSE_BY_INVENTORY_ITEM, what RedM
// stables do), the item the game itself computes for the carcass it came from, the look from the pelt's albedo; the
// object is removed only once the horse's stack has grown.
// 24.11 («шкура на лошади есть визуально, а в лошади пусто»): the stack holds the pelt's own carriable item
// (_GET_CARRIABLE_FROM_ENTITY - what player_horse.c records for a stowed pelt and what the trapper and the butcher
// count); the satchel item computed from the carcass drew the pelt but the shops did not see it. Only when the pelt has
// no carriable item does the old way stay.
void CarryTask::stowPeltOnHorse(Entity pelt, Ped horse) {
    if (!exists(pelt) || !alive(horse)) return;
    Hash albedo = 0;
    ENTITY::_GET_ENTITY_ALBEDO(pelt, &albedo);
    Ped carcass = ENTITY::_GET_CARCASS_FROM_PELT(pelt);
    stowCount_ = peltsOnHorse(horse);
    if (stowCount_ >= kPeltPlaces) {  // a fourth pelt took the place of one already there (24.11 «заменяет шкуру»)
        FD_INFO("carry: the horse's pelt stack is full (%d), the pelt stays by the horse", stowCount_);
        helpText(ctx().t("help.horse_pelts_full"), 6000);
        return;
    }
    Hash item = ENTITY::_GET_CARRIABLE_FROM_ENTITY(pelt);
    int ret = -1;
    alignas(8) uint64_t items[32] = {};
    if (!item && carcass && exists(carcass)) {
        items[0] = 8;
        ret = PED::_COMPUTE_SATCHEL_ITEM_FOR_PED_CARCASS(reinterpret_cast<Any*>(items), carcass,
                                                        PED::_GET_PED_DAMAGE_CLEANLINESS(carcass), PED::_GET_PED_QUALITY(carcass));
        for (int i = 0; i < 12 && !item; ++i)
            if (items[i] > 0xFFFF && items[i] <= 0xFFFFFFFFull) item = Hash(items[i]);
        if (!item && uint32_t(ret) > 0xFFFF) item = Hash(ret);
    }
    FD_INFO("carry: pelt %d (model 0x%08X, albedo 0x%08X), carcass %d -> item 0x%08X (ret %d: %llX %llX %llX %llX), %d pelts on the horse",
            pelt, ENTITY::GET_ENTITY_MODEL(pelt), albedo, carcass, item, ret, (unsigned long long)items[0],
            (unsigned long long)items[1], (unsigned long long)items[2], (unsigned long long)items[3], stowCount_);
    if (item) PED::_SET_PELT_FOR_HORSE_BY_INVENTORY_ITEM(horse, item, albedo, 0, false);
    stowCarcass_ = pelt;
    stowHorse_ = horse;
    stowSlot_ = -1;
    stowPelt_ = true;
    stowItem_ = item;
    stowCheckAt_ = now() + 1500;
}

void CarryTask::checkStow() {
    stowCheckAt_ = 0;
    if (stowPelt_) {
        stowPelt_ = false;
        int at = -1, count = 0;
        for (int i = 0; i < kPeltPlaces; ++i) {
            int id = PED::_GET_PELT_FROM_HORSE(stowHorse_, i);
            if (!id) continue;
            ++count;
            if (Hash(id) == stowItem_) at = i;
        }
        FD_INFO("carry: pelt check - %d pelts on the horse (were %d), item 0x%08X at place %d", count, stowCount_, stowItem_, at);
        if (count > stowCount_ && exists(stowCarcass_)) {  // the horse has it: the object on the ground goes
            Entity e = stowCarcass_;
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, true, true);
            ENTITY::DELETE_ENTITY(&e);
            FD_INFO("carry: the pelt is on the horse");
        } else {
            FD_INFO("carry: the horse did not take the pelt, it stays by the horse");
        }
        return;
    }
    if (!exists(stowCarcass_) || !exists(stowHorse_)) {
        FD_INFO("carry: stow check - carcass %d / horse %d gone", int(exists(stowCarcass_)), int(exists(stowHorse_)));
        return;
    }
    CarriedInfo ci;
    bool got = carriedInSlot(stowHorse_, stowSlot_, ci);
    int onHorseSlot = -1;
    for (int sl = 0; sl < 12 && onHorseSlot < 0; ++sl) {
        CarriedInfo c2;
        if (carriedInSlot(stowHorse_, sl, c2) && c2.entity == stowCarcass_) onHorseSlot = sl;
    }
    FD_INFO("carry: stow check - on the horse in slot %d, visible %d, alpha %d", onHorseSlot,
            int(ENTITY::IS_ENTITY_VISIBLE(stowCarcass_)), ENTITY::GET_ENTITY_ALPHA(stowCarcass_));
    FD_INFO("carry: stow check - slot %d: info %d, entity %d (ours %d), model 0x%08X, config 0x%08X", stowSlot_, int(got), ci.entity,
            int(ci.entity == stowCarcass_), ci.model, ci.config);
    Vector3 c = pos(stowCarcass_), h = pos(stowHorse_);
    FD_INFO("carry: stow check - attached %d, carrier mount %d, carriable state %d, %.1f m from the horse (dz %.1f), horse "
            "speed %.2f, horse task 0x%08X",
            int(ENTITY::IS_ENTITY_ATTACHED_TO_ENTITY(stowCarcass_, stowHorse_)), PED::_GET_CARRIER_AS_MOUNT(stowCarcass_),
            ENTITY::GET_CARRIABLE_ENTITY_STATE(stowCarcass_), dist(c, h), c.z - h.z, ENTITY::GET_ENTITY_SPEED(stowHorse_),
            TASK::GET_SCRIPT_TASK_STATUS(stowHorse_, MISC::GET_HASH_KEY("SCRIPT_TASK_CARRIABLE"), false));
}

void CarryTask::fail(const char* why) {
    FD_INFO("carry: failed - %s", why);
    release();
}

}  // namespace fd
