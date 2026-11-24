#ifdef FD_TEST
#include <cmath>
#include "mod/testhooks.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "core/fs.h"
#include "core/log.h"
#include "core/strutil.h"
#include "dog/coat.h"
#include "dog/stables.h"
#include "dog/carry.h"
#include "dog/companion.h"
#include "dog/delivery.h"
#include "dog/hunting.h"
#include "dog/strays.h"
#include "game/game.h"
#include "game/prompt.h"
#include "mod/context.h"
#include "ui/card.h"
#include "ui/core.h"
#include "ui/draw.h"
#include "ui/talk.h"

namespace fd::testhooks {

using namespace game;

static std::vector<Ped> g_spawned;

static int g_ctlUntil = 0;  // see probeControls
static int g_aimUntil = 0, g_aimNext = 0;  // see aimlog
static int g_watchUntil = 0;  // see watchHud
static int g_spritesUntil = 0;  // see drawSprites
static int g_fontsUntil = 0;    // see ui::talkFontSurvey
static std::vector<std::pair<std::string, std::string>> g_sprites;
static void run(const std::vector<std::string>& a) {
    auto& c = ctx();
    auto& comp = companion();
    const std::string& cmd = a[0];
    auto num = [&](size_t i, double def) { double v; return i < a.size() && str::parseFloat(a[i], v) ? v : def; };
    Ped pl = player();
    Vector3 p = pos(pl);
    Vector3 fwd = ENTITY::GET_ENTITY_FORWARD_VECTOR(pl);
    if (cmd == "talk" && a.size() >= 2) {  // talk open|up|down|accept|back|<digit>|close
        stables::testTalk(a[1]);
        FD_INFO("test: talk %s (open %d)", a[1].c_str(), int(stables::talking()));
    } else if (cmd == "tune" && a.size() >= 3) {  // tune <Companion.dat key> <value>: live, not saved
        c.tuning.set(a[1], a[2]);
        FD_INFO("test: tune %s = %s", a[1].c_str(), a[2].c_str());
    } else if (cmd == "fonts") {  // fonts <seconds>: sample lines in the game's font faces
        g_fontsUntil = now() + int(num(1, 20) * 1000);
    } else if (cmd == "stray" && a.size() >= 2) {
        const BreedDef* b = c.breeds.find(a[1]);
        if (!b) { FD_INFO("test: no breed %s", a[1].c_str()); return; }
        Ped s = spawnPed(b->models[0], add(p, scale(fwd, 5)), 0);
        if (s) { TASK::TASK_WANDER_STANDARD(s, 3.0f, 0); g_spawned.push_back(s); }
        FD_INFO("test: stray %s -> %d", a[1].c_str(), s);
    } else if (cmd == "give" && a.size() >= 2) {
        const BreedDef* b = c.breeds.find(a[1]);
        if (!b) return;
        Grade g = a.size() > 2 ? Grade(std::atoi(a[2].c_str())) : Grade::Pedigree;
        DogRecord d = makeDog(*b, c.breeds, g, c.rng, c.chances, {});
        d.name = a.size() > 3 ? a[3] : c.t("dog.default_name");
        d.where = Where::Home;
        int s = c.store.put(d);
        FD_INFO("test: give %s grade %d -> slot %d", a[1].c_str(), int(g), s + 1);
        if (s >= 0) comp.callFromHome(s);
    } else if (cmd == "dogs") {
        for (int i = 0; i < DogStore::kSlots; ++i) {
            if (!c.store.used(i)) continue;
            const auto& d = c.store.at(i);
            FD_INFO("test: slot %d %s %s grade %d where %d bond %d/%.1f stats %d,%d,%d,%d,%d traits %zu", i + 1,
                    d.name.c_str(), d.breed.c_str(), int(d.grade), int(d.where), d.bondLevel, d.bondPoints, d.stats[0],
                    d.stats[1], d.stats[2], d.stats[3], d.stats[4], d.traits.size());
        }
    } else if (cmd == "whistle") { comp.whistle();
    } else if (cmd == "stay") { comp.stay();
    } else if (cmd == "follow") { comp.follow();
    } else if (cmd == "far" && comp.present()) {
        Vector3 to = add(p, scale(fwd, -float(num(1, 40))));
        ENTITY::SET_ENTITY_COORDS(comp.ped(), to.x, to.y, to.z, false, false, false, true);
        TASK::CLEAR_PED_TASKS(comp.ped(), true, true);
        FD_INFO("test: dog moved %.0f m behind", num(1, 40));
    } else if (cmd == "jump" && comp.present()) {
        Vector3 to = add(p, Vector3(600, 0, 0));
        ENTITY::SET_ENTITY_COORDS(comp.ped(), to.x, to.y, to.z, false, false, false, true);
        FD_INFO("test: dog moved 600 m away");
    } else if (cmd == "kill" && comp.present()) {
        ENTITY::SET_ENTITY_HEALTH(comp.ped(), 0, 0);
    } else if (cmd == "bond") {
        comp.addBond(num(1, 10), "test", 0);
    } else if (cmd == "home") {
        comp.sendHome(true);
    } else if (cmd == "call") {
        comp.callFromHome(int(num(1, 1)) - 1);
    } else if (cmd == "money") {
        giveCash(int(num(1, 100) * 100));
    } else if (cmd == "tp" && a.size() >= 4) {
        ENTITY::SET_ENTITY_COORDS(pl, float(num(1, 0)), float(num(2, 0)), float(num(3, 0)), false, false, false, true);
    } else if (cmd == "time") {
        CLOCK::SET_CLOCK_TIME(int(num(1, 12)), 0, 0);
    } else if (cmd == "skipdays") {
        CLOCK::ADD_TO_CLOCK_TIME(int(num(1, 1)) * 24, 0, 0);
    } else if (cmd == "clean") {
        for (Ped& s : g_spawned) deletePed(s);
        g_spawned.clear();
    } else if (cmd == "prey" && a.size() >= 2) {
        // prey <model> [quality 0-2] [metres]: an animal killed by Arthur in front of him (fetch / carry tests)
        Ped s = spawnPed(a[1], add(p, scale(fwd, float(num(3, 25)))), 0);
        if (s) {
            g_spawned.push_back(s);
            PED::_SET_PED_QUALITY(s, int(num(2, 2)));
            ENTITY::SET_ENTITY_HEALTH(s, 0, pl);
        }
        FD_INFO("test: prey %s -> %d", a[1].c_str(), s);
    } else if (cmd == "animal" && a.size() >= 2) {
        // animal <model> [metres] [quality]: a live animal (Huntress, trail, pointing, flush, predators)
        Ped s = spawnPed(a[1], add(p, scale(fwd, float(num(2, 20)))), 0);
        if (s) { g_spawned.push_back(s); if (a.size() > 3) PED::_SET_PED_QUALITY(s, int(num(3, 2))); }
        FD_INFO("test: animal %s -> %d", a[1].c_str(), s);
    } else if (cmd == "foe") {
        // foe [model] [metres]: a hostile man who attacks Arthur (guard, grip, honor shield)
        Ped s = spawnPed(a.size() > 1 ? a[1] : "a_m_m_huntertravelers_cool_01", add(p, scale(fwd, float(num(2, 15)))), 0);
        if (s) { g_spawned.push_back(s); TASK::TASK_COMBAT_PED(s, pl, 0, 16); }
    } else if (cmd == "horseinv") {
        // horseinv [sync]: the horse's pelt stack and how many of each are in its inventory (what the trapper sells)
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        if (!alive(horse)) { FD_INFO("test: no horse"); return; }
        int inv = INVENTORY::_INVENTORY_GET_INVENTORY_ID_FROM_PED(horse);
        for (int i = 0; i < 3; ++i) {
            Hash id = Hash(PED::_GET_PELT_FROM_HORSE(horse, i));
            FD_INFO("test: horse %d inv %d place %d item 0x%08X pelt %d count %d", horse, inv, i, id,
                    id ? int(INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_IS_ANIMAL_PELT(id)) : 0,
                    id ? INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_COUNT_WITH_ITEMID(inv, id, false) : 0);
        }
        if (a.size() > 1 && a[1] == "sync") FD_INFO("test: sync added %d", syncHorsePelts(horse));
    } else if (cmd == "hunt" && a.size() >= 2) {
        // hunt <model> [metres]: a live animal that Arthur shoots himself, as in play (24.11: an animal created dead
        // with 'prey' crashed HuntingGrounds.asi)
        Ped s = spawnPed(a[1], add(p, scale(fwd, float(num(2, 12)))), 0);
        if (s) {
            g_spawned.push_back(s);
            PED::_SET_PED_QUALITY(s, 2);
            TASK::TASK_SHOOT_AT_ENTITY(pl, s, 3000, MISC::GET_HASH_KEY("FIRING_PATTERN_FULL_AUTO"), true);
            int until = now() + 5000;
            while (now() < until && !ENTITY::IS_ENTITY_DEAD(s)) scriptWait(100);
            if (!ENTITY::IS_ENTITY_DEAD(s)) PED::APPLY_DAMAGE_TO_PED(s, 1000, false, 0, pl);
        }
        FD_INFO("test: hunt %s -> %d, dead %d", a[1].c_str(), s, s ? int(ENTITY::IS_ENTITY_DEAD(s)) : -1);
    } else if (cmd == "selfstow") {
        // selfstow: Arthur skins the nearest dead animal (or picks up the nearest pelt) and puts the pelt on his horse
        // with the game's own place-on-mount task - the way he stows a pelt himself. Before and after: the stack, each
        // pelt's count in the horse's inventory and what syncHorsePelts would add. Something to add after the game's
        // own stow = the sync would double that pelt at the trapper (unverified on 24.11: keyboard input died)
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        if (!alive(horse)) { FD_INFO("test: selfstow - no horse"); return; }
        int inv = INVENTORY::_INVENTORY_GET_INVENTORY_ID_FROM_PED(horse);
        auto snapshot = [&](const char* when) {
            std::map<Hash, int> stack;
            for (int i = 0; i < 3; ++i)
                if (Hash id = Hash(PED::_GET_PELT_FROM_HORSE(horse, i))) ++stack[id];
            int wouldAdd = 0;
            for (const auto& [id, n] : stack) {
                int have = INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_COUNT_WITH_ITEMID(inv, id, false);
                bool isPelt = INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_IS_ANIMAL_PELT(id);
                if (isPelt && have < n) wouldAdd += n - have;
                FD_INFO("test: selfstow %s: item 0x%08X on the horse %d, in its inventory %d, pelt %d", when, id, n, have, int(isPelt));
            }
            FD_INFO("test: selfstow %s: %d on the horse, the sync would add %d", when, peltsOnHorse(horse), wouldAdd);
        };
        Entity pelt = PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(pl);
        if (pelt && !ENTITY::_GET_IS_CARRIABLE_PELT(pelt)) pelt = 0;
        if (!pelt) {  // a pelt on the ground first
            static int objs[1024];
            int n = worldGetAllObjects(objs, 1024);
            float best = 15.0f;
            for (int i = 0; i < n; ++i)
                if (ENTITY::_GET_IS_CARRIABLE_PELT(objs[i]) && dist(objs[i], pl) < best) { best = dist(objs[i], pl); pelt = objs[i]; }
            if (pelt) TASK::TASK_PICKUP_CARRIABLE_ENTITY(pl, pelt);
        }
        if (!pelt) {  // else skin the nearest dead animal
            Ped carcass = 0;
            float best = 25.0f;
            int peds[256];
            int n = worldGetAllPeds(peds, 256);
            for (int i = 0; i < n; ++i) {
                Ped e = peds[i];
                if (!exists(e) || !ENTITY::IS_ENTITY_DEAD(e) || !ENTITY::GET_IS_ANIMAL(e) || e == horse || PED::_GET_CARRIER_AS_MOUNT(e)) continue;
                if (dist(e, pl) < best) { best = dist(e, pl); carcass = e; }
            }
            FD_INFO("test: selfstow - skinning carcass %d (%.1f m)", carcass, best);
            if (carcass) TASK::TASK_LOOT_ENTITY(pl, carcass);
        }
        int until = now() + 25000;
        Entity carried = 0;
        while (now() < until) {
            carried = PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(pl);
            if (carried && ENTITY::_GET_IS_CARRIABLE_PELT(carried)) break;
            carried = 0;
            scriptWait(200);
        }
        if (!carried) { FD_INFO("test: selfstow - Arthur is not carrying a pelt"); return; }
        FD_INFO("test: selfstow - Arthur carries pelt %d, item 0x%08X, horse %.1f m", carried, ENTITY::_GET_CARRIABLE_FROM_ENTITY(carried),
                dist(horse, pl));
        snapshot("before");
        int had = peltsOnHorse(horse);
        TASK::TASK_PLACE_CARRIED_ENTITY_ON_MOUNT(pl, carried, horse, 1.0f);
        until = now() + 20000;
        while (now() < until && peltsOnHorse(horse) == had && PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(pl) == carried) scriptWait(200);
        scriptWait(1500);  // the game's inventory write may come a moment after the stack
        snapshot("after");
    } else if (cmd == "hogtied") {
        // hogtied [hate|town|crim] [metres]: a man lying hogtied in front of Arthur who never fought him (Honor Shield
        // 1.4.0: robbing a hogtied O'Driscoll cost -5 on 24.11). hate = a group that hates the player, crim = REL_CRIMINALS
        std::string kind = a.size() > 1 ? a[1] : "hate";
        Ped s = spawnPed("a_m_m_huntertravelers_cool_01", add(p, scale(fwd, float(num(2, 3)))), 0);
        if (s) {
            g_spawned.push_back(s);
            if (kind == "hate") {
                static Hash grp = 0;
                if (!grp) PED::ADD_RELATIONSHIP_GROUP("FD_TEST_HATE", &grp);
                PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, grp, MISC::GET_HASH_KEY("PLAYER"));
                PED::SET_RELATIONSHIP_BETWEEN_GROUPS(5, MISC::GET_HASH_KEY("PLAYER"), grp);
                PED::SET_PED_RELATIONSHIP_GROUP_HASH(s, grp);
            } else if (kind == "crim") {
                PED::SET_PED_RELATIONSHIP_GROUP_HASH(s, MISC::GET_HASH_KEY("REL_CRIMINALS"));
            }
            TASK::TASK_KNOCKED_OUT_AND_HOGTIED(s, 0.0f, 0);
        }
        FD_INFO("test: hogtied %s -> %d, relationship to Arthur %d", kind.c_str(), s, s ? PED::GET_RELATIONSHIP_BETWEEN_PEDS(s, pl) : -1);
    } else if (cmd == "loot") {
        // loot: Arthur loots the nearest hogtied man (the game's own looting task)
        Ped best = 0;
        float bd = 10.0f;
        for (Ped s : g_spawned)
            if (exists(s) && PED::IS_PED_HOGTIED(s) && dist(s, pl) < bd) { bd = dist(s, pl); best = s; }
        if (best) TASK::TASK_LOOT_ENTITY(pl, best);
        FD_INFO("test: loot %d (%.1f m), hogtied %d", best, bd, best ? int(PED::IS_PED_HOGTIED(best)) : -1);
    } else if (cmd == "walker") {
        // walker [metres]: a stranger walking past (shy of strangers, guard L1)
        Ped s = spawnPed("a_m_m_rancher_01", add(p, scale(fwd, float(num(1, 8)))), 0);
        if (s) { g_spawned.push_back(s); TASK::TASK_GO_TO_ENTITY(s, pl, -1, 1.0f, 1.0f, 0, 0); }
    } else if (cmd == "hurt" && comp.present()) {
        int mx = ENTITY::GET_ENTITY_MAX_HEALTH(comp.ped(), false);
        ENTITY::SET_ENTITY_HEALTH(comp.ped(), std::max(1, int(mx * num(1, 10) / 100.0)), 0);
    } else if (cmd == "stable" && a.size() >= 2) {
        // stable <id>: outside that stable, 12 m and 14 m off the dogs for sale in the yard (never inside: the game opens
        // its stable menu)
        KvFile st;
        st.load(fs::join(c.dataDir, "Stables.dat"));
        auto v = st.getFloats(a[1] + (st.has(a[1] + ".yard") ? ".yard" : ".dogs"));
        if (v.size() == 3) ENTITY::SET_ENTITY_COORDS(pl, float(v[0]) + 12, float(v[1]) - 14, float(v[2]) + 1, false, false, false, true);  // well outside the building
    } else if (cmd == "set" && a.size() >= 3 && comp.record()) {
        // set grade|hunger|dirt|bond|scale|preset|maxhp|stat<1-5> <value>; skill <id> <level>; trait <id>; untrait <id>
        DogRecord& d = *comp.record();
        double v = num(2, 0);
        if (a[1] == "grade") d.grade = Grade(int(v));
        else if (a[1] == "hunger") d.hunger = v;
        else if (a[1] == "dirt") d.dirt = v;
        else if (a[1] == "bond") { d.bondPoints = v; }
        else if (a[1] == "scale") { d.scale = int(v); PED::_SET_PED_SCALE(comp.ped(), float(v) / 100.0f); }
        else if (a[1] == "preset") { d.preset = int(v); PED::_EQUIP_META_PED_OUTFIT_PRESET(comp.ped(), int(v), false); }
        else if (a[1].rfind("stat", 0) == 0 && a[1].size() == 5) d.stats[std::clamp(a[1][4] - '1', 0, 4)] = int(v);
        else if (a[1] == "maxhp" && comp.present()) {  // the core's grey track follows max health (D148)
            Ped dp = comp.ped();
            float f = float(ENTITY::GET_ENTITY_HEALTH(dp)) / std::max(1, ENTITY::GET_ENTITY_MAX_HEALTH(dp, false));
            ENTITY::SET_ENTITY_MAX_HEALTH(dp, int(v));
            ENTITY::SET_ENTITY_HEALTH(dp, std::max(1, int(v * f)), 0);
        }
        comp.save();
    } else if (cmd == "skill" && a.size() >= 3 && comp.record()) {
        comp.record()->skillLevel[a[1]] = int(num(2, 1));
        comp.save();
    } else if (cmd == "trait" && a.size() >= 2 && comp.record()) {
        if (!comp.record()->hasTrait(a[1])) comp.record()->traits.push_back(a[1]);
        comp.save();
    } else if (cmd == "untrait" && a.size() >= 2 && comp.record()) {
        auto& tr = comp.record()->traits;
        tr.erase(std::remove(tr.begin(), tr.end(), a[1]), tr.end());
        comp.save();
    } else if (cmd == "presets" && comp.present()) {
        FD_INFO("test: model 0x%08X has %d outfit presets", ENTITY::GET_ENTITY_MODEL(comp.ped()), PED::GET_NUM_META_PED_OUTFITS(comp.ped()));
    } else if (cmd == "dogmodel" && a.size() >= 2) {
        // dogmodel <model> [preset] [metres]: a dog of that model in front of Arthur (look / collar search)
        Ped s = spawnPed(a[1], add(p, scale(fwd, float(num(3, 3)))), 0);
        if (s) {
            g_spawned.push_back(s);
            FD_INFO("test: %s has %d presets", a[1].c_str(), PED::GET_NUM_META_PED_OUTFITS(s));
            if (a.size() > 2) PED::_EQUIP_META_PED_OUTFIT_PRESET(s, int(num(2, 0)), false);
            TASK::TASK_STAND_STILL(s, -1);
        }
    } else if (cmd == "god") {  // god 1: Arthur cannot be hurt and nobody minds him (towns where he is wanted)
        bool on = num(1, 1) != 0;
        PLAYER::SET_PLAYER_INVINCIBLE(PLAYER::PLAYER_ID(), on);
        PLAYER::SET_EVERYONE_IGNORE_PLAYER(PLAYER::PLAYER_ID(), on);
        if (on) PLAYER::SET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID(), 0, false);
    } else if (cmd == "wanted") {
        PLAYER::SET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID(), int(num(1, 1)), false);
    } else if (cmd == "woundexpire" && comp.record()) {
        comp.record()->woundedUntil = unixNow();  // not saved in time, now
    } else if (cmd == "dismount") {
        TASK::TASK_DISMOUNT_ANIMAL(pl, 0, 0, 0, 0, 0);
    } else if (cmd == "revive" && comp.present()) {
        PED::_INCAPACITATED_REVIVE(comp.ped(), comp.ped());
        comp.record()->woundedUntil = 0;
        ENTITY::SET_ENTITY_INVINCIBLE(comp.ped(), false);
        ENTITY::SET_ENTITY_HEALTH(comp.ped(), ENTITY::GET_ENTITY_MAX_HEALTH(comp.ped(), false) / 2, 0);
        TASK::CLEAR_PED_TASKS(comp.ped(), true, true);
    } else if (cmd == "help") {
        helpText(a.size() > 1 ? a[1] : "Frontier Dog test", 5000);
    } else if (cmd == "pos") {
        FD_INFO("test: Arthur %.2f %.2f %.2f, district 0x%08X, town %d, interior %d", p.x, p.y, p.z, districtHash(p), int(inTown(p)), INTERIOR::GET_INTERIOR_FROM_ENTITY(pl));
        if (comp.present()) { Vector3 dp = pos(comp.ped()); FD_INFO("test: dog %.2f %.2f %.2f, %.1f m, in group %d", dp.x, dp.y, dp.z, dist(dp, p), int(PED::IS_PED_GROUP_MEMBER(comp.ped(), PED::GET_PED_GROUP_INDEX(pl), false))); }
    } else if (cmd == "tpg" && a.size() >= 3) {
        // tpg <x> <y>: Arthur to the ground at x,y (collision streamed first), the dog and the horse beside him
        float x = float(num(1, 0)), y = float(num(2, 0)), z = 0;
        STREAMING::REQUEST_COLLISION_AT_COORD(x, y, 100.0f);
        ENTITY::SET_ENTITY_COORDS(pl, x, y, 1000.0f, false, false, false, true);
        ENTITY::FREEZE_ENTITY_POSITION(pl, true);
        bool ok = false;
        for (int i = 0; i < 100 && !ok; ++i) {
            STREAMING::REQUEST_COLLISION_AT_COORD(x, y, 100.0f);
            scriptWait(50);
            ok = MISC::GET_GROUND_Z_FOR_3D_COORD(x, y, 1000.0f, &z, false) && z > -100.0f;
        }
        ENTITY::FREEZE_ENTITY_POSITION(pl, false);
        if (!ok) z = 120.0f;
        ENTITY::SET_ENTITY_COORDS(pl, x, y, z + 0.5f, false, false, false, true);
        FD_INFO("test: tpg %.1f %.1f -> ground %.2f (%s)", x, y, z, ok ? "found" : "NOT found, z 120");
        Vector3 fwd2 = ENTITY::GET_ENTITY_FORWARD_VECTOR(pl);
        if (comp.present()) {
            Vector3 d = add(Vector3(x, y, z + 0.5f), scale(fwd2, -3));
            ENTITY::SET_ENTITY_COORDS(comp.ped(), d.x, d.y, d.z, false, false, false, true);
        }
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        if (alive(horse) && a.size() > 3 && a[3] == "horse") {
            Vector3 h = add(Vector3(x, y, z + 0.5f), Vector3(4, 0, 0));
            ENTITY::SET_ENTITY_COORDS(horse, h.x, h.y, h.z, false, false, false, true);
        }
    } else if (cmd == "horsehere") {
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        if (alive(horse)) {
            Vector3 h = add(p, Vector3(4, 0, 0));
            ENTITY::SET_ENTITY_COORDS(horse, h.x, h.y, h.z, false, false, false, true);
        }
        FD_INFO("test: horse %d %s", horse, alive(horse) ? "moved beside Arthur" : "not found");
    } else if (cmd == "dmg" && comp.present()) {  // real damage (SET_ENTITY_HEALTH ignores Invincible): 'dmg <hp> [shot]'
        Ped dog = comp.ped();
        int hp0 = ENTITY::GET_ENTITY_HEALTH(dog);
        if (a.size() > 2 && a[2] == "shot") {
            Vector3 d = pos(dog);
            MISC::SHOOT_SINGLE_BULLET_BETWEEN_COORDS(d.x + 1.5f, d.y, d.z + 1.2f, d.x, d.y, d.z + 0.3f, int(num(1, 50)), true,
                                                     MISC::GET_HASH_KEY("WEAPON_REVOLVER_CATTLEMAN"), 0, true, false, -1.0f, true);
        } else {
            PED::APPLY_DAMAGE_TO_PED(dog, int(num(1, 50)), false, 0, 0);
        }
        FD_INFO("test: damage %d to the dog, health %d -> %d (max %d)", int(num(1, 50)), hp0, ENTITY::GET_ENTITY_HEALTH(dog),
                ENTITY::GET_ENTITY_MAX_HEALTH(dog, false));
    } else if (cmd == "hp" && comp.present()) {
        FD_INFO("test: dog health %d / %d, invincible ini %d, can be damaged %d", ENTITY::GET_ENTITY_HEALTH(comp.ped()),
                ENTITY::GET_ENTITY_MAX_HEALTH(comp.ped(), false), int(c.ini.getBool("Invincible", false)),
                int(ENTITY::_GET_ENTITY_CAN_BE_DAMAGED(comp.ped())));
    } else if (cmd == "inv" && a.size() >= 2 && comp.present()) {  // 'inv <0|1>' SET_ENTITY_INVINCIBLE on the dog
        ENTITY::SET_ENTITY_INVINCIBLE(comp.ped(), num(1, 1) != 0);
        FD_INFO("test: dog invincible %d", int(num(1, 1)));
    } else if (cmd == "candmg" && a.size() >= 2 && comp.present()) {  // 'candmg <0|1>' SET_ENTITY_CAN_BE_DAMAGED
        ENTITY::SET_ENTITY_CAN_BE_DAMAGED(comp.ped(), num(1, 1) != 0);
        FD_INFO("test: dog can be damaged %d", int(num(1, 1)));
    } else if (cmd == "objective" && a.size() >= 2) {
        // 'objective <text>': the mid-screen objective line from a plain C string, as Rustling sets it (Telegraph 1.0.2
        // translates _UILOG_SET_CACHED_OBJECTIVE); logs what the game keeps
        static std::string text;
        text = a[1];
        for (size_t i = 2; i < a.size(); ++i) text += " " + a[i];
        UILOG::_UILOG_CLEAR_HAS_DISPLAYED_CACHED_OBJECTIVE();
        UILOG::_UILOG_SET_CACHED_OBJECTIVE(text.c_str());
        UILOG::_UILOG_PRINT_CACHED_OBJECTIVE();
        const char* kept = UILOG::_UILOG_GET_CACHED_OBJECTIVE();
        FD_INFO("test: objective set '%s', the game keeps '%s'", text.c_str(), kept ? kept : "(null)");
    } else if (cmd == "scen") {
        delivery::testScenario();
    } else if (cmd == "prompts") {
        testListPrompts();
    } else if (cmd == "press" && a.size() >= 2) {
        std::string part = a[1];
        for (size_t i = 2; i < a.size(); ++i) part += " " + a[i];
        testPress(part);
    } else if (cmd == "card") {
        // card [off|2]: the companion's card without a focus (stays until 'card off'); 2 = page two
        if (a.size() > 1 && a[1] == "off") ui::cardHide();
        else if (comp.record()) { ui::cardShow(*comp.record(), 0); ui::cardPage(a.size() > 1 && a[1] == "2" ? 2 : 1); }
    } else if (cmd == "infoctl" && a.size() >= 2) {
        ui::cardInfoControl(a[1]);
    } else if (cmd == "ctl") {
        g_ctlUntil = now() + int(num(1, 20) * 1000);
    } else if (cmd == "watch") {
        g_watchUntil = now() + int(num(1, 20) * 1000);
    } else if (cmd == "slots") {
        // every HUD visibility slot state 0..99 in one line (which ones change when the radar expands)
        std::string line;
        for (int i = 0; i < 100; ++i) line += std::to_string(i) + ":" + std::to_string(HUD::_GET_HUD_VISIBILITY_SLOT_STATE(i)) + " ";
        FD_INFO("test: slots %s", line.c_str());
    } else if (cmd == "comps") {
        // comps [dog]: the metaped components of the last spawned ped (or the companion) - collar search
        Ped t = (a.size() > 1 && comp.present()) ? comp.ped() : (g_spawned.empty() ? 0 : g_spawned.back());
        if (!t) return;
        int n = PED::_GET_NUM_COMPONENTS_IN_PED(t);
        FD_INFO("test: comps of 0x%08X: %d", ENTITY::GET_ENTITY_MODEL(t), n);
        for (int i = 0; i < n; ++i) {
            BOOL st = 0;
            Hash wear = 0, d = 0, al = 0, nm = 0, mt = 0;
            Hash item = PED::_GET_SHOP_ITEM_COMPONENT_AT_INDEX(t, i, true, &st, &wear);
            Hash cat = PED::_GET_PED_COMPONENT_CATEGORY_BY_INDEX(t, i);
            PED::GET_META_PED_ASSET_GUIDS(t, i, &d, &al, &nm, &mt);
            FD_INFO("test:  %d item 0x%08X cat 0x%08X drawable 0x%08X albedo 0x%08X", i, item, cat, d, al);
        }
    } else if (cmd == "dogcam") {
        // dogcam [off|dist height side]: a script camera on the companion's head and neck (collar shots); off = back
        static Cam cam = 0;
        if (cam) {
            CAMERA::RENDER_SCRIPT_CAMS(false, false, 0, true, true, 0);
            CAMERA::DESTROY_CAM(cam, false);
            cam = 0;
        }
        if ((a.size() > 1 && a[1] == "off") || !comp.present()) return;
        Ped d = comp.ped();
        float h = ENTITY::GET_ENTITY_HEADING(d) * 3.14159265f / 180.0f;
        Vector3 dfwd(-std::sin(h), std::cos(h), 0), side(std::cos(h), std::sin(h), 0);
        Vector3 at = add(add(pos(d), scale(dfwd, float(num(1, 1.1)))), scale(side, float(num(3, 0.3))));
        at.z += float(num(2, 0.35));
        // aimed by hand (POINT_CAM_AT_ENTITY left the dog at the frame's edge): at the neck, 0.25 m above the origin
        Vector3 look = pos(d);
        look.z += 0.25f;
        float dx = look.x - at.x, dy = look.y - at.y, dz = look.z - at.z;
        float yaw = std::atan2(-dx, dy) * 180.0f / 3.14159265f;
        float pitch = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180.0f / 3.14159265f;
        cam = CAMERA::CREATE_CAM_WITH_PARAMS("DEFAULT_SCRIPTED_CAMERA", at.x, at.y, at.z, pitch, 0, yaw, float(num(4, 35)), false, 2);
        CAMERA::SET_CAM_ACTIVE(cam, true);
        CAMERA::RENDER_SCRIPT_CAMS(true, false, 0, true, true, 0);
    } else if (cmd == "camat" && a.size() >= 7) {
        // camat <x y z> <tx ty tz> [fov]: a script camera at x,y,z looking at tx,ty,tz (spot checks); 'dogcam off' ends it
        CAMERA::RENDER_SCRIPT_CAMS(false, false, 0, true, true, 0);
        CAMERA::DESTROY_ALL_CAMS(false);
        float x = float(num(1, 0)), y = float(num(2, 0)), z = float(num(3, 0));
        float dx = float(num(4, 0)) - x, dy = float(num(5, 0)) - y, dz = float(num(6, 0)) - z;
        float yaw = std::atan2(-dx, dy) * 180.0f / 3.14159265f;
        float pitch = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 180.0f / 3.14159265f;
        Cam c = CAMERA::CREATE_CAM_WITH_PARAMS("DEFAULT_SCRIPTED_CAMERA", x, y, z, pitch, 0, yaw, float(num(7, 50)), false, 2);
        CAMERA::SET_CAM_ACTIVE(c, true);
        CAMERA::RENDER_SCRIPT_CAMS(true, false, 0, true, true, 0);
    } else if (cmd == "trader") {
        // trader [x y z [heading]]: where the dog trader stands; with coordinates he is moved there (placing him by eye)
        Ped t = stables::trader();
        if (!t) { FD_INFO("test: no trader"); return; }
        if (a.size() >= 4) {
            ENTITY::SET_ENTITY_COORDS(t, float(num(1, 0)), float(num(2, 0)), float(num(3, 0)), false, false, false, true);
            if (a.size() >= 5) ENTITY::SET_ENTITY_HEADING(t, float(num(4, 0)));
            TASK::TASK_STAND_STILL(t, -1);
        }
        Vector3 tp = pos(t);
        FD_INFO("test: trader %.2f %.2f %.2f heading %.0f, %.1f m from Arthur", tp.x, tp.y, tp.z, ENTITY::GET_ENTITY_HEADING(t), dist(t, pl));
    } else if (cmd == "peltstow") {
        // peltstow [clear]: the nearest pelt on the ground goes into the pelt stack of Arthur's horse the game's way
        // (_SET_PELT_FOR_HORSE), then the stack is listed; 'clear' empties it (17.11: the pelt laid like a carcass)
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        if (!alive(horse)) { FD_INFO("test: no horse"); return; }
        if (a.size() > 1 && a[1] == "clear") {
            for (int i = 0; i < 8; ++i) if (int id = PED::_GET_PELT_FROM_HORSE(horse, i)) PED::_CLEAR_PELT_FROM_HORSE(horse, id);
        } else {
            Entity best = 0;
            float bd = 15.0f;
            const int kMax = 256;
            int objs[kMax];
            int n = worldGetAllObjects(objs, kMax);
            for (int i = 0; i < n; ++i)
                if (ENTITY::_GET_IS_CARRIABLE_PELT(objs[i]) && dist(objs[i], pl) < bd) { bd = dist(objs[i], pl); best = objs[i]; }
            FD_INFO("test: pelt %d %.1f m away", best, bd);
            if (best) PED::_SET_PELT_FOR_HORSE(horse, best);
        }
        for (int i = 0; i < 8; ++i) FD_INFO("test: horse pelt %d = %d", i, PED::_GET_PELT_FROM_HORSE(horse, i));
    } else if ((cmd == "walkto" || cmd == "walkstraight") && a.size() >= 3) {
        // walkto <x> <y> [speed 1 walk .. 3 sprint] [z]: Arthur walks there on the navmesh like a player would (a stable
        // hand's menu reached on foot, not by a teleport into the building); walkstraight ignores the navmesh
        float x = float(num(1, 0)), y = float(num(2, 0)), z = a.size() >= 5 ? float(num(4, p.z)) : p.z;
        if (cmd == "walkto") TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(pl, x, y, z, float(num(3, 1)), -1, 0.5f, 0, 40000.0f);
        else TASK::TASK_GO_STRAIGHT_TO_COORD(pl, x, y, z, float(num(3, 1)), -1, 40000.0f, 0.5f, 0);
        FD_INFO("test: %s to %.1f %.1f %.1f", cmd.c_str(), x, y, z);
    } else if (cmd == "face" && a.size() >= 3) {
        TASK::TASK_TURN_PED_TO_FACE_COORD(pl, float(num(1, 0)), float(num(2, 0)), p.z, 2000);
    } else if (cmd == "camfront") {
        CAMERA::SET_GAMEPLAY_CAM_RELATIVE_HEADING(0.0f, 1.0f);
        CAMERA::SET_GAMEPLAY_CAM_RELATIVE_PITCH(-8.0f, 1.0f);
    } else if (cmd == "stop") {
        TASK::CLEAR_PED_TASKS(pl, true, true);
    } else if (cmd == "peltinfo" || cmd == "peltstow") {
        // pelts on the horse (18.11: the dog left the pelt by the horse - _SET_PELT_FOR_HORSE takes the game's pelt id,
        // not the pelt object). peltinfo: what the game says about the nearest pelt and the horse's stack;
        // peltstow <way>: 1 = by inventory item from the carcass + albedo, then the object goes; 2 = the same, p4 true;
        // 3 = _SET_PELT_FOR_HORSE with the albedo hash; 4 = TASK_CARRIABLE <slot a[2]> with the pelt's own config
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        Entity pelt = PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(pl);
        if (!pelt || !ENTITY::_GET_IS_CARRIABLE_PELT(pelt)) {
            pelt = 0;
            static int objs[1024];
            int n = worldGetAllObjects(objs, 1024);
            float best = 15.0f;
            for (int i = 0; i < n; ++i)
                if (ENTITY::_GET_IS_CARRIABLE_PELT(objs[i]) && game::dist(objs[i], pl) < best) {
                    best = game::dist(objs[i], pl);
                    pelt = objs[i];
                }
        }
        auto num2 = [](unsigned long long v, bool hex) {
            char b[24];
            std::snprintf(b, sizeof b, hex ? "%llX " : "%lld ", v);
            return std::string(b);
        };
        auto stack = [&](const char* when) {
            std::string ids;
            for (int i = 0; i < 8; ++i) ids += num2((unsigned long long)(long long)PED::_GET_PELT_FROM_HORSE(horse, i), false);
            alignas(8) uint64_t skins[40] = {};
            skins[0] = 16;
            int ns = PED::_GET_CARRIED_PELT_SKINS(horse, reinterpret_cast<Any*>(skins));
            std::string dump;
            for (int i = 0; i < 12; ++i) dump += num2(skins[i], true);
            FD_INFO("test: %s horse %d stack ids [%s] skins %d [%s]", when, horse, ids.c_str(), ns, dump.c_str());
        };
        stack("before");
        if (!pelt) {
            FD_INFO("test: no pelt within 15 m and none carried");
        } else {
            Hash albedo = 0;
            ENTITY::_GET_ENTITY_ALBEDO(pelt, &albedo);
            Ped carcass = ENTITY::_GET_CARCASS_FROM_PELT(pelt);
            int clean = carcass ? PED::_GET_PED_DAMAGE_CLEANLINESS(carcass) : -1, q = carcass ? PED::_GET_PED_QUALITY(carcass) : -1;
            alignas(8) uint64_t items[32] = {};
            items[0] = 8;
            int ni = carcass ? PED::_COMPUTE_SATCHEL_ITEM_FOR_PED_CARCASS(reinterpret_cast<Any*>(items), carcass, clean, q) : -1;
            std::string dump;
            for (int i = 0; i < 12; ++i) dump += num2(items[i], true);
            FD_INFO("test: pelt %d model 0x%08X albedo 0x%08X config 0x%08X carriable 0x%08X | carcass %d model 0x%08X clean %d quality %d"
                    " | satchel item -> %d (0x%08X) [%s]",
                    pelt, ENTITY::GET_ENTITY_MODEL(pelt), albedo, ENTITY::_GET_ENTITY_CARRY_CONFIG(pelt),
                    ENTITY::_GET_CARRIABLE_FROM_ENTITY(pelt), carcass, carcass ? ENTITY::GET_ENTITY_MODEL(carcass) : 0, clean, q,
                    ni, uint32_t(ni), dump.c_str());
            if (cmd == "peltstow" && alive(horse)) {
                int way = int(num(1, 1));
                Hash item = 0;  // the first thing in the array that looks like a hash, else the return value
                for (int i = 0; i < 12 && !item; ++i)
                    if (items[i] > 0xFFFF && items[i] <= 0xFFFFFFFFull) item = Hash(items[i]);
                if (!item && ni > 0xFFFF) item = Hash(ni);
                if (way == 1 || way == 2) {
                    PED::_SET_PELT_FOR_HORSE_BY_INVENTORY_ITEM(horse, item, albedo, 0, way == 2);
                    Entity e = pelt;
                    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, true, true);
                    ENTITY::DELETE_ENTITY(&e);
                } else if (way == 3) {
                    PED::_SET_PELT_FOR_HORSE(horse, int(albedo));
                } else if (way == 4) {
                    Hash cfg = ENTITY::_GET_ENTITY_CARRY_CONFIG(pelt);
                    TASK::_REQUEST_CARRIABLE_CONFIG(cfg);
                    for (int i = 0; i < 50 && !TASK::_HAS_REQUESTED_CARRIABLE_CONFIG_LOADED(cfg); ++i) scriptWait(20);
                    TASK::TASK_CARRIABLE(pelt, cfg, horse, int(num(2, 1)), int(num(3, 0)));
                }
                FD_INFO("test: peltstow way %d, item 0x%08X", way, item);
                scriptWait(1500);
                stack("after");
            }
        }
    } else if (cmd == "dogahead" && comp.present()) {
        // dogahead <metres>: the companion stands that far in front of Arthur, facing him (focus range tests)
        Ped d = comp.ped();
        Vector3 at = add(p, scale(fwd, float(num(1, 10))));
        float gz;
        if (MISC::GET_GROUND_Z_FOR_3D_COORD(at.x, at.y, at.z + 5, &gz, false)) at.z = gz;
        ENTITY::SET_ENTITY_COORDS(d, at.x, at.y, at.z, false, false, false, true);
        ENTITY::SET_ENTITY_HEADING(d, ENTITY::GET_ENTITY_HEADING(pl) + 180.0f);
        TASK::TASK_STAND_STILL(d, 30000);
        FD_INFO("test: dog %.0f m ahead", num(1, 10));
    } else if (cmd == "aimpitch" && comp.present()) {
        // aimpitch: the gameplay camera straight at the companion (the reticle on it), relative to Arthur's heading
        Vector3 cam = CAMERA::GET_GAMEPLAY_CAM_COORD(), d = pos(comp.ped());
        float dx = d.x - cam.x, dy = d.y - cam.y, dz = d.z + 0.4f - cam.z;
        float pitch = std::atan2(dz, std::sqrt(dx * dx + dy * dy)) * 57.2958f;
        float head = std::atan2(-dx, dy) * 57.2958f - ENTITY::GET_ENTITY_HEADING(pl);
        CAMERA::SET_GAMEPLAY_CAM_RELATIVE_HEADING(head, 1.0f);
        CAMERA::SET_GAMEPLAY_CAM_RELATIVE_PITCH(pitch, 1.0f);
        FD_INFO("test: camera at the dog: heading %.1f, pitch %.1f", head, pitch);
    } else if (cmd == "aimlog") {
        g_aimUntil = now() + int(num(1, 10) * 1000);
    } else if (cmd == "lockon") {
        PLAYER::SET_PLAYER_LOCKON_RANGE_OVERRIDE(PLAYER::PLAYER_ID(), float(num(1, 0)));
        FD_INFO("test: lockon range override %.0f", num(1, 0));
    } else if (cmd == "dogfront" && comp.present()) {
        // dogfront: the companion stands 2.5 m in front of Arthur, facing him (screenshots of the dog)
        Ped d = comp.ped();
        Vector3 at = add(p, scale(fwd, 2.5f));
        ENTITY::SET_ENTITY_COORDS(d, at.x, at.y, at.z, false, false, false, true);
        ENTITY::SET_ENTITY_HEADING(d, ENTITY::GET_ENTITY_HEADING(pl) + 180.0f);
        TASK::TASK_STAND_STILL(d, 15000);
        ENTITY::FREEZE_ENTITY_POSITION(d, a.size() > 1 && a[1] == "freeze");  // 'dogfront freeze': it stays for shots
    } else if (cmd == "tagcopy" && a.size() >= 2) {
        // tagcopy <index> [prev]: copy one metaped component (drawable, textures, tint) of the last spawned ped onto the
        // companion, or with 'prev' onto the ped spawned before it (a collar from a hound onto another dog)
        if (g_spawned.empty()) return;
        Ped src = g_spawned.back();
        Ped dst = (a.size() > 2 && g_spawned.size() > 1) ? g_spawned[g_spawned.size() - 2] : comp.ped();
        if (!dst) return;
        int i = int(num(1, 0));
        Hash d = 0, al = 0, nm = 0, mt = 0, pal = 0;
        int t0 = 0, t1 = 0, t2 = 0;
        PED::GET_META_PED_ASSET_GUIDS(src, i, &d, &al, &nm, &mt);
        PED::GET_META_PED_ASSET_TINT(src, i, &pal, &t0, &t1, &t2);
        PED::_SET_META_PED_TAG(dst, d, al, nm, mt, pal, t0, t1, t2);
        PED::_UPDATE_PED_VARIATION(dst, false, true, true, true, false);
        FD_INFO("test: tagcopy %d: drawable 0x%08X albedo 0x%08X normal 0x%08X material 0x%08X palette 0x%08X tint %d,%d,%d -> 0x%08X",
                i, d, al, nm, mt, pal, t0, t1, t2, ENTITY::GET_ENTITY_MODEL(dst));
    } else if (cmd == "applycomp" && a.size() >= 2) {
        // applycomp <hash> [dog]: put a shop item (metaped component) on the last spawned ped or the companion
        Ped t = (a.size() > 2 && comp.present()) ? comp.ped() : (g_spawned.empty() ? 0 : g_spawned.back());
        if (!t) return;
        Hash h = Hash(std::stoul(a[1], nullptr, 0));
        PED::_APPLY_SHOP_ITEM_TO_PED(t, h, true, false, false);
        PED::_UPDATE_PED_VARIATION(t, false, true, true, true, false);
        FD_INFO("test: applycomp 0x%08X on 0x%08X", h, ENTITY::GET_ENTITY_MODEL(t));
    } else if (cmd == "hardstray") {
        strays::hardNow();
    } else if (cmd == "visitor") {
        strays::visitorNow();
    } else if (cmd == "shoot") {
        // shoot [ms]: Arthur fires his gun into the air ahead (wary strays run from shots)
        TASK::TASK_SHOOT_AT_COORD(pl, p.x + fwd.x * 10, p.y + fwd.y * 10, p.z + 12, int(num(1, 2000)),
                                  MISC::GET_HASH_KEY("FIRING_PATTERN_FULL_AUTO"), 0);
    } else if (cmd == "killfoe") {
        // killfoe: Arthur kills the last 'foe', man or animal (honor shield and its hidden honor flash)
        for (auto it = g_spawned.rbegin(); it != g_spawned.rend(); ++it)
            if (alive(*it) && PED::IS_PED_IN_COMBAT(*it, pl)) {
                PED::APPLY_DAMAGE_TO_PED(*it, 1000, false, 0, pl);
                FD_INFO("test: killfoe %d", *it);
                break;
            }
    } else if (cmd == "objs") {
        // objs [metres]: every object within the radius with its model (looking for the herb plants of the world)
        int objs[1024];
        int n = worldGetAllObjects(objs, 1024), k = 0;
        float r = float(num(1, 6));
        for (int i = 0; i < n; ++i) {
            if (!ENTITY::DOES_ENTITY_EXIST(objs[i]) || dist(objs[i], pl) > r) continue;
            Vector3 o = pos(objs[i]);
            FD_INFO("test: obj %d model 0x%08X at %.1f %.1f %.1f, %.1f m, pickup %d", objs[i], ENTITY::GET_ENTITY_MODEL(objs[i]),
                    o.x, o.y, o.z, dist(objs[i], pl), int(OBJECT::DOES_PICKUP_EXIST(objs[i])));
            ++k;
        }
        FD_INFO("test: %d objects within %.0f m (of %d)", k, r, n);
    } else if (cmd == "killall") {
        // killall [metres]: Arthur kills every man fighting him within the radius (clearing a Bandit Hideouts camp)
        int peds[256];
        int n = worldGetAllPeds(peds, 256), k = 0;
        float r = float(num(1, 80));
        for (int i = 0; i < n; ++i) {
            Ped q = peds[i];
            if (q == pl || q == comp.ped() || !alive(q) || !PED::IS_PED_HUMAN(q) || dist(q, pl) > r) continue;
            if (!PED::IS_PED_IN_COMBAT(q, pl)) continue;
            PED::APPLY_DAMAGE_TO_PED(q, 1000, false, 0, pl);
            ++k;
        }
        FD_INFO("test: killall %d within %.0f m", k, r);
    } else if (cmd == "beggar") {
        // beggar: a begging mutt by the nearest butcher/saloon within strays.beggar_radius (Arthur must be in town)
        strays::beggarNow();
    } else if (cmd == "coreshow") {
        // coreshow [seconds]: the dog's core on screen whatever the game's cores do
        ui::coreForce(now() + int(num(1, 20) * 1000));
    } else if (cmd == "sprites" && a.size() >= 3) {
        // sprites <seconds> dict:name ...: draws those textures in a row on black (checks streamed add-on .ytd)
        g_sprites.clear();
        for (size_t i = 2; i < a.size(); ++i) {
            auto p = a[i].find(':');
            if (p == std::string::npos) continue;
            g_sprites.push_back({a[i].substr(0, p), a[i].substr(p + 1)});
            const char* d = g_sprites.back().first.c_str();
            if (!TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(d)) TXD::REQUEST_STREAMED_TEXTURE_DICT(d, false);
            FD_INFO("test: sprite %s exists %d loaded %d", a[i].c_str(), int(TXD::DOES_STREAMED_TEXTURE_DICT_EXIST(d)),
                    int(TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(d)));
        }
        g_spritesUntil = now() + int(num(1, 20) * 1000);
    } else if (cmd == "hud") {
        // the game's HUD slot states (39 Arthur's cores, 42 horse's, 17 radar, 3 cash) and the hide flags
        FD_INFO("test: hud player %d horse %d radar %d cash %d hidden %d radarHidden %d", HUD::_GET_HUD_VISIBILITY_SLOT_STATE(39),
                HUD::_GET_HUD_VISIBILITY_SLOT_STATE(42), HUD::_GET_HUD_VISIBILITY_SLOT_STATE(17),
                HUD::_GET_HUD_VISIBILITY_SLOT_STATE(3), int(HUD::IS_HUD_HIDDEN()), int(HUD::IS_RADAR_HIDDEN()));
    } else if (cmd == "strays") {
        strays::spawnNow();
    } else if (cmd == "item" && a.size() >= 2) {
        // item <KEY>: is the key in the item database, how many in the satchel
        FD_INFO("test: item %s known %d, count %d", a[1].c_str(), int(itemKnown(a[1].c_str())), itemCount(a[1].c_str()));
    } else if (cmd == "giveitem" && a.size() >= 2) {
        bool ok = giveItem(a[1].c_str(), int(num(2, 1)));
        FD_INFO("test: giveitem %s -> %d, count now %d", a[1].c_str(), int(ok), itemCount(a[1].c_str()));
    } else if (cmd == "stownew" && a.size() >= 2) {
        // stownew <model> [slot] [flags]: exactly the game's stable code - a dead animal created 1 m off the horse and given
        // to it with TASK_CARRIABLE in the same frame (net_stable_mount.c func_120)
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        Vector3 at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(horse, 0.0f, -1.0f, 0.0f);
        Hash model = MISC::GET_HASH_KEY(a[1].c_str());
        if (alive(horse) && loadModel(model, 3000)) {
            Ped c = PED::CREATE_PED(model, at.x, at.y, at.z, 0.0f, false, false, false, false);
            STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
            ENTITY::SET_ENTITY_HEALTH(c, 0, 0);
            PED::SET_PED_CONFIG_FLAG(c, 517, true);
            int slot = int(num(2, -1));
            if (slot < 0) slot = hunting::freeHorseSlot(horse);
            hunting::stowOnHorse(c, horse, slot, int(num(3, 256)), false);
            g_spawned.push_back(c);
        }
    } else if (cmd == "horseslots") {
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        for (int slot = 0; slot < 12; ++slot) {
            CarriedInfo ci;
            if (carriedInSlot(horse, slot, ci) && (ci.entity || ci.model))
                FD_INFO("test: horse slot %d: entity %d model 0x%08X config 0x%08X", slot, ci.entity, ci.model, ci.config);
        }
        FD_INFO("test: horse %d, free slot %d", horse, hunting::freeHorseSlot(horse));
    } else if (cmd == "stow" || cmd == "stowhelper") {
        // stow [slot|-1 free] [flags 256] [own|dead]: the nearest dead animal onto Arthur's horse (TASK_CARRIABLE)
        // stowhelper: a man picks it up and puts it on the horse with the game's own place-on-mount task
        Ped horse = PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        Ped carcass = 0;
        float best = 25.0f;
        int peds[256];
        int n = worldGetAllPeds(peds, 256);
        for (int i = 0; i < n; ++i) {
            Ped e = peds[i];
            if (!exists(e) || !ENTITY::IS_ENTITY_DEAD(e) || !ENTITY::GET_IS_ANIMAL(e) || e == horse) continue;
            if (PED::_GET_CARRIER_AS_MOUNT(e)) continue;
            float d = dist(e, pl);
            if (d < best) { best = d; carcass = e; }
        }
        if (!alive(horse) || !carcass) {
            FD_INFO("test: %s - horse %d, carcass %d", cmd.c_str(), horse, carcass);
        } else if (cmd == "stow") {
            // stow [slot] [flags] [own|dead] [near]: 'near' first puts it 1 m behind the horse, as the game's stable code does
            int slot = int(num(1, -1));
            if (slot < 0) slot = hunting::freeHorseSlot(horse);
            if (a.size() > 4 && a[4] == "near") {
                Vector3 at = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(horse, 0.0f, -1.0f, 0.0f);
                ENTITY::SET_ENTITY_COORDS(carcass, at.x, at.y, at.z, false, false, false, true);
                scriptWait(0);
            }
            FD_INFO("test: carcass %d at %.1f m from the horse", carcass, dist(carcass, horse));
            hunting::stowOnHorse(carcass, horse, slot, int(num(2, 256)), a.size() > 3 && a[3] == "dead");
        } else {
            Vector3 cp = pos(carcass);
            Ped man = spawnPed("a_m_m_rancher_01", add(cp, Vector3(1.0f, 0, 0)), 0);
            if (man) {
                g_spawned.push_back(man);
                PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(man, true);
                TASK::TASK_PICKUP_CARRIABLE_ENTITY(man, carcass);
                int until = now() + 10000;
                while (now() < until && PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(man) != carcass) scriptWait(100);
                bool carrying = PED::_GET_FIRST_ENTITY_PED_IS_CARRYING(man) == carcass;
                FD_INFO("test: helper carrying it: %d", int(carrying));
                if (carrying) {
                    TASK::TASK_PLACE_CARRIED_ENTITY_ON_MOUNT(man, carcass, horse, 1.0f);
                    until = now() + 15000;
                    while (now() < until && !PED::_GET_CARRIER_AS_MOUNT(carcass)) scriptWait(100);
                    FD_INFO("test: carcass carried by the horse: %d", int(PED::_GET_CARRIER_AS_MOUNT(carcass) == horse));
                }
            }
        }
    } else if (cmd == "deliver") {
        FD_INFO("test: delivery order -> %d", int(delivery::order(int(num(1, 2)) - 1, a.size() > 2 ? a[2].c_str() : "telegraph")));
    } else {
        FD_INFO("test: unknown command %s", cmd.c_str());
        return;
    }
    FD_INFO("test: done %s", cmd.c_str());
}

// 'ctl <s>': for s seconds logs every candidate control pressed (which one opens the game's animal "Show info")
static void probeControls() {
    static const char* kNames[] = {
        "INPUT_INTERACT_LOCKON_DETAILS_ANIMAL", "INPUT_INTERACT_LOCKON_TARGET_INFO", "INPUT_INTERACT_LOCKON_ANIMAL",
        "INPUT_INTERACT_LOCKON_STUDY_BINOCULARS", "INPUT_INTERACT_LOCKON_TRACK_ANIMAL", "INPUT_INTERACT_LOCKON_CALL_ANIMAL",
        "INPUT_INTERACT_LOCKON_POS", "INPUT_INTERACT_LOCKON_NEG", "INPUT_INTERACT_LOCKON_ROB", "INPUT_INTERACT_LOCKON_Y",
        "INPUT_INTERACT_LOCKON_A", "INPUT_INTERACT_ANIMAL", "INPUT_INTERACT_HORSE_BRUSH", "INPUT_COVER", "INPUT_CONTEXT_RB",
        "INPUT_CONTEXT_LB", "INPUT_CONTEXT_LT", "INPUT_CONTEXT_RT", "INPUT_CONTEXT_A", "INPUT_CONTEXT_B", "INPUT_CONTEXT_X",
        "INPUT_CONTEXT_Y", "INPUT_FRONTEND_RB", "INPUT_FRONTEND_LB", "INPUT_INTERACT_OPTION1", "INPUT_INTERACT_OPTION2",
        "INPUT_LOOK_BEHIND", "INPUT_REVEAL_HUD", "INPUT_HUD_SPECIAL", "INPUT_SPECIAL_ABILITY", "INPUT_FOCUS_CAM",
        "INPUT_INSPECT_ZOOM", "INPUT_SHOP_INSPECT", "INPUT_PLAYER_MENU", "INPUT_QUICK_USE_ITEM", "INPUT_INTERACT_NEG",
        "INPUT_INTERACT_POS", "INPUT_INTERACT_LEAD_ANIMAL", "INPUT_INTERACT_WILD_ANIMAL", "INPUT_INTERACT_HIT_CARRIABLE"};
    for (const char* n : kNames) {
        Hash h = MISC::GET_HASH_KEY(n);
        if (PAD::IS_CONTROL_JUST_PRESSED(0, h) || PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0, h)) {
            Entity focus = 0;
            PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(PLAYER::PLAYER_ID(), &focus, false, false);
            FD_INFO("test: control %s pressed (focus %d, dog %d)", n, focus, companion().ped());
        }
    }
}

// 'watch <s>': every change of the HUD slots that move cores and of the radar mode Care Overhaul reads from the
// game's memory (RDR2.exe + 0x3ef1490 and its fallbacks, build 1491.50), for s seconds
static int readGameInt(uintptr_t off) {
    HMODULE m = GetModuleHandleW(L"RDR2.exe");
    if (!m) return -99;
    auto p = reinterpret_cast<const int*>(reinterpret_cast<uintptr_t>(m) + off);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return -98;
    return *p;
}
static void watchHud() {
    static std::string last;
    std::string s = "p" + std::to_string(HUD::_GET_HUD_VISIBILITY_SLOT_STATE(39)) + " h" +
                    std::to_string(HUD::_GET_HUD_VISIBILITY_SLOT_STATE(42)) + " r" +
                    std::to_string(HUD::_GET_HUD_VISIBILITY_SLOT_STATE(17)) + " s14:" +
                    std::to_string(HUD::_GET_HUD_VISIBILITY_SLOT_STATE(14)) + " mem " + std::to_string(readGameInt(0x3ef1490)) + "/" +
                    std::to_string(readGameInt(0x3eb9df0)) + "/" + std::to_string(readGameInt(0x3ed0180)) + "/" +
                    std::to_string(readGameInt(0x3ef1448)) + " radarHidden " + std::to_string(int(HUD::IS_RADAR_HIDDEN()));
    if (s != last) FD_INFO("test: watch %s", s.c_str());
    last = s;
    // every other HUD slot that changes (which one tells the HUD is revealed / the radar grown)
    static int prev[100];
    static bool init = false;
    std::string diff;
    for (int i = 0; i < 100; ++i) {
        int v = HUD::_GET_HUD_VISIBILITY_SLOT_STATE(i);
        if (init && v != prev[i]) diff += std::to_string(i) + ":" + std::to_string(prev[i]) + ">" + std::to_string(v) + " ";
        prev[i] = v;
    }
    init = true;
    if (!diff.empty()) FD_INFO("test: slots changed %s", diff.c_str());
}

static void drawSprites() {
    float w = 0.06f, h = w * 16 / 9, x = 0.04f, y = 0.30f;
    for (auto& [d, n] : g_sprites) {
        ui::rect(x - 0.004f, y - 0.007f, w + 0.008f, h + 0.014f, 0, 0, 0, 255);
        ui::sprite(d.c_str(), n.c_str(), x, y, w, h, 255, 255, 255, 255);
        x += w + 0.016f;
    }
}

static void aimLog() {
    auto& comp = companion();
    if (!comp.present()) return;
    Player id = PLAYER::PLAYER_ID();
    Entity inter = 0, target = 0, freeAim = 0;
    PLAYER::GET_PLAYER_INTERACTION_TARGET_ENTITY(id, &inter, false, false);
    PLAYER::GET_PLAYER_TARGET_ENTITY(id, &target);
    PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(id, &freeAim);
    Ped d = comp.ped();
    static const Hash kAim = MISC::GET_HASH_KEY("INPUT_AIM");
    FD_INFO("aim: %.1f m, aimbtn %d, afar %d, interaction %d, target %d, free-aim %d, targetting %d, free aiming %d", dist(d, player()),
            int(PAD::IS_CONTROL_PRESSED(0, kAim) || PAD::IS_DISABLED_CONTROL_PRESSED(0, kAim)), int(comp.aimedFromAfar()),
            int(inter == d), int(target == d), int(freeAim == d), int(PLAYER::IS_PLAYER_TARGETTING_ENTITY(id, d, false)),
            int(PLAYER::IS_PLAYER_FREE_AIMING(id)));
}

void tick() {
    if (now() < g_aimUntil && now() >= g_aimNext) {
        g_aimNext = now() + 300;
        aimLog();
    }
    if (now() < g_spritesUntil) drawSprites();
    if (now() < g_fontsUntil) ui::talkFontSurvey();
    if (now() < g_ctlUntil) probeControls();
    if (now() < g_watchUntil) watchHud();
    static int next = 0;
    if (now() < next) return;
    next = now() + 1000;
    const std::string path = fs::join(ctx().asiDir, "FrontierDog.test");
    std::string text;
    if (!fs::readFile(path, text)) return;
    fs::remove(path);
    for (auto& line : str::split(text, '\n')) {
        auto parts = str::split(str::trim(line), ' ');
        std::vector<std::string> args;
        for (auto& x : parts)
            if (!x.empty()) args.push_back(x);
        if (!args.empty()) {
            FD_INFO("test: > %s", str::trim(line).c_str());
            run(args);
        }
    }
}

}  // namespace fd::testhooks
#endif
