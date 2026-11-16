// Thin helpers over the natives the whole mod uses: positions, bounded model loads, help texts, speech.
#pragma once
#include <string>

#include "sdk/natives.gen.h"

namespace fd::game {

Ped player();
Vector3 pos(Entity e);
float dist(const Vector3& a, const Vector3& b);
float dist(Entity a, Entity b);
Vector3 add(const Vector3& a, const Vector3& b);
Vector3 scale(const Vector3& a, float k);
int now();  // game timer, ms
bool exists(Entity e);
bool alive(Entity e);

// Requests a model and waits at most timeoutMs (never forever: an endless wait froze the game at a stable, 15.11).
bool loadModel(Hash model, int timeoutMs);
// Creates a ped from a model name; 0 if the model is invalid or did not load in time.
Ped spawnPed(const std::string& modelName, const Vector3& at, float heading, int timeoutMs = 3000);
void deletePed(Ped& ped);

// A spot on the navmesh near 'around' that the camera does not see (arrival out of view, like the horse).
bool hiddenSpotNear(const Vector3& around, float minDist, float maxDist, Vector3& out);
bool safeCoord(const Vector3& around, Vector3& out);

const char* literal(const std::string& text);  // VAR_STRING(10, "LITERAL_STRING", text)
void helpText(const std::string& text, int ms = 5000);
void speech(Ped ped, const char* line);        // ambient speech, e.g. HORSE_FOLLOW_ME_MALE

bool onMission();
bool inCutscene();
bool playerInControl();
long long gameMinutes();   // in-game minutes since the clock's day 0 (from the game calendar)
long long gameDay();
long long unixNow();
unsigned districtHash(const Vector3& at);  // _GET_MAP_ZONE_AT_COORDS type 10 (DISTRICT)
bool inTown(const Vector3& at);            // type 1 (TOWN; includes ranches and oil fields)

int itemCount(const char* item);           // in Arthur's satchel
bool takeItem(const char* item, int n = 1);  // no toast: the item toast faulted on some items (old 0.6)

int cash();             // cents
bool takeCash(int cents);
void giveCash(int cents);

}  // namespace fd::game
