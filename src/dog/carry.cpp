#include "dog/carry.h"

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
    if (!active()) return;
    int t = now();
    if (!exists(job_.carcass) || !alive(dog_)) {
        fail("the carcass or the dog is gone");
        return;
    }
    switch (stage_) {
        case Stage::GoTo: {
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
                return;
            }
            if (t - orderedAt_ > (job_.inWater ? 1500 : 4000)) {
                if (job_.inWater) TASK::TASK_GO_STRAIGHT_TO_COORD(dog_, g.x, g.y, g.z, 3.0f, -1, 0.0f, 0.3f, 0);
                else TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(dog_, g.x, g.y, g.z, 3.0f, -1, 0.3f, 0, 0.0f);
                orderedAt_ = t;
            }
            if (t - stageAt_ > 40000) fail("could not reach it in 40 s");
            return;
        }
        case Stage::Grab:
            if (t - stageAt_ > 600) {
                take();
                stage_ = Stage::Return;
                stageAt_ = t;
                orderedAt_ = 0;
                straight_ = job_.inWater;
            }
            return;
        case Stage::Return: {
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
            int limit = job_.size == CarrySize::Large ? 240000 : 120000;
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

void CarryTask::pullCarcass(float scale) {
    float force = job_.pull * scale;
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
            if (lastGap_ - d < 0.8f) {  // backing up gets nowhere: walk forward with the pull instead
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
    lastDelivered_ = true;
    ++deliveries_;
}

void CarryTask::fail(const char* why) {
    FD_INFO("carry: failed - %s", why);
    release();
}

}  // namespace fd
