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
    void callFromHomeAt(int slot, const Vector3& at);  // brought by the stable hand to a spot
    bool adopt(Ped ped, DogRecord d);
    void takeFromCamp(int slot, Ped ped);  // a home dog seen in camp joins Arthur (free swap, D98)   // an existing ped becomes the companion (tamed stray, bought dog)
    void releaseForSale();              // the companion was sold: it stays with the stable hand
    bool isShopOrHomeDog(Ped p) const;  // stable dogs for sale must not be tamed
    void follow();
    void stay();
    void whistle();
    bool arriveOutOfView();

private:
    void spawn(bool outOfView, const Vector3* at = nullptr);
    void setup(Ped dog);
    void releasePed(bool runOff);
    void updatePrompts();
    void updateFollow();
    void updateBond();
    void updateHealth();
    void updateStrays();
    void praise();
    void died();
    bool updateWounded();   // true while the wounded state owns the dog this frame
    void startWounded();
    void endWounded(bool revived);
    void updateAway();
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

    game::UiPrompt stayPrompt_, praisePrompt_, homePrompt_, tamePrompt_, revivePrompt_;
    Blip woundedBlip_ = 0;
    int woundedGraceUntil_ = 0;
    int nextWoundedCheck_ = 0;
    int nextAwayCheck_ = 0;
    Ped tameCandidate_ = 0;
    int tameCandidateUntil_ = 0;
    std::map<int, DogRecord> strayRolls_;  // ped handle -> rolled card (D102: known before taming)
};

Companion& companion();

}  // namespace fd
