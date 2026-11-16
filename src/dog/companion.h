// The dog with Arthur: its ped, blip and prompts, following and staying, the whistle, coming back on its own, arriving
// out of view after fast travel, bonding growth, taming a stray, going home. Old plan D7–D10, D24, D47, D78, D82.
#pragma once
#include <map>
#include <string>

#include "game/prompt.h"
#include "logic/dog.h"
#include "sdk/natives.gen.h"

namespace fd {

class Companion {
public:
    void start();                       // after the game has loaded
    void tick();                        // every frame
    void onEvent(Hash type, int index); // game events (group 0)

    bool present() const { return ped_ != 0; }
    Ped ped() const { return ped_; }
    int slot() const { return slot_; }
    DogRecord* record();
    bool staying() const { return stay_; }
    float whistleRange() const;
    void markActive(int ms = 6000);     // the dog's core shows for a while (command, whistle)
    bool hudActive() const;
    void addBond(double points, const std::string& reason, int cooldownMs);
    void save();

    void sendHome(bool runOff);         // the dog goes home to camp (ped removed out of view)
    void callFromHome(int slot);        // a home dog joins Arthur (arrives out of view)
    void follow();
    void stay();
    void whistle();
    bool arriveOutOfView();

private:
    void spawn(bool outOfView);
    void setup(Ped dog);
    void releasePed(bool runOff);
    void updatePrompts();
    void updateFollow();
    void updateBond();
    void updateHealth();
    void updateStrays();
    void praise();
    void died();
    void tame(Ped stray);

    Ped ped_ = 0;
    Blip blip_ = 0;
    int slot_ = -1;
    bool stay_ = false;
    int pendingFollowAt_ = 0;
    int hudUntil_ = 0;
    int nextBondTick_ = 0;
    int saveAt_ = 0;
    bool dirty_ = false;
    Vector3 lastPlayerPos_;
    int lastPosAt_ = 0;
    int stuckSince_ = 0;
    int lastArrival_ = 0;
    int nextFollowCheck_ = 0;
    float lastFollowDist_ = 0;
    std::map<std::string, int> cooldownUntil_;

    game::UiPrompt stayPrompt_, praisePrompt_, homePrompt_, tamePrompt_;
    Ped tameCandidate_ = 0;
    int tameCandidateUntil_ = 0;
    std::map<int, DogRecord> strayRolls_;  // ped handle -> rolled card (D102: known before taming)
};

Companion& companion();

}  // namespace fd
