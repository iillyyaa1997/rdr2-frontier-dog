#ifdef FD_TEST
#include "mod/testhooks.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/fs.h"
#include "core/log.h"
#include "core/strutil.h"
#include "dog/companion.h"
#include "game/game.h"
#include "mod/context.h"

namespace fd::testhooks {

using namespace game;

static std::vector<Ped> g_spawned;

static void run(const std::vector<std::string>& a) {
    auto& c = ctx();
    auto& comp = companion();
    const std::string& cmd = a[0];
    auto num = [&](size_t i, double def) { double v; return i < a.size() && str::parseFloat(a[i], v) ? v : def; };
    Ped pl = player();
    Vector3 p = pos(pl);
    Vector3 fwd = ENTITY::GET_ENTITY_FORWARD_VECTOR(pl);
    if (cmd == "stray" && a.size() >= 2) {
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
    } else if (cmd == "walker") {
        // walker [metres]: a stranger walking past (shy of strangers, guard L1)
        Ped s = spawnPed("a_m_m_rancher_01", add(p, scale(fwd, float(num(1, 8)))), 0);
        if (s) { g_spawned.push_back(s); TASK::TASK_GO_TO_ENTITY(s, pl, -1, 1.0f, 1.0f, 0, 0); }
    } else if (cmd == "hurt" && comp.present()) {
        int mx = ENTITY::GET_ENTITY_MAX_HEALTH(comp.ped(), false);
        ENTITY::SET_ENTITY_HEALTH(comp.ped(), std::max(1, int(mx * num(1, 10) / 100.0)), 0);
    } else if (cmd == "stable" && a.size() >= 2) {
        // stable <id>: outside that stable, next to the dogs for sale (never inside: trespass)
        KvFile st;
        st.load(fs::join(c.dataDir, "Stables.dat"));
        auto v = st.getFloats(a[1] + ".dogs");
        if (v.size() == 3) ENTITY::SET_ENTITY_COORDS(pl, float(v[0]) + 3, float(v[1]) + 3, float(v[2]), false, false, false, true);
    } else if (cmd == "set" && a.size() >= 3 && comp.record()) {
        // set grade|hunger|dirt|bond|scale|preset|stat<1-5> <value>; skill <id> <level>; trait <id>; untrait <id>
        DogRecord& d = *comp.record();
        double v = num(2, 0);
        if (a[1] == "grade") d.grade = Grade(int(v));
        else if (a[1] == "hunger") d.hunger = v;
        else if (a[1] == "dirt") d.dirt = v;
        else if (a[1] == "bond") { d.bondPoints = v; }
        else if (a[1] == "scale") { d.scale = int(v); PED::_SET_PED_SCALE(comp.ped(), float(v) / 100.0f); }
        else if (a[1] == "preset") { d.preset = int(v); PED::_EQUIP_META_PED_OUTFIT_PRESET(comp.ped(), int(v), false); }
        else if (a[1].rfind("stat", 0) == 0 && a[1].size() == 5) d.stats[std::clamp(a[1][4] - '1', 0, 4)] = int(v);
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
        // dogmodel <model> [preset]: a dog of that model in front of Arthur (look / collar search)
        Ped s = spawnPed(a[1], add(p, scale(fwd, 3)), 0);
        if (s) {
            g_spawned.push_back(s);
            FD_INFO("test: %s has %d presets", a[1].c_str(), PED::GET_NUM_META_PED_OUTFITS(s));
            if (a.size() > 2) PED::_EQUIP_META_PED_OUTFIT_PRESET(s, int(num(2, 0)), false);
            TASK::TASK_STAND_STILL(s, -1);
        }
    } else if (cmd == "wanted") {
        PLAYER::SET_PLAYER_WANTED_LEVEL(PLAYER::PLAYER_ID(), int(num(1, 1)), false);
    } else if (cmd == "pos") {
        FD_INFO("test: Arthur %.2f %.2f %.2f, district 0x%08X, town %d", p.x, p.y, p.z, districtHash(p), int(inTown(p)));
    } else {
        FD_INFO("test: unknown command %s", cmd.c_str());
        return;
    }
    FD_INFO("test: done %s", cmd.c_str());
}

void tick() {
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
