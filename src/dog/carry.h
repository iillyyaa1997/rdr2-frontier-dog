// The dog brings a carcass (old D12, D84; plus 0.9–0.12 windows): small game hung from the jaw on a hidden 5 cm rope
// (limp) or held stiff, medium dragged on a hidden 0.3 m jaw rope, large on the rope plus a pull scaled by strength,
// dragged backwards in jerks with the game's step-back clips. Rests when tired (stamina), drops it if stuck.
#pragma once
#include "sdk/natives.gen.h"

namespace fd {

enum class CarrySize { Small, Medium, Large };

struct CarryJob {
    Ped carcass = 0;
    CarrySize size = CarrySize::Small;
    float length = 0;
    float pull = 0;           // newtons-ish force for large game
    float speed = 2.0f;       // return speed
    bool losesQuality = false;
    bool inWater = false;
    bool body = false;        // a man's body (rope at SKEL_Head)
    bool pelt = false;
    Ped horse = 0;            // bring it to Arthur's horse instead of Arthur
    float staminaSeconds = 45;
    bool tenacious = false;   // Tenacious trait: drags a large carcass however slowly, never gives up on it (D143)
};

class CarryTask {
public:
    static CarrySize sizeOf(Ped carcass, float* length);
    void start(Ped dog, const CarryJob& job);
    void update();            // every frame while active()
    void release();           // let go where it is
    bool active() const { return stage_ != Stage::Idle; }
    bool delivered() const { return lastDelivered_; }
    Ped carcass() const { return job_.carcass; }
    int deliveries() { int n = deliveries_; deliveries_ = 0; return n; }  // read once (skill uses, deeds)
    CarrySize size() const { return job_.size; }

private:
    enum class Stage { Idle, GoTo, Grab, Return };
    Vector3 grabPoint() const;
    Vector3 jawPoint() const;
    Ped dest();
    void take();
    void attachRigid();
    bool tire(int t);
    void pullCarcass(float scale);
    void stepBack(int t, Ped to);
    void deliver();
    void stowOnHorse();
    void fail(const char* why);

    Ped dog_ = 0;
    CarryJob job_;
    Stage stage_ = Stage::Idle;
    int stageAt_ = 0, orderedAt_ = 0, progressAt_ = 0, restUntil_ = 0, effortAt_ = 0, stepAt_ = 0, steps_ = 0, stuckTries_ = 0;
    float lastGap_ = 0, effort_ = 0;
    float pullBoost_ = 1.0f;  // grows while a large carcass will not move (window 16.11: the Shiba stalled on a deer)
    bool boostPull();         // true if the pull could still grow
    void keepOnGround(int t); // the rope drags a scaled-down dog into the ground (16.11 #3: Shiba 72 % sank, then fell through)
    int groundCheckAt_ = 0;
    float standHeight_ = -1;  // the dog's origin above the ground while standing, measured before the rope goes on
    int rope_ = 0;
    bool attached_ = false, walkForward_ = true, straight_ = false, lastDelivered_ = false;
    int deliveries_ = 0;
    // after a stow onto the horse: look 3 s later whether it really sits there and the horse still moves (window 16.11
    // 03:58: the horse froze mid-step after TASK_CARRIABLE with slot 0)
    Ped stowCarcass_ = 0, stowHorse_ = 0;
    int stowCheckAt_ = 0;
    bool stowPelt_ = false;  // the last stow was a pelt into the horse's pelt stack
    int stowCount_ = 0;      // pelts on the horse before it
    Hash stowItem_ = 0;      // the pelt's item put into the stack
    void stowPeltOnHorse(Entity pelt, Ped horse);
    void checkStow();
    int stowSlot_ = -1;

public:
    // shared with the test hooks: the game's own stow onto a horse (see carry.cpp)
    static int freeHorseSlot(Ped horse);
    bool stowCarcassOnHorse(Ped carcass, Ped horse, int slot, int flags, bool deadConfig);
};


}  // namespace fd
