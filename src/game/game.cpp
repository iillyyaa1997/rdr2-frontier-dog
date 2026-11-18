#include "game/game.h"

#include <cstdint>

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

// A man made by a script stays invisible until he gets an outfit (17.11: the dog trader stood in the yard, his prompts
// worked, nobody was there); animals get their coat preset from the caller.
static void dressNew(Ped p) {
    if (p && PED::IS_PED_HUMAN(p)) PED::_SET_RANDOM_OUTFIT_VARIATION(p, true);
}

Ped spawnPed(const std::string& modelName, const Vector3& at, float heading, int timeoutMs) {
    Hash model = MISC::GET_HASH_KEY(modelName.c_str());
    if (!loadModel(model, timeoutMs)) {
        FD_WARN("model %s did not load in %d ms", modelName.c_str(), timeoutMs);
        return 0;
    }
    Ped p = PED::CREATE_PED(model, at.x, at.y, at.z, heading, false, false, false, false);
    dressNew(p);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    if (!p) FD_WARN("CREATE_PED %s returned 0 (ped pool full?)", modelName.c_str());
    return p;
}

Ped stepSpawn(SpawnRequest& r) {
    if (r.done) return 0;
    const std::string& name = r.onFallback ? r.fallback : r.model;
    Hash model = MISC::GET_HASH_KEY(name.c_str());
    if (!r.startedAt) r.startedAt = now();
    bool valid = STREAMING::IS_MODEL_VALID(model);
    if (valid) {
        STREAMING::REQUEST_MODEL(model, false);
        if (STREAMING::HAS_MODEL_LOADED(model)) {
            Ped p = PED::CREATE_PED(model, r.at.x, r.at.y, r.at.z, r.heading, false, false, false, false);
            dressNew(p);
            STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
            if (!p) FD_WARN("CREATE_PED %s returned 0 (ped pool full?)", name.c_str());
            r.done = true;
            return p;
        }
    }
    if (!valid || now() - r.startedAt > r.timeoutMs) {
        FD_WARN("model %s %s", name.c_str(), valid ? "did not load in time" : "is not valid");
        if (valid) STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
        if (!r.onFallback && !r.fallback.empty() && r.fallback != r.model) {
            r.onFallback = true;
            r.startedAt = now();
        } else {
            r.done = true;
        }
    }
    return 0;
}

void deletePed(Ped& ped) {
    if (exists(ped)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
        PED::DELETE_PED(&ped);
    }
    ped = 0;
}

bool safeCoord(const Vector3& around, Vector3& out) {
    return PATH::GET_SAFE_COORD_FOR_PED(around.x, around.y, around.z, false, &out, 16) != 0;  // onGround false as in 0.14 (tested)
}

bool hiddenSpotNear(const Vector3& around, float minDist, float maxDist, Vector3& out, bool outdoors) {
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
            if (outdoors && INTERIOR::GET_INTERIOR_AT_COORDS(safe.x, safe.y, safe.z)) continue;
            out = safe;
            return true;
        }
    }
    return false;
}

const char* literal(const std::string& text) { return MISC::VAR_STRING(10, "LITERAL_STRING", text.c_str()); }

// _UI_FEED_POST_HELP_TEXT takes a duration block and a text block. The game reads more of each block than the two
// fields we fill (Trail Guide 18.11: 16-byte blocks made the native throw — «Error executing native 0x049D5C615BD38BAD»
// in the ScriptHook log, nothing on screen), so both are zeroed 128-byte buffers: duration at 0, the text pointer at 8.
void helpText(const std::string& text, int ms) {
    alignas(16) uint64_t duration[16] = {};
    alignas(16) uint64_t data[16] = {};
    duration[0] = uint64_t(uint32_t(ms));
    data[1] = reinterpret_cast<uint64_t>(literal(text));
    UIFEED::_UI_FEED_POST_HELP_TEXT(reinterpret_cast<Any*>(duration), reinterpret_cast<Any*>(data), true);
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

int itemCount(const char* item) {
    int inv = INVENTORY::_INVENTORY_GET_INVENTORY_ID_FROM_PED(player());
    return INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_COUNT_WITH_ITEMID(inv, MISC::GET_HASH_KEY(item), false);
}

bool takeItem(const char* item, int n) {
    if (itemCount(item) < n) return false;
    int inv = INVENTORY::_INVENTORY_GET_INVENTORY_ID_FROM_PED(player());
    return INVENTORY::_INVENTORY_REMOVE_INVENTORY_ITEM_WITH_ITEMID(inv, MISC::GET_HASH_KEY(item), n,
                                                                  MISC::GET_HASH_KEY("REMOVE_REASON_DEFAULT")) != 0;
}

bool itemKnown(const char* item) { return ITEMDATABASE::_ITEMDATABASE_IS_KEY_VALID(MISC::GET_HASH_KEY(item), 0) != 0; }

namespace {
struct Guid {  // script struct<4>
    alignas(8) long long f[4] = {0, 0, 0, 0};
};
struct SlotGuid {  // script struct<5>: the parent guid + the slot
    Guid guid;
    alignas(8) long long slot = 0;
};
}  // namespace

bool giveItem(const char* item, int n) {
    if (n <= 0 || !itemKnown(item)) return false;
    int inv = INVENTORY::_INVENTORY_GET_INVENTORY_ID_FROM_PED(player());
    Hash satchel = MISC::GET_HASH_KEY("SLOTID_SATCHEL");
    Guid none;
    SlotGuid parent;
    if (!INVENTORY::INVENTORY_GET_GUID_FROM_ITEMID(inv, (Any*)&none, MISC::GET_HASH_KEY("CHARACTER"), MISC::GET_HASH_KEY("SLOTID_NONE"),
                                                   (Any*)&parent.guid))
        return false;
    parent.slot = satchel;
    Guid itemGuid;
    Hash h = MISC::GET_HASH_KEY(item);
    INVENTORY::INVENTORY_GET_GUID_FROM_ITEMID(inv, (Any*)&parent.guid, h, satchel, (Any*)&itemGuid);
    return INVENTORY::_INVENTORY_ADD_ITEM_WITH_GUID(inv, (Any*)&itemGuid, (Any*)&parent, h, satchel, n,
                                                    MISC::GET_HASH_KEY("ADD_REASON_DEFAULT")) != 0;
}

bool carriedInSlot(Ped ped, int slot, CarriedInfo& out) {
    struct {
        alignas(8) long long model = 0, config = 0, slot = 0, entity = 0;
    } raw;
    if (!PED::GET_CARRIED_ATTACHED_INFO_FOR_SLOT((Any*)&raw, ped, slot, 0)) return false;
    out.model = Hash(raw.model);
    out.config = Hash(raw.config);
    out.slot = int(raw.slot);
    out.entity = Entity(raw.entity);
    return true;
}

int cash() { return MONEY::_MONEY_GET_CASH_BALANCE(); }

bool takeCash(int cents) {
    if (cents <= 0) return true;
    if (cash() < cents) return false;
    return MONEY::_MONEY_DECREMENT_CASH_BALANCE(cents) != 0;
}

void giveCash(int cents) {
    if (cents > 0) MONEY::_MONEY_INCREMENT_CASH_BALANCE(cents, MISC::GET_HASH_KEY("ADD_REASON_DEFAULT"));
}

// ---- radar mode (16.11, measured on 1.0.1491.50: hold Alt / D-pad down picks the minimap mode, a tap reveals the HUD)
// RDR2.exe + 0x3ef1490: 0 = see the chosen mode, 1 = radar off, 2 = grown by the HUD reveal; + 0x3eb9df0 / 0x3ed0180 /
// 0x3ef1448: the chosen mode 0 off, 1 regular, 2 expanded, 3 compass (the offsets RDR2CareOverhaul.asi reads).
static bool verifiedBuild() {
    static int ok = -1;
    if (ok >= 0) return ok == 1;
    ok = 0;
    HMODULE ver = LoadLibraryW(L"version.dll");
    if (!ver) return false;
    using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto size = reinterpret_cast<SizeFn>(GetProcAddress(ver, "GetFileVersionInfoSizeW"));
    auto info = reinterpret_cast<InfoFn>(GetProcAddress(ver, "GetFileVersionInfoW"));
    auto query = reinterpret_cast<QueryFn>(GetProcAddress(ver, "VerQueryValueW"));
    wchar_t exe[MAX_PATH];
    if (size && info && query && GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        DWORD ignored = 0;
        DWORD n = size(exe, &ignored);
        std::string data(n, '\0');
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT len = 0;
        if (n && info(exe, 0, n, &data[0]) && query(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed), &len) && fixed)
            ok = fixed->dwFileVersionMS == ((1u << 16) | 0u) && fixed->dwFileVersionLS == ((1491u << 16) | 50u) ? 1 : 0;
    }
    FreeLibrary(ver);
    FD_INFO("radar mode from memory: %s", ok ? "yes (1.0.1491.50)" : "no (another game build)");
    return ok == 1;
}

static int readGameInt(uintptr_t off) {
    HMODULE m = GetModuleHandleW(nullptr);
    if (!m) return -1;
    auto p = reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(m) + off);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return -1;
    return *p;
}

int radarLayout() {
    if (verifiedBuild()) {
        int now = readGameInt(0x3ef1490);
        if (now == 2) return 2;  // the HUD is revealed: the radar grows whatever was chosen (timed shots 16.11)
        int chosen = now == 1 ? 0 : -1;
        if (chosen < 0)
            for (uintptr_t off : {uintptr_t(0x3eb9df0), uintptr_t(0x3ed0180), uintptr_t(0x3ef1448)}) {
                int v = readGameInt(off);
                if (v >= 0 && v <= 3) {
                    chosen = v;
                    break;
                }
            }
        if (chosen == 1 || chosen == 2) return chosen;
        if (chosen == 0 || chosen == 3) return 0;  // off / compass: no radar circle
    }
    // another build: no radar circle when it is hidden (grown while the HUD is revealed: slot 2 goes 0 -> 1 with the
    // cash and the place name), else regular (expanded and compass cannot be told apart this way)
    if (HUD::IS_RADAR_HIDDEN()) return HUD::_GET_HUD_VISIBILITY_SLOT_STATE(2) == 1 ? 2 : 0;
    return 1;
}

}  // namespace fd::game
