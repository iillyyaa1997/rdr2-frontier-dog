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
    int rope_ = 0;
    bool attached_ = false, walkForward_ = true, straight_ = false, lastDelivered_ = false;
    int deliveries_ = 0;
};

}  // namespace fd
