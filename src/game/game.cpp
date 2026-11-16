#include "game/game.h"

#include <cmath>
#include <ctime>

#include "core/log.h"
#include "core/strutil.h"

namespace fd::game {

Ped player() { return PLAYER::PLAYER_PED_ID(); }

Vector3 pos(Entity e) { return ENTITY::GET_ENTITY_COORDS(e, true, false); }

float dist(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float dist(Entity a, Entity b) { return dist(pos(a), pos(b)); }

Vector3 add(const Vector3& a, const Vector3& b) { return Vector3(a.x + b.x, a.y + b.y, a.z + b.z); }

Vector3 scale(const Vector3& a, float k) { return Vector3(a.x * k, a.y * k, a.z * k); }

int now() { return MISC::GET_GAME_TIMER(); }

bool exists(Entity e) { return e && ENTITY::DOES_ENTITY_EXIST(e); }

bool alive(Entity e) { return exists(e) && !ENTITY::IS_ENTITY_DEAD(e); }

bool loadModel(Hash model, int timeoutMs) {
    if (!STREAMING::IS_MODEL_VALID(model)) return false;
    STREAMING::REQUEST_MODEL(model, false);
    int until = now() + timeoutMs;
    while (!STREAMING::HAS_MODEL_LOADED(model)) {
        if (now() > until) return false;
        STREAMING::REQUEST_MODEL(model, false);
        scriptWait(0);
    }
    return true;
}

Ped spawnPed(const std::string& modelName, const Vector3& at, float heading, int timeoutMs) {
    Hash model = MISC::GET_HASH_KEY(modelName.c_str());
    if (!loadModel(model, timeoutMs)) {
        FD_WARN("model %s did not load in %d ms", modelName.c_str(), timeoutMs);
        return 0;
    }
    Ped p = PED::CREATE_PED(model, at.x, at.y, at.z, heading, false, false, false, false);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!p) FD_WARN("CREATE_PED %s returned 0 (ped pool full?)", modelName.c_str());
    return p;
}

void deletePed(Ped& ped) {
    if (exists(ped)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
        PED::DELETE_PED(&ped);
    }
    ped = 0;
}

bool safeCoord(const Vector3& around, Vector3& out) {
    return PATH::GET_SAFE_COORD_FOR_PED(around.x, around.y, around.z, true, &out, 16) != 0;
}

bool hiddenSpotNear(const Vector3& around, float minDist, float maxDist, Vector3& out) {
    // try points on rings around Arthur, prefer the ones behind the camera; must be on the navmesh and unseen
    Vector3 cam = CAMERA::GET_GAMEPLAY_CAM_ROT(2);
    float camHeading = cam.z * 3.14159265f / 180.0f;
    for (int ring = 0; ring < 3; ++ring) {
        float r = minDist + (maxDist - minDist) * (ring / 2.0f);
        for (int k = 0; k < 12; ++k) {
            float a = camHeading + 3.14159265f + (k % 2 ? 1 : -1) * (k / 2) * 0.35f;  // start behind the camera
            Vector3 p(around.x - std::sin(a) * r, around.y + std::cos(a) * r, around.z);
            Vector3 safe;
            if (!safeCoord(p, safe)) continue;
            if (std::fabs(safe.z - around.z) > 25.0f) continue;
            if (CAMERA::IS_SPHERE_VISIBLE(safe.x, safe.y, safe.z + 0.5f, 1.0f)) continue;
            out = safe;
            return true;
        }
    }
    return false;
}

const char* literal(const std::string& text) { return MISC::VAR_STRING(10, "LITERAL_STRING", text.c_str()); }

// _UI_FEED_POST_HELP_TEXT takes a duration block and a text block (layouts as used by other ScriptHook mods)
struct FeedDuration {
    alignas(8) int duration;
    alignas(8) const char* soundSet;
    alignas(8) const char* sound;
    alignas(8) int unk;
};
struct FeedText {
    alignas(8) int unk;
    alignas(8) const char* text;
};

void helpText(const std::string& text, int ms) {
    FeedDuration d{ms, nullptr, nullptr, 0};
    FeedText t{0, literal(text)};
    UIFEED::_UI_FEED_POST_HELP_TEXT((Any*)&d, (Any*)&t, true);
}

struct SpeechParams {
    alignas(8) const char* line;
    alignas(8) const char* voice;
    alignas(8) int v1;
    alignas(8) int v2;
    alignas(8) int speaker;
    alignas(8) int v3;
    alignas(8) int v4;
};

void speech(Ped ped, const char* line) {
    SpeechParams p{line, nullptr, 1, 1744022339, ped, 1, 1};
    AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(ped, (Any*)&p);
}

bool onMission() { return MISC::GET_MISSION_FLAG() != 0; }

bool playerInControl() { return PLAYER::IS_PLAYER_CONTROL_ON(PLAYER::PLAYER_ID()) != 0; }

// The game gives scripts no plain 'cutscene playing' flag in story mode: a mission with player control taken away or
// a faded screen is treated as one.
bool inCutscene() { return (onMission() && !playerInControl()) || CAMERA::IS_SCREEN_FADED_OUT(); }

long long gameMinutes() {
    // days counted from a fixed calendar origin, so periods (shop rotation, training days) survive saves and reloads
    long long y = CLOCK::GET_CLOCK_YEAR(), m = CLOCK::GET_CLOCK_MONTH(), d = CLOCK::GET_CLOCK_DAY_OF_MONTH();
    // civil-from-days inverse (Howard Hinnant), month is 0-based in the game clock
    long long mm = m + 1;
    y -= mm <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (mm + (mm > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097 + doe;
    return days * 1440 + CLOCK::GET_CLOCK_HOURS() * 60 + CLOCK::GET_CLOCK_MINUTES();
}

long long gameDay() { return gameMinutes() / 1440; }

long long unixNow() { return (long long)std::time(nullptr); }

unsigned districtHash(const Vector3& at) { return ZONE::_GET_MAP_ZONE_AT_COORDS(at.x, at.y, at.z, 10); }

bool inTown(const Vector3& at) { return ZONE::_GET_MAP_ZONE_AT_COORDS(at.x, at.y, at.z, 1) != 0; }

int cash() { return MONEY::_MONEY_GET_CASH_BALANCE(); }

bool takeCash(int cents) {
    if (cents <= 0) return true;
    if (cash() < cents) return false;
    return MONEY::_MONEY_DECREMENT_CASH_BALANCE(cents) != 0;
}

void giveCash(int cents) {
    if (cents > 0) MONEY::_MONEY_INCREMENT_CASH_BALANCE(cents, MISC::GET_HASH_KEY("ADD_REASON_DEFAULT"));
}

}  // namespace fd::game
